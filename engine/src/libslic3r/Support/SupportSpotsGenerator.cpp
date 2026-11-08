#include "SupportSpotsGenerator.hpp"

#include "ExPolygon.hpp"
#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "libslic3r/Flow.hpp"
#include "GCode/ExtrusionProcessor.hpp"
#include "Line.hpp"
#include "Point.hpp"
#include "tbb/parallel_for.h"
#include "tbb/blocked_range.h"
#include "tbb/blocked_range2d.h"
#include "tbb/parallel_reduce.h"
#include <algorithm>
#include <boost/log/trivial.hpp>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <stack>
#include <utility>
#include <vector>

#include "AABBTreeLines.hpp"
#include "libslic3r/Layer.hpp"

#ifdef DEBUG_FILES
#include <boost/nowide/cstdio.hpp>
#include "libslic3r/Color.hpp"
#endif


namespace Slic3r {

class ExtrusionLine
{
public:
    ExtrusionLine() : a(Vec2f::Zero()), b(Vec2f::Zero()), len(0.0), origin_entity(nullptr) {}
    ExtrusionLine(const Vec2f &a, const Vec2f &b, float len, const ExtrusionEntity *origin_entity)
        : a(a), b(b), len(len), origin_entity(origin_entity)
    {}

    ExtrusionLine(const Vec2f &a, const Vec2f &b)
        : a(a), b(b), len((a-b).norm()), origin_entity(nullptr)
    {}

    bool is_external_perimeter() const
    {
        assert(origin_entity != nullptr);
        return origin_entity->role() == erExternalPerimeter;
    }

    Vec2f                  a;
    Vec2f                  b;
    float                  len;
    const ExtrusionEntity *origin_entity;

    std::optional<SupportSpotsGenerator::SupportPointCause> support_point_generated = {};
    float form_quality            = 1.0f;
    float curled_up_height        = 0.0f;

    static const constexpr int Dim = 2;
    using Scalar                   = Vec2f::Scalar;
};

auto get_a(ExtrusionLine &&l) { return l.a; }
auto get_b(ExtrusionLine &&l) { return l.b; }

namespace SupportSpotsGenerator {

using LD = AABBTreeLines::LinesDistancer<ExtrusionLine>;

float get_flow_width(const LayerRegion *region, ExtrusionRole role)
{
    if (role == ExtrusionRole::erBridgeInfill) return region->flow(FlowRole::frExternalPerimeter).width();
    if (role == ExtrusionRole::erExternalPerimeter) return region->flow(FlowRole::frExternalPerimeter).width();
    if (role == ExtrusionRole::erGapFill) return region->flow(FlowRole::frInfill).width();
    if (role == ExtrusionRole::erPerimeter) return region->flow(FlowRole::frPerimeter).width();
    if (role == ExtrusionRole::erSolidInfill) return region->flow(FlowRole::frSolidInfill).width();
    if (role == ExtrusionRole::erInternalInfill) return region->flow(FlowRole::frInfill).width();
    if (role == ExtrusionRole::erTopSolidInfill) return region->flow(FlowRole::frTopSolidInfill).width();
    return region->flow(FlowRole::frPerimeter).width();
}

float estimate_curled_up_height(
    float distance, float curvature, float layer_height, float flow_width, float prev_line_curled_height, Params params)
{
    float curled_up_height = 0;
    if (fabs(distance) < 3.0 * flow_width) {
        curled_up_height = std::max(prev_line_curled_height - layer_height * 0.75f, 0.0f);
    }

    if (distance > params.malformation_distance_factors.first * flow_width &&
        distance < params.malformation_distance_factors.second * flow_width) {
        float curling_section = distance;

        float swelling_radius = (layer_height + curling_section) / 2.0f;
        curled_up_height += std::max(0.f, (swelling_radius - layer_height) / 2.0f);

        if (curvature > 0.01) {
            float radius    = (1.0 / curvature);
            float curling_t = sqrt(radius / 100);
            float b         = curling_t * flow_width;
            float a         = curling_section;
            float c         = sqrt(std::max(0.0f, a * a - b * b));

            curled_up_height += c;
        }
        curled_up_height = std::min(curled_up_height, params.max_curled_height_factor * layer_height);
    }

    return curled_up_height;
}

void estimate_malformations(LayerPtrs &layers, const Params &params)
{
#ifdef DEBUG_FILES
    FILE *debug_file = boost::nowide::fopen(debug_out_path("object_malformations.obj").c_str(), "w");
    FILE *full_file  = boost::nowide::fopen(debug_out_path("object_full.obj").c_str(), "w");
#endif

    LD prev_layer_lines{};

    for (Layer *l : layers) {
        l->curled_lines.clear();
        std::vector<Linef> boundary_lines = l->lower_layer != nullptr ? to_unscaled_linesf(l->lower_layer->lslices) : std::vector<Linef>();
        AABBTreeLines::LinesDistancer<Linef> prev_layer_boundary{std::move(boundary_lines)};
        std::vector<ExtrusionLine>           current_layer_lines;
        for (const LayerRegion *layer_region : l->regions()) {
            for (const ExtrusionEntity *extrusion : layer_region->perimeters.flatten().entities) {
                if (extrusion->role() != Slic3r::erExternalPerimeter)
                    continue;

                Points extrusion_pts;
                extrusion->collect_points(extrusion_pts);
                float flow_width       = get_flow_width(layer_region, extrusion->role());
                auto  annotated_points = estimate_points_properties<true, true, false, false>(extrusion_pts,
                                                                                                                 prev_layer_lines,
                                                                                                                 flow_width,
                                                                                                                 params.bridge_distance);
                for (size_t i = 0; i < annotated_points.size(); ++i) {
                    const ExtendedPoint<2>& a = i > 0 ? annotated_points[i - 1] : annotated_points[i];
                    const ExtendedPoint<2>& b = annotated_points[i];
                    ExtrusionLine line_out{a.position.cast<float>(), b.position.cast<float>(), float((a.position - b.position).norm()),
                                           extrusion};

                    Vec2f middle                               = 0.5 * (line_out.a + line_out.b);
                    auto [middle_distance, bottom_line_idx, x] = prev_layer_lines.distance_from_lines_extra<false>(middle);
                    ExtrusionLine bottom_line                  = prev_layer_lines.get_lines().empty() ? ExtrusionLine{} :
                                                                                                        prev_layer_lines.get_line(bottom_line_idx);

                    float sign = (prev_layer_boundary.distance_from_lines<true>(middle.cast<double>()) + 0.5f * flow_width) < 0.0f ? -1.0f :
                                                                                                                                     1.0f;

                    line_out.curled_up_height = estimate_curled_up_height(middle_distance * sign * params.curled_distance_expansion, 0.5 * (a.curvature + b.curvature),
                                                                          l->height, flow_width, bottom_line.curled_up_height, params);

                    current_layer_lines.push_back(line_out);
                }
            }
        }

        for (const ExtrusionLine &line : current_layer_lines) {
            if (line.curled_up_height > params.curling_tolerance_limit) {
                l->curled_lines.push_back(CurledLine{Point::new_scale(line.a), Point::new_scale(line.b), line.curled_up_height});
            }
        }

#ifdef DEBUG_FILES
        for (const ExtrusionLine &line : current_layer_lines) {
            if (line.curled_up_height > params.curling_tolerance_limit) {
                Vec3f color = value_to_rgbf(-EPSILON, l->height * params.max_curled_height_factor, line.curled_up_height);
                fprintf(debug_file, "v %f %f %f  %f %f %f\n", line.b[0], line.b[1], l->print_z, color[0], color[1], color[2]);
            }
        }
        for (const ExtrusionLine &line : current_layer_lines) {
            Vec3f color = value_to_rgbf(-EPSILON, l->height * params.max_curled_height_factor, line.curled_up_height);
            fprintf(full_file, "v %f %f %f  %f %f %f\n", line.b[0], line.b[1], l->print_z, color[0], color[1], color[2]);
        }
#endif

        prev_layer_lines = LD{current_layer_lines};
    }

#ifdef DEBUG_FILES
    fclose(debug_file);
    fclose(full_file);
#endif
}

}
}
