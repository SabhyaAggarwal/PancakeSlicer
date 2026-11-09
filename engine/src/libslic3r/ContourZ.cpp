#include "ClipperUtils.hpp"
#include "Exception.hpp"
#include "ExPolygon.hpp"
#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Layer.hpp"
#include "Line.hpp"
#include "Nonplanar/AutoMode.hpp"
#include "Nonplanar/ConformalShells.hpp"
#include "Nonplanar/HeadEnvelope.hpp"
#include "Point.hpp"
#include "Print.hpp"
#include "SLA/IndexedMesh.hpp"
#include "libslic3r.h"
#include <Eigen/Core>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <math.h>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Config.hpp"
#include "Polyline.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

static void contour_extrusion_entity(LayerRegion *region, const sla::IndexedMesh &mesh, ExtrusionEntity *extr);

static bool plane_only_role(ExtrusionRole role) { return role == erSolidInfill || role == erGapFill; }

static double follow_slope_down(double angle_rad, double dist)
{
	return -dist * std::sin(angle_rad);
}

static double slope_from_normal(const Eigen::Vector3d& normal)
{
    Eigen::Vector3d n = normal.normalized();

    double angle_rad = std::acos(std::abs(n.z()));
	return angle_rad;
}

static bool contour_extrusion_path(LayerRegion *region, const sla::IndexedMesh &mesh, ExtrusionPath &path)
{
    const PrintRegionConfig &rcfg   = region->region().config();
    const PrintObjectConfig &ocfg   = region->layer()->object()->config();
    const bool zaa_active = rcfg.zaa_enabled || region->layer()->object()->curved_fallback_zaa() || region->layer()->object()->auto_z_contouring();
    const bool               banded = Nonplanar::top_shells_enabled(rcfg, ocfg) &&
                                      Nonplanar::effective_fallback(rcfg, ocfg) == NonplanarFallback::None;
    if (!zaa_active && !banded && path.role() != erTopSolidInfill)
        return false;

    if (path.role() != erTopSolidInfill && path.role() != erIroning && path.role() != erExternalPerimeter && path.role() != erPerimeter && !plane_only_role(path.role())) {
		return false;
	}
	
	Layer *layer = region->layer();
    std::vector<const ExPolygon *> planar_faces;
    if (banded)
        for (const auto &diag : layer->object()->conformal().diagnostics)
            if (diag.fallback_reason == "planar_patch")
                for (const ExPolygon &face : diag.fallback_shape)
                    planar_faces.push_back(&face);
    if (plane_only_role(path.role()) && planar_faces.empty())
        return false;
    const auto &config = rcfg.zaa_enabled || region->layer()->object()->curved_fallback_zaa()
        ? region->region().config() : PrintRegionConfig::defaults();
	coordf_t min_z = config.zaa_min_z;
	const coordf_t ray_z = zaa_active || !banded ? layer->slice_z : layer->bottom_z() + min_z;
	coordf_t mesh_slice_z = ray_z + mesh.ground_level();

	const Points3 &points = path.polyline.points;
	double resolution_mm = 0.1;

	coordf_t height = layer->height;

	double minimize_perimeter_height_angle = zaa_active ? config.zaa_minimize_perimeter_height.value : 0.;
	const bool mid_layer = ((rcfg.zaa_enabled && rcfg.zaa_mid_layer_slice) || region->layer()->object()->curved_fallback_zaa()) &&
	                       !region->layer()->object()->auto_z_contouring();
	if (mid_layer)
		minimize_perimeter_height_angle = 0.;

	Pointf3s contoured_points;
	bool was_contoured = false;

    if (points.size() < 2) {
        return false;
    }

    for (Points3::const_iterator it = points.begin(); it != points.end()-1; ++it) {
		Vec2d p1d(unscale_(it->x()), unscale_(it->y()));
		Vec2d p2d(unscale_((it+1)->x()), unscale_((it+1)->y()));
		Linef line(p1d, p2d);

		double length_mm = line.length();
		int num_segments = int(std::ceil(length_mm / resolution_mm));
		Vec2d delta = line.vector();

        if (num_segments == 0) {
            continue;
        }

        for (int i = 0; i < num_segments + 1; i++) {
            Vec2d p = p1d + delta * i / num_segments;

            coordf_t x = p.x();
			coordf_t y = p.y();

			sla::IndexedMesh::hit_result hit_up = mesh.query_ray_hit({x, y, mesh_slice_z}, {0.0, 0.0, 1.0});

			double d = hit_up.distance() - (layer->print_z - ray_z);
			const double surface = d;

            const bool on_planar_face = banded && !zaa_active && hit_up.is_hit() &&
                std::any_of(planar_faces.begin(), planar_faces.end(), [&](const ExPolygon *face) {
                    return face->contains(Point::new_scale(x, y));
                });
			double max_up = min_z;
			double min_down = -(height - min_z);
			double half_width = path.width / 2.0;
			if (path.role() == erIroning) {
				max_up = height;
				min_down = -(height + 0.1);
			} else if (banded && !zaa_active && path.role() == erTopSolidInfill && hit_up.is_hit() &&
			           hit_up.normal().z() > std::cos(M_PI / 18.)) {
				max_up = 0.5 * height;
			} else if (mid_layer && path.role() == erTopSolidInfill) {
				max_up = 0.5 * height;
			}

            if (on_planar_face)
                max_up = std::max(max_up, 0.5 * height);

            if (is_perimeter(path.role()) && hit_up.is_hit()) {
				const Vec3d &normal = hit_up.normal();
                double slope_rad     = slope_from_normal(normal);
                double slope_degrees = slope_rad * 180.0 / M_PI;

                if (d > min_down && minimize_perimeter_height_angle > 0 && minimize_perimeter_height_angle < slope_degrees) {
                    double adjustment = follow_slope_down(slope_rad, half_width);
                    if (adjustment > 0) {
                        throw RuntimeError("ContourZ: got positive adjustment");
                    }
                    d += adjustment;
                    if (d < min_down) {
                        d = min_down;
                    }
                }
            }

            if (d < -height || d > max_up + (on_planar_face ? 0. : 0.03)) {
                d = 0;
            }

            if (d < min_down) {
                d = min_down;
            } else if (d > max_up) {
                d = max_up;
            }

            if (plane_only_role(path.role()) && !on_planar_face)
                d = 0.;

            if (is_perimeter(path.role()) && d > 0 && !on_planar_face) {
                d = 0;
            }

            if (std::abs(d) > EPSILON) {
				was_contoured = true;
			}

            Vec3d new_point = {p.x(), p.y(), d};

            if (contoured_points.size() >= 2 && i != 0) {
                double dist = Linef3::distance_to_infinite_squared(new_point, contoured_points[contoured_points.size() - 2],
                                                                   contoured_points[contoured_points.size() - 1]);
                if (dist < EPSILON * EPSILON) {
                    contoured_points[contoured_points.size() - 1] = new_point;
                    continue;
                }
            }

            contoured_points.push_back(new_point);
        }
    }

    if (!was_contoured) {
		return false;
	}

	Polyline3 polyline;
	for (const Vec3d &point : contoured_points) {
		polyline.append(Point3(scale_(point.x()), scale_(point.y()), scale_(point.z())));
	}

	path.polyline = std::move(polyline);
	path.z_contoured = true;
	return true;
}

static void contour_extrusion_multipath(LayerRegion *region, const sla::IndexedMesh &mesh, ExtrusionMultiPath &multipath) 
{
	for (ExtrusionPath &path : multipath.paths) {
		contour_extrusion_path(region, mesh, path);
	}
}

static void contour_extrusion_loop(LayerRegion *region, const sla::IndexedMesh &mesh, ExtrusionLoop &loop) 
{
	for (ExtrusionPath &path : loop.paths) {
		contour_extrusion_path(region, mesh, path);
	}
}

static void contour_extrusion_entitiy_collection(LayerRegion *region, const sla::IndexedMesh &mesh, ExtrusionEntityCollection &collection)
{
	for (ExtrusionEntity *entity : collection.entities) {
		contour_extrusion_entity(region, mesh, entity);
	}
}

static void contour_extrusion_entity(LayerRegion *region, const sla::IndexedMesh &mesh, ExtrusionEntity *extr)
{
	if (dynamic_cast<const ExtrusionPathConformal*>(extr) != nullptr)
		return;

	const ExtrusionPathSloped *sloped = dynamic_cast<const ExtrusionPathSloped*>(extr);
	if (sloped != nullptr) {
		throw RuntimeError("ExtrusionPathSloped not implemented");
		return;
	}

	ExtrusionMultiPath *multipath = dynamic_cast<ExtrusionMultiPath*>(extr);
	if (multipath != nullptr) {
		contour_extrusion_multipath(region, mesh, *multipath);
		return;
	}

	ExtrusionPath *path = dynamic_cast<ExtrusionPath*>(extr);
	if (path != nullptr) {
		contour_extrusion_path(region, mesh, *path);
		return;
	}

	ExtrusionLoop *loop = dynamic_cast<ExtrusionLoop*>(extr);
	if (loop != nullptr) {
		contour_extrusion_loop(region, mesh, *loop);
		return;
	}

	const ExtrusionLoopSloped *loop_sloped = dynamic_cast<const ExtrusionLoopSloped*>(extr);
	if (loop_sloped != nullptr) {
		throw RuntimeError("ExtrusionLoopSloped not implemented");
		return;
	}

	ExtrusionEntityCollection *collection = dynamic_cast<ExtrusionEntityCollection*>(extr);
	if (collection != nullptr) {
		contour_extrusion_entitiy_collection(region, mesh, *collection);
		return;
	}

	throw RuntimeError("ContourZ: ExtrusionEntity type not implemented: " + std::string(typeid(*extr).name()));
	return;
}

static void handle_extrusion_collection(LayerRegion *region, const sla::IndexedMesh &mesh, ExtrusionEntityCollection &collection,
                                        const std::vector<ExtrusionRole> &roles, const ExPolygons &shell_area,
                                        const ExPolygons &covered = {})
{
    auto out_of_reach = [&covered](const Polylines &lines) { return !covered.empty() && diff_pl(lines, covered).empty(); };
    ExtrusionEntitiesPtr out;
    out.reserve(collection.entities.size());
    for (ExtrusionEntity *extr : collection.entities) {
        if (!contains(roles, extr->role()) || dynamic_cast<ExtrusionPathConformal *>(extr) != nullptr) {
            out.push_back(extr);
            continue;
        }
        if (auto *nested = dynamic_cast<ExtrusionEntityCollection *>(extr)) {
            handle_extrusion_collection(region, mesh, *nested, roles, shell_area, covered);
            if (nested->empty())
                delete nested;
            else
                out.push_back(nested);
            continue;
        }
        if (shell_area.empty() || plane_only_role(extr->role()) || intersection_pl(extr->as_polylines(), shell_area).empty()) {
            if (!out_of_reach(extr->as_polylines()))
                contour_extrusion_entity(region, mesh, extr);
            out.push_back(extr);
            continue;
        }
        const ExtrusionPaths *paths = nullptr;
        ExtrusionPaths single;
        if (auto *loop = dynamic_cast<ExtrusionLoop *>(extr))
            paths = &loop->paths;
        else if (auto *multi = dynamic_cast<ExtrusionMultiPath *>(extr))
            paths = &multi->paths;
        else if (auto *path = dynamic_cast<ExtrusionPath *>(extr); path &&
                 (typeid(*path) == typeid(ExtrusionPath) || typeid(*path) == typeid(ExtrusionPathOriented))) {
            single.push_back(*path);
            if (!path->can_reverse())
                single.back().set_reverse();
            paths = &single;
        }
        if (!paths) {
            contour_extrusion_entity(region, mesh, extr);
            out.push_back(extr);
            continue;
        }
        for (const ExtrusionPath &path : *paths) {
            if (!contains(roles, path.role())) {
                out.push_back(path.clone());
                continue;
            }
            if (plane_only_role(path.role())) {
                ExtrusionEntity *copy = path.clone();
                contour_extrusion_entity(region, mesh, copy);
                out.push_back(copy);
                continue;
            }
            for (Polyline &line : diff_pl(Polylines{path.polyline.to_polyline()}, shell_area)) {
                if (!line.is_valid())
                    continue;
                auto *piece = new ExtrusionPath(Polyline3(line), path);
                if (!out_of_reach({line}))
                    contour_extrusion_path(region, mesh, *piece);
                out.push_back(piece);
            }
        }
        delete extr;
    }
    collection.entities = std::move(out);
}

namespace {
constexpr double OrderStep = 0.2, OrderTol = 0.025;
thread_local double t_reach = 0.65;
thread_local bool t_textured = false;
constexpr double TextureTol = 0.015;
thread_local double t_tol = OrderTol;

bool ordered(ExtrusionRole role)
{
    return role == erTopSolidInfill || (t_textured && (role == erSolidInfill || role == erGapFill));
}

void densify(Points3 &pts)
{
    if (pts.size() < 2)
        return;
    const coord_t step = scaled<coord_t>(OrderStep);
    Points3 dense;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const Vec3d a = pts[i].cast<double>(), b = pts[i + 1].cast<double>();
        const int n = std::max(1, int(std::ceil((b - a).head<2>().norm() / double(step))));
        for (int k = 0; k < n; ++k) {
            const Vec3d q = a + (b - a) * (double(k) / n);
            dense.emplace_back(coord_t(std::lround(q.x())), coord_t(std::lround(q.y())), coord_t(std::lround(q.z())));
        }
    }
    dense.push_back(pts.back());
    pts = std::move(dense);
}

struct HeightGrid
{
    coord_t reach = scaled<coord_t>(t_reach);
    std::unordered_map<int64_t, Points3> cells;
    int64_t key(coord_t x, coord_t y) const { return (int64_t(x / reach) << 32) ^ int64_t(uint32_t(y / reach)); }
    void add(const Point3 &p) { cells[key(p.x(), p.y())].push_back(p); }
    template<class Visit> void near(const Point3 &p, Visit &&visit) const
    {
        for (coord_t dx = -1; dx <= 1; ++dx)
            for (coord_t dy = -1; dy <= 1; ++dy)
                if (auto it = cells.find(key(p.x() + dx * reach, p.y() + dy * reach)); it != cells.end())
                    for (const Point3 &q : it->second)
                        if (Vec2d(double(q.x() - p.x()), double(q.y() - p.y())).squaredNorm() <= double(reach) * double(reach))
                            visit(q);
    }
    coord_t highest(const Point3 &p) const
    {
        coord_t z = std::numeric_limits<coord_t>::min();
        near(p, [&z](const Point3 &q) { z = std::max(z, q.z()); });
        return z;
    }
};

void collect_paths(ExtrusionEntity &e, std::vector<ExtrusionPath *> &out)
{
    if (auto *c = dynamic_cast<ExtrusionEntityCollection *>(&e)) {
        for (ExtrusionEntity *x : c->entities)
            collect_paths(*x, out);
    } else if (auto *loop = dynamic_cast<ExtrusionLoop *>(&e)) {
        for (ExtrusionPath &p : loop->paths)
            out.push_back(&p);
    } else if (auto *multi = dynamic_cast<ExtrusionMultiPath *>(&e)) {
        for (ExtrusionPath &p : multi->paths)
            out.push_back(&p);
    } else if (auto *path = dynamic_cast<ExtrusionPath *>(&e))
        out.push_back(path);
}

bool has_top(const ExtrusionEntity &e)
{
    if (const auto *c = dynamic_cast<const ExtrusionEntityCollection *>(&e))
        return std::any_of(c->entities.begin(), c->entities.end(), [](const ExtrusionEntity *x) { return has_top(*x); });
    return ordered(e.role());
}

coord_t lowest_top(ExtrusionEntity &e)
{
    std::vector<ExtrusionPath *> paths;
    collect_paths(e, paths);
    coord_t z = std::numeric_limits<coord_t>::max();
    for (const ExtrusionPath *p : paths)
        if (ordered(p->role()))
            for (const Point3 &q : p->polyline.points)
                z = std::min(z, q.z());
    return z;
}

void order_lines(ExtrusionEntityCollection &collection)
{
    std::vector<ExtrusionPath *> lines;
    for (ExtrusionEntity *e : collection.entities) {
        auto *path = dynamic_cast<ExtrusionPath *>(e);
        if (path == nullptr || typeid(*path) != typeid(ExtrusionPath) || !ordered(path->role()) || path->polyline.points.size() < 2)
            return;
        lines.push_back(path);
    }
    if (lines.size() < 2)
        return;
    const coord_t tol = scaled<coord_t>(t_tol);
    HeightGrid grid;
    std::unordered_map<int64_t, std::vector<std::pair<Point3, int>>> owners;
    for (int i = 0; i < int(lines.size()); ++i)
        for (const Point3 &p : lines[i]->polyline.points)
            owners[grid.key(p.x(), p.y())].emplace_back(p, i);
    std::vector<std::vector<int>> after(lines.size());
    std::vector<int> pending(lines.size(), 0);
    for (int i = 0; i < int(lines.size()); ++i) {
        std::vector<char> seen(lines.size(), 0);
        for (const Point3 &p : lines[i]->polyline.points)
            for (coord_t dx = -1; dx <= 1; ++dx)
                for (coord_t dy = -1; dy <= 1; ++dy)
                    if (auto it = owners.find(grid.key(p.x() + dx * grid.reach, p.y() + dy * grid.reach)); it != owners.end())
                        for (const auto &[q, j] : it->second)
                            if (j != i && !seen[j] && q.z() - p.z() > tol &&
                                Vec2d(double(q.x() - p.x()), double(q.y() - p.y())).squaredNorm() <= double(grid.reach) * double(grid.reach)) {
                                seen[j] = 1;
                                after[i].push_back(j);
                                ++pending[j];
                            }
    }
    std::vector<char> done(lines.size(), 0);
    ExtrusionEntitiesPtr out;
    Point here = lines.front()->first_point();
    for (size_t n = 0; n < lines.size(); ++n) {
        int best = -1; bool best_rev = false; double best_d = 0.;
        for (int pass = 0; pass < 2 && best < 0; ++pass)
            for (int i = 0; i < int(lines.size()); ++i) {
                if (done[i] || (pass == 0 && pending[i] > 0))
                    continue;
                const double ds = (lines[i]->first_point() - here).cast<double>().norm(), de = (lines[i]->last_point() - here).cast<double>().norm();
                const double d = (pass == 0 ? 0. : 1e12 * pending[i]) + std::min(ds, de);
                if (best < 0 || d < best_d)
                    best = i, best_d = d, best_rev = de < ds;
            }
        done[best] = 1;
        for (int j : after[best])
            --pending[j];
        ExtrusionPath &line = *lines[best];
        if (best_rev && line.can_reverse())
            line.reverse();
        if (line.polyline.points.front().z() > line.polyline.points.back().z() + tol && line.can_reverse())
            line.reverse();
        here = line.last_point();
        out.push_back(&line);
    }
    collection.entities = std::move(out);
    collection.no_sort  = true;
}

bool order_tops(ExtrusionEntityCollection &collection, bool root)
{
    bool any = false;
    for (ExtrusionEntity *e : collection.entities)
        if (auto *nested = dynamic_cast<ExtrusionEntityCollection *>(e))
            any |= order_tops(*nested, false);
        else
            any |= ordered(e->role());
    if (!any)
        return false;
    order_lines(collection);
    std::vector<std::pair<coord_t, ExtrusionEntity *>> tops;
    ExtrusionEntitiesPtr out;
    for (ExtrusionEntity *e : collection.entities)
        if (dynamic_cast<ExtrusionEntityCollection *>(e) != nullptr && has_top(*e))
            tops.emplace_back(lowest_top(*e), e);
        else
            out.push_back(e);
    std::stable_sort(tops.begin(), tops.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &t : tops)
        out.push_back(t.second);
    collection.entities = std::move(out);
    if (!root)
        collection.no_sort = true;
    return true;
}

void clamp_tops(ExtrusionEntityCollection &collection, HeightGrid &printed)
{
    const coord_t tol = scaled<coord_t>(t_tol);
    for (ExtrusionEntity *e : collection.entities) {
        if (auto *nested = dynamic_cast<ExtrusionEntityCollection *>(e)) {
            clamp_tops(*nested, printed);
            continue;
        }
        std::vector<ExtrusionPath *> paths;
        collect_paths(*e, paths);
        for (ExtrusionPath *line : paths) {
            if (!ordered(line->role()))
                continue;
            densify(line->polyline.points);
            for (Point3 &p : line->polyline.points)
                if (const coord_t over = printed.highest(p); over != std::numeric_limits<coord_t>::min() && over - p.z() > tol) {
                    p.z() = over - tol;
                    line->z_contoured = true;
                }
            HeightGrid own;
            for (const Point3 &p : line->polyline.points)
                own.add(p);
            const bool loop = dynamic_cast<ExtrusionLoop *>(e) != nullptr ||
                (line->polyline.points.front() - line->polyline.points.back()).cast<double>().head<2>().norm() < scale_(line->width);
            for (size_t i = loop ? 0 : line->polyline.points.size() - 1; i < line->polyline.points.size(); ++i)
                if (Point3 &end = line->polyline.points[i]; own.highest(end) - end.z() > tol) {
                    end.z() = own.highest(end) - tol;
                    line->z_contoured = true;
                }
            for (const Point3 &p : line->polyline.points)
                printed.add(p);
        }
    }
}

void lift_wall_dips(ExtrusionEntityCollection &perimeters)
{
    const coord_t tol = scaled<coord_t>(t_tol);
    std::function<void(ExtrusionEntity &)> visit = [&](ExtrusionEntity &e) {
        if (auto *c = dynamic_cast<ExtrusionEntityCollection *>(&e)) {
            for (ExtrusionEntity *x : c->entities)
                visit(*x);
            return;
        }
        std::vector<ExtrusionPath *> paths;
        collect_paths(e, paths);
        if (std::none_of(paths.begin(), paths.end(), [](const ExtrusionPath *p) { return p->z_contoured; }))
            return;
        HeightGrid grid;
        for (ExtrusionPath *p : paths) {
            if (p->z_contoured)
                densify(p->polyline.points);
            for (const Point3 &q : p->polyline.points)
                grid.add(q);
        }
        for (ExtrusionPath *p : paths)
            for (Point3 &q : p->polyline.points)
                if (const coord_t over = grid.highest(q); over - q.z() > tol) {
                    q.z() = over - tol;
                    p->z_contoured = true;
                }
    };
    visit(perimeters);
}
}

namespace Nonplanar {

void order_textured(LayerRegion &region)
{
    t_reach    = resolve_head_envelope(region.layer()->object()->print()->config()).tip_flat_radius + 0.34;
    t_textured = true;
    t_tol      = TextureTol;
    HeightGrid printed;
    std::vector<ExtrusionPath *> earlier;
    collect_paths(region.perimeters, earlier);
    collect_paths(region.fills, earlier);
    for (ExtrusionPath *p : earlier)
        if (!ordered(p->role())) {
            Points3 pts = p->polyline.points;
            densify(pts);
            for (const Point3 &q : pts)
                printed.add(q);
        }
    if (order_tops(region.fills, true))
        clamp_tops(region.fills, printed);
    t_textured = false;
    t_tol      = OrderTol;
}

}

void Layer::make_contour_z(const sla::IndexedMesh &mesh)
{
	ExPolygons shell_area;
	const PrintObject &object = *this->object();
    std::vector<ExtrusionRole> fill_roles{ erTopSolidInfill, erMixed };
    if (std::any_of(object.conformal().diagnostics.begin(), object.conformal().diagnostics.end(),
                    [](const auto &diag) { return diag.fallback_reason == "planar_patch"; })) {
        fill_roles.push_back(erSolidInfill);
        fill_roles.push_back(erGapFill);
    }
	for (const Nonplanar::ConformalPatch &patch : object.conformal().patches)
		if ((patch.kind == Nonplanar::PatchKind::Top || patch.kind == Nonplanar::PatchKind::Volume) &&
			this->print_z > patch.z_min + EPSILON &&
			this->print_z <= object.get_layer(int(patch.host_layer))->print_z + EPSILON)
			append(shell_area, diff_ex(patch.footprint, patch.wall_band));
	for (const auto &diag : object.conformal().diagnostics)
		if (diag.fallback_reason == "contact")
			append(shell_area, diag.fallback_shape);
	ExPolygons covered;
	bool       covered_ready = false, shells_checked = false;

	for (LayerRegion *region : this->regions()) {
        const auto &config = region->region().config();
        if (!config.zaa_enabled && !object.curved_fallback_zaa() && !object.auto_z_contouring()) {
            if (Nonplanar::zaa_fallback(config, this->object()->config()))
                handle_extrusion_collection(region, mesh, region->fills, {erTopSolidInfill, erMixed}, shell_area);
            if (Nonplanar::top_shells_enabled(config, this->object()->config()) &&
                Nonplanar::effective_fallback(config, this->object()->config()) == NonplanarFallback::None)
            {
                if (!covered_ready) {
                    covered_ready     = true;
                    const double reach = this->print_z + PrintRegionConfig::defaults().zaa_min_z.value + 0.03;
                    const Layer *above = this->upper_layer;
                    while (above != nullptr && above->slice_z <= reach + EPSILON)
                        above = above->upper_layer;
                    if (above != nullptr)
                        covered = offset_ex(above->lslices, -float(scale_(0.05)));
                    if (const ExPolygons &clearance = object.conformal().clearance; !clearance.empty())
                        covered = covered.empty() ? clearance : union_ex(covered, clearance);
                }
                if (!shells_checked) {
                    shells_checked = true;
                    const bool shells  = std::any_of(object.conformal().patches.begin(), object.conformal().patches.end(),
                                                     [](const Nonplanar::ConformalPatch &p) { return p.kind == Nonplanar::PatchKind::Top; });
                    if (!shells && object.print()->config().nonplanar_kinematics.value != RotaryLayout::None)
                        covered = this->lslices;
                }
                handle_extrusion_collection(region, mesh, region->fills, fill_roles, shell_area, covered);
                handle_extrusion_collection(region, mesh, region->perimeters, {erPerimeter, erExternalPerimeter, erMixed}, {}, covered);
            }
            continue;
        }

        handle_extrusion_collection(region, mesh, region->fills, {erTopSolidInfill, erIroning, erPerimeter, erExternalPerimeter, erMixed}, shell_area);
        handle_extrusion_collection(region, mesh, region->perimeters, {erPerimeter, erExternalPerimeter, erMixed}, shell_area);
        if ((config.zaa_mid_layer_slice || object.curved_fallback_zaa()) && !object.auto_z_contouring()) {
            t_reach = Nonplanar::resolve_head_envelope(object.print()->config()).tip_flat_radius + 0.34;
            lift_wall_dips(region->perimeters);
            HeightGrid printed;
            std::vector<ExtrusionPath *> earlier;
            collect_paths(region->perimeters, earlier);
            collect_paths(region->fills, earlier);
            for (ExtrusionPath *p : earlier)
                if (p->role() != erTopSolidInfill) {
                    Points3 pts = p->polyline.points;
                    densify(pts);
                    for (const Point3 &q : pts)
                        printed.add(q);
                }
            if (order_tops(region->fills, true))
                clamp_tops(region->fills, printed);
        }
    }
}
}
