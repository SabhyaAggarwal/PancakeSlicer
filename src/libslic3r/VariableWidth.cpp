#include "VariableWidth.hpp"
#include "ExtrusionEntity.hpp"
#include "Polyline.hpp"
#include "Flow.hpp"
#include "Line.hpp"
#include <cassert>
#include "libslic3r.h"
#include "Point.hpp"
#include <cmath>
#include <vector>
#include <cstddef>
#include <utility>
#include <algorithm>

namespace Slic3r {

ExtrusionMultiPath thick_polyline_to_multi_path(const ThickPolyline& thick_polyline, ExtrusionRole role, const Flow& flow, const float tolerance, const float merge_tolerance)
{
    ExtrusionMultiPath multi_path;
    ExtrusionPath      path(role);
    ThickLines         lines = thick_polyline.thicklines();

    for (int i = 0; i < (int)lines.size(); ++i) {
        const ThickLine& line = lines[i];
        assert(line.a_width >= SCALED_EPSILON && line.b_width >= SCALED_EPSILON);

        const coordf_t line_len = line.length();
        if (line_len < SCALED_EPSILON) {
            if (!path.empty())
                path.polyline.points.back() = Point3(line.b);
            else if (i + 1 < (int)lines.size())
                lines[i + 1].a = line.a;
            else if (!multi_path.paths.empty())
                multi_path.paths.back().polyline.points.back() = Point3(line.b);
            continue;
        }

        double thickness_delta = fabs(line.a_width - line.b_width);
        if (thickness_delta > tolerance) {
            const auto segments = (unsigned int)ceil(thickness_delta / tolerance);
            const coordf_t seg_len = line_len / segments;
            Points pp;
            std::vector<coordf_t> width;
            {
                pp.push_back(line.a);
                width.push_back(line.a_width);
                for (size_t j = 1; j < segments; ++j) {
                    pp.push_back((line.a.cast<double>() + (line.b - line.a).cast<double>().normalized() * (j * seg_len)).cast<coord_t>());

                    coordf_t w = line.a_width + (j*seg_len) * (line.b_width-line.a_width) / line_len;
                    width.push_back(w);
                    width.push_back(w);
                }
                pp.push_back(line.b);
                width.push_back(line.b_width);

                assert(pp.size() == segments + 1u);
                assert(width.size() == segments*2);
            }

            lines.erase(lines.begin() + i);
            for (size_t j = 0; j < segments; ++j) {
                ThickLine new_line(pp[j], pp[j+1]);
                new_line.a_width = width[2*j];
                new_line.b_width = width[2*j+1];
                lines.insert(lines.begin() + i + j, new_line);
            }

            -- i;
            continue;
        }

        const double w        = fmax(line.a_width, line.b_width);
        const Flow   new_flow = (role == erOverhangPerimeter && flow.bridge()) ? flow : flow.with_width(unscale<float>(w) + flow.height() * float(1. - 0.25 * PI));
        if (path.polyline.points.empty()) {
            path.polyline.append(Point3(line.a));
            path.polyline.append(Point3(line.b));
            #ifdef SLIC3R_DEBUG
            printf("  filling %f gap\n", flow.width);
            #endif
            path.mm3_per_mm  = new_flow.mm3_per_mm();
            path.width       = new_flow.width();
            path.height      = new_flow.height();
        } else {
            assert(path.width >= EPSILON);
            thickness_delta = scaled<double>(fabs(path.width - new_flow.width()));
            if (thickness_delta <= merge_tolerance) {
                path.polyline.append(Point3(line.b));
            } else {
                multi_path.paths.emplace_back(std::move(path));
                path = ExtrusionPath(role);
                -- i;
            }
        }
    }
    if (path.polyline.is_valid())
        multi_path.paths.emplace_back(std::move(path));
    return multi_path;
}

static ExtrusionPaths thick_polyline_to_extrusion_paths_2(const ThickPolyline& thick_polyline, ExtrusionRole role, const Flow& flow, const float tolerance)
{
    ExtrusionPaths paths;
    ExtrusionPath path(role);
    ThickLines lines = thick_polyline.thicklines();

    size_t start_index = 0;
    double max_width, min_width;

    for (int i = 0; i < (int)lines.size(); ++i) {
        const ThickLine& line = lines[i];

        if (i == 0) {
            max_width = line.a_width;
            min_width = line.a_width;
        }

        const coordf_t line_len = line.length();
        if (line_len < SCALED_EPSILON) continue;

        double thickness_delta = std::max(fabs(max_width - line.b_width), fabs(min_width - line.b_width));
        if (thickness_delta > tolerance) {
            if (start_index != i){
                path = ExtrusionPath(role);
                double length = lines[start_index].length();
                double sum = lines[start_index].length() * 0.5 * (lines[start_index].a_width + lines[start_index].b_width);
                path.polyline.append(Point3(lines[start_index].a));
                for (int idx = start_index + 1; idx < i; idx++) {
                    length += lines[idx].length();
                    sum += lines[idx].length() * 0.5 * (lines[idx].a_width + lines[idx].b_width);
                    path.polyline.append(Point3(lines[idx].a));
                }
                path.polyline.append(Point3(lines[i].a));
                if (length > SCALED_EPSILON) {
                    double w = sum / length;
                    Flow new_flow = flow.with_width(unscale<float>(w) + flow.height() * float(1. - 0.25 * PI));
                    path.mm3_per_mm = new_flow.mm3_per_mm();
                    path.width = new_flow.width();
                    path.height = new_flow.height();
                    paths.emplace_back(std::move(path));
                }
            }

            start_index = i;
            max_width = line.a_width;
            min_width = line.a_width;

            thickness_delta = fabs(line.a_width - line.b_width);
            if (thickness_delta > tolerance){
                const unsigned int segments = (unsigned int)ceil(thickness_delta / tolerance);
                const coordf_t seg_len = line_len / segments;
                Points pp;
                std::vector<coordf_t> width;
                {
                    pp.push_back(line.a);
                    width.push_back(line.a_width);
                    for (size_t j = 1; j < segments; ++j) {
                        pp.push_back((line.a.cast<double>() + (line.b - line.a).cast<double>().normalized() * (j * seg_len)).cast<coord_t>());

                        coordf_t w = line.a_width + (j * seg_len) * (line.b_width - line.a_width) / line_len;
                        width.push_back(w);
                        width.push_back(w);
                    }
                    pp.push_back(line.b);
                    width.push_back(line.b_width);

                    assert(pp.size() == segments + 1u);
                    assert(width.size() == segments * 2);
                }

                lines.erase(lines.begin() + i);
                for (size_t j = 0; j < segments; ++j) {
                    ThickLine new_line(pp[j], pp[j + 1]);
                    new_line.a_width = width[2 * j];
                    new_line.b_width = width[2 * j + 1];
                    lines.insert(lines.begin() + i + j, new_line);
                }
                --i;
                continue;
            }
        }
        else {
            max_width = std::max(max_width, std::max(line.a_width, line.b_width));
            min_width = std::min(min_width, std::min(line.a_width, line.b_width));
        }
    }
    size_t final_size = lines.size();
    if (start_index < final_size) {
        path = ExtrusionPath(role);
        double length = lines[start_index].length();
        double sum = lines[start_index].length() * lines[start_index].a_width;
        path.polyline.append(Point3(lines[start_index].a));
        for (int idx = start_index + 1; idx < final_size; idx++) {
            length += lines[idx].length();
            sum += lines[idx].length() * lines[idx].a_width;
            path.polyline.append(Point3(lines[idx].a));
        }
        path.polyline.append(Point3(lines[final_size - 1].b));
        if (length > SCALED_EPSILON) {
            double w = sum / length;
            Flow new_flow = flow.with_width(unscale<float>(w) + flow.height() * float(1. - 0.25 * PI));
            path.mm3_per_mm = new_flow.mm3_per_mm();
            path.width = new_flow.width();
            path.height = new_flow.height();
            paths.emplace_back(std::move(path));
        }
    }

    return paths;
}

void variable_width(const ThickPolylines& polylines, ExtrusionRole role, const Flow& flow, std::vector<ExtrusionEntity*>& out)
{
    const float tolerance = float(scale_(0.05));
    for (const ThickPolyline& p : polylines) {
        ExtrusionPaths paths = thick_polyline_to_extrusion_paths_2(p, role, flow, tolerance);
        if (!paths.empty()) {
            if (paths.front().first_point() == paths.back().last_point())
                out.emplace_back(new ExtrusionLoop(std::move(paths)));
            else {
                for (ExtrusionPath& path : paths)
                    out.emplace_back(new ExtrusionPath(std::move(path)));
            }
        }
    }
}

}
