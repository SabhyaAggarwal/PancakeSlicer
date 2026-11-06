// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <cassert>
#include <cmath>
#include <math.h>
#include <limits>
#include <unordered_set>

#include "WallToolPaths.hpp"

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Arachne/utils/PolygonsPointIndex.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Arachne/BeadingStrategy/BeadingStrategyFactory.hpp"
#include "libslic3r/Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r/Arachne/utils/SparsePointGrid.hpp"
#include "libslic3r/Arachne/utils/SquareGrid.hpp"
#include "SkeletalTrapezoidation.hpp"
#include "../ClipperUtils.hpp"
#include "libslic3r/libslic3r.h"
#include "utils/linearAlg2D.hpp"
#include "EdgeGrid.hpp"
#include "utils/SparseLineGrid.hpp"
#include "Geometry.hpp"
#include "utils/PolylineStitcher.hpp"
#include "SVG.hpp"
#include "Utils.hpp"

#include <boost/log/trivial.hpp>
#include <utility>
#include <vector>



namespace Slic3r::Arachne
{

WallToolPathsParams make_paths_params(const int layer_id, const PrintObjectConfig &print_object_config, const PrintConfig &print_config)
{
    WallToolPathsParams input_params;
    {
        const double min_nozzle_diameter = *std::min_element(print_config.nozzle_diameter.values.begin(), print_config.nozzle_diameter.values.end());
        if (const auto &min_feature_size_opt = print_object_config.min_feature_size)
            input_params.min_feature_size = min_feature_size_opt.value * 0.01 * min_nozzle_diameter;

        if (const auto &min_wall_length_factor_opt = print_object_config.min_length_factor)
            input_params.min_length_factor = min_wall_length_factor_opt.value;
        else
            input_params.min_length_factor = 0.5f;

        if (layer_id == 0) {
            if (const auto &initial_layer_min_bead_width_opt = print_object_config.initial_layer_min_bead_width)
                input_params.min_bead_width = initial_layer_min_bead_width_opt.value * 0.01 * min_nozzle_diameter;
        } else {
            if (const auto &min_bead_width_opt = print_object_config.min_bead_width)
                input_params.min_bead_width = min_bead_width_opt.value * 0.01 * min_nozzle_diameter;
        }

        if (const auto &wall_transition_filter_deviation_opt = print_object_config.wall_transition_filter_deviation)
            input_params.wall_transition_filter_deviation = wall_transition_filter_deviation_opt.value * 0.01 * min_nozzle_diameter;

        if (const auto &wall_transition_length_opt = print_object_config.wall_transition_length)
            input_params.wall_transition_length = wall_transition_length_opt.value * 0.01 * min_nozzle_diameter;

        input_params.wall_transition_angle   = print_object_config.wall_transition_angle.value;
        input_params.wall_distribution_count = print_object_config.wall_distribution_count.value;

        input_params.is_top_or_bottom_layer = false;

        if (const auto& wall_maximum_resolution_opt = print_object_config.wall_maximum_resolution)
            input_params.wall_maximum_resolution = scaled<coord_t>(wall_maximum_resolution_opt.value);

        if (const auto& wall_maximum_deviation_opt = print_object_config.wall_maximum_deviation)
            input_params.wall_maximum_deviation = scaled<coord_t>(wall_maximum_deviation_opt.value);
    }

    return input_params;
}

WallToolPaths::WallToolPaths(const Polygons& outline, const coord_t bead_width_0, const coord_t bead_width_x,
                             const size_t inset_count, const coord_t wall_0_inset, const coordf_t layer_height, const WallToolPathsParams &params)
    : outline(outline)
    , bead_width_0(bead_width_0)
    , bead_width_x(bead_width_x)
    , inset_count(inset_count)
    , wall_0_inset(wall_0_inset)
    , layer_height(layer_height)
    , print_thin_walls(Slic3r::Arachne::fill_outline_gaps)
    , min_feature_size(scaled<coord_t>(params.min_feature_size))
    , min_bead_width(scaled<coord_t>(params.min_bead_width))
    , small_area_length(static_cast<double>(bead_width_0) / 2.)
    , wall_transition_filter_deviation(scaled<coord_t>(params.wall_transition_filter_deviation))
    , toolpaths_generated(false)
    , m_params(params)
{
}

void simplify(Polygon &thiss, const int64_t smallest_line_segment_squared, const int64_t allowed_error_distance_squared)
{
    if (thiss.size() < 3) {
        thiss.points.clear();
        return;
    }
    if (thiss.size() == 3)
        return;

    Polygon new_path;
    Point previous = thiss.points.back();
    Point previous_previous = thiss.points.at(thiss.points.size() - 2);
    Point current = thiss.points.at(0);
















    int64_t accumulated_area_removed = int64_t(previous.x()) * int64_t(current.y()) - int64_t(previous.y()) * int64_t(current.x());

    for (size_t point_idx = 0; point_idx < thiss.points.size(); point_idx++) {
        current = thiss.points.at(point_idx % thiss.points.size());


        Point next;
        if (point_idx + 1 < thiss.points.size()) {
            next = thiss.points.at(point_idx + 1);
        } else if (point_idx + 1 == thiss.points.size() && new_path.size() > 1) {
            next = new_path[0];
        } else {
            next = thiss.points.at((point_idx + 1) % thiss.points.size());
        }
        const int64_t removed_area_next = int64_t(current.x()) * int64_t(next.y()) - int64_t(current.y()) * int64_t(next.x());
        const int64_t negative_area_closing = int64_t(next.x()) * int64_t(previous.y()) - int64_t(next.y()) * int64_t(previous.x());
        accumulated_area_removed += removed_area_next;

        const int64_t length2 = (current - previous).cast<int64_t>().squaredNorm();




        if (length2 < Slic3r::sqr(scaled<coord_t>(0.005))) {

            continue;
        }

        const int64_t area_removed_so_far = accumulated_area_removed + negative_area_closing;
        const int64_t base_length_2 = (next - previous).cast<int64_t>().squaredNorm();

        if (base_length_2 == 0)
            continue;







        const int64_t height_2 = double(area_removed_so_far) * double(area_removed_so_far) / double(base_length_2);

        if ((height_2 <= Slic3r::sqr(colinear_vertex_tolerance())
             && Line::distance_to_infinite(current, previous, next) <= double(colinear_vertex_tolerance())))
            continue;

        if (length2 < smallest_line_segment_squared
            && height_2 <= allowed_error_distance_squared)
        {
            const int64_t next_length2 = (current - next).cast<int64_t>().squaredNorm();
            if (next_length2 > 4 * smallest_line_segment_squared) {




                Point intersection_point;
                bool has_intersection = Line(previous_previous, previous).intersection_infinite(Line(current, next), &intersection_point);
                if (!has_intersection
                    || Line::distance_to_infinite_squared(intersection_point, previous, current) > double(allowed_error_distance_squared)
                    || (intersection_point - previous).cast<int64_t>().squaredNorm() > smallest_line_segment_squared
                    || (intersection_point - next).cast<int64_t>().squaredNorm() > smallest_line_segment_squared)
                {


                }
                else {

                    current = intersection_point;

                    if(!new_path.empty()) {
                        new_path.points.pop_back();
                        previous = previous_previous;
                    }
                }
            } else {
                continue;
            }
        }

        accumulated_area_removed = removed_area_next;
        previous_previous = previous;
        previous = current;
        new_path.points.push_back(current);
    }

    thiss = new_path;
}






























void simplify(Polygons &thiss, const int64_t smallest_line_segment = scaled<coord_t>(0.01), const int64_t allowed_error_distance = scaled<coord_t>(0.005))
{
    const int64_t allowed_error_distance_squared = int64_t(allowed_error_distance) * int64_t(allowed_error_distance);
    const int64_t smallest_line_segment_squared = int64_t(smallest_line_segment) * int64_t(smallest_line_segment);
    for (size_t p = 0; p < thiss.size(); p++)
    {
        simplify(thiss[p], smallest_line_segment_squared, allowed_error_distance_squared);
        if (thiss[p].size() < 3)
        {
            thiss.erase(thiss.begin() + p);
            p--;
        }
    }
}

typedef SparseLineGrid<PolygonsPointIndex, PolygonsPointIndexSegmentLocator> LocToLineGrid;
std::unique_ptr<LocToLineGrid>                                               createLocToLineGrid(const Polygons &polygons, int square_size)
{
    unsigned int n_points = 0;
    for (const auto &poly : polygons)
        n_points += poly.size();

    auto ret = std::make_unique<LocToLineGrid>(square_size, n_points);

    for (unsigned int poly_idx = 0; poly_idx < polygons.size(); poly_idx++)
        for (unsigned int point_idx = 0; point_idx < polygons[poly_idx].size(); point_idx++)
            ret->insert(PolygonsPointIndex(&polygons, poly_idx, point_idx));
    return ret;
}



void fixSelfIntersections(const coord_t epsilon, Polygons &thiss)
{
    if (epsilon < 1)
        return;

    const int64_t half_epsilon = (epsilon + 1) / 2;



    const coord_t grid_size  = scaled<coord_t>(2.);
    auto              query_grid = createLocToLineGrid(thiss, grid_size);

    const auto    move_dist         = std::max<int64_t>(2L, half_epsilon - 2);
    const int64_t half_epsilon_sqrd = half_epsilon * half_epsilon;

    const size_t n = thiss.size();
    for (size_t poly_idx = 0; poly_idx < n; poly_idx++) {
        const size_t pathlen = thiss[poly_idx].size();
        for (size_t point_idx = 0; point_idx < pathlen; ++point_idx) {
            Point &pt = thiss[poly_idx][point_idx];
            for (const auto &line : query_grid->getNearby(pt, epsilon)) {
                const size_t line_next_idx = (line.point_idx + 1) % thiss[line.poly_idx].size();
                if (poly_idx == line.poly_idx && (point_idx == line.point_idx || point_idx == line_next_idx))
                    continue;

                const Line segment(thiss[line.poly_idx][line.point_idx], thiss[line.poly_idx][line_next_idx]);
                Point      segment_closest_point;
                segment.distance_to_squared(pt, &segment_closest_point);

                if (half_epsilon_sqrd >= (pt - segment_closest_point).cast<int64_t>().squaredNorm()) {
                    const Point  &other = thiss[poly_idx][(point_idx + 1) % pathlen];
                    const Vec2i64 vec   = (LinearAlg2D::pointIsLeftOfLine(other, segment.a, segment.b) > 0 ? segment.b - segment.a : segment.a - segment.b).cast<int64_t>();
                    assert(Slic3r::sqr(double(vec.x())) < double(std::numeric_limits<int64_t>::max()));
                    assert(Slic3r::sqr(double(vec.y())) < double(std::numeric_limits<int64_t>::max()));
                    const int64_t len   = vec.norm();
                    pt.x() += (-vec.y() * move_dist) / len;
                    pt.y() += (vec.x() * move_dist) / len;
                }
            }
        }
    }
}




void removeDegenerateVerts(Polygons &thiss)
{
    for (size_t poly_idx = 0; poly_idx < thiss.size(); poly_idx++) {
        Polygon &poly = thiss[poly_idx];
        Polygon  result;

        auto isDegenerate = [](const Point &last, const Point &now, const Point &next) {
            Vec2i64 last_line = (now - last).cast<int64_t>();
            Vec2i64 next_line = (next - now).cast<int64_t>();
            return last_line.dot(next_line) == -1 * last_line.norm() * next_line.norm();
        };
        bool isChanged = false;
        for (size_t idx = 0; idx < poly.size(); idx++) {
            const Point &last = (result.size() == 0) ? poly.back() : result.back();
            if (idx + 1 == poly.size() && result.size() == 0)
                break;

            const Point &next = (idx + 1 == poly.size()) ? result[0] : poly[idx + 1];
            if (isDegenerate(last, poly[idx], next)) {

                isChanged = true;
                while (result.size() > 1 && isDegenerate(result[result.size() - 2], result.back(), next))
                    result.points.pop_back();
            } else {
                result.points.emplace_back(poly[idx]);
            }
        }

        if (isChanged) {
            if (result.size() > 2) {
                poly = result;
            } else {
                thiss.erase(thiss.begin() + poly_idx);
                poly_idx--;
            }
        }
    }
}

void removeSmallAreas(Polygons &thiss, const double min_area_size, const bool remove_holes)
{
    auto new_end = thiss.end();
    if (remove_holes) {
        for (auto it = thiss.begin(); it < new_end;) {

            if (fabs(it->area()) < min_area_size) {
                --new_end;
                *it = std::move(*new_end);
                continue;
            }
            ++it;
        }
    } else {

        Polygons small_holes;
        for (auto it = thiss.begin(); it < new_end;) {
            if (double area = it->area(); fabs(area) < min_area_size) {
                if (area >= 0) {
                    --new_end;
                    if (it < new_end) {
                        std::swap(*new_end, *it);
                        continue;
                    } else {
                        break;
                    }
                } else {
                    small_holes.push_back(*it);
                }
            }
            ++it;
        }



        const auto removed_outlines_start = new_end;
        for (auto hole_it = small_holes.rbegin(); hole_it < small_holes.rend(); hole_it++)
            for (auto outline_it = removed_outlines_start; outline_it < thiss.end(); outline_it++)
                if (Polygon(*outline_it).contains(*hole_it->begin())) {
                    new_end--;
                    *hole_it = std::move(*new_end);
                    break;
                }
    }
    thiss.resize(new_end-thiss.begin());
}

void removeColinearEdges(Polygon &poly, const double max_deviation_angle)
{

    size_t num_removed_in_iteration = 0;
    do {
        num_removed_in_iteration = 0;
        std::vector<bool> process_indices(poly.points.size(), true);

        bool go = true;
        while (go) {
            go = false;

            const auto  &rpath   = poly;
            const size_t pathlen = rpath.size();
            if (pathlen <= 3)
                return;

            std::vector<bool> skip_indices(poly.points.size(), false);

            Polygon new_path;
            for (size_t point_idx = 0; point_idx < pathlen; ++point_idx) {


                if (!process_indices[point_idx]) {
                    new_path.points.push_back(rpath[point_idx]);
                    continue;
                }


                if (point_idx == (pathlen - 1) && skip_indices[0]) {
                    skip_indices[new_path.size()] = true;
                    go                            = true;
                    new_path.points.push_back(rpath[point_idx]);
                    break;
                }

                const Point &prev = rpath[(point_idx - 1 + pathlen) % pathlen];
                const Point &pt   = rpath[point_idx];
                const Point &next = rpath[(point_idx + 1) % pathlen];

                float angle = LinearAlg2D::getAngleLeft(prev, pt, next);
                if (angle >= float(M_PI)) { angle -= float(M_PI); }



                if (angle > max_deviation_angle && angle < M_PI - max_deviation_angle) {
                    new_path.points.push_back(pt);
                } else if (point_idx != (pathlen - 1)) {

                    skip_indices[new_path.size()] = true;
                    go                            = true;
                    new_path.points.push_back(next);
                    ++point_idx;
                }
            }
            poly = new_path;
            num_removed_in_iteration += pathlen - poly.points.size();

            process_indices.clear();
            process_indices.insert(process_indices.end(), skip_indices.begin(), skip_indices.end());
        }
    } while (num_removed_in_iteration > 0);
}

void removeColinearEdges(Polygons &thiss, const double max_deviation_angle = 0.0005)
{
    for (int p = 0; p < int(thiss.size()); p++) {
        removeColinearEdges(thiss[p], max_deviation_angle);
        if (thiss[p].size() < 3) {
            thiss.erase(thiss.begin() + p);
            p--;
        }
    }
}

const std::vector<VariableWidthLines> &WallToolPaths::generate()
{
    if (this->inset_count < 1)
        return toolpaths;

    const coord_t smallest_segment = m_params.wall_maximum_resolution;
    const coord_t allowed_distance = m_params.wall_maximum_deviation;
    const coord_t epsilon_offset = (allowed_distance / 2) - 1;
    const double  transitioning_angle = Geometry::deg2rad(m_params.wall_transition_angle);
    const coord_t discretization_step_size = scaled<coord_t>(0.8);



    Polygons prepared_outline = offset(offset(offset(outline, -epsilon_offset), epsilon_offset * 2), -epsilon_offset);
    simplify(prepared_outline, smallest_segment, allowed_distance);
    fixSelfIntersections(epsilon_offset, prepared_outline);
    removeDegenerateVerts(prepared_outline);
    removeColinearEdges(prepared_outline, 0.005);

    fixSelfIntersections(epsilon_offset, prepared_outline);
    removeDegenerateVerts(prepared_outline);
    removeSmallAreas(prepared_outline, small_area_length * small_area_length, false);





    prepared_outline = union_(prepared_outline);

    if (area(prepared_outline) <= 0) {
        assert(toolpaths.empty());
        return toolpaths;
    }

    const float external_perimeter_extrusion_width = Flow::rounded_rectangle_extrusion_width_from_spacing(unscale<float>(bead_width_0), float(this->layer_height));
    const float perimeter_extrusion_width          = Flow::rounded_rectangle_extrusion_width_from_spacing(unscale<float>(bead_width_x), float(this->layer_height));

    const coord_t wall_transition_length = scaled<coord_t>(this->m_params.wall_transition_length);

	const double wall_split_middle_threshold = std::clamp(2. * unscaled<double>(this->min_bead_width) / external_perimeter_extrusion_width - 1., 0.01, 0.99);
    const double wall_add_middle_threshold   = std::clamp(unscaled<double>(this->min_bead_width) / perimeter_extrusion_width, 0.01, 0.99);

    const int wall_distribution_count = this->m_params.wall_distribution_count;
    const size_t max_bead_count = (inset_count < std::numeric_limits<coord_t>::max() / 2) ? 2 * inset_count : std::numeric_limits<coord_t>::max();
    const auto beading_strat = BeadingStrategyFactory::makeStrategy
        (
            bead_width_0,
            bead_width_x,
            wall_transition_length,
            transitioning_angle,
            print_thin_walls,
            min_bead_width,
            min_feature_size,
            wall_split_middle_threshold,
            wall_add_middle_threshold,
            max_bead_count,
            wall_0_inset,
            wall_distribution_count
        );
    const coord_t transition_filter_dist   = scaled<coord_t>(100.f);
    const coord_t allowed_filter_deviation = wall_transition_filter_deviation;
    SkeletalTrapezoidation wall_maker
    (
        prepared_outline,
        *beading_strat,
        beading_strat->getTransitioningAngle(),
        discretization_step_size,
        transition_filter_dist,
        allowed_filter_deviation,
        wall_transition_length
    );
    wall_maker.generateToolpaths(toolpaths);

    stitchToolPaths(toolpaths, this->bead_width_x);

    removeSmallLines(toolpaths);

    separateOutInnerContour();

    simplifyToolPaths(toolpaths, m_params);

    removeEmptyToolPaths(toolpaths);
    assert(std::is_sorted(toolpaths.cbegin(), toolpaths.cend(),
                          [](const VariableWidthLines& l, const VariableWidthLines& r)
                          {
                              return l.front().inset_idx < r.front().inset_idx;
                          }) && "WallToolPaths should be sorted from the outer 0th to inner_walls");
    toolpaths_generated = true;
    return toolpaths;
}

void WallToolPaths::stitchToolPaths(std::vector<VariableWidthLines> &toolpaths, const coord_t bead_width_x)
{
    const coord_t stitch_distance = bead_width_x - 1;

    for (unsigned int wall_idx = 0; wall_idx < toolpaths.size(); wall_idx++) {
        VariableWidthLines& wall_lines = toolpaths[wall_idx];

        VariableWidthLines stitched_polylines;
        VariableWidthLines closed_polygons;
        PolylineStitcher<VariableWidthLines, ExtrusionLine, ExtrusionJunction>::stitch(wall_lines, stitched_polylines, closed_polygons, stitch_distance);
#ifdef ARACHNE_STITCH_PATCH_DEBUG
        for (const ExtrusionLine& line : stitched_polylines) {
            if ( ! line.is_odd && line.polylineLength() > 3 * stitch_distance && line.size() > 3) {
                BOOST_LOG_TRIVIAL(error) << "Some even contour lines could not be closed into polygons!";
                assert(false && "Some even contour lines could not be closed into polygons!");
                BoundingBox aabb;
                for (auto line2 : wall_lines)
                    for (auto j : line2)
                        aabb.merge(j.p);
                {
                    static int iRun = 0;
                    SVG svg(debug_out_path("contours_before.svg-%d.png", iRun), aabb);
                    std::array<const char *, 8> colors    = {"gray", "black", "blue", "green", "lime", "purple", "red", "yellow"};
                    size_t                      color_idx = 0;
                    for (auto& inset : toolpaths)
                        for (auto& line2 : inset) {


                            Polygon poly = line2.toPolygon();
                            Point last = poly.front();
                            for (size_t idx = 1 ; idx < poly.size(); idx++) {
                                Point here = poly[idx];
                                svg.draw(Line(last, here), colors[color_idx]);

                                last = here;
                            }
                            svg.draw(poly[0], colors[color_idx]);



                            color_idx = (color_idx + 1) % colors.size();
                        }
                }
                {
                    static int iRun = 0;
                    SVG svg(debug_out_path("contours-%d.svg", iRun), aabb);
                    for (auto& inset : toolpaths)
                        for (auto& line2 : inset)
                            svg.draw_outline(line2.toPolygon(), "gray");
                    for (auto& line2 : stitched_polylines) {
                        const char *col = line2.is_odd ? "gray" : "red";
                        if ( ! line2.is_odd)
                            std::cerr << "Non-closed even wall of size: " << line2.size()  << " at " << line2.front().p << "\n";
                        if ( ! line2.is_odd)
                            svg.draw(line2.front().p);
                        Polygon poly = line2.toPolygon();
                        Point last = poly.front();
                        for (size_t idx = 1 ; idx < poly.size(); idx++)
                        {
                            Point here = poly[idx];
                            svg.draw(Line(last, here), col);
                            last = here;
                        }
                    }
                    for (auto line2 : closed_polygons)
                        svg.draw(line2.toPolygon());
                }
            }
        }
#endif
        wall_lines = stitched_polylines;

        for (ExtrusionLine& wall_polygon : closed_polygons)
        {
            if (wall_polygon.junctions.empty())
            {
                continue;
            }




            if (wall_polygon.junctions.front().p != wall_polygon.junctions.back().p &&
                (wall_polygon.junctions.back().p - wall_polygon.junctions.front().p).cast<double>().norm() < stitch_distance) {
                wall_polygon.junctions.emplace_back(wall_polygon.junctions.front());
            }
            wall_polygon.is_closed = true;
            wall_lines.emplace_back(std::move(wall_polygon));
        }
#ifdef DEBUG
        for (ExtrusionLine& line : wall_lines)
        {
            assert(line.inset_idx == wall_idx);
        }
#endif
    }
}

template<typename T> bool shorterThan(const T &shape, const coord_t check_length)
{
    const auto *p0     = &shape.back();
    int64_t     length = 0;
    for (const auto &p1 : shape) {
        length += (*p0 - p1).template cast<int64_t>().norm();
        if (length >= check_length)
            return false;
        p0 = &p1;
    }
    return true;
}

void WallToolPaths::removeSmallLines(std::vector<VariableWidthLines> &toolpaths)
{
    for (VariableWidthLines &inset : toolpaths) {
        for (size_t line_idx = 0; line_idx < inset.size(); line_idx++) {
            ExtrusionLine &line      = inset[line_idx];
            coord_t        min_width = std::numeric_limits<coord_t>::max();
            for (const ExtrusionJunction &j : line)
                min_width = std::min(min_width, j.w);

            if (line.is_odd && !line.is_closed && shorterThan(line, m_params.is_top_or_bottom_layer ? (min_width / 2) : (min_width * m_params.min_length_factor))) {
                line = std::move(inset.back());
                inset.erase(--inset.end());
                line_idx--;
            }
        }
    }
}

void WallToolPaths::simplifyToolPaths(std::vector<VariableWidthLines>& toolpaths, const WallToolPathsParams& params)
{
    const int64_t maximum_resolution = params.wall_maximum_resolution;
    const int64_t maximum_deviation  = params.wall_maximum_deviation;

    const int64_t smallest_line_segment_squared  = maximum_resolution * maximum_resolution;
    const int64_t allowed_error_distance_squared = maximum_deviation * maximum_deviation;

    const int64_t maximum_extrusion_area_deviation = Slic3r::Arachne::meshfix_maximum_extrusion_area_deviation();

    for (VariableWidthLines& lines : toolpaths)
    {
        for (ExtrusionLine& line : lines)
        {
            line.simplify(smallest_line_segment_squared, allowed_error_distance_squared, maximum_extrusion_area_deviation);
        }
    }
}

const std::vector<VariableWidthLines> &WallToolPaths::getToolPaths()
{
    if (!toolpaths_generated)
        return generate();
    return toolpaths;
}

void WallToolPaths::separateOutInnerContour()
{

    std::vector<VariableWidthLines> actual_toolpaths;
    actual_toolpaths.reserve(toolpaths.size());
    std::vector<VariableWidthLines> contour_paths;
    contour_paths.reserve(toolpaths.size() / inset_count);
    inner_contour.clear();
    for (const VariableWidthLines &inset : toolpaths) {
        if (inset.empty())
            continue;
        bool is_contour = false;
        for (const ExtrusionLine &line : inset) {
            for (const ExtrusionJunction &j : line) {
                if (j.w == 0)
                    is_contour = true;
                else
                    is_contour = false;
                break;
            }
        }

        if (is_contour) {
#ifdef DEBUG
            for (const ExtrusionLine &line : inset)
                for (const ExtrusionJunction &j : line)
                    assert(j.w == 0);
#endif
            for (const ExtrusionLine &line : inset) {
                if (line.is_odd)
                    continue;
                else if (line.is_closed)
                    inner_contour.emplace_back(line.toPolygon());
            }
        } else {
            actual_toolpaths.emplace_back(inset);
        }
    }
    if (!actual_toolpaths.empty())
        toolpaths = std::move(actual_toolpaths);
    else
        toolpaths.clear();






    inner_contour = union_(inner_contour, pftEvenOdd);
}

const Polygons& WallToolPaths::getInnerContour()
{
    if (!toolpaths_generated && inset_count > 0)
    {
        generate();
    }
    else if(inset_count == 0)
    {
        return outline;
    }
    return inner_contour;
}

bool WallToolPaths::removeEmptyToolPaths(std::vector<VariableWidthLines> &toolpaths)
{
    toolpaths.erase(std::remove_if(toolpaths.begin(), toolpaths.end(), [](const VariableWidthLines& lines)
                                   {
                                       return lines.empty();
                                   }), toolpaths.end());
    return toolpaths.empty();
}









WallToolPaths::ExtrusionLineSet WallToolPaths::getRegionOrder(const std::vector<ExtrusionLine *> &input, const bool outer_to_inner)
{
    ExtrusionLineSet order_requirements;




















    coord_t max_line_w = 0u;
    for (const ExtrusionLine *line : input)
        for (const ExtrusionJunction &junction : *line)
            max_line_w = std::max(max_line_w, junction.w);
    if (max_line_w == 0u)
        return order_requirements;

    struct LineLoc
    {
        ExtrusionJunction    j;
        const ExtrusionLine *line;
    };
    struct Locator
    {
        Point operator()(const LineLoc &elem) { return elem.j.p; }
    };







    constexpr float diagonal_extension = 1.9f;
    const auto      searching_radius   = coord_t(max_line_w * diagonal_extension);
    using GridT                        = SparsePointGrid<LineLoc, Locator>;
    GridT grid(searching_radius);

    for (const ExtrusionLine *line : input)
        for (const ExtrusionJunction &junction : *line) grid.insert(LineLoc{junction, line});
    for (const std::pair<const SquareGrid::GridPoint, LineLoc> &pair : grid) {
        const LineLoc       &lineloc_here = pair.second;
        const ExtrusionLine *here         = lineloc_here.line;
        Point                loc_here     = pair.second.j.p;
        std::vector<LineLoc> nearby_verts = grid.getNearby(loc_here, searching_radius);
        for (const LineLoc &lineloc_nearby : nearby_verts) {
            const ExtrusionLine *nearby = lineloc_nearby.line;
            if (nearby == here)
                continue;
            if (nearby->inset_idx == here->inset_idx)
                continue;
            if (nearby->inset_idx > here->inset_idx + 1)
                continue;
            if (here->inset_idx > nearby->inset_idx + 1)
                continue;
            if (!shorter_then(loc_here - lineloc_nearby.j.p, (lineloc_here.j.w + lineloc_nearby.j.w) / 2 * diagonal_extension))
                continue;
            if (here->is_odd || nearby->is_odd) {
                if (here->is_odd && !nearby->is_odd && nearby->inset_idx < here->inset_idx)
                    order_requirements.emplace(std::make_pair(nearby, here));
                if (nearby->is_odd && !here->is_odd && here->inset_idx < nearby->inset_idx)
                    order_requirements.emplace(std::make_pair(here, nearby));
            } else if ((nearby->inset_idx < here->inset_idx) == outer_to_inner) {
                order_requirements.emplace(std::make_pair(nearby, here));
            } else {
                assert((nearby->inset_idx > here->inset_idx) == outer_to_inner);
                order_requirements.emplace(std::make_pair(here, nearby));
            }
        }
    }
    return order_requirements;
}

}
