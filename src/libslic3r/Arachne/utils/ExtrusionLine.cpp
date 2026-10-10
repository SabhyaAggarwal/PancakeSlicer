//Copyright (c) 2020 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "ExtrusionLine.hpp"
#include "../../VariableWidth.hpp"
#include "libslic3r/ClipperZUtils.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"
#include "libslic3r/Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"

namespace Slic3r {
class Flow;
}

namespace Slic3r::Arachne
{

ExtrusionLine::ExtrusionLine(const size_t inset_idx, const bool is_odd) : inset_idx(inset_idx), is_odd(is_odd), is_closed(false) {}

int64_t ExtrusionLine::getLength() const
{
    if (junctions.empty())
        return 0;

    int64_t           len  = 0;
    ExtrusionJunction prev = junctions.front();
    for (const ExtrusionJunction &next : junctions) {
        len += (next.p - prev.p).cast<int64_t>().norm();
        prev = next;
    }
    if (is_closed)
        len += (front().p - back().p).cast<int64_t>().norm();

    return len;
}

void ExtrusionLine::simplify(const int64_t smallest_line_segment_squared, const int64_t allowed_error_distance_squared, const int64_t maximum_extrusion_area_deviation)
{
    const size_t min_path_size = is_closed ? 3 : 2;
    if (junctions.size() <= min_path_size)
        return;

    std::vector<ExtrusionJunction> new_junctions;
    new_junctions.emplace_back(junctions.front());

    ExtrusionJunction previous = junctions.front();
    ExtrusionJunction previous_previous = this->is_closed ? junctions[junctions.size() - 2] : junctions.front();

    const ExtrusionJunction& initial = junctions[1];
    int64_t accumulated_area_removed = int64_t(previous.p.x()) * int64_t(initial.p.y()) - int64_t(previous.p.y()) * int64_t(initial.p.x());
    for (size_t point_idx = 1; point_idx < junctions.size() - (this->is_closed ? 0 : 1); point_idx++)
    {
        const bool is_last = point_idx + 1 == junctions.size();
        const ExtrusionJunction& current = is_last ? new_junctions[0] : junctions[point_idx];

        if (this->is_closed && new_junctions.size() + (junctions.size() - point_idx) <= 3) {
            new_junctions.push_back(current);
            continue;
        }

        const bool spill_over = this->is_closed && point_idx + 2 >= junctions.size() &&
            point_idx + 2 - junctions.size() < new_junctions.size();
        ExtrusionJunction& next = spill_over ? new_junctions[point_idx + 2 - junctions.size()] : junctions[point_idx + 1];

        const int64_t removed_area_next = int64_t(current.p.x()) * int64_t(next.p.y()) - int64_t(current.p.y()) * int64_t(next.p.x());
        const int64_t negative_area_closing = int64_t(next.p.x()) * int64_t(previous.p.y()) - int64_t(next.p.y()) * int64_t(previous.p.x());
        accumulated_area_removed += removed_area_next;

        const int64_t length2 = (current - previous).cast<int64_t>().squaredNorm();

        if (length2 < Slic3r::sqr(scaled<coord_t>(0.005)))
        {
            continue;
        }

        const int64_t area_removed_so_far = accumulated_area_removed + negative_area_closing;
        const int64_t base_length_2 = (next - previous).cast<int64_t>().squaredNorm();

        if (base_length_2 == 0)
        {
            continue;
        }
        const auto    height_2 = int64_t(double(area_removed_so_far) * double(area_removed_so_far) / double(base_length_2));
        const int64_t extrusion_area_error = calculateExtrusionAreaDeviationError(previous, current, next);
        if ((height_2 <= Slic3r::sqr(colinear_vertex_tolerance())
             && Line::distance_to_infinite(current.p, previous.p, next.p) <= double(colinear_vertex_tolerance()))
             && extrusion_area_error <= maximum_extrusion_area_deviation)
        {
            continue;
        }

        if (length2 < smallest_line_segment_squared
            && height_2 <= allowed_error_distance_squared)
        {
            const int64_t next_length2 = (current - next).cast<int64_t>().squaredNorm();
            if (next_length2 > 4 * smallest_line_segment_squared)
            {
                Point intersection_point;
                bool has_intersection = Line(previous_previous.p, previous.p).intersection_infinite(Line(current.p, next.p), &intersection_point);
                const auto dist_greater = [](const Point& p1, const Point& p2, const int64_t threshold) {
                    const auto vec = (p1 - p2).cwiseAbs().cast<uint64_t>().eval();
                    if(vec.x() > threshold || vec.y() > threshold) {
                        return true;
                    }
                    return vec.squaredNorm() > threshold;
                };
                if (!has_intersection
                    || Line::distance_to_infinite_squared(intersection_point, previous.p, current.p) > double(allowed_error_distance_squared)
                    || dist_greater(intersection_point, previous.p, smallest_line_segment_squared)
                    || dist_greater(intersection_point, current.p, smallest_line_segment_squared))
                {
                }
                else
                {
                    const ExtrusionJunction new_to_add = ExtrusionJunction(intersection_point, current.w, current.perimeter_index);
                    if(!new_junctions.empty())
                    {
                        new_junctions.pop_back();
                        previous = previous_previous;
                    }

                    accumulated_area_removed = removed_area_next;
                    previous_previous = previous;
                    previous = new_to_add;
                    new_junctions.push_back(new_to_add);
                    continue;
                }
            }
            else
            {
                continue;
            }
        }
        accumulated_area_removed = removed_area_next;
        previous_previous = previous;
        previous = current;
        new_junctions.push_back(current);
    }

    if (this->is_closed) {
        new_junctions.front().p = new_junctions.back().p;
    } else {
        new_junctions.emplace_back(junctions.back());
    }

    junctions = new_junctions;
}

int64_t ExtrusionLine::calculateExtrusionAreaDeviationError(ExtrusionJunction A, ExtrusionJunction B, ExtrusionJunction C) {
    const int64_t ab_length = (B.p - A.p).cast<int64_t>().norm();
    const int64_t bc_length = (C.p - B.p).cast<int64_t>().norm();
    if (const coord_t width_diff = std::max(std::abs(B.w - A.w), std::abs(C.w - B.w)); width_diff > 1) {
        const int64_t ab_weight              = (A.w + B.w) / 2;
        const int64_t bc_weight              = (B.w + C.w) / 2;
        const int64_t weighted_average_width = (ab_length * ab_weight + bc_length * bc_weight) / (ab_length + bc_length);
        const int64_t ac_length              = (C.p - A.p).cast<int64_t>().norm();
        return std::abs((ab_weight * ab_length + bc_weight * bc_length) - (weighted_average_width * ac_length));
    } else {
        return ab_length > bc_length ? int64_t(width_diff) * bc_length : int64_t(width_diff) * ab_length;
    }
}

bool ExtrusionLine::is_contour() const
{
    if (!this->is_closed)
        return false;

    Polygon poly;
    poly.points.reserve(this->junctions.size());
    for (const ExtrusionJunction &junction : this->junctions)
        poly.points.emplace_back(junction.p);

    return poly.is_clockwise();
}

double ExtrusionLine::area() const
{
    assert(this->is_closed);
    double a = 0.;
    if (this->junctions.size() >= 3) {
        Vec2d p1 = this->junctions.back().p.cast<double>();
        for (const ExtrusionJunction &junction : this->junctions) {
            Vec2d p2 = junction.p.cast<double>();
            a += cross2(p1, p2);
            p1 = p2;
        }
    }
    return 0.5 * a;
}

}

namespace Slic3r {
void extrusion_paths_append(ExtrusionPaths &dst, const ClipperZUtils::ZPaths &extrusion_paths, const ExtrusionRole role, const Flow &flow)
{
    for (const ClipperZUtils::ZPath &extrusion_path : extrusion_paths) {
        ThickPolyline thick_polyline = Arachne::to_thick_polyline(extrusion_path);
        Slic3r::append(dst, thick_polyline_to_multi_path(thick_polyline, role, flow, scaled<float>(0.05), float(SCALED_EPSILON)).paths);
    }
}

void extrusion_paths_append(ExtrusionPaths &dst, const Arachne::ExtrusionLine &extrusion, const ExtrusionRole role, const Flow &flow)
{
    ThickPolyline thick_polyline = Arachne::to_thick_polyline(extrusion);
    Slic3r::append(dst, thick_polyline_to_multi_path(thick_polyline, role, flow, scaled<float>(0.05), float(SCALED_EPSILON)).paths);
}
}
