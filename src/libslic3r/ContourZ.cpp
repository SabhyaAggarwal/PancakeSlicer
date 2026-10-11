#include "ClipperUtils.hpp"
#include "Exception.hpp"
#include "ExPolygon.hpp"
#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Layer.hpp"
#include "Line.hpp"
#include "Nonplanar/ConformalShells.hpp"
#include "Point.hpp"
#include "Print.hpp"
#include "SLA/IndexedMesh.hpp"
#include "libslic3r.h"
#include <Eigen/Core>
#include <cfloat>
#include <cmath>
#include <initializer_list>
#include <math.h>
#include <string>
#include <utility>
#include "Config.hpp"
#include "Polyline.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

static void contour_extrusion_entity(LayerRegion *region, const sla::IndexedMesh &mesh, ExtrusionEntity *extr);

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
    if (!region->region().config().zaa_enabled && path.role() != erTopSolidInfill)
        return false;

    if (path.role() != erTopSolidInfill && path.role() != erIroning && path.role() != erExternalPerimeter && path.role() != erPerimeter) {
		return false;
	}
	
	Layer *layer = region->layer();
	coordf_t mesh_slice_z = layer->slice_z + mesh.ground_level();
    const auto &config = region->region().config().zaa_enabled
        ? region->region().config() : PrintRegionConfig::defaults();
	coordf_t min_z = config.zaa_min_z;

	const Points3 &points = path.polyline.points;
	double resolution_mm = 0.1;

	coordf_t height = layer->height;

	double minimize_perimeter_height_angle = config.zaa_minimize_perimeter_height;

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

			double d = hit_up.distance() - (layer->print_z - layer->slice_z);

			double max_up = min_z;
			double min_down = -(height - min_z);
			double half_width = path.width / 2.0;
			if (path.role() == erIroning) {
				max_up = height;
				min_down = -(height + 0.1);
			}

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

            if (d < -height || d > max_up + 0.03) {
                d = 0;
            }

            if (d < min_down) {
                d = min_down;
            } else if (d > max_up) {
                d = max_up;
            }

            if (is_perimeter(path.role()) && d > 0) {
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

static void handle_extrusion_collection(LayerRegion *region, const sla::IndexedMesh &mesh, ExtrusionEntityCollection &collection, std::initializer_list<ExtrusionRole> roles, const ExPolygons &shell_area) {
    for (ExtrusionEntity* extr : collection.entities) {
        if (!contains(roles, extr->role())) {
			continue;
		}
		if (!shell_area.empty() && diff_pl(extr->as_polylines(), shell_area).empty())
			continue;

		contour_extrusion_entity(region, mesh, extr);
    }
}

void Layer::make_contour_z(const sla::IndexedMesh &mesh)
{
	ExPolygons shell_area;
	for (const Nonplanar::ConformalPatch &patch : this->object()->conformal().patches)
		if (patch.kind == Nonplanar::PatchKind::Top || patch.kind == Nonplanar::PatchKind::Volume)
			append(shell_area, diff_ex(patch.footprint, patch.wall_band));

	for (LayerRegion *region : this->regions()) {
        const auto &config = region->region().config();
        if (!config.zaa_enabled) {
            if (config.nonplanar_top_shells.value > 0 && config.nonplanar_fallback.value == NonplanarFallback::ZAA)
                handle_extrusion_collection(region, mesh, region->fills, {erTopSolidInfill, erMixed}, shell_area);
            if (config.nonplanar_top_shells.value > 0 && config.nonplanar_fallback.value == NonplanarFallback::None)
                handle_extrusion_collection(region, mesh, region->perimeters, {erPerimeter, erExternalPerimeter, erMixed}, shell_area);
            continue;
        }

        handle_extrusion_collection(region, mesh, region->fills, {erTopSolidInfill, erIroning, erPerimeter, erExternalPerimeter, erMixed}, shell_area);
        handle_extrusion_collection(region, mesh, region->perimeters, {erPerimeter, erExternalPerimeter, erMixed}, shell_area);
    }
}
}
