#include "ArcFitter.hpp"
#include "Point.hpp"
#include "Circle.hpp"
#include "MultiPoint.hpp"
#include "Polyline.hpp"
#include "libslic3r.h"

#include <cmath>
#include <cassert>
#include <vector>
#include <cstddef>

namespace Slic3r {

void ArcFitter::do_arc_fitting(const Points& points, std::vector<PathFittingData>& result, double tolerance)
{
#ifdef DEBUG_ARC_FITTING
    static int irun = 0;
    BoundingBox bbox_svg;
    bbox_svg.merge(get_extents(points));
    Polyline temp = Polyline(points);
    {
        std::stringstream stri;
        stri << "debug_arc_fitting_" << irun << ".svg";
        SVG svg(stri.str(), bbox_svg);
        svg.draw(points, "blue", 50000);
        svg.draw(temp, "red", 1);
        svg.Close();
    }
    ++ irun;
#endif

    result.clear();
    result.reserve(points.size() / 2);
    if (points.size() < 3) {
        PathFittingData data;
        data.start_point_index = 0;
        data.end_point_index = points.size() - 1;
        data.path_type = EMovePathType::Linear_move;
        result.push_back(data);
        return;
    }

    size_t front_index = 0;
    size_t back_index = 0;
    ArcSegment last_arc;
    bool can_fit = false;
    Points current_segment;
    current_segment.reserve(points.size());
    ArcSegment target_arc;
    for (size_t i = 0; i < points.size(); i++) {
        back_index = i;
        current_segment.push_back(points[i]);
        if (back_index - front_index < 2)
            continue;

        can_fit = ArcSegment::try_create_arc(current_segment, target_arc, Polyline(current_segment).length(),
                                             DEFAULT_SCALED_MAX_RADIUS,
                                             tolerance,
                                             DEFAULT_ARC_LENGTH_PERCENT_TOLERANCE);
        if (can_fit) {
            last_arc = target_arc;
            if (back_index == points.size() - 1) {
                result.emplace_back(PathFittingData{ front_index,
                                   back_index,
                                   last_arc.direction == ArcDirection::Arc_Dir_CCW ? EMovePathType::Arc_move_ccw : EMovePathType::Arc_move_cw,
                                   last_arc });
                front_index = back_index;
            }
        } else {
            if (back_index - front_index > 2) {
                result.emplace_back(PathFittingData{ front_index,
                                   back_index - 1,
                                   last_arc.direction == ArcDirection::Arc_Dir_CCW ? EMovePathType::Arc_move_ccw : EMovePathType::Arc_move_cw,
                                   last_arc });
            } else {
                if (result.empty() || result.back().path_type != EMovePathType::Linear_move)
                    result.emplace_back(PathFittingData{front_index, front_index + 1, EMovePathType::Linear_move, ArcSegment()});
                else if(result.back().path_type == EMovePathType::Linear_move)
                    result.back().end_point_index = front_index + 1;
            }
            front_index = back_index - 1;
            current_segment.clear();
            current_segment.push_back(points[front_index]);
            current_segment.push_back(points[front_index + 1]);
        }
    }
    if (front_index != back_index) {
        if (result.empty() || result.back().path_type != EMovePathType::Linear_move)
            result.emplace_back(PathFittingData{front_index, back_index, EMovePathType::Linear_move, ArcSegment()});
        else if (result.back().path_type == EMovePathType::Linear_move)
            result.back().end_point_index = back_index;
    }
    result.shrink_to_fit();
}

void ArcFitter::do_arc_fitting_and_simplify(Points& points, std::vector<PathFittingData>& result, double tolerance)
{
    if (abs(tolerance) > SCALED_EPSILON)
        ArcFitter::do_arc_fitting(points, result, tolerance);
    else
        result.push_back(PathFittingData{ 0, points.size() - 1, EMovePathType::Linear_move, ArcSegment() });

    if (result.size() == 1 && result[0].path_type == EMovePathType::Linear_move) {
        points = MultiPoint::_douglas_peucker(points, tolerance);
        result[0].end_point_index = points.size() - 1;
        return;
    } else {
        Points simplified_points;
        simplified_points.reserve(points.size());
        simplified_points.push_back(points[0]);
        std::vector<size_t> reduce_count(result.size(), 0);
        for (size_t i = 0; i < result.size(); i++)
        {
            size_t start_index = result[i].start_point_index;
            size_t end_index = result[i].end_point_index;
            Points straight_or_arc_part;
            straight_or_arc_part.reserve(end_index - start_index + 1);
            for (size_t j = start_index; j <= end_index; j++)
                straight_or_arc_part.push_back(points[j]);
            straight_or_arc_part = MultiPoint::_douglas_peucker(straight_or_arc_part, tolerance);
            reduce_count[i] = end_index - start_index + 1 - straight_or_arc_part.size();
            for (size_t j = 1; j < straight_or_arc_part.size(); j++) {
                simplified_points.push_back(straight_or_arc_part[j]);
            }
        }
        points = simplified_points;
        for (size_t j = 1; j < reduce_count.size(); j++)
            reduce_count[j] += reduce_count[j - 1];
        for (size_t j = 0; j < result.size(); j++)
        {
            result[j].end_point_index -= reduce_count[j];
            if (j != result.size() - 1)
                result[j + 1].start_point_index = result[j].end_point_index;
        }
    }
}

}