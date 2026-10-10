#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "Arachne/WallToolPaths.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/BoundingBox.hpp"

#include "FillSpiralInset.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <functional>
#include <math.h>
#include <utility>
#include <vector>
#include "libslic3r/Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Line.hpp"

namespace Slic3r {

static int find_spiral_start_corner(const Polygon& loop)
{
    const size_t n = loop.points.size();
    if (n < 3)
        return 0;

    static const double right_angle_cos = 0.08716;
    static const double straight_cos = -0.99985;

    const double convex_turn = loop.is_counter_clockwise() ? 1.0 : -1.0;

    double best_right_cos = right_angle_cos;
    int    best_right     = -1;
    double best_wide_cos  = 1.0;
    int    best_wide      = -1;
    for (size_t i = 0; i < n; ++i) {
        const Point& p_prev = loop.points[(i - 1 + n) % n];
        const Point& p      = loop.points[i];
        const Point& p_next = loop.points[(i + 1) % n];

        Vec2d  e_in  = (p - p_prev).cast<double>();
        Vec2d  e_out = (p_next - p).cast<double>();
        double len1  = e_in.norm();
        double len2  = e_out.norm();
        if (len1 < 1e-6 || len2 < 1e-6)
            continue;
        if (convex_turn * (e_in.x() * e_out.y() - e_in.y() * e_out.x()) <= 0.0)
            continue;

        double cos_val = -e_in.dot(e_out) / (len1 * len2);
        if (std::abs(cos_val) < best_right_cos) {
            best_right_cos = std::abs(cos_val);
            best_right     = int(i);
        }
        if (cos_val > straight_cos && cos_val < best_wide_cos) {
            best_wide_cos = cos_val;
            best_wide     = int(i);
        }
    }
    if (best_right >= 0)
        return best_right;
    return best_wide < 0 ? 0 : best_wide;
}

static double loop_clip_length(const Polyline& loop_path, const double gap)
{
    const Point& p_prev = loop_path.points[loop_path.points.size() - 2];
    const Point& p_last = loop_path.points.back();
    const Point& p_next = loop_path.points[1];
    Vec2d        v1     = (p_last - p_prev).cast<double>();
    Vec2d        v2     = (p_next - p_last).cast<double>();
    if (v1.norm() < 1e-6 || v2.norm() < 1e-6)
        return gap;

    double alpha = std::atan2(std::abs(v1.x() * v2.y() - v1.y() * v2.x()), v1.dot(v2));
    return (alpha > M_PI / 4 && alpha < 2 * M_PI / 3) ? gap / std::sin(alpha) : gap;
}

static Polyline open_loop(const Polygon& loop, int start_index) { return loop.split_at_index(start_index); }

static ThickPolyline open_loop(const Arachne::ExtrusionLine& loop, int start_index)
{
    ThickPolyline path = Arachne::to_thick_polyline(loop);
    if (path.points.front() != path.points.back()) {
        const coordf_t w_first = path.width.front(), w_last = path.width.back();
        path.points.emplace_back(path.points.front());
        path.width.emplace_back(w_last);
        path.width.emplace_back(w_first);
    }
    path.start_at_index(start_index);
    return path;
}

static void clip_path_end(Polyline& path, double distance) { path.clip_end(distance); }

static void clip_path_end(ThickPolyline& path, double distance)
{
    while (distance > 0 && path.points.size() >= 2) {
        const Point    last  = path.points.back();
        const coordf_t w_end = path.width.back();
        path.points.pop_back();
        path.width.pop_back();
        const coordf_t w_start = path.width.back();
        path.width.pop_back();

        const Vec2d  v   = (path.points.back() - last).cast<double>();
        const double len = v.norm();
        if (len > distance) {
            const double t = distance / len;
            path.points.emplace_back((last.cast<double>() + v * t).cast<coord_t>());
            path.width.emplace_back(w_start);
            path.width.emplace_back(w_start + (w_end - w_start) * (1.0 - t));
            return;
        }
        distance -= len;
    }
    path.clear();
}

static void append_path(Polyline& dst, Polyline&& src) { dst.append(std::move(src)); }

static void append_path(ThickPolyline& dst, ThickPolyline&& src)
{
    if (dst.empty()) {
        dst = std::move(src);
        return;
    }
    if (dst.points.back() == src.points.front()) {
        src.points.erase(src.points.begin());
        src.width.erase(src.width.begin(), src.width.begin() + 2);
    } else {
        const coordf_t w_from = dst.width.back(), w_to = src.width.front();
        dst.width.emplace_back(w_from);
        dst.width.emplace_back(w_to);
    }
    append(dst.points, std::move(src.points));
    append(dst.width, std::move(src.width));
}

static bool leaves_a_centre_hole(const Polygon&) { return true; }
static bool leaves_a_centre_hole(const Arachne::ExtrusionLine&) { return false; }

static void append_path_point(Polyline& path, const Point& point) { path.points.emplace_back(point); }

static void append_path_point(ThickPolyline& path, const Point& point)
{
    const coordf_t w = path.width.back();
    path.points.emplace_back(point);
    path.width.emplace_back(w);
    path.width.emplace_back(w);
}

template<class LoopType, class PathType>
static std::vector<PathType> generate_spiral_insets(const FillParams&                   params,
                                                         const std::vector<const LoopType*>& loops,
                                                         const Polygons&                     loop_outlines,
                                                         const coord_t                       distance,
                                                         const ExPolygon&                    original_expoly)
{
    std::vector<PathType> output;
    PathType              spiral;
    Point                 current_pos(0, 0);
    int innermost_loop = -1;

    const double free_hop = 1.5 * double(distance);
    const double max_hop  = 4.0 * double(distance);
    auto reachable = [&](const Point& from, const Point& to) {
        const double hop = from.distance_to(to);
        if (hop > max_hop)
            return false;
        return hop <= free_hop || original_expoly.contains(Line(from, to));
    };

    const double max_center_stub = 2.0 * double(distance);

    auto flush_spiral = [&]() {
        if (spiral.empty())
            return;
        if (innermost_loop >= 0 && leaves_a_centre_hole(*loops[innermost_loop])) {
            const Polygon& innermost = loop_outlines[innermost_loop];
            const Point    centroid  = innermost.centroid();
            if (!offset(innermost, -float(0.5 * double(distance))).empty() && centroid != spiral.last_point() &&
                spiral.last_point().distance_to(centroid) <= max_center_stub && innermost.contains(centroid) &&
                reachable(spiral.last_point(), centroid))
                append_path_point(spiral, centroid);
        }
        output.emplace_back(std::move(spiral));
        spiral.clear();
        innermost_loop = -1;
        current_pos    = Point(0, 0);
    };

    for (size_t i = 0; i < loops.size(); ++i) {
        const Polygon& outline = loop_outlines[i];
        if (outline.points.empty())
            continue;

        PathType loop_path = open_loop(*loops[i], spiral.empty() ? find_spiral_start_corner(outline) :
                                                                  current_pos.nearest_point_index(outline.points));
        if (loop_path.size() < 3)
            continue;

        const bool same_island = innermost_loop >= 0 && loop_outlines[innermost_loop].contains(loop_path.points.front());
        if (!spiral.empty() && (!same_island || !reachable(spiral.last_point(), loop_path.points.front()))) {
            flush_spiral();
            loop_path = open_loop(*loops[i], find_spiral_start_corner(outline));
            if (loop_path.size() < 3)
                continue;
        }

        clip_path_end(loop_path, loop_clip_length(loop_path, (i + 1 == loops.size() ? 0.5 : 1.0) * double(distance)));
        if (loop_path.size() < 2)
            continue;

        append_path(spiral, std::move(loop_path));
        innermost_loop = int(i);
        current_pos    = spiral.last_point();
    }

    flush_spiral();

    if (params.fill_order != SurfaceFillOrder::Inward) {
        for (PathType& path : output)
            path.reverse();
        std::reverse(output.begin(), output.end());
    }

    return output;
}

void FillSpiralInset::_fill_surface_single(const FillParams& params,
                                                unsigned int thickness_layers,
                                                const std::pair<float, Point>& direction,
                                                ExPolygon expolygon,
                                                Polylines& polylines_out)
{
    BoundingBox bounding_box = expolygon.contour.bounding_box();

    coord_t min_spacing = scale_(this->spacing);
    coord_t distance    = coord_t(min_spacing / params.density);

    if (params.density > 0.9999f && !params.dont_adjust) {
        distance      = this->_adjust_solid_spacing(bounding_box.size()(0), distance);
        this->spacing = unscale<double>(distance);
    }

    Polygons loops = to_polygons(expolygon);

    ExPolygons last{std::move(expolygon)};
    while (!last.empty()) {
        last = offset2_ex(last, -(distance + min_spacing / 2), +min_spacing / 2);
        append(loops, to_polygons(last));
    }

    loops = union_pt_chained_outside_in(loops);

    std::vector<const Polygon*> loop_refs;
    loop_refs.reserve(loops.size());
    for (const Polygon& loop : loops)
        loop_refs.emplace_back(&loop);

    Polylines spiral_result = generate_spiral_insets<Polygon, Polyline>(params, loop_refs, loops, distance, expolygon);

    append(polylines_out, spiral_result);
}

void FillSpiralInset::_fill_surface_single(const FillParams& params,
                                                unsigned int thickness_layers,
                                                const std::pair<float, Point>& direction,
                                                ExPolygon expolygon,
                                                ThickPolylines& thick_polylines_out)
{
    assert(params.use_arachne);
    assert(this->print_config != nullptr && this->print_object_config != nullptr);

    if (params.extrusion_role == erSolidInfill) {
        Polylines polylines;
        this->_fill_surface_single(params, thickness_layers, direction, expolygon, polylines);
        append(thick_polylines_out, to_thick_polylines(std::move(polylines), scaled<coord_t>(this->spacing)));
        return;
    }

    if (params.density <= 0.9999f || params.dont_adjust) {
        Polylines polylines;
        this->_fill_surface_single(params, thickness_layers, direction, expolygon, polylines);
        append(thick_polylines_out, to_thick_polylines(std::move(polylines), scaled<coord_t>(this->spacing)));
        return;
    }

    Point   bbox_size   = expolygon.contour.bounding_box().size();
    coord_t min_spacing = scaled<coord_t>(this->spacing);

    coord_t  loops_count = std::max(bbox_size.x(), bbox_size.y()) / min_spacing + 1;
    Polygons polygons    = offset(expolygon, float(min_spacing) / 2.f);

    double min_nozzle_diameter = *std::min_element(print_config->nozzle_diameter.values.begin(), print_config->nozzle_diameter.values.end());
    Arachne::WallToolPathsParams input_params;
    input_params.min_bead_width                = 0.85 * min_nozzle_diameter;
    input_params.min_feature_size              = 0.25 * min_nozzle_diameter;
    input_params.wall_transition_length        = 1.0 * min_nozzle_diameter;
    input_params.wall_transition_angle         = 10;
    input_params.wall_transition_filter_deviation = 0.25 * min_nozzle_diameter;
    input_params.wall_distribution_count       = 1;

    Arachne::WallToolPaths wallToolPaths(polygons, min_spacing, min_spacing, loops_count, 0, params.layer_height, input_params);
    std::vector<Arachne::VariableWidthLines> walls_by_inset = wallToolPaths.getToolPaths();

    std::vector<const Arachne::ExtrusionLine*> walls;
    Polygons                                   wall_outlines;
    ThickPolylines                             open_walls;
    for (const Arachne::VariableWidthLines& inset : walls_by_inset)
        for (const Arachne::ExtrusionLine& wall : inset) {
            if (wall.empty())
                continue;
            if (wall.is_closed) {
                walls.emplace_back(&wall);
                wall_outlines.emplace_back(wall.toPolygon());
            } else {
                open_walls.emplace_back(Arachne::to_thick_polyline(wall));
            }
        }

    const size_t      wall_count = walls.size();
    std::vector<int>  nesting_depth(wall_count, 0), parent(wall_count, -1);
    std::vector<char> inside(wall_count * wall_count, 0);
    for (size_t i = 0; i < wall_count; ++i)
        for (size_t j = 0; j < wall_count; ++j)
            if (i != j && wall_outlines[j].contains(walls[i]->junctions.front().p)) {
                inside[i * wall_count + j] = 1;
                ++nesting_depth[i];
            }
    for (size_t i = 0; i < wall_count; ++i)
        for (size_t j = 0; j < wall_count; ++j)
            if (inside[i * wall_count + j] && (parent[i] < 0 || nesting_depth[parent[i]] < nesting_depth[j]))
                parent[i] = int(j);

    std::vector<const Arachne::ExtrusionLine*> ordered;
    Polygons                                   outlines;
    ordered.reserve(wall_count);
    outlines.reserve(wall_count);
    std::function<void(int)> descend = [&](int idx) {
        ordered.emplace_back(walls[idx]);
        outlines.emplace_back(wall_outlines[idx]);
        for (size_t k = 0; k < wall_count; ++k)
            if (parent[k] == idx)
                descend(int(k));
    };
    for (size_t i = 0; i < wall_count; ++i)
        if (parent[i] < 0)
            descend(int(i));

    ThickPolylines spiral_result =
        generate_spiral_insets<Arachne::ExtrusionLine, ThickPolyline>(params, ordered, outlines, min_spacing, expolygon);

    append(thick_polylines_out, std::move(spiral_result));
    append(thick_polylines_out, std::move(open_walls));
}

}
