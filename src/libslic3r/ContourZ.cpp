#include "ClipperUtils.hpp"
#include "Exception.hpp"
#include "ExPolygon.hpp"
#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Layer.hpp"
#include "Line.hpp"
#include "Nonplanar/AutoMode.hpp"
#include "Nonplanar/ConformalShells.hpp"
#include "Point.hpp"
#include "Print.hpp"
#include "SLA/IndexedMesh.hpp"
#include "libslic3r.h"
#include <Eigen/Core>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <initializer_list>
#include <math.h>
#include <string>
#include <typeinfo>
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
    const bool zaa_active = rcfg.zaa_enabled || region->layer()->object()->auto_z_contouring();
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
    const auto &config = rcfg.zaa_enabled
        ? region->region().config() : PrintRegionConfig::defaults();
	coordf_t min_z = config.zaa_min_z;
	const coordf_t ray_z = zaa_active || !banded ? layer->slice_z : layer->bottom_z() + min_z;
	coordf_t mesh_slice_z = ray_z + mesh.ground_level();

	const Points3 &points = path.polyline.points;
	double resolution_mm = 0.1;

	coordf_t height = layer->height;

	double minimize_perimeter_height_angle = zaa_active ? config.zaa_minimize_perimeter_height.value : 0.;

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
        if (!config.zaa_enabled && !object.auto_z_contouring()) {
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
    }
}
}
