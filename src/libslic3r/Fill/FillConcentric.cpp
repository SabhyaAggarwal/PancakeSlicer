#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../Surface.hpp"
#include "../VariableWidth.hpp"
#include "Arachne/WallToolPaths.hpp"

#include "FillConcentric.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "FillCornerSmoothing.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Polygon.hpp"
#include <algorithm>
#include "libslic3r/PrintConfig.hpp"
#include <cstddef>
#include <cassert>
#include <libslic3r/ShortestPath.hpp>
#include <utility>
#include <vector>
#include "libslic3r/ExtrusionEntity.hpp"

namespace Slic3r {

void FillConcentric::_fill_surface_single(
    const FillParams                &params, 
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction, 
    ExPolygon                        expolygon,
    Polylines                       &polylines_out)
{
    BoundingBox bounding_box = expolygon.contour.bounding_box();
    
    coord_t min_spacing = scale_(this->spacing) * params.multiline;
    coord_t distance = coord_t(min_spacing / params.density);
    if (min_spacing <= 0 || distance <= 0)
        return;
    
    if (params.density > 0.9999f && !params.dont_adjust) {
        distance = this->_adjust_solid_spacing(bounding_box.size()(0), distance);
        this->spacing = unscale<double>(distance);
    }

    ExPolygons contracted = offset_ex(expolygon, -float(scale_(0.5 * (params.multiline - 1) * this->spacing )));

    Polygons loops = to_polygons(contracted);

    ExPolygons last { contracted };
    while (! last.empty()) {
        last = offset2_ex(last, -(distance + min_spacing/2), +min_spacing/2);
        append(loops, to_polygons(last));
    }

    auto corner_stays_inside = [&contracted](const Vec2d &from, const Vec2d &to) {
        for (const double t : { 0.25, 0.5, 0.75 }) {
            const Vec2d  sample = from + t * (to - from);
            const Point  point(coord_t(sample.x()), coord_t(sample.y()));
            if (std::none_of(contracted.begin(), contracted.end(),
                             [&point](const ExPolygon &region) { return region.contains(point); }))
                return false;
        }
        return true;
    };
    smooth_polygons_corners(loops, params.smooth_factor, scaled<double>(params.resolution), 0.5 * distance,
                            corner_stays_inside);

    loops = union_pt_chained_outside_in(loops);

    if (params.fill_order == SurfaceFillOrder::Outward)
        std::reverse(loops.begin(), loops.end());
    
    size_t iPathFirst = polylines_out.size();
    Point last_pos(0, 0);
    for (const Polygon &loop : loops) {
        polylines_out.emplace_back(loop.split_at_index(last_pos.nearest_point_index(loop.points)));
        last_pos = polylines_out.back().last_point();
    }

    multiline_fill(polylines_out, params, spacing);

    size_t j = iPathFirst;
    for (size_t i = iPathFirst; i < polylines_out.size(); ++ i) {
        polylines_out[i].clip_end(this->loop_clipping);
        if (polylines_out[i].is_valid()) {
            if (j < i)
                polylines_out[j] = std::move(polylines_out[i]);
            ++ j;
        }
    }
    if (j < polylines_out.size())
        polylines_out.erase(polylines_out.begin() + j, polylines_out.end());
}

void FillConcentric::_fill_surface_single(const FillParams& params,
    unsigned int                   thickness_layers,
    const std::pair<float, Point>& direction,
    ExPolygon                      expolygon,
    ThickPolylines& thick_polylines_out)
{
    assert(params.use_arachne);
    assert(this->print_config != nullptr && this->print_object_config != nullptr);

    Point   bbox_size = expolygon.contour.bounding_box().size();
    coord_t min_spacing = scaled<coord_t>(this->spacing);
    if (min_spacing <= 0)
        return;

    if (params.density > 0.9999f && !params.dont_adjust) {
        coord_t                loops_count = std::max(bbox_size.x(), bbox_size.y()) / min_spacing + 1;
        Polygons               polygons = offset(expolygon, float(min_spacing) / 2.f);

        double min_nozzle_diameter = *std::min_element(print_config->nozzle_diameter.values.begin(), print_config->nozzle_diameter.values.end());
        Arachne::WallToolPathsParams input_params;
        input_params.min_bead_width = 0.85 * min_nozzle_diameter;
        input_params.min_feature_size = 0.25 * min_nozzle_diameter;
        input_params.wall_transition_length = 1.0 * min_nozzle_diameter;
        input_params.wall_transition_angle = 10;
        input_params.wall_transition_filter_deviation = 0.25 * min_nozzle_diameter;
        input_params.wall_distribution_count = 1;

        Arachne::WallToolPaths wallToolPaths(polygons, min_spacing, min_spacing, loops_count, 0, params.layer_height, input_params);

        std::vector<Arachne::VariableWidthLines>    loops = wallToolPaths.getToolPaths();
        std::vector<const Arachne::ExtrusionLine*> all_extrusions;
        for (Arachne::VariableWidthLines& loop : loops) {
            if (loop.empty())
                continue;
            for (const Arachne::ExtrusionLine& wall : loop)
                all_extrusions.emplace_back(&wall);
        }

        const bool forced_fill_order = params.fill_order != SurfaceFillOrder::Default;
        if (forced_fill_order) {
            const bool outward = params.fill_order == SurfaceFillOrder::Outward;
            std::stable_sort(all_extrusions.begin(), all_extrusions.end(),
                             [outward](const Arachne::ExtrusionLine *a, const Arachne::ExtrusionLine *b) {
                                 return outward ? a->inset_idx > b->inset_idx : a->inset_idx < b->inset_idx;
                             });
        }

        size_t firts_poly_idx = thick_polylines_out.size();
        Point  last_pos(0, 0);
        for (const Arachne::ExtrusionLine* extrusion : all_extrusions) {
            if (extrusion->empty())
                continue;

            ThickPolyline thick_polyline = Arachne::to_thick_polyline(*extrusion);
            if (extrusion->is_closed)
                thick_polyline.start_at_index(last_pos.nearest_point_index(thick_polyline.points));
            thick_polylines_out.emplace_back(std::move(thick_polyline));
            last_pos = thick_polylines_out.back().last_point();
        }

        size_t j = firts_poly_idx;
        for (size_t i = firts_poly_idx; i < thick_polylines_out.size(); ++i) {
            thick_polylines_out[i].clip_end(this->loop_clipping);
            if (thick_polylines_out[i].is_valid()) {
                if (j < i)
                    thick_polylines_out[j] = std::move(thick_polylines_out[i]);
                ++j;
            }
        }
        if (j < thick_polylines_out.size())
            thick_polylines_out.erase(thick_polylines_out.begin() + int(j), thick_polylines_out.end());

        if (!forced_fill_order)
            reorder_by_shortest_traverse(thick_polylines_out);
    }
    else {
        Polylines polylines;
        this->_fill_surface_single(params, thickness_layers, direction, expolygon, polylines);
        append(thick_polylines_out, to_thick_polylines(std::move(polylines), min_spacing));
    }
}

}
