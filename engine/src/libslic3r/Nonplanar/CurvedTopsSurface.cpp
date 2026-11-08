// Curved tops port of fich-l / OrcaSlicer-Nonplanar v0.1 (AGPL-3.0).
#include "CurvedTopsSurface.hpp"
#include "Repair.hpp"
#include "Validate.hpp"
#include "libslic3r/GCode/ClearanceRepair.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/ClipperUtils.hpp"
#include <tbb/parallel_for.h>
#include <algorithm>
#include <map>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <list>
#include <mutex>
#include <set>
#include <utility>

#include <cstdio>
#include <boost/functional/hash.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r {

NonplanarSurface::NonplanarSurface(std::map<int, NonplanarFacet> &_mesh)
{
    this->mesh = _mesh;
    this->calculate_stats();
}

bool
NonplanarSurface::operator==(const NonplanarSurface& other) const {
   return (stats.min.x == other.stats.min.x &&
           stats.min.y == other.stats.min.y &&
           stats.min.z == other.stats.min.z &&
           stats.max.x == other.stats.max.x &&
           stats.max.y == other.stats.max.y &&
           stats.max.z == other.stats.max.z);
}

void
NonplanarSurface::calculate_stats()
{
    this->stats.min.x = 10000000;
    this->stats.min.y = 10000000;
    this->stats.min.z = 10000000;
    this->stats.max.x = -10000000;
    this->stats.max.y = -10000000;
    this->stats.max.z = -10000000;
    for(auto& facet : this->mesh) {
        this->stats.min.x = std::min(this->stats.min.x, facet.second.stats.min.x);
        this->stats.min.y = std::min(this->stats.min.y, facet.second.stats.min.y);
        this->stats.min.z = std::min(this->stats.min.z, facet.second.stats.min.z);
        this->stats.max.x = std::max(this->stats.max.x, facet.second.stats.max.x);
        this->stats.max.y = std::max(this->stats.max.y, facet.second.stats.max.y);
        this->stats.max.z = std::max(this->stats.max.z, facet.second.stats.max.z);
    }
}

void
NonplanarSurface::translate(float x, float y, float z)
{
    for(auto& facet : this->mesh) {
        facet.second.translate(x, y, z);
    }

    this->stats.min.x += x;
    this->stats.min.y += y;
    this->stats.min.z += z;
    this->stats.max.x += x;
    this->stats.max.y += y;
    this->stats.max.z += z;
}

void
NonplanarSurface::scale(float factor)
{
    float versor[3];
    versor[0] = factor;
    versor[1] = factor;
    versor[2] = factor;
    this->scale(versor);
}

void
NonplanarSurface::scale(float versor[3])
{
    for(auto& facet : this->mesh) {
        facet.second.scale(versor);
    }

    this->stats.min.x *= versor[0];
    this->stats.min.y *= versor[1];
    this->stats.min.z *= versor[2];
    this->stats.max.x *= versor[0];
    this->stats.max.y *= versor[1];
    this->stats.max.z *= versor[2];
}

void
NonplanarSurface::rotate_z(float angle) {
  double radian_angle = (angle / 180.0) * 3.1415927;
  double c = cos(radian_angle);
  double s = sin(radian_angle);

  for(auto& facet : this->mesh) {
    for(int j = 0; j < 3; j++) {
        double xold = facet.second.vertex[j].x;
        double yold = facet.second.vertex[j].y;
        facet.second.vertex[j].x = c * xold - s * yold;
        facet.second.vertex[j].y = s * xold + c * yold;
    }
    facet.second.calculate_stats();
  }

  this->calculate_stats();
}

void
NonplanarSurface::debug_output()
{
    std::cout << "Facets(" << this->mesh.size() << "): (min:X:" << this->stats.min.x << " Y:" << this->stats.min.y << " Z:" << this->stats.min.z <<
                           " max:X:" << this->stats.max.x << " Y:" << this->stats.max.y << " Z:" << this->stats.max.z << ")" <<
                           "Height " << this->stats.max.z - this->stats.min.z << std::endl;
    for(auto& facet : this->mesh) {
        std::cout << "triangle: (" << facet.first << ")(" << facet.second.marked << ") ";
        std::cout << " (" << (180*std::acos(facet.second.normal.z))/3.14159265 << "°)";

        std::cout << " | V0:";
        std::cout << " X:"<< facet.second.vertex[0].x;
        std::cout << " Y:"<< facet.second.vertex[0].y;
        std::cout << " Z:"<< facet.second.vertex[0].z;

        std::cout << " | V1:";
        std::cout << " X:"<< facet.second.vertex[1].x;
        std::cout << " Y:"<< facet.second.vertex[1].y;
        std::cout << " Z:"<< facet.second.vertex[1].z;

        std::cout << " | V2:";
        std::cout << " X:"<< facet.second.vertex[2].x;
        std::cout << " Y:"<< facet.second.vertex[2].y;
        std::cout << " Z:"<< facet.second.vertex[2].z;

        std::cout << " | Normal:";
        std::cout << " X:"<< facet.second.normal.x;
        std::cout << " Y:"<< facet.second.normal.y;
        std::cout << " Z:"<< facet.second.normal.z;

        std::cout << " | Neighbors:";
        std::cout << " 0:"<< facet.second.neighbor[0];
        std::cout << " 1:"<< facet.second.neighbor[1];
        std::cout << " 2:"<< facet.second.neighbor[2];
        std::cout << std::endl;
    }
}

NonplanarSurfaces
NonplanarSurface::group_surfaces()
{
    NonplanarSurfaces out;
    std::set<int> seen;
    for (const auto &[seed, facet] : mesh) {
        if (!seen.insert(seed).second) continue;
        NonplanarSurface surface;
        std::vector<int> pending{seed};
        while (!pending.empty()) {
            const int id = pending.back();
            pending.pop_back();
            const NonplanarFacet &current = mesh.at(id);
            surface.mesh.emplace(id, current);
            for (int neighbour : current.neighbor)
                if (mesh.count(neighbour) && seen.insert(neighbour).second) pending.push_back(neighbour);
        }
        surface.calculate_stats();
        out.emplace_back(std::move(surface));
    }
    return out;
}

void
NonplanarSurface::mark_neighbor_surfaces(int id)
{
    if(this->mesh.find(id) == this->mesh.end() || this->mesh[id].marked == true) return;
    this->mesh[id].marked = true;
    for(int j = 0; j < 3; j++) {
        this->mark_neighbor_surfaces(this->mesh[id].neighbor[j]);
    }
}

bool
NonplanarSurface::check_max_printing_height(float height)
{
    if ((this->stats.max.z - this->stats.min.z) > height ) {
        BOOST_LOG_TRIVIAL(trace) << "Surface removed: printheight too heigh (" << (this->stats.max.z - this->stats.min.z) << " mm)";
        return true;
    } else {
        return false;
    }
}

bool
NonplanarSurface::check_surface_area()
{
    float area = 0.0f;
    for(auto& facet : this->mesh) {
        area += facet.second.calculate_surface_area();
    }
    if (area < 20.0f) {
        BOOST_LOG_TRIVIAL(trace) << "Surface removed: area too small (" << area << " mm²)";
        return true;
    } else {
        return false;
    }
}

void
NonplanarSurface::check_printable_surfaces(float max_angle)
{
}

ExPolygons
NonplanarSurface::horizontal_projection() const
{
    Polygons pp;
    pp.reserve(this->mesh.size());
    Points key;
    key.reserve(3 * this->mesh.size());
    size_t hash = this->mesh.size();
    for(auto& facet : this->mesh) {
        Polygon p;
        p.points.resize(3);
        p.points[0] = Point(scale_(facet.second.vertex[0].x), scale_(facet.second.vertex[0].y));
        p.points[1] = Point(scale_(facet.second.vertex[1].x), scale_(facet.second.vertex[1].y));
        p.points[2] = Point(scale_(facet.second.vertex[2].x), scale_(facet.second.vertex[2].y));
        p.make_counter_clockwise();
        for (const Point &pt : p.points) {
            key.push_back(pt);
            boost::hash_combine(hash, pt.x());
            boost::hash_combine(hash, pt.y());
        }
        pp.push_back(p);
    }

    struct Entry { size_t hash; Points key; ExPolygons projection; };
    static std::mutex       mutex;
    static std::list<Entry> cache;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto it = cache.begin(); it != cache.end(); ++ it)
            if (it->hash == hash && it->key == key) {
                cache.splice(cache.begin(), cache, it);
                return cache.front().projection;
            }
    }
    constexpr size_t chunk = 2048;
    std::vector<Polygons> merged((pp.size() + chunk - 1) / chunk);
    tbb::parallel_for(size_t(0), merged.size(), [&](size_t c) {
        const Polygons part(pp.begin() + c * chunk, pp.begin() + std::min(pp.size(), (c + 1) * chunk));
        merged[c] = union_(offset(part, scale_(0.01)));
    });
    Polygons outlines;
    for (Polygons &m : merged)
        append(outlines, std::move(m));
    ExPolygons projection = union_ex(outlines);
    std::lock_guard<std::mutex> lock(mutex);
    cache.push_front({ hash, std::move(key), projection });
    if (cache.size() > 64)
        cache.pop_back();
    return projection;
}

std::string nonplanar_location(double z, size_t layer_id)
{
    char buf[80];
    std::snprintf(buf, sizeof(buf), " at height %.2f mm (layer %zu)", z, layer_id + 1);
    return buf;
}

double nonplanar_suggested_angle(double angle)
{
    return angle > 10. + EPSILON ? 10. : std::max(1., std::floor(angle) - 2.);
}

void throw_nonplanar_too_steep(double z, size_t layer_id, double angle, size_t object_id, const std::string &detail)
{
    BOOST_LOG_TRIVIAL(error) << "Nonplanar surface too steep" << nonplanar_location(z, layer_id) << ": " << detail;
    char buf[400];
    std::snprintf(buf, sizeof(buf), "Nonplanar slicing failed%s: a nonplanar line would have to climb more than %g° there "
        "(a sharp edge or step in the surface). Lower \"Maximum nonplanar angle\" to %g° or less and slice again.",
        nonplanar_location(z, layer_id).c_str(), std::max(12., angle), nonplanar_suggested_angle(angle));
    throw Slic3r::SlicingError(buf, object_id);
}

void throw_nonplanar_internal(const std::string &where, const std::string &detail, size_t object_id)
{
    BOOST_LOG_TRIVIAL(error) << "Nonplanar internal check failed" << where << ": " << detail;
    throw Slic3r::SlicingError("Nonplanar slicing stopped" + where + ": an internal check failed (" + detail + "). "
        "This is a bug, not a setting problem: please report it with the 3MF file. "
        "To slice now, turn off nonplanar layers for this object.", object_id);
}

double curved_tops_cone_tolerance(const Print &print)
{
    double tolerance = 0.13;
    bool any = false;
    for (const PrintObject *object : print.objects())
        if (curved_tops_mode(object->config().nonplanar_mode.value) || object->config().nonplanar_mode.value == NonplanarMode::SmoothMorph ||
            object->config().nonplanar_mode.value == NonplanarMode::ConformalLayers) {
            if (object->config().nonplanar_mode.value == NonplanarMode::CurvedTops)
                tolerance = std::min(tolerance, object->config().nonplanar_curved_cone_tolerance.value);
            any = true;
        }
    if (!any) return 0.05;
    if (const char *value = std::getenv("NP_CONTACT_CONE_TOL"); value != nullptr)
        tolerance = std::atof(value);
    return std::clamp(tolerance, 0., 0.13);
}

namespace {
std::mutex                                                      rejections_mutex;
std::map<const PrintObject *, CurvedSurfaceRejections>          rejections_of;
}

void note_curved_surface_rejections(const PrintObject &object, const CurvedSurfaceRejections &rejections)
{
    std::lock_guard<std::mutex> lock(rejections_mutex);
    rejections_of[&object] = rejections;
}

static double upward_area_between(const PrintObject &object, double low, double high)
{
    if (!(high > low))
        return 0.;
    const double cos_low = std::cos(low * M_PI / 180.), cos_high = std::cos(high * M_PI / 180.);
    double area = 0.;
    for (const ModelVolume *volume : object.model_object()->volumes) {
        if (!volume->is_model_part())
            continue;
        const Transform3d m = object.trafo_centered() * volume->get_matrix();
        const indexed_triangle_set &its = volume->mesh().its;
        for (const stl_triangle_vertex_indices &f : its.indices) {
            const Vec3d a = m * its.vertices[f[0]].cast<double>(), b = m * its.vertices[f[1]].cast<double>(), c = m * its.vertices[f[2]].cast<double>();
            const Vec3d n = (b - a).cross(c - a);
            const double length = n.norm();
            if (length <= 0. || n.z() <= 0.)
                continue;
            const double nz = n.z() / length;
            if (nz < cos_low && nz >= cos_high)
                area += 0.5 * n.z();
        }
    }
    return area;
}

CurvedTopsStats curved_tops_stats(const PrintObject &object)
{
    CurvedTopsStats out;
    out.candidates = object.curved_surface_candidates();
    out.upward_area_mm2 = object.curved_upward_area();
    std::map<int, std::pair<double, double>> top_heights;
    std::map<int, double> road_lengths;
    std::function<void(const ExtrusionEntity &)> visit = [&](const ExtrusionEntity &entity) {
        if (const auto *path = dynamic_cast<const ExtrusionPath *>(&entity)) {
            if (!path->nonplanar_source) return;
            for (const Point3 &point : path->polyline.points) {
                const double z = unscale<double>(point.z());
                out.max_z = std::max(out.max_z, z);
                if (path->distance_to_top >= 0.f && path->distance_to_top < 0.001f) {
                    auto [it, inserted] = top_heights.emplace(path->source_surface_id, std::make_pair(z, z));
                    it->second.first = std::min(it->second.first, z);
                    it->second.second = std::max(it->second.second, z);
                }
            }
            for (size_t i = 1; i < path->polyline.points.size(); ++i) {
                road_lengths[path->source_surface_id] +=
                    (path->polyline.points[i].cast<double>() - path->polyline.points[i - 1].cast<double>()).norm() * SCALING_FACTOR;

            }
        } else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(&entity)) {
            for (const auto &path : loop->paths) visit(path);
        } else if (const auto *multi = dynamic_cast<const ExtrusionMultiPath *>(&entity)) {
            for (const auto &path : multi->paths) visit(path);
        } else if (const auto *collection = dynamic_cast<const ExtrusionEntityCollection *>(&entity)) {
            for (const auto *child : collection->entities) visit(*child);
        }
    };
    for (const Layer *layer : object.layers())
        for (const LayerRegion *region : layer->regions()) {
            visit(region->perimeters);
            visit(region->fills);
        }
    std::set<int> used;
    for (const auto &[id, heights] : top_heights)
        if (heights.second - heights.first >= 0.03 - EPSILON) {
            used.insert(id);
            out.extrusion_mm += road_lengths[id];
        }
    out.kept = used.size();
    if (const auto *hints = object.nonplanar_repair())
        for (int id : used) {
            const auto found = hints->curved_top_areas.find(id);
            if (found != hints->curved_top_areas.end()) out.footprint_mm2 += found->second;
        }
    out.area_fraction = out.upward_area_mm2 > 0. ? std::clamp(out.footprint_mm2 / out.upward_area_mm2, 0., 1.) : 0.;
    if (out.kept == 0)
        out.planar_fallback_reason = out.candidates == 0 ? "no nonflat top surface meets the angle, height, area and access limits" :
                                                        "selected surfaces were rejected by head clearance or left no top roads with at least 0.03 mm of height variation";
    const NonplanarMode mode  = object.config().nonplanar_mode.value;
    const double        limit = Nonplanar::curved_max_angle_limit(mode);
    out.max_angle_deg         = object.config().nonplanar_curved_max_angle.value;
    constexpr double    seen  = 1.;
    CurvedSurfaceRejections rejections;
    {
        std::lock_guard<std::mutex> lock(rejections_mutex);
        if (const auto it = rejections_of.find(&object); it != rejections_of.end()) rejections = it->second;
    }
    if (rejections.head_unknown)
        out.planar_limit = "head_unknown";
    else if (mode == NonplanarMode::NonplanarTop && Nonplanar::wide_tops_refused(*object.model_object()) &&
        upward_area_between(object, out.max_angle_deg, limit) >= seen) {
        out.planar_limit        = "access";
        out.reachable_slope_deg = out.max_angle_deg;
    } else if (out.kept == 0) {
        if (out.candidates > 0 || rejections.collision > 0)
            out.planar_limit = "access";
        else if (upward_area_between(object, out.max_angle_deg, limit) >= seen)
            out.planar_limit = "angle";
        else if (rejections.height > 0)
            out.planar_limit = "height";
        else if (rejections.area > 0)
            out.planar_limit = "area";
    }
    return out;
}

}
