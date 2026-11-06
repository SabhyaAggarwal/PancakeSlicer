#include "../Layer.hpp"
#include "../GCode.hpp"
#include "../EdgeGrid.hpp"
#include "../Print.hpp"
#include "../Polygon.hpp"
#include "../ExPolygon.hpp"
#include "../Geometry.hpp"
#include "../ClipperUtils.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/Config.hpp"
#include "AvoidCrossingPerimeters.hpp"

#include <cstddef>
#include <boost/container_hash/hash.hpp>
#include <cassert>
#include <limits>
#include <algorithm>
#include <iterator>
#include <cmath>
#include <math.h>
#include <numeric>
#include <unordered_set>
#include <boost/range/adaptor/reversed.hpp>
#include <vector>
#include <utility>
#include "libslic3r/Extruder.hpp"
#include "libslic3r/GCodeWriter.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SurfaceCollection.hpp"

namespace Slic3r {

struct TravelPoint
{
    Point point;

    int   border_idx;

    bool do_not_remove = false;
};

struct Intersection
{

    size_t border_idx;

    size_t line_idx;

    Point  point;

    float  distance;

    bool do_not_remove = false;
};

struct ClosestLine
{

    size_t border_idx;

    size_t line_idx;

    Point  point;
};


struct AllIntersectionsVisitor
{
    AllIntersectionsVisitor(const EdgeGrid::Grid &grid, std::vector<Intersection> &intersections) : grid(grid), intersections(intersections)
    {
        intersection_set.reserve(intersections.capacity());
    }

    AllIntersectionsVisitor(const EdgeGrid::Grid &grid, std::vector<Intersection> &intersections, const Line &travel_line)
        : grid(grid), intersections(intersections), travel_line(travel_line)
    {
        intersection_set.reserve(intersections.capacity());
    }

    void reset() {
        intersection_set.clear();
    }

    bool operator()(coord_t iy, coord_t ix)
    {

        auto cell_data_range = grid.cell_data_range(iy, ix);
        for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second; ++it_contour_and_segment) {
            Point intersection_point;
            if (travel_line.intersection(grid.line(*it_contour_and_segment), &intersection_point) &&
                intersection_set.find(*it_contour_and_segment) == intersection_set.end()) {
                intersections.push_back({ it_contour_and_segment->first, it_contour_and_segment->second, intersection_point });
                intersection_set.insert(*it_contour_and_segment);
            }
        }

        return true;
    }

    const EdgeGrid::Grid                                                                 &grid;
    std::vector<Intersection>                                                            &intersections;
    Line                                                                                  travel_line;
    std::unordered_set<std::pair<size_t, size_t>, boost::hash<std::pair<size_t, size_t>>> intersection_set;
};


struct FirstIntersectionVisitor
{
    explicit FirstIntersectionVisitor(const EdgeGrid::Grid &grid) : grid(grid) {}

    bool operator()(coord_t iy, coord_t ix)
    {
        assert(pt_current != nullptr);
        assert(pt_next != nullptr);

        auto cell_data_range = grid.cell_data_range(iy, ix);
        this->intersect      = false;
        for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second; ++it_contour_and_segment) {

            auto segment = grid.segment(*it_contour_and_segment);
            if (Geometry::segments_intersect(segment.first, segment.second, *pt_current, *pt_next)) {
                this->intersect = true;
                return false;
            }
        }

        return true;
    }

    const EdgeGrid::Grid &grid;
    const Slic3r::Point  *pt_current = nullptr;
    const Slic3r::Point  *pt_next    = nullptr;
    bool                  intersect  = false;
};


struct MinDistanceVisitor
{
    explicit MinDistanceVisitor(const EdgeGrid::Grid &grid, const Point &center, double max_distance_squared)
        : grid(grid), center(center), max_distance_squared(max_distance_squared)
    {}

    void init()
    {
        this->closest_lines.clear();
        this->closest_lines_set.clear();
    }

    bool operator()(coord_t iy, coord_t ix)
    {

        auto cell_data_range = grid.cell_data_range(iy, ix);
        for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second; ++it_contour_and_segment) {

            auto  segment = grid.segment(*it_contour_and_segment);
            Point closest_point;
            if (closest_lines_set.find(*it_contour_and_segment) == closest_lines_set.end() &&
                line_alg::distance_to_squared(Line(segment.first, segment.second), center, &closest_point) <= this->max_distance_squared) {
                closest_lines.push_back({it_contour_and_segment->first, it_contour_and_segment->second, closest_point});
                closest_lines_set.insert(*it_contour_and_segment);
            }
        }

        return true;
    }

    const EdgeGrid::Grid &                                                                grid;
    const Slic3r::Point                                                                   center;
    std::vector<ClosestLine>                                                              closest_lines;
    std::unordered_set<std::pair<size_t, size_t>, boost::hash<std::pair<size_t, size_t>>> closest_lines_set;
    double                                                                                max_distance_squared = std::numeric_limits<double>::max();
};


static std::vector<ClosestLine> get_closest_lines_in_radius(const EdgeGrid::Grid &grid, const Point &center, float search_radius)
{
    Point              radius_vector(search_radius, search_radius);
    MinDistanceVisitor visitor(grid, center, search_radius * search_radius);
    grid.visit_cells_intersecting_box(BoundingBox(center - radius_vector, center + radius_vector), visitor);
    std::sort(visitor.closest_lines.begin(), visitor.closest_lines.end(), [&center](const auto &l, const auto &r) {
        return (center - l.point).template cast<double>().squaredNorm() < (center - r.point).template cast<double>().squaredNorm();
    });

    return visitor.closest_lines;
}



static std::vector<Intersection> extend_for_closest_lines(const std::vector<Intersection>         &intersections,
                                                          const AvoidCrossingPerimeters::Boundary &boundary,
                                                          const Point                             &start,
                                                          const Point                             &end,
                                                          const float                              search_radius)
{
    const std::vector<ClosestLine> start_lines = get_closest_lines_in_radius(boundary.grid, start, search_radius);
    const std::vector<ClosestLine> end_lines   = get_closest_lines_in_radius(boundary.grid, end, search_radius);


    auto compute_distance = [&boundary](const ClosestLine &closest_line) -> float {
        float dist_from_line_begin = (closest_line.point - boundary.boundaries[closest_line.border_idx][closest_line.line_idx]).cast<float>().norm();
        return boundary.boundaries_params[closest_line.border_idx][closest_line.line_idx] + dist_from_line_begin;
    };


    auto endpoints_close_to_same_boundary = [&start_lines, &end_lines]() -> std::pair<size_t, size_t> {
        std::unordered_set<size_t> boundaries_from_start;
        for (const ClosestLine &cl_start : start_lines)
            boundaries_from_start.insert(cl_start.border_idx);
        for (const ClosestLine &cl_end : end_lines)
            if (boundaries_from_start.find(cl_end.border_idx) != boundaries_from_start.end())
                for (const ClosestLine &cl_start : start_lines)
                    if (cl_start.border_idx == cl_end.border_idx) {
                        size_t cl_start_idx = &cl_start - &start_lines.front();
                        size_t cl_end_idx   = &cl_end - &end_lines.front();
                        return std::make_pair(cl_start_idx, cl_end_idx);
                    }
        return std::make_pair(std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max());
    };



    if (!start_lines.empty() && !end_lines.empty()) {
        std::pair<size_t, size_t> cl_indices = endpoints_close_to_same_boundary();
        if (cl_indices.first != std::numeric_limits<size_t>::max()) {
            assert(cl_indices.second != std::numeric_limits<size_t>::max());
            const ClosestLine &cl_start = start_lines[cl_indices.first];
            const ClosestLine &cl_end   = end_lines[cl_indices.second];
            std::vector<Intersection> new_intersections;
            new_intersections.push_back({cl_start.border_idx, cl_start.line_idx, cl_start.point, compute_distance(cl_start), true});
            new_intersections.push_back({cl_end.border_idx, cl_end.line_idx, cl_end.point, compute_distance(cl_end), true});
            return new_intersections;
        }
    }


    auto get_closer = [&search_radius](const std::vector<ClosestLine> &closest_lines, const Intersection &intersection,
                                       const Point &close_to) -> size_t {
        for (const ClosestLine &cl : closest_lines) {
            double old_dist = (close_to - intersection.point).cast<float>().squaredNorm();
            if (cl.border_idx == intersection.border_idx && old_dist <= (search_radius * search_radius) &&
                (close_to - cl.point).cast<float>().squaredNorm() < old_dist)
                return &cl - &closest_lines.front();
        }
        return std::numeric_limits<size_t>::max();
    };


    auto find_closest_line_with_same_boundary_idx = [](const std::vector<ClosestLine> & closest_lines,
                                                       const std::vector<Intersection> &intersections, const bool reverse) -> size_t {
        std::unordered_set<size_t> boundaries_indices;
        for (const ClosestLine &closest_line : closest_lines)
            boundaries_indices.insert(closest_line.border_idx);


        auto find_closest_line_index = [&closest_lines](const Intersection &intersection) -> size_t {
            for (const ClosestLine &closest_line : closest_lines)
                if (closest_line.border_idx == intersection.border_idx) return &closest_line - &closest_lines.front();

            assert(false);
            return std::numeric_limits<size_t>::max();
        };

        if (reverse) {
            for (const Intersection &intersection : boost::adaptors::reverse(intersections))
                if (boundaries_indices.find(intersection.border_idx) != boundaries_indices.end())
                    return find_closest_line_index(intersection);
        } else {
            for (const Intersection &intersection : intersections)
                if (boundaries_indices.find(intersection.border_idx) != boundaries_indices.end())
                    return find_closest_line_index(intersection);
        }
        return std::numeric_limits<size_t>::max();
    };

    std::vector<Intersection> new_intersections = intersections;
    if (!new_intersections.empty() && !start_lines.empty()) {
        size_t cl_start_idx = get_closer(start_lines, new_intersections.front(), start);
        if (cl_start_idx != std::numeric_limits<size_t>::max()) {

            const ClosestLine &cl_start = start_lines[cl_start_idx];
            new_intersections.front()   = {cl_start.border_idx, cl_start.line_idx, cl_start.point, compute_distance(cl_start), true};
        } else {




            size_t             start_closest_lines_idx = find_closest_line_with_same_boundary_idx(start_lines, new_intersections, true);
            const ClosestLine &cl_start                = (start_closest_lines_idx != std::numeric_limits<size_t>::max()) ? start_lines[start_closest_lines_idx] : start_lines.front();
            new_intersections.insert(new_intersections.begin(), {cl_start.border_idx, cl_start.line_idx, cl_start.point, compute_distance(cl_start), true});
        }
    }

    if (!new_intersections.empty() && !end_lines.empty()) {
        size_t cl_end_idx = get_closer(end_lines, new_intersections.back(), end);
        if (cl_end_idx != std::numeric_limits<size_t>::max()) {

            const ClosestLine &cl_end = end_lines[cl_end_idx];
            new_intersections.back()  = {cl_end.border_idx, cl_end.line_idx, cl_end.point, compute_distance(cl_end), true};
        } else {




            size_t             end_closest_lines_idx = find_closest_line_with_same_boundary_idx(end_lines, new_intersections, false);
            const ClosestLine &cl_end                = (end_closest_lines_idx != std::numeric_limits<size_t>::max()) ? end_lines[end_closest_lines_idx] : end_lines.front();
            new_intersections.push_back({cl_end.border_idx, cl_end.line_idx, cl_end.point, compute_distance(cl_end), true});
        }
    }
    return new_intersections;
}


template<bool forward>
static Point find_first_different_vertex(const Polygon &polygon, const size_t point_idx, const Point &point)
{
    assert(point_idx < polygon.size());

    if (point != polygon.points[point_idx])
        return polygon.points[point_idx];

    auto line_idx = (int(point_idx) + 1) % int(polygon.points.size());
    assert(line_idx != int(point_idx));
    if constexpr (forward)
        for (; point == polygon.points[line_idx] && line_idx != int(point_idx); line_idx = line_idx + 1 < int(polygon.points.size()) ? line_idx + 1 : 0);
    else
        for (; point == polygon.points[line_idx] && line_idx != int(point_idx); line_idx = line_idx - 1 >= 0 ? line_idx - 1 : int(polygon.points.size()) - 1);
    assert(point != polygon.points[line_idx]);
    return polygon.points[line_idx];
}

static Vec2d three_points_inward_normal(const Point &left, const Point &middle, const Point &right)
{
    assert(left != middle);
    assert(middle != right);
    return (perp(Point(middle - left)).cast<double>().normalized() + perp(Point(right - middle)).cast<double>().normalized()).normalized();
}


static Vec2d get_polygon_vertex_inward_normal(const Polygon &polygon, const size_t point_idx)
{
    const size_t left_idx  = prev_idx_modulo(point_idx, polygon.points);
    const size_t right_idx = next_idx_modulo(point_idx, polygon.points);
    const Point &middle    = polygon.points[point_idx];
    const Point &left      = find_first_different_vertex<false>(polygon, left_idx, middle);
    const Point &right     = find_first_different_vertex<true>(polygon, right_idx, middle);
    return three_points_inward_normal(left, middle, right);
}


static Point get_polygon_vertex_offset(const Polygon &polygon, const size_t point_idx, const int offset)
{
    return polygon.points[point_idx] + (get_polygon_vertex_inward_normal(polygon, point_idx) * double(offset)).cast<coord_t>();
}


static Point get_middle_point_offset(const Polygon &polygon, const size_t left_idx, const size_t right_idx, const Point &middle, const coord_t offset)
{
    const Point &left  = find_first_different_vertex<false>(polygon, left_idx, middle);
    const Point &right = find_first_different_vertex<true>(polygon, right_idx, middle);
    return middle + (three_points_inward_normal(left, middle, right) * double(offset)).cast<coord_t>();
}

static Polyline to_polyline(const std::vector<TravelPoint> &travel)
{
    Polyline result;
    result.points.reserve(travel.size());
    for (const TravelPoint &t_point : travel)
        result.append(t_point.point);
    return result;
}



#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
static void export_travel_to_svg(const Polygons                  &boundary,
                                 const Line                      &original_travel,
                                 const Polyline                  &result_travel,
                                 const std::vector<Intersection> &intersections,
                                 const std::string               &path)
{
    BoundingBox   bbox = get_extents(boundary);
    ::Slic3r::SVG svg(path, bbox);
    svg.draw_outline(boundary, "green");
    svg.draw(original_travel, "blue");
    svg.draw(result_travel, "red");
    svg.draw(original_travel.a, "black");
    svg.draw(original_travel.b, "grey");

    for (const Intersection &intersection : intersections)
        svg.draw(intersection.point, "lightseagreen");
}

static void export_travel_to_svg(const Polygons                  &boundary,
                                 const Line                      &original_travel,
                                 const std::vector<TravelPoint>  &result_travel,
                                 const std::vector<Intersection> &intersections,
                                 const std::string               &path)
{
    export_travel_to_svg(boundary, original_travel, to_polyline(result_travel), intersections, path);
}
#endif


enum class Direction { Forward, Backward };

static Direction get_shortest_direction(const AvoidCrossingPerimeters::Boundary &boundary,
                                        const Intersection                      &intersection_first,
                                        const Intersection                      &intersection_second,
                                        float                                    contour_length)
{
    assert(intersection_first.border_idx == intersection_second.border_idx);
    const Polygon &poly        = boundary.boundaries[intersection_first.border_idx];
    float          dist_first  = intersection_first.distance;
    float          dist_second = intersection_second.distance;

    assert(dist_first  >= 0.f && dist_first  <= contour_length);
    assert(dist_second >= 0.f && dist_second <= contour_length);

    bool reversed = false;
    if (dist_first > dist_second) {
        std::swap(dist_first, dist_second);
        reversed = true;
    }
    float total_length_forward  = dist_second - dist_first;
    float total_length_backward = dist_first + contour_length - dist_second;
    if (reversed) std::swap(total_length_forward, total_length_backward);

    total_length_forward  -= (intersection_first.point - poly[intersection_first.line_idx]).cast<float>().norm();
    total_length_backward -= (poly[(intersection_first.line_idx + 1) % poly.size()] - intersection_first.point).cast<float>().norm();

    total_length_forward  -= (poly[(intersection_second.line_idx + 1) % poly.size()] - intersection_second.point).cast<float>().norm();
    total_length_backward -= (intersection_second.point - poly[intersection_second.line_idx]).cast<float>().norm();

    if (total_length_forward < total_length_backward) return Direction::Forward;
    return Direction::Backward;
}

Polyline ConvertBBoxToPolyline(const BoundingBoxf &bbox)
{
    Point left_bottom = bbox.min.cast<coord_t>();
    Point left_up(bbox.min.cast<coord_t>()[0], bbox.max.cast<coord_t>()[1]);
    Point right_up = bbox.max.cast<coord_t>();
    Point right_bottom(bbox.max.cast<coord_t>()[0], bbox.min.cast<coord_t>()[1]);

    return Polyline({left_bottom, right_bottom, right_up, left_up, left_bottom});
}



static std::vector<TravelPoint> simplify_travel(const AvoidCrossingPerimeters::Boundary &boundary, const std::vector<TravelPoint> &travel)
{
    FirstIntersectionVisitor visitor(boundary.grid);
    std::vector<TravelPoint> simplified_path;
    simplified_path.reserve(travel.size());
    simplified_path.emplace_back(travel.front());



    for (size_t point_idx = 1; point_idx < travel.size(); ++point_idx) {
        const Point &current_point = travel[point_idx - 1].point;
        TravelPoint  next          = travel[point_idx];

        visitor.pt_current = &current_point;

        if (!next.do_not_remove)
            for (size_t point_idx_2 = point_idx + 1; point_idx_2 < travel.size(); ++point_idx_2) {

#if defined(_WIN32) && !defined(_WIN64)
                if (bool volatile do_not_remove = travel[point_idx_2].do_not_remove; do_not_remove) break;
#else
                if (travel[point_idx_2].do_not_remove) break;
#endif
                if (travel[point_idx_2].point == current_point) {
                    next      = travel[point_idx_2];
                    point_idx = point_idx_2;
                    continue;
                }

                visitor.pt_next = &travel[point_idx_2].point;
                boundary.grid.visit_cells_intersecting_line(*visitor.pt_current, *visitor.pt_next, visitor);

                if (!visitor.intersect) {
                    next      = travel[point_idx_2];
                    point_idx = point_idx_2;
                }
            }

        simplified_path.emplace_back(next);
    }

    return simplified_path;
}


static inline float get_default_perimeter_spacing(const PrintObject &print_object)
{
    std::vector<unsigned int> printing_extruders = print_object.object_extruders();
    assert(!printing_extruders.empty());
    float avg_extruder = 0;
    for(unsigned int extruder_id : printing_extruders)
        avg_extruder += float(scale_(print_object.print()->config().nozzle_diameter.get_at(extruder_id)));
    avg_extruder /= printing_extruders.size();
    return avg_extruder;
}


static float get_perimeter_spacing(const Layer &layer)
{
    size_t regions_count     = 0;
    float  perimeter_spacing = 0.f;
    for (const LayerRegion *layer_region : layer.regions())
        if (layer_region != nullptr && !layer_region->slices.empty()) {
            perimeter_spacing += layer_region->flow(frPerimeter).scaled_spacing();
            ++regions_count;
        }

    assert(perimeter_spacing >= 0.f);
    if (regions_count != 0)
        perimeter_spacing /= float(regions_count);
    else
        perimeter_spacing = get_default_perimeter_spacing(*layer.object());
    return perimeter_spacing;
}


static float get_perimeter_spacing_external(const Layer &layer)
{
    size_t regions_count     = 0;
    float  perimeter_spacing = 0.f;
    for (const PrintObject *object : layer.object()->print()->objects())
        if (const Layer *l = object->get_layer_at_printz(layer.print_z, EPSILON); l)
            for (const LayerRegion *layer_region : l->regions())
                if (layer_region != nullptr && !layer_region->slices.empty()) {
                    perimeter_spacing += layer_region->flow(frPerimeter).scaled_spacing();
                    ++ regions_count;
                }

    assert(perimeter_spacing >= 0.f);
    if (regions_count != 0)
        perimeter_spacing /= float(regions_count);
    else
        perimeter_spacing = get_default_perimeter_spacing(*layer.object());
    return perimeter_spacing;
}

static float get_external_perimeter_width(const Layer &layer)
{
    size_t regions_count   = 0;
    float  perimeter_width = 0.f;
    for (const LayerRegion *layer_region : layer.regions())
        if (layer_region != nullptr && !layer_region->slices.empty()) {
            perimeter_width += float(layer_region->flow(frExternalPerimeter).scaled_width());
            ++regions_count;
        }

    assert(perimeter_width >= 0.f);
    if (regions_count != 0)
        perimeter_width /= float(regions_count);
    else
        perimeter_width = get_default_perimeter_spacing(*layer.object());
    return perimeter_width;
}

static size_t avoid_perimeters_inner(
    const AvoidCrossingPerimeters::Boundary &boundary, const Point &start_point, const Point &end_point, const Layer &layer, std::vector<TravelPoint> &result_out)
{
    const Polygons       &boundaries = boundary.boundaries;
    const EdgeGrid::Grid &edge_grid  = boundary.grid;
    Point                 start = start_point, end = end_point;

    std::vector<Intersection> intersections;
    {
        intersections.reserve(boundaries.size());
        AllIntersectionsVisitor visitor(edge_grid, intersections, Line(start, end));
        edge_grid.visit_cells_intersecting_line(start, end, visitor);
        Vec2d dir = (end - start).cast<double>();

        if (intersections.empty()) {

            auto                           search_radius         = 1.5 * get_perimeter_spacing(layer);
            const std::vector<ClosestLine> closest_line_to_start = get_closest_lines_in_radius(boundary.grid, start, search_radius);
            const std::vector<ClosestLine> closest_line_to_end   = get_closest_lines_in_radius(boundary.grid, end, search_radius);
            if (!(closest_line_to_start.empty() && closest_line_to_end.empty())) {
                auto new_start_point = closest_line_to_start.empty() ? start : closest_line_to_start.front().point;
                auto new_end_point   = closest_line_to_end.empty() ? end : closest_line_to_end.front().point;
                dir                  = (new_end_point - new_start_point).cast<double>();
                auto unit_direction  = dir.normalized();

                new_start_point = new_start_point - (unit_direction * double(coord_t(SCALED_EPSILON))).cast<coord_t>();
                new_end_point   = new_end_point + (unit_direction * double(coord_t(SCALED_EPSILON))).cast<coord_t>();
                AllIntersectionsVisitor visitor(edge_grid, intersections, Line(new_start_point, new_end_point));
                edge_grid.visit_cells_intersecting_line(new_start_point, new_end_point, visitor);
                if (!intersections.empty()) {
                    start = new_start_point;
                    end   = new_end_point;
                }
            }
        }

        for (Intersection &intersection : intersections) {
            float dist_from_line_begin = (intersection.point - boundary.boundaries[intersection.border_idx][intersection.line_idx]).cast<float>().norm();
            intersection.distance      = boundary.boundaries_params[intersection.border_idx][intersection.line_idx] + dist_from_line_begin;
        }
        std::sort(intersections.begin(), intersections.end(), [dir](const auto &l, const auto &r) { return (r.point - l.point).template cast<double>().dot(dir) > 0.; });


        const float search_radius = 2.f * get_perimeter_spacing(layer);


        intersections = extend_for_closest_lines(intersections, boundary, start, end, search_radius);
    }

    std::vector<TravelPoint> result;
    result.push_back({start, -1});

#if 0
    auto crossing_boundary_from_inside = [&boundary](const Point &start, const Intersection &intersection) {
        const Polygon &poly             = boundary.boundaries[intersection.border_idx];
        Vec2d          poly_line        = Line(poly[intersection.line_idx], poly[(intersection.line_idx + 1) % poly.size()]).normal().cast<double>();
        Vec2d          intersection_vec = (intersection.point - start).cast<double>();
        return poly_line.normalized().dot(intersection_vec.normalized()) >= 0;
    };
#endif

    for (auto it_first = intersections.begin(); it_first != intersections.end(); ++it_first) {

        const Intersection &intersection_first = *it_first;



        auto it_last_item = std::make_reverse_iterator(it_first) - 1;

        auto it_second_r = std::find_if(intersections.rbegin(), it_last_item,
                                        [&intersection_first](const Intersection &intersection) { return intersection_first.border_idx == intersection.border_idx; });


        size_t left_idx  = intersection_first.line_idx;
        size_t right_idx = intersection_first.line_idx + 1 == boundaries[intersection_first.border_idx].points.size() ? 0 : intersection_first.line_idx + 1;



        result.push_back({get_middle_point_offset(boundaries[intersection_first.border_idx], left_idx, right_idx, intersection_first.point, coord_t(SCALED_EPSILON)),
                          int(intersection_first.border_idx), intersection_first.do_not_remove});


        if (it_second_r != it_last_item) {

            auto it_second = it_second_r.base() - 1;

            const Intersection &intersection_second = *it_second;
            Direction           shortest_direction  = get_shortest_direction(boundary, intersection_first, intersection_second,
                                                                             boundary.boundaries_params[intersection_first.border_idx].back());

            if (shortest_direction == Direction::Forward)
                for (int line_idx = int(intersection_first.line_idx); line_idx != int(intersection_second.line_idx);
                     line_idx     = line_idx + 1 < int(boundaries[intersection_first.border_idx].size()) ? line_idx + 1 : 0)
                    result.push_back(
                        {get_polygon_vertex_offset(boundaries[intersection_first.border_idx],
                                                   (line_idx + 1 == int(boundaries[intersection_first.border_idx].points.size())) ? 0 : (line_idx + 1), coord_t(SCALED_EPSILON)),
                             int(intersection_first.border_idx)});
            else
                for (int line_idx = int(intersection_first.line_idx); line_idx != int(intersection_second.line_idx);
                     line_idx     = line_idx - 1 >= 0 ? line_idx - 1 : int(boundaries[intersection_first.border_idx].size()) - 1)
                    result.push_back(
                        {get_polygon_vertex_offset(boundaries[intersection_second.border_idx], line_idx + 0, coord_t(SCALED_EPSILON)), int(intersection_first.border_idx)});


            left_idx  = intersection_second.line_idx;
            right_idx = (intersection_second.line_idx >= (boundaries[intersection_second.border_idx].points.size() - 1)) ? 0 : (intersection_second.line_idx + 1);
            result.push_back({get_middle_point_offset(boundaries[intersection_second.border_idx], left_idx, right_idx, intersection_second.point, coord_t(SCALED_EPSILON)),
                              int(intersection_second.border_idx), intersection_second.do_not_remove});

            it_first = it_second;
        }
    }

    result.push_back({end, -1});

    auto result_polyline = to_polyline(result);
    (void) result_polyline;

#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_travel_to_svg(boundaries, Line(start, end), result, intersections, debug_out_path("AvoidCrossingPerimetersInner-initial-%d-%d.svg", layer.id(), iRun++));
    }
#endif

    if (!intersections.empty()) result = simplify_travel(boundary, result);

    auto simplified_result_polyline = to_polyline(result);
    (void) simplified_result_polyline;

#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_travel_to_svg(boundaries, Line(start, end), result, intersections, debug_out_path("AvoidCrossingPerimetersInner-final-%d-%d.svg", layer.id(), iRun++));
    }
#endif

    append(result_out, std::move(result));
    return intersections.size();
}


static size_t avoid_perimeters(const AvoidCrossingPerimeters::Boundary &boundary,
                               const Point                             &start,
                               const Point                             &end,
                               const Layer                             &layer,
                               Polyline                                &result_out)
{

    std::vector<TravelPoint> path;
    size_t num_intersections = avoid_perimeters_inner(boundary, start, end, layer, path);
    result_out = to_polyline(path);


#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_travel_to_svg(boundary.boundaries, Line(start, end), path, {}, debug_out_path("AvoidCrossingPerimeters-final-%d-%d.svg", layer.id(), iRun ++));
    }
#endif

    return num_intersections;
}




static bool any_expolygon_contains(const ExPolygons               &ex_polygons,
                                   const std::vector<BoundingBox> &ex_polygons_bboxes,
                                   const EdgeGrid::Grid            &grid_lslice,
                                   const Line                      &travel)
{
    assert(ex_polygons.size() == ex_polygons_bboxes.size());
    if(!grid_lslice.bbox().contains(travel.a) || !grid_lslice.bbox().contains(travel.b))
        return false;

    FirstIntersectionVisitor visitor(grid_lslice);
    visitor.pt_current = &travel.a;
    visitor.pt_next    = &travel.b;
    grid_lslice.visit_cells_intersecting_line(*visitor.pt_current, *visitor.pt_next, visitor);
    if (!visitor.intersect) {
        for (const ExPolygon &ex_polygon : ex_polygons) {
            const BoundingBox &bbox = ex_polygons_bboxes[&ex_polygon - &ex_polygons.front()];
            if (bbox.contains(travel.a) && bbox.contains(travel.b) && ex_polygon.contains(travel.a))
                return true;
        }
    }
    return false;
}



static bool any_expolygon_contains(const ExPolygons &ex_polygons, const std::vector<BoundingBox> &ex_polygons_bboxes, const EdgeGrid::Grid &grid_lslice, const Polyline &travel)
{
    assert(ex_polygons.size() == ex_polygons_bboxes.size());
    if(std::any_of(travel.points.begin(), travel.points.end(), [&grid_lslice](const Point &point) { return !grid_lslice.bbox().contains(point); }))
        return false;

    FirstIntersectionVisitor visitor(grid_lslice);
    bool any_intersection = false;
    for (size_t line_idx = 1; line_idx < travel.size(); ++line_idx) {
        visitor.pt_current = &travel.points[line_idx - 1];
        visitor.pt_next    = &travel.points[line_idx];
        grid_lslice.visit_cells_intersecting_line(*visitor.pt_current, *visitor.pt_next, visitor);
        any_intersection = visitor.intersect;
        if (any_intersection) break;
    }

    if (!any_intersection) {
        for (const ExPolygon &ex_polygon : ex_polygons) {
            const BoundingBox &bbox = ex_polygons_bboxes[&ex_polygon - &ex_polygons.front()];
            if (std::all_of(travel.points.begin(), travel.points.end(), [&bbox](const Point &point) { return bbox.contains(point); }) &&
                ex_polygon.contains(travel.points.front()))
                return true;
        }
    }
    return false;
}

static bool need_wipe(const GCode                    &gcodegen,
                      const ExPolygons               &lslices_offset,
                      const std::vector<BoundingBox> &lslices_offset_bboxes,
                      const EdgeGrid::Grid           &grid_lslices_offset,
                      const Line                     &original_travel,
                      const Polyline                 &result_travel,
                      const size_t                    intersection_count)
{
    bool z_lift_enabled = gcodegen.config().z_hop.get_at(gcodegen.writer().filament()->id()) > 0.;
    bool wipe_needed    = false;



    if (intersection_count > 0) {


        if (z_lift_enabled) {
            if (any_expolygon_contains(lslices_offset, lslices_offset_bboxes, grid_lslices_offset, original_travel)) {


                wipe_needed = !(result_travel.size() > 2 && result_travel.first_point() == original_travel.a && result_travel.last_point() == original_travel.b) &&
                              !any_expolygon_contains(lslices_offset, lslices_offset_bboxes, grid_lslices_offset, result_travel);
            } else {
                wipe_needed = true;
            }
        } else {
            wipe_needed = !any_expolygon_contains(lslices_offset, lslices_offset_bboxes, grid_lslices_offset, result_travel);
        }
    }

    return wipe_needed;
}


static void resample_polygon(Polygon &polygon, double dist_from_vertex, double max_allowed_distance)
{
    Points resampled_poly;
    resampled_poly.reserve(3 * polygon.size());
    for (size_t pt_idx = 0; pt_idx < polygon.size(); ++pt_idx) {
        resampled_poly.emplace_back(polygon[pt_idx]);

        const Point &p1          = polygon[pt_idx];
        const Point &p2          = polygon[next_idx_modulo(pt_idx, polygon.size())];
        const Vec2d  line_vec    = (p2 - p1).cast<double>();
        double       line_length = line_vec.norm();
        const Vector vertex_offset_vec = (line_vec.normalized() * dist_from_vertex).cast<coord_t>();
        if (line_length > 2 * dist_from_vertex && vertex_offset_vec != Vector(0, 0)) {
            resampled_poly.emplace_back(p1 + vertex_offset_vec);

            const Vec2d  new_vertex_vec        = (p2 - p1 - 2 * vertex_offset_vec).cast<double>();
            const double new_vertex_vec_length = new_vertex_vec.norm();
            if (new_vertex_vec_length > max_allowed_distance) {
                const Vec2d &prev_point  = resampled_poly.back().cast<double>();
                const size_t parts_count = size_t(ceil(new_vertex_vec_length / max_allowed_distance));
                for (size_t part_idx = 1; part_idx < parts_count; ++part_idx) {
                    const double part_param = double(part_idx) / double(parts_count);
                    const Vec2d  new_point  = prev_point + new_vertex_vec * part_param;
                    resampled_poly.emplace_back(new_point.cast<coord_t>());
                }
            }

            resampled_poly.emplace_back(p2 - vertex_offset_vec);
        }
    }
    polygon.points = std::move(resampled_poly);
}

static void resample_expolygon(ExPolygon &ex_polygon, double dist_from_vertex, double max_allowed_distance)
{
    resample_polygon(ex_polygon.contour, dist_from_vertex, max_allowed_distance);
    for (Polygon &polygon : ex_polygon.holes)
        resample_polygon(polygon, dist_from_vertex, max_allowed_distance);
}

static void resample_expolygons(ExPolygons &ex_polygons, double dist_from_vertex, double max_allowed_distance)
{
    for (ExPolygon &ex_poly : ex_polygons)
        resample_expolygon(ex_poly, dist_from_vertex, max_allowed_distance);
}

static void precompute_polygon_distances(const Polygon &polygon, std::vector<float> &polygon_distances_out)
{
    polygon_distances_out.assign(polygon.size() + 1, 0.f);
    for (size_t point_idx = 1; point_idx < polygon.size(); ++point_idx)
        polygon_distances_out[point_idx] = polygon_distances_out[point_idx - 1] + float((polygon[point_idx] - polygon[point_idx - 1]).cast<double>().norm());
    polygon_distances_out.back() = polygon_distances_out[polygon.size() - 1] + float((polygon.points.back() - polygon.points.front()).cast<double>().norm());
}

static void precompute_expolygon_distances(const ExPolygon &ex_polygon, std::vector<std::vector<float>> &expolygon_distances_out)
{
    expolygon_distances_out.assign(ex_polygon.holes.size() + 1, std::vector<float>());
    precompute_polygon_distances(ex_polygon.contour, expolygon_distances_out.front());
    for (size_t hole_idx = 0; hole_idx < ex_polygon.holes.size(); ++hole_idx)
        precompute_polygon_distances(ex_polygon.holes[hole_idx], expolygon_distances_out[hole_idx + 1]);
}


static std::vector<float> contour_distance(const EdgeGrid::Grid     &grid,
                                    const std::vector<float> &poly_distances,
                                    const size_t              contour_idx,
                                    const Polygon            &polygon,
                                    double                    compensation,
                                    double                    search_radius)
{
    assert(! polygon.empty());
    assert(polygon.size() >= 2);

    std::vector<float> out;

    if (polygon.size() > 2)
    {
        struct Visitor {
            Visitor(const EdgeGrid::Grid &grid, const size_t contour_idx, const std::vector<float> &polygon_distances, double dist_same_contour_accept, double dist_same_contour_reject) :
                grid(grid), idx_contour(contour_idx), contour(grid.contours()[contour_idx]), boundary_parameters(polygon_distances), dist_same_contour_accept(dist_same_contour_accept), dist_same_contour_reject(dist_same_contour_reject) {}

            void init(const Points &contour, const Point &apoint)
            {
                this->idx_point  = &apoint - contour.data();
                this->point      = apoint;
                this->found      = false;
                this->dir_inside = this->dir_inside_at_point(contour, this->idx_point);
                this->distance   = std::numeric_limits<double>::max();
            }

            bool operator()(coord_t iy, coord_t ix)
            {

                auto cell_data_range = this->grid.cell_data_range(iy, ix);
                for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second;
                     ++it_contour_and_segment) {

                    std::pair<const Point &, const Point &> segment = this->grid.segment(*it_contour_and_segment);
                    const Vec2d  v        = (segment.second - segment.first).cast<double>();
                    const Vec2d  va       = (this->point - segment.first).cast<double>();
                    const double l2       = v.squaredNorm();
                    const double t        = (l2 == 0.0) ? 0. : std::clamp(va.dot(v) / l2, 0., 1.);

                    const Vec2d  foot     = segment.first.cast<double>() + t * v;
                    const Vec2d  bisector = foot - this->point.cast<double>();
                    const double dist     = bisector.norm();

                    if ((!this->found || dist < this->distance) && this->dir_inside.dot(bisector) > 0) {
                        bool accept = true;
                        if (it_contour_and_segment->first == idx_contour) {



                            const EdgeGrid::Contour &contour   = grid.contours()[it_contour_and_segment->first];
                            double                   param_lo  = boundary_parameters[this->idx_point];
                            double                   param_hi  = t * sqrt(l2);
                            double                   param_end = boundary_parameters.back();
                            const size_t ipt = it_contour_and_segment->second;
                            if (contour.begin() + ipt + 1 < contour.end())
                                param_hi += boundary_parameters[ipt];
                            if (param_lo > param_hi)
                                std::swap(param_lo, param_hi);
                            assert(param_lo > -SCALED_EPSILON && param_lo <= param_end + SCALED_EPSILON);
                            assert(param_hi > -SCALED_EPSILON && param_hi <= param_end + SCALED_EPSILON);
                            double dist_along_contour = std::min(param_hi - param_lo, param_lo + param_end - param_hi);
                            if (dist_along_contour < dist_same_contour_accept)
                                accept = false;
                            else if (dist < dist_same_contour_reject + SCALED_EPSILON) {




                                bool inside = (t == 0.) ? this->inside_corner(contour, ipt, this->point) :
                                              (t == 1.) ? this->inside_corner(contour, contour.segment_idx_next(ipt), this->point) :
                                                          this->left_of_segment(contour, ipt, this->point);
                                accept      = inside && dist_along_contour > 0.6 * M_PI * dist;
                            }
                        }
                        if (accept && (!this->found || dist < this->distance)) {

                            this->distance = dist;
                            this->found    = true;
                        }
                    }
                }

                return true;
            }

            const EdgeGrid::Grid 			   &grid;
            const size_t 		  				idx_contour;
            const EdgeGrid::Contour            &contour;

            const std::vector<float>           &boundary_parameters;
            const double                        dist_same_contour_accept;
            const double 						dist_same_contour_reject;

            size_t 								idx_point;
            Point			      				point;

            Vec2d								dir_inside;
            bool 								found;
            double 								distance;

        private:
            static Vec2d dir_inside_at_point(const Points &contour, size_t i)
            {
                size_t iprev = prev_idx_modulo(i, contour);
                size_t inext = next_idx_modulo(i, contour);
                Vec2d  v1    = (contour[i] - contour[iprev]).cast<double>();
                Vec2d  v2    = (contour[inext] - contour[i]).cast<double>();
                return Vec2d(-v1.y() - v2.y(), v1.x() + v2.x());
            }

            static bool inside_corner(const EdgeGrid::Contour &contour, size_t i, const Point &pt_oposite)
            {
                const Vec2d pt         = pt_oposite.cast<double>();
                const Point &pt_prev   = contour.segment_prev(i);
                const Point &pt_this   = contour.segment_start(i);
                const Point &pt_next   = contour.segment_end(i);
                Vec2d       v1         = (pt_this - pt_prev).cast<double>();
                Vec2d       v2         = (pt_next - pt_this).cast<double>();
                bool        left_of_v1 = cross2(v1, pt - pt_prev.cast<double>()) > 0.;
                bool        left_of_v2 = cross2(v2, pt - pt_this.cast<double>()) > 0.;
                return cross2(v1, v2) > 0 ? left_of_v1 && left_of_v2 :
                                            left_of_v1 || left_of_v2;
            }

            static bool left_of_segment(const EdgeGrid::Contour &contour, size_t i, const Point &pt_oposite)
            {
                const Vec2d  pt      = pt_oposite.cast<double>();
                const Point &pt_this = contour.segment_start(i);
                const Point &pt_next = contour.segment_end(i);
                Vec2d        v       = (pt_next - pt_this).cast<double>();
                return cross2(v, pt - pt_this.cast<double>()) > 0.;
            }
        } visitor(grid, contour_idx, poly_distances, 0.5 * compensation * M_PI, search_radius);

        out.reserve(polygon.size());
        Point radius_vector(search_radius, search_radius);
        for (const Point &pt : polygon.points) {
            visitor.init(polygon.points, pt);
            grid.visit_cells_intersecting_box(BoundingBox(pt - radius_vector, pt + radius_vector), visitor);
            out.emplace_back(float(visitor.found ? std::min(visitor.distance, search_radius) : search_radius));
        }
    }

    return out;
}



static ExPolygons inner_offset(const ExPolygons &ex_polygons, double offset_dis)
{

    const std::vector<double> min_contour_width_values = {offset_dis / 2., offset_dis, 2. * offset_dis + SCALED_EPSILON};
    ExPolygons                ex_poly_result           = ex_polygons;

    for (ExPolygon &ex_poly : ex_poly_result) {
        for (auto iter = ex_poly.holes.begin(); iter != ex_poly.holes.end();) {
            auto out_offset_holes = offset(*iter, scale_(0.1f));
            if (out_offset_holes.empty()) {
                iter = ex_poly.holes.erase(iter);
            } else {
                ++iter;
            }
        }
    }
    resample_expolygons(ex_poly_result, offset_dis / 2, scaled<double>(0.5));

    for (ExPolygon &ex_poly : ex_poly_result) {
        BoundingBox bbox(get_extents(ex_poly));
        bbox.offset(SCALED_EPSILON);


        if (Vec2d bbox_size = bbox.size().cast<double>(); bbox_size.x() * bbox_size.y() < Slic3r::sqr(scale_(0.1f))) continue;

        for (const double &min_contour_width : min_contour_width_values) {
            const size_t min_contour_width_idx = &min_contour_width - &min_contour_width_values.front();
            const double search_radius         = 2. * (offset_dis + min_contour_width);

            EdgeGrid::Grid grid;
            grid.set_bbox(bbox);
            grid.create(ex_poly, coord_t(0.7 * search_radius));

            std::vector<std::vector<float>> ex_poly_distances;
            precompute_expolygon_distances(ex_poly, ex_poly_distances);

            std::vector<std::vector<float>> offsets;
            offsets.reserve(ex_poly.holes.size() + 1);
            for (size_t idx_contour = 0; idx_contour <= ex_poly.holes.size(); ++idx_contour) {
                const Polygon &poly = (idx_contour == 0) ? ex_poly.contour : ex_poly.holes[idx_contour - 1];
                assert(poly.is_counter_clockwise() == (idx_contour == 0));
                std::vector<float> distances = contour_distance(grid, ex_poly_distances[idx_contour], idx_contour, poly, offset_dis, search_radius);
                for (float &distance : distances) {
                    if (distance < min_contour_width)
                        distance = 0.0f;
                    else if (distance > min_contour_width + 2. * offset_dis)
                        distance = -float(offset_dis);
                    else
                        distance = -(distance - float(min_contour_width)) / 2.f;
                }
                offsets.emplace_back(distances);
            }

            ExPolygons offset_ex_poly = variable_offset_inner_ex(ex_poly, offsets);

            if (offset_ex_poly.size() == 1 && offset_ex_poly.front().holes.size() == ex_poly.holes.size()) {
                ex_poly = std::move(offset_ex_poly.front());
                break;
            } else if ((min_contour_width_idx + 1) < min_contour_width_values.size()) {
                continue;
            } else if (offset_ex_poly.size() == 1) {
                ex_poly = std::move(offset_ex_poly.front());
                break;
            } else if (offset_ex_poly.size() > 1) {


                double max_area     = offset_ex_poly.front().area();
                size_t max_area_idx = 0;
                for (size_t poly_idx = 1; poly_idx < offset_ex_poly.size(); ++poly_idx) {
                    double area = offset_ex_poly[poly_idx].area();
                    if (max_area < area) {
                        max_area     = area;
                        max_area_idx = poly_idx;
                    }
                }
                ex_poly = std::move(offset_ex_poly[max_area_idx]);
                break;
            }
        }
    }
    return ex_poly_result;
}




static ExPolygons get_boundary(const Layer &layer, float perimeter_spacing)
{

    const float perimeter_offset  = perimeter_spacing / 2.f;
    auto const *support_layer     = dynamic_cast<const SupportLayer *>(&layer);
    ExPolygons  boundary          = union_ex(inner_offset(layer.lslices, 1.5 * perimeter_spacing));
    if(support_layer) {
#ifdef INCLUDE_SUPPORTS_IN_BOUNDARY
        append(boundary, inner_offset(support_layer->support_islands, 1.5 * perimeter_spacing));
#endif
        auto *layer_below = layer.object()->get_first_layer_bellow_printz(layer.print_z, EPSILON);
        if (layer_below)
            append(boundary, inner_offset(layer_below->lslices, 1.5 * perimeter_spacing));

        boundary = union_ex(boundary);
    }

    size_t      polygons_count    = 0;
    for (const LayerRegion *layer_region : layer.regions())
        for (const Surface &surface : layer_region->fill_surfaces.surfaces)
            if (surface.is_top()) ++polygons_count;

    if (polygons_count > 0) {
        ExPolygons top_layer_polygons;
        top_layer_polygons.reserve(polygons_count);
        for (const LayerRegion *layer_region : layer.regions())
            for (const Surface &surface : layer_region->fill_surfaces.surfaces)
                if (surface.is_top()) top_layer_polygons.emplace_back(surface.expolygon);

        top_layer_polygons = union_ex(top_layer_polygons);
        return diff_ex(boundary, offset_ex(top_layer_polygons, -1.2 * perimeter_offset));
    }

    return boundary;
}


static Polygons get_boundary_external(const Layer &layer)
{
    const float perimeter_spacing = get_perimeter_spacing_external(layer);
    const float perimeter_offset  = perimeter_spacing / 2.f;
    auto const *support_layer     = dynamic_cast<const SupportLayer *>(&layer);
    Polygons    boundary;
#ifdef INCLUDE_SUPPORTS_IN_BOUNDARY
    ExPolygons  supports_boundary;
#endif

    for (const PrintObject *object : layer.object()->print()->objects()) {
        Polygons   holes_per_obj;
#ifdef INCLUDE_SUPPORTS_IN_BOUNDARY
        ExPolygons supports_per_obj;
#endif
        if (const Layer *l = object->get_layer_at_printz(layer.print_z, EPSILON); l)
            for (const ExPolygon &island : l->lslices)
                append(holes_per_obj, island.holes);
        if (support_layer) {
            auto *layer_below = object->get_first_layer_bellow_printz(layer.print_z, EPSILON);
            if (layer_below)
                for (const ExPolygon &island : layer_below->lslices)
                    append(holes_per_obj, island.holes);
#ifdef INCLUDE_SUPPORTS_IN_BOUNDARY
            append(supports_per_obj, support_layer->support_islands);
#endif
        }



        polygons_reverse(holes_per_obj);

        for (const PrintInstance &instance : object->instances()) {
            size_t boundary_idx = boundary.size();
            append(boundary, holes_per_obj);
            for (; boundary_idx < boundary.size(); ++boundary_idx)
                boundary[boundary_idx].translate(instance.shift);
#ifdef INCLUDE_SUPPORTS_IN_BOUNDARY
            size_t support_idx = supports_boundary.size();
            append(supports_boundary, supports_per_obj);
            for (; support_idx < supports_boundary.size(); ++support_idx)
                supports_boundary[support_idx].translate(instance.shift);
#endif
        }
    }


    boundary = expand(boundary, perimeter_offset);

    for (Polygon &poly : boundary)
        poly.reverse();
#ifdef INCLUDE_SUPPORTS_IN_BOUNDARY
    append(boundary, to_polygons(inner_offset(supports_boundary, perimeter_offset)));
#endif
    return boundary;
}

static void init_boundary_distances(AvoidCrossingPerimeters::Boundary *boundary)
{
    boundary->boundaries_params.assign(boundary->boundaries.size(), std::vector<float>());
    for (size_t poly_idx = 0; poly_idx < boundary->boundaries.size(); ++poly_idx)
        precompute_polygon_distances(boundary->boundaries[poly_idx], boundary->boundaries_params[poly_idx]);
}

void init_boundary(AvoidCrossingPerimeters::Boundary *boundary, Polygons &&boundary_polygons)
{
    boundary->clear();
    boundary->boundaries = std::move(boundary_polygons);

    BoundingBox bbox(get_extents(boundary->boundaries));
    bbox.offset(SCALED_EPSILON);
    boundary->bbox = BoundingBoxf(bbox.min.cast<double>(), bbox.max.cast<double>());
    boundary->grid.set_bbox(bbox);

    boundary->grid.create(boundary->boundaries, coord_t(scale_(1.)));
    init_boundary_distances(boundary);
}

static void init_boundary(AvoidCrossingPerimeters::Boundary *boundary, Polygons &&boundary_polygons, const std::vector<Point>& merge_poins)
{
    boundary->clear();
    boundary->boundaries = std::move(boundary_polygons);

    BoundingBox bbox(get_extents(boundary->boundaries));
    for (const auto& merge_point : merge_poins) {
        bbox.merge(merge_point);
    }
    bbox.offset(bbox.radius());
    boundary->bbox = BoundingBoxf(bbox.min.cast<double>(), bbox.max.cast<double>());
    boundary->grid.set_bbox(bbox);

    boundary->grid.create(boundary->boundaries, coord_t(scale_(1.)));
    init_boundary_distances(boundary);
}


Polyline AvoidCrossingPerimeters::travel_to(const GCode &gcodegen, const Point &point, bool *could_be_wipe_disabled)
{


    bool        use_external  = m_use_external_mp || m_use_external_mp_once;
    Point       scaled_origin = use_external ? Point::new_scale(gcodegen.origin()(0), gcodegen.origin()(1)) : Point(0, 0);
    const Point start         = gcodegen.last_pos() + scaled_origin;
    const Point end           = point + scaled_origin;
    const Line  travel(start, end);
    Polyline result_pl;
    size_t   travel_intersection_count = 0;
    Vec2d startf = start.cast<double>();
    Vec2d endf   = end  .cast<double>();

    const ExPolygons               &lslices          = gcodegen.layer()->lslices;
    const std::vector<BoundingBox> &lslices_bboxes   = gcodegen.layer()->lslices_bboxes;
    bool                            is_support_layer = (dynamic_cast<const SupportLayer *>(gcodegen.layer()) != nullptr);
    if (!use_external && (is_support_layer || (!m_lslices_offset.empty() && !any_expolygon_contains(m_lslices_offset, m_lslices_offset_bboxes, m_grid_lslice, travel)))) {

        if (m_internal.boundaries.empty()) {
            init_boundary(&m_internal, to_polygons(get_boundary(*gcodegen.layer(), get_perimeter_spacing(*gcodegen.layer()))), {start, end});
        } else if (!(m_internal.bbox.contains(startf) && m_internal.bbox.contains(endf))) {

            m_internal.clear();
            init_boundary(&m_internal, to_polygons(get_boundary(*gcodegen.layer(), get_perimeter_spacing(*gcodegen.layer()))), {start, end});
        }

        if (!m_internal.boundaries.empty()) {
            travel_intersection_count = avoid_perimeters(m_internal, start, end, *gcodegen.layer(), result_pl);
            result_pl.points.front()  = start;
            result_pl.points.back()   = end;
        }
    } else if (use_external) {

        if (m_external.boundaries.empty()) {
            init_boundary(&m_external, get_boundary_external(*gcodegen.layer()), {start, end});
        } else if (!(m_external.bbox.contains(startf) && m_external.bbox.contains(endf))) {

            m_external.clear();
            init_boundary(&m_external, get_boundary_external(*gcodegen.layer()), {start, end});
        }


        if (!m_external.boundaries.empty())
        {
            travel_intersection_count = avoid_perimeters(m_external, start, end, *gcodegen.layer(), result_pl);
            result_pl.points.front()  = start;
            result_pl.points.back()   = end;

        }
    }

    if(result_pl.empty()) {

        result_pl                 = {start, end};
        travel_intersection_count = 0;
    }

    const ConfigOptionFloatOrPercent &opt_max_detour             = gcodegen.config().max_travel_detour_distance;
    bool                              max_detour_length_exceeded = false;
    if (opt_max_detour.value > 0) {
        double direct_length     = travel.length();
        double detour            = result_pl.length() - direct_length;
        double max_detour_length = opt_max_detour.percent ?
            direct_length * 0.01 * opt_max_detour.value :
            scale_(opt_max_detour.value);
        if (detour > max_detour_length) {
            result_pl = {start, end};
            max_detour_length_exceeded = true;
        }
    }

    if (use_external) {
        result_pl.translate(-scaled_origin);
        *could_be_wipe_disabled = false;
    } else if (max_detour_length_exceeded) {
        *could_be_wipe_disabled = false;
    } else
        *could_be_wipe_disabled = !need_wipe(gcodegen, m_lslices_offset, m_lslices_offset_bboxes, m_grid_lslice, travel, result_pl, travel_intersection_count);

    return result_pl;
}



void AvoidCrossingPerimeters::init_layer(const Layer &layer)
{
    m_internal.clear();
    m_external.clear();

    m_lslices_offset.clear();
    m_lslices_offset_bboxes.clear();
    for (auto coeff : {0.6f, 0.5f, 0.45f}) {
        m_lslices_offset = offset_ex(layer.lslices, -get_external_perimeter_width(layer) * coeff);
        if (!m_lslices_offset.empty()) break;
    }
    m_lslices_offset_bboxes.reserve(m_lslices_offset.size());
    for (const auto &ex_polygon : m_lslices_offset) m_lslices_offset_bboxes.emplace_back(get_extents(ex_polygon));

    BoundingBox bbox_slice(get_extents(layer.lslices));
    bbox_slice.offset(SCALED_EPSILON);

    m_grid_lslice.set_bbox(bbox_slice);

    m_grid_lslice.create(m_lslices_offset, coord_t(scale_(1.)));
}

#if 0
static double travel_length(const std::vector<TravelPoint> &travel) {
    double total_length = 0;
    for (size_t idx = 1; idx < travel.size(); ++idx)
        total_length += (travel[idx].point - travel[idx - 1].point).cast<double>().norm();

    return total_length;
}


static size_t avoid_perimeters_inner(const AvoidCrossingPerimeters::Boundary &boundary,
                                     const Point              &start,
                                     const Point              &end,
                                     std::vector<TravelPoint> &result_out)
{
    const Polygons           &boundaries = boundary.boundaries;
    const EdgeGrid::Grid     &edge_grid = boundary.grid;

    std::vector<Intersection> intersections;
    {
        intersections.reserve(boundaries.size());
        AllIntersectionsVisitor visitor(edge_grid, intersections, Line(start, end));
        edge_grid.visit_cells_intersecting_line(start, end, visitor);
        Vec2d dir = (end - start).cast<double>();
        for (Intersection &intersection : intersections)
            intersection.distance = boundary.boundaries_params[intersection.border_idx][intersection.line_idx];
        std::sort(intersections.begin(), intersections.end(), [dir](const auto &l, const auto &r) { return (r.point - l.point).template cast<double>().dot(dir) > 0.; });
    }

    std::vector<TravelPoint> result;
    result.push_back({start, -1});
    for (auto it_first = intersections.begin(); it_first != intersections.end(); ++it_first) {

        const Intersection &intersection_first = *it_first;

        auto it_last_item = std::make_reverse_iterator(it_first) - 1;

        auto it_second_r  = std::find_if(intersections.rbegin(), it_last_item, [&intersection_first](const Intersection &intersection) {
            return intersection_first.border_idx == intersection.border_idx;
        });


        size_t left_idx  = intersection_first.line_idx;
        size_t right_idx = intersection_first.line_idx + 1 == boundaries[intersection_first.border_idx].points.size() ? 0 : intersection_first.line_idx + 1;



        result.push_back({get_middle_point_offset(boundaries[intersection_first.border_idx], left_idx, right_idx, intersection_first.point, coord_t(SCALED_EPSILON)), int(intersection_first.border_idx)});


        if (it_second_r != it_last_item) {

            auto it_second = it_second_r.base() - 1;

            const Intersection &intersection_second = *it_second;
            Direction           shortest_direction  = get_shortest_direction(boundary, intersection_first, intersection_second,
                                                                  boundary.boundaries_params[intersection_first.border_idx].back());

            if (shortest_direction == Direction::Forward)
                for (int line_idx = int(intersection_first.line_idx); line_idx != int(intersection_second.line_idx);
                    line_idx      = line_idx + 1 < int(boundaries[intersection_first.border_idx].size()) ? line_idx + 1 : 0)
                    result.push_back({get_polygon_vertex_offset(boundaries[intersection_first.border_idx],
                                                                (line_idx + 1 == int(boundaries[intersection_first.border_idx].points.size())) ? 0 : (line_idx + 1), coord_t(SCALED_EPSILON)), int(intersection_first.border_idx)});
            else
                for (int line_idx = int(intersection_first.line_idx); line_idx != int(intersection_second.line_idx);
                    line_idx      = line_idx - 1 >= 0 ? line_idx - 1 : int(boundaries[intersection_first.border_idx].size()) - 1)
                    result.push_back({get_polygon_vertex_offset(boundaries[intersection_second.border_idx], line_idx + 0, coord_t(SCALED_EPSILON)), int(intersection_first.border_idx)});


            left_idx  = intersection_second.line_idx;
            right_idx = (intersection_second.line_idx >= (boundaries[intersection_second.border_idx].points.size() - 1)) ? 0 : (intersection_second.line_idx + 1);
            result.push_back({get_middle_point_offset(boundaries[intersection_second.border_idx], left_idx, right_idx, intersection_second.point, coord_t(SCALED_EPSILON)), int(intersection_second.border_idx)});

            it_first = it_second;
        }
    }

    result.push_back({end, -1});

#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_travel_to_svg(boundaries, Line(start, end), result, intersections,
                             debug_out_path("AvoidCrossingPerimetersInner-initial-%d.svg", iRun++));
    }
#endif

    if (! intersections.empty())
        result = simplify_travel(boundary, result);

#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_travel_to_svg(boundaries, Line(start, end), result, intersections,
                             debug_out_path("AvoidCrossingPerimetersInner-final-%d.svg", iRun++));
    }
#endif

    append(result_out, std::move(result));
    return intersections.size();
}

static std::vector<TravelPoint> simplify_travel_heuristics(const AvoidCrossingPerimeters::Boundary &boundary,
                                                           const std::vector<TravelPoint> &travel)
{
    std::vector<TravelPoint>  simplified_path;
    std::vector<Intersection> intersections;
    AllIntersectionsVisitor   visitor(boundary.grid, intersections);
    simplified_path.reserve(travel.size());
    simplified_path.emplace_back(travel.front());
    for (size_t point_idx = 1; point_idx < travel.size(); ++point_idx) {

        while (point_idx < travel.size() && travel[point_idx - 1].border_idx == travel[point_idx].border_idx) {
            simplified_path.emplace_back(travel[point_idx]);
            point_idx++;
        }

        if (point_idx < travel.size()) {
            const TravelPoint       &current                 = travel[point_idx - 1];
            const TravelPoint       &next                    = travel[point_idx];
            TravelPoint              new_next                = next;
            size_t                   new_point_idx           = point_idx;
            double                   path_length             = (next.point - current.point).cast<double>().norm();
            double                   new_path_shorter_by     = 0.;
            size_t                   border_idx_change_count = 0;
            std::vector<TravelPoint> shortcut;
            for (size_t point_idx_2 = point_idx + 1; point_idx_2 < travel.size(); ++point_idx_2) {
                const TravelPoint &possible_new_next = travel[point_idx_2];
                if (travel[point_idx_2 - 1].border_idx != travel[point_idx_2].border_idx)
                    border_idx_change_count++;

                if (border_idx_change_count >= 2)
                    break;

                path_length += (possible_new_next.point - travel[point_idx_2 - 1].point).cast<double>().norm();
                double shortcut_length = (possible_new_next.point - current.point).cast<double>().norm();
                if ((path_length - shortcut_length) <= scale_(10.0))
                    continue;

                intersections.clear();
                visitor.reset();
                visitor.travel_line.a       = current.point;
                visitor.travel_line.b       = possible_new_next.point;
                boundary.grid.visit_cells_intersecting_line(visitor.travel_line.a, visitor.travel_line.b, visitor);
                if (!intersections.empty()) {
                    Vec2d dir = (visitor.travel_line.b - visitor.travel_line.a).cast<double>();
                    std::sort(intersections.begin(), intersections.end(), [dir](const auto &l, const auto &r) { return (r.point - l.point).template cast<double>().dot(dir) > 0.; });
                    size_t last_border_idx_count = 0;
                    for (const Intersection &intersection : intersections)
                        if (int(intersection.border_idx) == possible_new_next.border_idx)
                            ++last_border_idx_count;

                    if (last_border_idx_count > 0)
                        continue;

                    std::vector<TravelPoint> possible_shortcut;
                    avoid_perimeters_inner(boundary, current.point, possible_new_next.point, possible_shortcut);
                    double shortcut_travel = travel_length(possible_shortcut);
                    if (path_length > shortcut_travel && path_length - shortcut_travel > new_path_shorter_by) {
                        new_path_shorter_by = path_length - shortcut_travel;
                        shortcut            = possible_shortcut;
                        new_next            = possible_new_next;
                        new_point_idx       = point_idx_2;
                    }
                }
            }

            if (!shortcut.empty()) {
                assert(shortcut.size() >= 2);
                simplified_path.insert(simplified_path.end(), shortcut.begin() + 1, shortcut.end() - 1);
                point_idx = new_point_idx;
            }

            simplified_path.emplace_back(new_next);
        }
    }

    return simplified_path;
}


static size_t avoid_perimeters(const AvoidCrossingPerimeters::Boundary &boundary,
                               const Point              &start,
                               const Point              &end,
                               Polyline                 &result_out)
{

    std::vector<TravelPoint> path;
    size_t num_intersections = avoid_perimeters_inner(boundary, start, end, path);
    if (num_intersections) {
        path = simplify_travel_heuristics(boundary, path);
        std::reverse(path.begin(), path.end());
        path = simplify_travel_heuristics(boundary, path);
        std::reverse(path.begin(), path.end());
    }

    result_out = to_polyline(path);

#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_travel_to_svg(boundaries, Line(start, end), path, {}, debug_out_path("AvoidCrossingPerimeters-final-%d.svg", iRun ++));
    }
#endif

    return num_intersections;
}


Polyline AvoidCrossingPerimeters::travel_to(const GCode &gcodegen, const Point &point, bool *could_be_wipe_disabled)
{


    bool     use_external  = m_use_external_mp || m_use_external_mp_once;
    Point    scaled_origin = use_external ? Point::new_scale(gcodegen.origin()(0), gcodegen.origin()(1)) : Point(0, 0);
    Point    start         = gcodegen.last_pos() + scaled_origin;
    Point    end           = point + scaled_origin;
    Polyline result_pl;
    size_t   travel_intersection_count = 0;
    Vec2d startf = start.cast<double>();
    Vec2d endf   = end  .cast<double>();

    if (Geometry::liang_barsky_line_clipping(startf, endf, (use_external ? m_external : m_internal).bbox)) {


        travel_intersection_count = avoid_perimeters((use_external ? m_external : m_internal), startf.cast<coord_t>(), endf.cast<coord_t>(),
                                                     result_pl);
        result_pl.points.front()  = start;
        result_pl.points.back()   = end;
    } else {

        result_pl                 = {start, end};
        travel_intersection_count = 0;
    }

    Line travel(start, end);
    double max_detour_length scale_(gcodegen.config().max_travel_detour_distance);
    if (max_detour_length > 0 && (result_pl.length() - travel.length()) > max_detour_length)
        result_pl = {start, end};

    if (use_external) {
        result_pl.translate(-scaled_origin);
        *could_be_wipe_disabled = false;
    } else
        *could_be_wipe_disabled = !need_wipe(gcodegen, m_grid_lslice, travel, result_pl, travel_intersection_count);

    return result_pl;
}


static std::pair<Polygons, Polygons> split_expolygon(const ExPolygons &ex_polygons)
{
    Polygons contours, holes;
    contours.reserve(ex_polygons.size());
    holes.reserve(std::accumulate(ex_polygons.begin(), ex_polygons.end(), size_t(0),
                                  [](size_t sum, const ExPolygon &ex_poly) { return sum + ex_poly.holes.size(); }));
    for (const ExPolygon &ex_poly : ex_polygons) {
        contours.emplace_back(ex_poly.contour);
        append(holes, ex_poly.holes);
    }
    return std::make_pair(std::move(contours), std::move(holes));
}


static ExPolygons get_boundary(const Layer &layer)
{
    const float perimeter_spacing = get_perimeter_spacing(layer);
    const float perimeter_offset  = perimeter_spacing / 2.f;
    size_t      polygons_count    = 0;
    for (const LayerRegion *layer_region : layer.regions())
        polygons_count += layer_region->slices.surfaces.size();

    ExPolygons boundary;
    boundary.reserve(polygons_count);
    for (const LayerRegion *layer_region : layer.regions())
        for (const Surface &surface : layer_region->slices.surfaces)
            boundary.emplace_back(surface.expolygon);

    boundary                      = union_ex(boundary);
    ExPolygons perimeter_boundary = offset_ex(boundary, -perimeter_offset);
    ExPolygons result_boundary;
    if (perimeter_boundary.size() != boundary.size()) {


        ExPolygons missing_perimeter_boundary = offset_ex(diff_ex(boundary,
                                                                  offset_ex(perimeter_boundary, perimeter_offset + float(SCALED_EPSILON) / 2.f)),
                                                          perimeter_offset + float(SCALED_EPSILON));
        perimeter_boundary                    = offset_ex(perimeter_boundary, perimeter_offset);
        append(perimeter_boundary, std::move(missing_perimeter_boundary));

        result_boundary = intersection_ex(offset_ex(perimeter_boundary, -perimeter_offset), boundary);
    } else {
        result_boundary = std::move(perimeter_boundary);
    }

    auto [contours, holes] = split_expolygon(boundary);

    ExPolygons outer_boundary = union_ex(
        diff(offset(Geometry::convex_hull(contours), 2.f * perimeter_spacing), offset(contours, perimeter_spacing + perimeter_offset)));
    result_boundary.insert(result_boundary.end(), outer_boundary.begin(), outer_boundary.end());
    ExPolygons holes_boundary = offset_ex(holes, -perimeter_spacing);
    result_boundary.insert(result_boundary.end(), holes_boundary.begin(), holes_boundary.end());
    result_boundary = union_ex(result_boundary);


    polygons_count = 0;
    for (const LayerRegion *layer_region : layer.regions())
        for (const Surface &surface : layer_region->fill_surfaces.surfaces)
            if (surface.is_top()) ++polygons_count;

    if (polygons_count > 0) {
        ExPolygons top_layer_polygons;
        top_layer_polygons.reserve(polygons_count);
        for (const LayerRegion *layer_region : layer.regions())
            for (const Surface &surface : layer_region->fill_surfaces.surfaces)
                if (surface.is_top()) top_layer_polygons.emplace_back(surface.expolygon);

        top_layer_polygons = union_ex(top_layer_polygons);
        return diff_ex(result_boundary, offset_ex(top_layer_polygons, -perimeter_offset));
    }

    return result_boundary;
}


static ExPolygons get_boundary_external(const Layer &layer)
{
    const float perimeter_spacing = get_perimeter_spacing_external(layer);
    const float perimeter_offset  = perimeter_spacing / 2.f;
    ExPolygons  boundary;

    for (const PrintObject *object : layer.object()->print()->objects()) {
        ExPolygons polygons_per_obj;


        if (const Layer* l = object->get_layer_at_printz(layer.print_z, EPSILON); l)
            for (const LayerRegion *layer_region : l->regions())
                for (const Surface &surface : layer_region->slices.surfaces)
                    polygons_per_obj.emplace_back(surface.expolygon);

        for (const PrintInstance &instance : object->instances()) {
            size_t boundary_idx = boundary.size();
            boundary.insert(boundary.end(), polygons_per_obj.begin(), polygons_per_obj.end());
            for (; boundary_idx < boundary.size(); ++boundary_idx)
                boundary[boundary_idx].translate(instance.shift);
        }
    }
    boundary               = union_ex(boundary);
    auto [contours, holes] = split_expolygon(boundary);


    ExPolygons result_boundary =
        diff_ex(offset(Geometry::convex_hull(contours), 2.f * perimeter_spacing),offset(contours,  perimeter_spacing + perimeter_offset));

    append(result_boundary, diff_ex(offset(holes, perimeter_spacing), offset(holes, perimeter_offset)));
    return union_ex(result_boundary);
}

void AvoidCrossingPerimeters::init_layer(const Layer &layer)
{
    m_internal.boundaries.clear();
    m_external.boundaries.clear();

    m_internal.boundaries = to_polygons(get_boundary(layer));
    m_external.boundaries = to_polygons(get_boundary_external(layer));

    BoundingBox bbox(get_extents(m_internal.boundaries));
    bbox.offset(SCALED_EPSILON);
    BoundingBox bbox_external = get_extents(m_external.boundaries);
    bbox_external.offset(SCALED_EPSILON);
    BoundingBox bbox_slice(get_extents(layer.lslices));
    bbox_slice.offset(SCALED_EPSILON);

    m_internal.bbox = BoundingBoxf(bbox.min.cast<double>(), bbox.max.cast<double>());
    m_external.bbox = BoundingBoxf(bbox_external.min.cast<double>(), bbox_external.max.cast<double>());

    m_internal.grid.set_bbox(bbox);

    m_internal.grid.create(m_internal.boundaries, coord_t(scale_(1.)));
    m_external.grid.set_bbox(bbox_external);

    m_external.grid.create(m_external.boundaries, coord_t(scale_(1.)));
    m_grid_lslice.set_bbox(bbox_slice);

    m_grid_lslice.create(layer.lslices, coord_t(scale_(1.)));

    init_boundary_distances(&m_internal);
    init_boundary_distances(&m_external);
}
#endif

}
