#include <algorithm>
#include <cassert>
#include <cfloat>
#include <math.h>
#include <limits>
#include <cstdint>
#include <stdio.h>
#include <numeric>

#include <cmath>
#include <string>
#include <vector>
#include <utility>
#include "../ClipperUtils.hpp"
#include "../EdgeGrid.hpp"
#include "../Geometry.hpp"
#include "../Geometry/Circle.hpp"
#include "../Point.hpp"
#include "../PrintConfig.hpp"
#include "../Surface.hpp"
#include "../libslic3r.h"
#include "../VariableWidth.hpp"

#include "FillBase.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "FillConcentric.hpp"
#include "FillSpiralInset.hpp"
#include "FillHoneycomb.hpp"
#include "Fill3DHoneycomb.hpp"
#include "FillGyroid.hpp"
#include "FillTpmsD.hpp"
#include "FillTpmsFK.hpp"
#include "FillPlanePath.hpp"
#include "FillLine.hpp"
#include "FillRectilinear.hpp"
#include "FillAdaptive.hpp"
#include "FillLightning.hpp"
#include "FillConcentricInternal.hpp"
#include "FillCrossHatch.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/ShortestPath.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Flow.hpp"

namespace Slic3r {

float Fill::infill_anchor = 400;
float Fill::infill_anchor_max = 20;

Fill* Fill::new_from_type(const InfillPattern type)
{
    switch (type) {
    case ipConcentric:          return new FillConcentric();
    case ipSpiralInset:    return new FillSpiralInset();
    case ipHoneycomb:           return new FillHoneycomb();
    case ipLateralHoneycomb:         return new FillLateralHoneycomb();
    case ip3DHoneycomb:         return new Fill3DHoneycomb();
    case ipGyroid:              return new FillGyroid();
    case ipTpmsD:               return new FillTpmsD();
    case ipTpmsFK:              return new FillTpmsFK();
    case ipRectilinear:         return new FillRectilinear();
    case ipAlignedRectilinear:  return new FillAlignedRectilinear();
    case ipCrossHatch:          return new FillCrossHatch();
    case ipMonotonic:           return new FillMonotonic();
    case ipLine:                return new FillLine();
    case ipGrid:                return new FillGrid();
    case ipLateralLattice:           return new FillLateralLattice();
    case ipTriangles:           return new FillTriangles();
    case ipStars:               return new FillStars();
    case ipCubic:               return new FillCubic();
    case ipQuarterCubic:        return new FillQuarterCubic();
    case ipArchimedeanChords:   return new FillArchimedeanChords();
    case ipHilbertCurve:        return new FillHilbertCurve();
    case ipOctagramSpiral:      return new FillOctagramSpiral();
    case ipAdaptiveCubic:       return new FillAdaptive::Filler();
    case ipSupportCubic:        return new FillAdaptive::Filler();
    case ipSupportBase:         return new FillSupportBase();
    case ipLightning:           return new FillLightning::Filler();
    case ipConcentricInternal:  return new FillConcentricInternal();
    case ipMonotonicLine:       return new FillMonotonicLines();
    case ipZigZag:              return new FillZigZag();
    case ipCrossZag:            return new FillCrossZag();
    case ipLockedZag:           return new FillLockedZag();
    default: throw Slic3r::InvalidArgument("unknown type");
    }
}

Fill* Fill::new_from_type(const std::string &type)
{
    const t_config_enum_values &enum_keys_map = ConfigOptionEnum<InfillPattern>::get_enum_values();
    t_config_enum_values::const_iterator it = enum_keys_map.find(type);
    return (it == enum_keys_map.end()) ? nullptr : new_from_type(InfillPattern(it->second));
}

static bool use_bridge_flow_initializer = Fill::use_bridge_flow(ipGrid);

bool Fill::use_bridge_flow(const InfillPattern type)
{
	static std::vector<unsigned char> cached;
	if (cached.empty()) {
		cached.assign(size_t(ipCount), 0);
		for (size_t i = 0; i < cached.size(); ++ i) {
			auto *fill = Fill::new_from_type((InfillPattern)i);
			cached[i] = fill->use_bridge_flow();
			delete fill;
		}
	}
	return cached[type] != 0;
}

Polylines Fill::fill_surface(const Surface *surface, const FillParams &params)
{
    Slic3r::ExPolygons expp = offset_ex(surface->expolygon, float(scale_(this->overlap - 0.5 * this->spacing)));
    const Point shift = this->aligned_to_origin() && ! empty(this->bounding_box) ? this->bounding_box.center() : Point::Zero();
    translate(expp, -shift);
    Polylines polylines_out;
    for (size_t i = 0; i < expp.size(); ++ i)
        _fill_surface_single(
            params,
            surface->thickness_layers,
            _infill_direction(surface),
            std::move(expp[i]),
            polylines_out);
    for (Polyline &pl : polylines_out)
        pl.translate(shift);
    return polylines_out;
}

ThickPolylines Fill::fill_surface_arachne(const Surface* surface, const FillParams& params)
{
    Slic3r::ExPolygons expp = offset_ex(surface->expolygon, float(scale_(this->overlap - 0.5 * this->spacing)));
    ThickPolylines thick_polylines_out;
    for (ExPolygon& expoly : expp)
        _fill_surface_single(params, surface->thickness_layers, _infill_direction(surface), std::move(expoly), thick_polylines_out);
    return thick_polylines_out;
}

void Fill::fill_surface_extrusion(const Surface* surface, const FillParams& params, ExtrusionEntitiesPtr& out)
{
    Polylines polylines;
    ThickPolylines thick_polylines;
    try {
        if (params.use_arachne)
            thick_polylines = this->fill_surface_arachne(surface, params);
        else
            polylines = this->fill_surface(surface, params);
    }
    catch (InfillFailedException&) {}

    if (!polylines.empty() || !thick_polylines.empty()) {
        double flow_mm3_per_mm = params.flow.mm3_per_mm();
        double flow_width = params.flow.width();
        if (params.using_internal_flow) {
        }
        else {
            Flow new_flow = params.flow.with_spacing(this->spacing);
            flow_mm3_per_mm = new_flow.mm3_per_mm();
            flow_width = new_flow.width();
        }
        ExtrusionEntityCollection* eec = nullptr;
        out.push_back(eec = new ExtrusionEntityCollection());
        eec->no_sort = this->no_sort();
        auto is_flow_calib = params.extrusion_role == erTopSolidInfill && this->print_object_config->has("calib_flowrate_topinfill_special_order") &&
                             this->print_object_config->option("calib_flowrate_topinfill_special_order")->getBool();
        const bool keep_fill_order = params.fill_order != SurfaceFillOrder::Default;
        if (is_flow_calib || keep_fill_order) {
            eec->no_sort = true;
        }
        size_t idx   = eec->entities.size();
        if (params.use_arachne) {
            Flow new_flow = params.flow.with_spacing(float(this->spacing));
            variable_width(thick_polylines, params.extrusion_role, new_flow, eec->entities);
            thick_polylines.clear();
        }
        else {
            extrusion_entities_append_paths(
                eec->entities, std::move(polylines),
                params.extrusion_role,
                flow_mm3_per_mm, float(flow_width), params.flow.height());
        }
        if (!params.can_reverse || is_flow_calib || keep_fill_order) {
            for (size_t i = idx; i < eec->entities.size(); i++)
                eec->entities[i]->set_reverse();
        }

        this->_create_gap_fill(surface, params, eec);
    }
}

void Fill::_create_gap_fill(const Surface* surface, const FillParams& params, ExtrusionEntityCollection* out){

    if (this->print_object_config == nullptr) return;

    if ((this->print_object_config->gap_fill_target.value == gftNowhere) ||
        (surface->surface_type == stInternalSolid && this->print_object_config->gap_fill_target.value != gftEverywhere))
        return;

    Flow new_flow = params.flow;
    ExPolygons unextruded_areas;
    unextruded_areas = diff_ex(this->no_overlap_expolygons, union_ex(out->polygons_covered_by_spacing(10)));
    ExPolygons gapfill_areas = union_ex(unextruded_areas);
    if (!this->no_overlap_expolygons.empty())
        gapfill_areas = intersection_ex(gapfill_areas, this->no_overlap_expolygons);

    if (gapfill_areas.size() > 0 && params.density >= 1) {
        double min = 0.2 * new_flow.scaled_spacing() * (1 - INSET_OVERLAP_TOLERANCE);
        double max = 2. * new_flow.scaled_spacing();
        ExPolygons gaps_ex = diff_ex(
                                     opening_ex(gapfill_areas, float(min / 2.)),
                                     offset2_ex(gapfill_areas, -float(max / 2.), float(max / 2. + ClipperSafetyOffset)));
        Points ordering_points;
        ordering_points.reserve(gaps_ex.size());
        ExPolygons gaps_ex_sorted;
        gaps_ex_sorted.reserve(gaps_ex.size());
        for (const ExPolygon &ex : gaps_ex)
            ordering_points.push_back(ex.contour.first_point());
        std::vector<Points::size_type> order2 = chain_points(ordering_points);
        for (size_t i : order2)
            gaps_ex_sorted.emplace_back(std::move(gaps_ex[i]));

        ThickPolylines polylines;
        for (ExPolygon& ex : gaps_ex_sorted) {
            ex.douglas_peucker(SCALED_RESOLUTION * 0.1);
            ex.medial_axis(min, max, &polylines);
        }

        if (!polylines.empty() && !is_bridge(params.extrusion_role)) {
            polylines.erase(std::remove_if(polylines.begin(), polylines.end(),
                                           [&](const ThickPolyline& p) {
                return p.length() < scale_(params.config->filter_out_gap_fill.value);
            }), polylines.end());

            variable_width(polylines, erGapFill, params.flow, out->entities);
        }
    }
}

coord_t Fill::_adjust_solid_spacing(const coord_t width, const coord_t distance)
{
    assert(width >= 0);
    assert(distance > 0);
    const auto  number_of_intervals = coord_t((width - EPSILON) / distance);
    coord_t     distance_new        = (number_of_intervals == 0) ?
        distance :
        coord_t((width - EPSILON) / number_of_intervals);
    const coordf_t factor = coordf_t(distance_new) / coordf_t(distance);
    assert(factor > 1. - 1e-5);
    const coordf_t factor_max = 1.2;
    if (factor > factor_max)
        distance_new = coord_t(floor((coordf_t(distance) * factor_max + 0.5)));
    return distance_new;
}

std::pair<float, Point> Fill::_infill_direction(const Surface *surface) const
{
    float out_angle = this->angle;

	if (out_angle == FLT_MAX) {
        printf("Using undefined infill angle\n");
        out_angle = 0.f;
    }

    Point out_shift = empty(this->bounding_box) ?
    	surface->expolygon.contour.bounding_box().center() :
        this->bounding_box.center();

#if 0
    if (empty(this->bounding_box)) {
        printf("Fill::_infill_direction: empty bounding box!");
    } else {
        printf("Fill::_infill_direction: reference point %d, %d\n", out_shift.x, out_shift.y);
    }
#endif

    if (surface->bridge_angle >= 0) {
#ifdef SLIC3R_DEBUG
        printf("Filling bridge with angle %f\n", surface->bridge_angle);
#endif
        out_angle = float(surface->bridge_angle);
    } else if (this->layer_id != size_t(-1) && !fixed_angle) {
        if (!this->dont_alternate_fill_direction) {
            out_angle += this->_layer_angle(this->layer_id / surface->thickness_layers);
        }
    } else {
    }

    out_angle += float(M_PI/2.);
    return std::pair<float, Point>(out_angle, out_shift);
}

struct ContourIntersectionPoint {
    size_t                      contour_idx;
    size_t                      point_idx;
    double                      param;
    ContourIntersectionPoint*   prev_on_contour { nullptr };
    ContourIntersectionPoint*   next_on_contour { nullptr };
    double                      contour_not_taken_length_prev { std::numeric_limits<double>::max() };
    double                      contour_not_taken_length_next { std::numeric_limits<double>::max() };
    bool                        consumed { false };
    bool                        prev_trimmed { false };
    bool                        next_trimmed { false };

    void                        consume_prev() { this->contour_not_taken_length_prev = 0.; this->prev_trimmed = true; this->consumed = true; }
    void                        consume_next() { this->contour_not_taken_length_next = 0.; this->next_trimmed = true; this->consumed = true; }

    void                        trim_prev(const double new_len) {
        if (new_len < this->contour_not_taken_length_prev) {
            this->contour_not_taken_length_prev = new_len;
            this->prev_trimmed = true;
        }
    }
    void                        trim_next(const double new_len) {
        if (new_len < this->contour_not_taken_length_next) {
            this->contour_not_taken_length_next = new_len;
            this->next_trimmed = true;
        }
    }

    bool                        could_take_prev() const throw() { return ! this->consumed && this->contour_not_taken_length_prev > SCALED_EPSILON; }
    bool                        could_take_next() const throw() { return ! this->consumed && this->contour_not_taken_length_next > SCALED_EPSILON; }

    bool                        could_connect_prev() const throw()
        { return ! this->consumed && this->prev_on_contour != this && ! this->prev_on_contour->consumed && ! this->prev_trimmed && ! this->prev_on_contour->next_trimmed; }
    bool                        could_connect_next() const throw()
        { return ! this->consumed && this->next_on_contour != this && ! this->next_on_contour->consumed && ! this->next_trimmed && ! this->next_on_contour->prev_trimmed; }
};

static inline double closed_contour_distance_ccw(double param1, double param2, double contour_length)
{
    assert(param1 >= 0. && param1 <= contour_length);
    assert(param2 >= 0. && param2 <= contour_length);
    double d = param2 - param1;
    if (d < 0.)
        d += contour_length;
    return d;
}

static inline double closed_contour_distance_cw(double param1, double param2, double contour_length)
{
    return closed_contour_distance_ccw(param2, param1, contour_length);
}

double path_length_along_contour_ccw(const ContourIntersectionPoint *cp1, const ContourIntersectionPoint *cp2, double contour_length)
{
    assert(cp1 != nullptr);
    assert(cp2 != nullptr);
    assert(cp1->contour_idx == cp2->contour_idx);
    assert(cp1 != cp2);
    return closed_contour_distance_ccw(cp1->param, cp2->param, contour_length);
}

std::pair<double, double> path_lengths_along_contour(const ContourIntersectionPoint *cp1, const ContourIntersectionPoint *cp2, double contour_length)
{
    double param_lo  = cp1->param;
    double param_hi  = cp2->param;
    assert(param_lo >= 0. && param_lo <= contour_length);
    assert(param_hi >= 0. && param_hi <= contour_length);
    bool  reversed  = false;
    if (param_lo > param_hi) {
        std::swap(param_lo, param_hi);
        reversed = true;
    }
    auto out = std::make_pair(param_hi - param_lo, param_lo + contour_length - param_hi);
    if (reversed)
        std::swap(out.first, out.second);
    return out;
}

static inline void take_cw_full(Polyline &pl, const Points &contour, size_t idx_start, size_t idx_end)
{
    assert(! pl.empty() && pl.points.back() == contour[idx_start]);
    size_t i = (idx_start == 0) ? contour.size() - 1 : idx_start - 1;
    while (i != idx_end) {
        pl.points.emplace_back(contour[i]);
        if (i == 0)
            i = contour.size();
        -- i;
    }
    pl.points.emplace_back(contour[i]);
}

static inline double take_cw_limited(Polyline &pl, const Points &contour, const std::vector<double> &params, size_t idx_start, size_t idx_end, double length_to_take)
{
    assert(pl.empty() || pl.points.back() == contour[idx_start]);
    assert(contour.size() + 1 == params.size());
    assert(length_to_take > SCALED_EPSILON);
    double length = params.back();
    double p0     = params[idx_start];
    size_t i      = (idx_start == 0) ? contour.size() - 1 : idx_start - 1;
    size_t iprev  = idx_start;
    double lprev  = 0.;

    for (;;) {
        double l = closed_contour_distance_cw(p0, params[i], length);
        if (l >= length_to_take) {
            double t = double(length_to_take - lprev) / (l - lprev);
            pl.points.emplace_back(lerp(contour[iprev], contour[i], t));
            return length_to_take;
        }
        pl.points.emplace_back(contour[i]);
        if (i == idx_end)
            return l;
        iprev = i;
        lprev = l;
        if (i == 0)
            i = contour.size();
        -- i;
    }
    assert(false);
    return 0;
}

static inline void take_ccw_full(Polyline &pl, const Points &contour, size_t idx_start, size_t idx_end)
{
    assert(! pl.empty() && pl.points.back() == contour[idx_start]);
    size_t i = idx_start;
    if (++ i == contour.size())
        i = 0;
    while (i != idx_end) {
        pl.points.emplace_back(contour[i]);
        if (++ i == contour.size())
            i = 0;
    }
    pl.points.emplace_back(contour[i]);
}

static inline double take_ccw_limited(Polyline &pl, const Points &contour, const std::vector<double> &params, size_t idx_start, size_t idx_end, double length_to_take)
{
    assert(pl.empty() || pl.points.back() == contour[idx_start]);
    assert(contour.size() + 1 == params.size());
    assert(length_to_take > SCALED_EPSILON);
    double length = params.back();
    double p0     = params[idx_start];
    size_t i      = idx_start;
    if (++ i == contour.size())
        i = 0;
    size_t iprev  = idx_start;
    double lprev  = 0;
    for (;;) {
        double l = closed_contour_distance_ccw(p0, params[i], length);
        if (l >= length_to_take) {
            double t = double(length_to_take - lprev) / (l - lprev);
            pl.points.emplace_back(lerp(contour[iprev], contour[i], t));
            return length_to_take;
        }
        pl.points.emplace_back(contour[i]);
        if (i == idx_end)
            return l;
        iprev = i;
        lprev = l;
        if (++ i == contour.size())
            i = 0;
    }
    assert(false);
    return 0;
}

static void take(Polyline &pl1, const Polyline &pl2, const Points &contour, size_t idx_start, size_t idx_end, bool clockwise)
{
#ifndef NDEBUG
	assert(idx_start != idx_end);
    assert(pl1.size() >= 2);
    assert(pl2.size() >= 2);
#endif

	{
		int new_points = int(idx_end) - int(idx_start) - 1;
		if (new_points < 0)
			new_points += int(contour.size());
		pl1.points.reserve(pl1.points.size() + size_t(new_points) + pl2.points.size());
	}

	if (clockwise)
        take_cw_full(pl1, contour, idx_start, idx_end);
	else
        take_ccw_full(pl1, contour, idx_start, idx_end);

    pl1.points.insert(pl1.points.end(), pl2.points.begin() + 1, pl2.points.end());
}

static void take(Polyline &pl1, const Polyline &pl2, const Points &contour, ContourIntersectionPoint *cp_start, ContourIntersectionPoint *cp_end, bool clockwise)
{
    assert(cp_start->prev_on_contour != nullptr);
    assert(cp_start->next_on_contour != nullptr);
    assert(cp_end  ->prev_on_contour != nullptr);
    assert(cp_end  ->next_on_contour != nullptr);
    assert(cp_start != cp_end);

    take(pl1, pl2, contour, cp_start->point_idx, cp_end->point_idx, clockwise);

    if (clockwise)
        std::swap(cp_start, cp_end);
    if (cp_start->next_on_contour != cp_end)
        for (auto *cp = cp_start->next_on_contour; cp->next_on_contour != cp_end; cp = cp->next_on_contour) {
            cp->consume_prev();
            cp->consume_next();
        }
    cp_start->consume_next();
    cp_end->consume_prev();
}

static void take_limited(
    Polyline &pl1, const Points &contour, const std::vector<double> &params,
    ContourIntersectionPoint *cp_start, ContourIntersectionPoint *cp_end, bool clockwise, double take_max_length, double line_half_width)
{
#ifndef NDEBUG
    assert(cp_start->prev_on_contour != nullptr);
    assert(cp_start->next_on_contour != nullptr);
    assert(cp_end  ->prev_on_contour != nullptr);
    assert(cp_end  ->next_on_contour != nullptr);
    assert(pl1.size() >= 2);
    assert(contour.size() + 1 == params.size());
#endif

    if (! (clockwise ? cp_start->could_take_prev() : cp_start->could_take_next()))
        return;

    assert(pl1.points.front() == contour[cp_start->point_idx] || pl1.points.back() == contour[cp_start->point_idx]);
    bool        add_at_start = pl1.points.front() == contour[cp_start->point_idx];
    Points      pl_tmp;
    if (add_at_start) {
        pl_tmp = std::move(pl1.points);
        pl1.points.clear();
    }

    {
        int new_points = int(cp_end->point_idx) - int(cp_start->point_idx) - 1;
        if (new_points < 0)
            new_points += int(contour.size());
        pl1.points.reserve(pl1.points.size() + pl_tmp.size() + size_t(new_points));
    }

    double length = params.back();
    double length_to_go = take_max_length;
    cp_start->consumed = true;
    if (cp_start == cp_end) {
        length_to_go = std::max(0., std::min(length_to_go, length - line_half_width));
        length_to_go = std::min(length_to_go, clockwise ? cp_start->contour_not_taken_length_prev : cp_start->contour_not_taken_length_next);
        cp_start->consume_prev();
        cp_start->consume_next();
        if (length_to_go > SCALED_EPSILON)
            clockwise ?
                take_cw_limited (pl1, contour, params, cp_start->point_idx, cp_start->point_idx, length_to_go) :
                take_ccw_limited(pl1, contour, params, cp_start->point_idx, cp_start->point_idx, length_to_go);
    } else if (clockwise) {
        assert(cp_start != cp_end);
        for (ContourIntersectionPoint *cp = cp_start; cp != cp_end; cp = cp->prev_on_contour) {
            double l = closed_contour_distance_cw(cp->param, cp->prev_on_contour->param, length);
            length_to_go = std::min(length_to_go, cp->contour_not_taken_length_prev);
                length_to_go = std::max(0., std::min(length_to_go, l - line_half_width));
            cp->consume_prev();
            if (l >= length_to_go) {
                if (length_to_go > SCALED_EPSILON) {
                    cp->prev_on_contour->trim_next(l - length_to_go);
                    take_cw_limited(pl1, contour, params, cp->point_idx, cp->prev_on_contour->point_idx, length_to_go);
                }
                break;
            } else {
                cp->prev_on_contour->trim_next(0.);
                take_cw_full(pl1, contour, cp->point_idx, cp->prev_on_contour->point_idx);
                length_to_go -= l;
            }
        }
    } else {
        assert(cp_start != cp_end);
        for (ContourIntersectionPoint *cp = cp_start; cp != cp_end; cp = cp->next_on_contour) {
            double l = closed_contour_distance_ccw(cp->param, cp->next_on_contour->param, length);
            length_to_go = std::min(length_to_go, cp->contour_not_taken_length_next);
                length_to_go = std::max(0., std::min(length_to_go, l - line_half_width));
            cp->consume_next();
            if (l >= length_to_go) {
                if (length_to_go > SCALED_EPSILON) {
                    cp->next_on_contour->trim_prev(l - length_to_go);
                    take_ccw_limited(pl1, contour, params, cp->point_idx, cp->next_on_contour->point_idx, length_to_go);
                }
                break;
            } else {
                cp->next_on_contour->trim_prev(0.);
                take_ccw_full(pl1, contour, cp->point_idx, cp->next_on_contour->point_idx);
                length_to_go -= l;
            }
        }
    }

    if (add_at_start) {
        pl1.reverse();
        append(pl1.points, pl_tmp);
    }
}

struct SegmentPoint {
	size_t idx_segment = std::numeric_limits<size_t>::max();
	double t;
	Vec2d  point;

	bool valid() const { return idx_segment != std::numeric_limits<size_t>::max(); }
};

static inline SegmentPoint clip_start_segment_and_point(const Points &polyline, double distance)
{
	assert(polyline.size() >= 2);
	assert(distance > 0.);
	SegmentPoint out;
	if (polyline.size() >= 2) {
	    Vec2d pt_prev = polyline.front().cast<double>();
        for (size_t i = 1; i < polyline.size(); ++ i) {
			Vec2d pt = polyline[i].cast<double>();
			Vec2d v = pt - pt_prev;
	        double l = v.norm();
	        if (l > distance) {
	        	out.idx_segment = i - 1;
	        	out.t 			= distance / l;
	        	out.point 		= pt_prev + out.t * v;
	            break;
	        }
	        distance -= l;
	        pt_prev = pt;
	    }
	}
	return out;
}

static inline SegmentPoint clip_end_segment_and_point(const Points &polyline, double distance)
{
	assert(polyline.size() >= 2);
	assert(distance > 0.);
	SegmentPoint out;
	if (polyline.size() >= 2) {
	    Vec2d pt_next = polyline.back().cast<double>();
		for (int i = int(polyline.size()) - 2; i >= 0; -- i) {
			Vec2d pt = polyline[i].cast<double>();
			Vec2d v = pt - pt_next;
	        double l = v.norm();
	        if (l > distance) {
	        	out.idx_segment = i;
	        	out.t 			= distance / l;
	        	out.point 		= pt_next + out.t * v;
				out.t			= 1. - out.t;
	            break;
	        }
	        distance -= l;
	        pt_next = pt;
	    }
	}
	return out;
}

static inline bool line_rounded_thick_segment_collision(
    const Vec2d &line_a,    const Vec2d &line_b,
    const Vec2d &segment_a, const Vec2d &segment_b, const double offset,
    std::pair<double, double> &out_interval)
{
    const Vec2d  line_v0   = line_b - line_a;
    double       lv        = line_v0.squaredNorm();

    const Vec2d  segment_v = segment_b - segment_a;
    const double segment_l = segment_v.norm();
    const double offset2   = offset * offset;

    bool intersects = false;
    if (lv < SCALED_EPSILON * SCALED_EPSILON)
    {
        Vec2d lpt = 0.5 * (line_a + line_b);
        if (segment_l > SCALED_EPSILON) {
            intersects = line_alg::distance_to_squared(Linef{ segment_a, segment_b }, lpt) < offset2;
        } else
            intersects = (0.5 * (segment_a + segment_b) - lpt).squaredNorm() < offset2;
        if (intersects) {
            out_interval.first = 0.;
            out_interval.second = sqrt(lv);
        }
    }
    else
    {
        double tmin = std::numeric_limits<double>::max();
        double tmax = -tmin;
        auto extend_interval = [&tmin, &tmax](double atmin, double atmax) {
            tmin = std::min(tmin, atmin);
            tmax = std::max(tmax, atmax);
        };

        auto ray_circle_intersection_interval_extend = [&extend_interval](const Vec2d &segment_pt, const double offset2, const Vec2d &line_pt, const Vec2d &line_vec) {
            std::pair<Vec2d, Vec2d> pts;
            Vec2d  p0 = line_pt - segment_pt;
            double lv2 = line_vec.squaredNorm();
            if (Geometry::ray_circle_intersections_r2_lv2_c(offset2, line_vec.y(), - line_vec.x(), lv2, - line_vec.y() * p0.x() + line_vec.x() * p0.y(), pts)) {
                double tmin = (pts.first  - p0).dot(line_vec) / lv2;
                double tmax = (pts.second - p0).dot(line_vec) / lv2;
                if (tmin > tmax)
                    std::swap(tmin, tmax);
                tmin = std::max(tmin, 0.);
                tmax = std::min(tmax, 1.);
                if (tmin <= tmax)
                    extend_interval(tmin, tmax);
            }
        };

        if (segment_l > SCALED_EPSILON) {
            ray_circle_intersection_interval_extend(segment_a, offset2, line_a, line_v0);
            ray_circle_intersection_interval_extend(segment_b, offset2, line_a, line_v0);
            const Vec2d dir_x = segment_v / segment_l;
            const Vec2d dir_y(- dir_x.y(), dir_x.x());
            const Vec2d line_p0(line_a - segment_a);
            std::pair<double, double> interval;
            if (Geometry::liang_barsky_line_clipping_interval(
                    Vec2d(line_p0.dot(dir_x), line_p0.dot(dir_y)),
                    Vec2d(line_v0.dot(dir_x), line_v0.dot(dir_y)),
                    BoundingBoxf(Vec2d(0., - offset), Vec2d(segment_l, offset)),
                    interval))
                extend_interval(interval.first, interval.second);
        } else
            ray_circle_intersection_interval_extend(0.5 * (segment_a + segment_b), offset, line_a, line_v0);

        intersects = tmin <= tmax;
        if (intersects) {
            lv = sqrt(lv);
            out_interval.first  = tmin * lv;
            out_interval.second = tmax * lv;
        }
    }

#if 0
    {
        BoundingBox bbox;
        bbox.merge(line_a.cast<coord_t>());
        bbox.merge(line_a.cast<coord_t>());
        bbox.merge(segment_a.cast<coord_t>());
        bbox.merge(segment_b.cast<coord_t>());
        static int iRun = 0;
        ::Slic3r::SVG svg(debug_out_path("%s-%03d.svg", "line-thick-segment-intersect", iRun ++), bbox);
        svg.draw(Line(line_a.cast<coord_t>(), line_b.cast<coord_t>()), "black");
        svg.draw(Line(segment_a.cast<coord_t>(), segment_b.cast<coord_t>()), "blue", offset * 2.);
        svg.draw(segment_a.cast<coord_t>(), "blue", offset);
        svg.draw(segment_b.cast<coord_t>(), "blue", offset);
        svg.draw(Line(segment_a.cast<coord_t>(), segment_b.cast<coord_t>()), "black");
        if (intersects)
            svg.draw(Line((line_a + (line_b - line_a).normalized() * out_interval.first).cast<coord_t>(),
                          (line_a + (line_b - line_a).normalized() * out_interval.second).cast<coord_t>()), "red");
    }
#endif

    return intersects;
}

#ifndef NDEBUG
static inline bool inside_interval(double low, double high, double p)
{
    return p >= low && p <= high;
}

static inline bool interval_inside_interval(double outer_low, double outer_high, double inner_low, double inner_high, double epsilon)
{
    outer_low -= epsilon;
    outer_high += epsilon;
    return inside_interval(outer_low, outer_high, inner_low) && inside_interval(outer_low, outer_high, inner_high);
}

static inline bool cyclic_interval_inside_interval(double outer_low, double outer_high, double inner_low, double inner_high, double length)
{
    if (outer_low > outer_high)
        outer_high += length;
    if (inner_low > inner_high)
        inner_high += length;
    else if (inner_high < outer_low) {
        inner_low += length;
        inner_high += length;
    }
    return interval_inside_interval(outer_low, outer_high, inner_low, inner_high, double(SCALED_EPSILON));
}
#endif

#ifdef INFILL_DEBUG_OUTPUT
static void export_infill_to_svg(
    const std::vector<Points>                              &boundary,
    const std::vector<std::vector<double>>                 &boundary_parameters,
    std::vector<std::vector<ContourIntersectionPoint*>>    &boundary_intersections,
    const Polylines                                        &infill,
    const coord_t                                           scaled_spacing,
    const std::string                                      &path,
    const Polylines                                        &overlap_lines = Polylines(),
    const Polylines                                        &polylines = Polylines(),
    const Points                                           &pts = Points())
{
    Polygons    polygons;
    std::transform(boundary.begin(), boundary.end(), std::back_inserter(polygons), [](auto &pts) { return Polygon(pts); });
    ExPolygons  expolygons = union_ex(polygons);
    BoundingBox bbox = get_extents(polygons);
    bbox.offset(scale_(3.));

    ::Slic3r::SVG svg(path, bbox);
    svg.draw(expolygons);

    const std::string color_boundary_trimmed     = "blue";
    const std::string color_boundary_not_trimmed = "yellow";
    const coordf_t    boundary_line_width        = scaled_spacing;
    svg.draw_outline(polygons, "red", boundary_line_width);
    for (const std::vector<ContourIntersectionPoint*> &intersections : boundary_intersections) {
        const size_t                 boundary_idx  = &intersections - boundary_intersections.data();
        const Points                &contour       = boundary[boundary_idx];
        const std::vector<double>   &contour_param = boundary_parameters[boundary_idx];
        for (const ContourIntersectionPoint *ip : intersections) {
            assert(ip->next_trimmed == ip->next_on_contour->prev_trimmed);
            assert(ip->prev_trimmed == ip->prev_on_contour->next_trimmed);
            {
                Polyline pl { contour[ip->point_idx] };
                if (ip->next_trimmed) {
                    if (ip->contour_not_taken_length_next > SCALED_EPSILON) {
                        take_ccw_limited(pl, contour, contour_param, ip->point_idx, ip->next_on_contour->point_idx, ip->contour_not_taken_length_next);
                        svg.draw(pl, color_boundary_trimmed, boundary_line_width);
                    }
                } else {
                    take_ccw_full(pl, contour, ip->point_idx, ip->next_on_contour->point_idx);
                    svg.draw(pl, color_boundary_not_trimmed, boundary_line_width);
                }
            }
            {
                Polyline pl { contour[ip->point_idx] };
                if (ip->prev_trimmed) {
                    if (ip->contour_not_taken_length_prev > SCALED_EPSILON) {
                        take_cw_limited(pl, contour, contour_param, ip->point_idx, ip->prev_on_contour->point_idx, ip->contour_not_taken_length_prev);
                        svg.draw(pl, color_boundary_trimmed, boundary_line_width);
                    }
                } else {
                    take_cw_full(pl, contour, ip->point_idx, ip->prev_on_contour->point_idx);
                    svg.draw(pl, color_boundary_not_trimmed, boundary_line_width);
                }
            }
        }
    }

    svg.draw_outline(polygons, "green");

    svg.draw(infill, "brown");
    static constexpr double trim_length = scale_(0.15);
    for (Polyline polyline : infill)
        if (! polyline.empty()) {
            Vec2d a = polyline.points.front().cast<double>();
            Vec2d d = polyline.points.back().cast<double>();
            if (polyline.size() == 2) {
                Vec2d v = d - a;
                double l = v.norm();
                if (l > 2. * trim_length) {
                    a += v * trim_length / l;
                    d -= v * trim_length / l;
                    polyline.points.front() = a.cast<coord_t>();
                    polyline.points.back() = d.cast<coord_t>();
                } else
                    polyline.points.clear();
            } else if (polyline.size() > 2) {
                Vec2d b = polyline.points[1].cast<double>();
                Vec2d c = polyline.points[polyline.points.size() - 2].cast<double>();
                Vec2d v = b - a;
                double l = v.norm();
                if (l > trim_length) {
                    a += v * trim_length / l;
                    polyline.points.front() = a.cast<coord_t>();
                } else
                    polyline.points.erase(polyline.points.begin());
                v = d - c;
                l = v.norm();
                if (l > trim_length)
                    polyline.points.back() = (d - v * trim_length / l).cast<coord_t>();
                else
                    polyline.points.pop_back();
            }
            svg.draw(polyline, "black");
        }

    svg.draw(overlap_lines, "red", scale_(0.05));
    svg.draw(polylines, "magenta", scale_(0.05));
    svg.draw(pts, "magenta");
}
#endif

#ifndef NDEBUG
bool validate_boundary_intersections(const std::vector<std::vector<ContourIntersectionPoint*>> &boundary_intersections)
{
    for (const std::vector<ContourIntersectionPoint*>& contour : boundary_intersections) {
        for (ContourIntersectionPoint* ip : contour) {
            assert(ip->next_trimmed == ip->next_on_contour->prev_trimmed);
            assert(ip->prev_trimmed == ip->prev_on_contour->next_trimmed);
        }
    }
    return true;
}
#endif
void mark_boundary_segments_touching_infill(
	const std::vector<Points>                              &boundary,
	const std::vector<std::vector<double>>                 &boundary_parameters,
    std::vector<std::vector<ContourIntersectionPoint*>>    &boundary_intersections,
	const BoundingBox 		                               &boundary_bbox,
	const Polylines 		                               &infill,
	const double			                                clip_distance,
	const double 				                            distance_colliding)
{
    assert(boundary.size() == boundary_parameters.size());
#ifndef NDEBUG
    for (size_t i = 0; i < boundary.size(); ++ i)
        assert(boundary[i].size() + 1 == boundary_parameters[i].size());
    assert(validate_boundary_intersections(boundary_intersections));
#endif

#ifdef INFILL_DEBUG_OUTPUT
    static int iRun = 0;
    ++ iRun;
    int iStep = 0;
    export_infill_to_svg(boundary, boundary_parameters, boundary_intersections, infill, distance_colliding * 2, debug_out_path("%s-%03d.svg", "FillBase-mark_boundary_segments_touching_infill-start", iRun));
    Polylines perimeter_overlaps;
#endif

	EdgeGrid::Grid grid;
	grid.set_bbox(boundary_bbox.inflated(distance_colliding * 1.43));
	grid.create(boundary, coord_t(std::max(clip_distance, distance_colliding) + scale_(10.)));

	struct Visitor {
		Visitor(const EdgeGrid::Grid &grid,
                const std::vector<Points> &boundary, const std::vector<std::vector<double>> &boundary_parameters, std::vector<std::vector<ContourIntersectionPoint*>> &boundary_intersections,
                const double radius) :
			grid(grid), boundary(boundary), boundary_parameters(boundary_parameters), boundary_intersections(boundary_intersections), radius(radius), trim_l_threshold(0.5 * radius) {}

		void init(const Vec2d &infill_pt1, const Vec2d &infill_pt2) {
			this->infill_pt1 = &infill_pt1;
			this->infill_pt2 = &infill_pt2;
            this->infill_bbox.reset();
            this->infill_bbox.merge(infill_pt1);
            this->infill_bbox.merge(infill_pt2);
            this->infill_bbox.offset(this->radius + SCALED_EPSILON);
        }

		bool operator()(coord_t iy, coord_t ix) {
			auto cell_data_range = this->grid.cell_data_range(iy, ix);
			for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second; ++ it_contour_and_segment) {
				auto segment = this->grid.segment(*it_contour_and_segment);
                std::vector<ContourIntersectionPoint*> &intersections = boundary_intersections[it_contour_and_segment->first];
                if (intersections.empty())
                    continue;
				const Vec2d seg_pt1 = segment.first.cast<double>();
				const Vec2d seg_pt2 = segment.second.cast<double>();
                std::pair<double, double> interval;
                BoundingBoxf bbox_seg;
                bbox_seg.merge(seg_pt1);
                bbox_seg.merge(seg_pt2);
#ifdef INFILL_DEBUG_OUTPUT
#endif
                if (this->infill_bbox.overlap(bbox_seg) && line_rounded_thick_segment_collision(seg_pt1, seg_pt2, *this->infill_pt1, *this->infill_pt2, this->radius, interval)) {
                    const std::vector<double> &contour_parameters = boundary_parameters[it_contour_and_segment->first];
                    const double contour_length = contour_parameters.back();
					const double param_seg_pt1  = contour_parameters[it_contour_and_segment->second];
                    const double param_seg_pt2  = contour_parameters[it_contour_and_segment->second + 1];
#ifdef INFILL_DEBUG_OUTPUT
                    this->perimeter_overlaps.push_back({ Point((seg_pt1 + (seg_pt2 - seg_pt1).normalized() * interval.first).cast<coord_t>()),
                                                         Point((seg_pt1 + (seg_pt2 - seg_pt1).normalized() * interval.second).cast<coord_t>()) });
#endif
                    assert(interval.first >= 0.);
                    assert(interval.second >= 0.);
                    assert(interval.first <= interval.second);
                    const auto param_overlap1 = std::min(param_seg_pt2, param_seg_pt1 + interval.first);
                    const auto param_overlap2 = std::min(param_seg_pt2, param_seg_pt1 + interval.second);
                    ContourIntersectionPoint *ip_low, *ip_high;
                    if (intersections.size() == 1) {
                        ip_low = ip_high = intersections.front();
                    } else {
                        assert(intersections.size() > 1);
                        auto it_low  = Slic3r::lower_bound_by_predicate(intersections.begin(), intersections.end(), [param_overlap1](const ContourIntersectionPoint *l) { return l->param < param_overlap1; });
                        auto it_high = Slic3r::lower_bound_by_predicate(intersections.begin(), intersections.end(), [param_overlap2](const ContourIntersectionPoint *l) { return l->param < param_overlap2; });
                        ip_low  = it_low  == intersections.end() ? intersections.front() : *it_low;
                        ip_high = it_high == intersections.end() ? intersections.front() : *it_high;
                        if (ip_low->param != param_overlap1)
                            ip_low = ip_low->prev_on_contour;
                        assert(ip_low != ip_high);
                        assert(cyclic_interval_inside_interval(ip_low->param, ip_high->param, param_overlap1, param_overlap2, contour_length));
                    }
                    assert(validate_boundary_intersections(boundary_intersections));
                    if (ip_low->next_on_contour != ip_high)
                        for (ContourIntersectionPoint *ip = ip_low->next_on_contour; ip != ip_high; ip = ip->next_on_contour) {
                            ip->consume_prev();
                            ip->consume_next();
                        }
                    double trim_l = closed_contour_distance_ccw(ip_low->param, param_overlap1, contour_length);
                        ip_low->trim_next(trim_l);
                    trim_l = closed_contour_distance_ccw(param_overlap2, ip_high->param, contour_length);
                        ip_high->trim_prev(trim_l);
                    assert(ip_low->next_trimmed == ip_high->prev_trimmed);
                    assert(validate_boundary_intersections(boundary_intersections));
#ifdef INFILL_DEBUG_OUTPUT
					{
#if 0
                        static size_t iRun = 0;
						ExPolygon expoly(Polygon(*grid.contours().front()));
						for (size_t i = 1; i < grid.contours().size(); ++i)
							expoly.holes.emplace_back(Polygon(*grid.contours()[i]));
						SVG svg(debug_out_path("%s-%d.svg", "FillBase-mark_boundary_segments_touching_infill", iRun ++).c_str(), get_extents(expoly));
						svg.draw(expoly, "green");
						svg.draw(Line(segment.first, segment.second), "red");
						svg.draw(Line(this->infill_pt1->cast<coord_t>(), this->infill_pt2->cast<coord_t>()), "magenta");
#endif
                    }
#endif
				}
			}
			return true;
		}

		const EdgeGrid::Grid 			   			        &grid;
		const std::vector<Points> 					        &boundary;
        const std::vector<std::vector<double>>              &boundary_parameters;
        std::vector<std::vector<ContourIntersectionPoint*>> &boundary_intersections;
		const double								         radius;
        const double                                         trim_l_threshold;

		const Vec2d 								        *infill_pt1;
		const Vec2d 								        *infill_pt2;
        BoundingBoxf                                         infill_bbox;

#ifdef INFILL_DEBUG_OUTPUT
        Polylines                                            perimeter_overlaps;
#endif
	} visitor(grid, boundary, boundary_parameters, boundary_intersections, distance_colliding);

	for (const Polyline &polyline : infill) {
#ifdef INFILL_DEBUG_OUTPUT
        ++ iStep;
#endif
		SegmentPoint start_point = clip_start_segment_and_point(polyline.points, clip_distance);
		SegmentPoint end_point   = clip_end_segment_and_point(polyline.points, clip_distance);
		if (start_point.valid() && end_point.valid() &&
			(start_point.idx_segment < end_point.idx_segment || (start_point.idx_segment == end_point.idx_segment && start_point.t < end_point.t))) {
#ifdef INFILL_DEBUG_OUTPUT
            visitor.perimeter_overlaps.clear();
#endif
			for (size_t point_idx = start_point.idx_segment; point_idx <= end_point.idx_segment; ++ point_idx) {
#if 0
				Point pt1, pt2;
				Vec2d pt1d, pt2d;
				if (point_idx == start_point.idx_segment) {
					pt1d = start_point.point;
					pt1  = pt1d.cast<coord_t>();
				} else {
					pt1  = polyline.points[point_idx];
					pt1d = pt1.cast<double>();
				}
				if (point_idx == start_point.idx_segment) {
					pt2d = end_point.point;
					pt2  = pt1d.cast<coord_t>();
				} else {
					pt2  = polyline.points[point_idx];
					pt2d = pt2.cast<double>();
				}
				visitor.init(pt1d, pt2d);
				grid.visit_cells_intersecting_thick_line(pt1, pt2, distance_colliding, visitor);
#else
				Vec2d pt1 = (point_idx == start_point.idx_segment) ? start_point.point : polyline.points[point_idx    ].cast<double>();
				Vec2d pt2 = (point_idx == end_point  .idx_segment) ? end_point  .point : polyline.points[point_idx + 1].cast<double>();
#if 0
					{
						static size_t iRun = 0;
						ExPolygon expoly(Polygon(*grid.contours().front()));
						for (size_t i = 1; i < grid.contours().size(); ++i)
							expoly.holes.emplace_back(Polygon(*grid.contours()[i]));
						SVG svg(debug_out_path("%s-%d.svg", "FillBase-mark_boundary_segments_touching_infill0", iRun ++).c_str(), get_extents(expoly));
						svg.draw(expoly, "green");
						svg.draw(polyline, "blue");
						svg.draw(Line(pt1.cast<coord_t>(), pt2.cast<coord_t>()), "magenta", scale_(0.1));
					}
#endif
				visitor.init(pt1, pt2);
				Vec2d v = (pt2 - pt1).normalized() * distance_colliding;
				Vec2d vperp = perp(v);
				Vec2d a = pt1 - v - vperp;
				Vec2d b = pt2 + v - vperp;
                assert(grid.bbox().contains(a.cast<coord_t>()));
                assert(grid.bbox().contains(b.cast<coord_t>()));
				grid.visit_cells_intersecting_line(a.cast<coord_t>(), b.cast<coord_t>(), visitor);
				a = pt1 - v + vperp;
				b = pt2 + v + vperp;
                assert(grid.bbox().contains(a.cast<coord_t>()));
                assert(grid.bbox().contains(b.cast<coord_t>()));
                grid.visit_cells_intersecting_line(a.cast<coord_t>(), b.cast<coord_t>(), visitor);
#endif
#ifdef INFILL_DEBUG_OUTPUT
#endif
			}
#ifdef INFILL_DEBUG_OUTPUT
            Polylines perimeter_overlaps;
            export_infill_to_svg(boundary, boundary_parameters, boundary_intersections, infill, distance_colliding * 2, debug_out_path("%s-%03d-%03d.svg", "FillBase-mark_boundary_segments_touching_infill-step", iRun, iStep), visitor.perimeter_overlaps, { polyline });
            append(perimeter_overlaps, std::move(visitor.perimeter_overlaps));
            perimeter_overlaps.clear();
#endif
        }
	}

#ifdef INFILL_DEBUG_OUTPUT
    export_infill_to_svg(boundary, boundary_parameters, boundary_intersections, infill, distance_colliding * 2, debug_out_path("%s-%03d.svg", "FillBase-mark_boundary_segments_touching_infill-end", iRun), perimeter_overlaps);
#endif
    assert(validate_boundary_intersections(boundary_intersections));
}

void Fill::connect_infill(Polylines &&infill_ordered, const ExPolygon &boundary_src, Polylines &polylines_out, const double spacing, const FillParams &params)
{
	assert(! boundary_src.contour.points.empty());
    auto polygons_src = reserve_vector<const Polygon*>(boundary_src.holes.size() + 1);
    polygons_src.emplace_back(&boundary_src.contour);
    for (const Polygon &polygon : boundary_src.holes)
        polygons_src.emplace_back(&polygon);

    connect_infill(std::move(infill_ordered), polygons_src, get_extents(boundary_src.contour), polylines_out, spacing, params);
}

void Fill::connect_infill(Polylines &&infill_ordered, const Polygons &boundary_src, const BoundingBox &bbox, Polylines &polylines_out, const double spacing, const FillParams &params)
{
    auto polygons_src = reserve_vector<const Polygon*>(boundary_src.size());
    for (const Polygon &polygon : boundary_src)
        polygons_src.emplace_back(&polygon);

    connect_infill(std::move(infill_ordered), polygons_src, bbox, polylines_out, spacing, params);
}

static constexpr auto boundary_idx_unconnected = std::numeric_limits<size_t>::max();

struct BoundaryInfillGraph
{
    std::vector<Points>                     boundary;
    std::vector<std::vector<double>>        boundary_params;
    std::vector<ContourIntersectionPoint>   map_infill_end_point_to_boundary;

    const Point&    point(const ContourIntersectionPoint &cp) const {
        assert(cp.contour_idx != size_t(-1));
        assert(cp.point_idx != size_t(-1));
        return this->boundary[cp.contour_idx][cp.point_idx];
    }

    const Point&    infill_end_point(size_t infill_end_point_idx) const {
        return this->point(this->map_infill_end_point_to_boundary[infill_end_point_idx]);
    }

    const Point     interpolate_contour_point(const ContourIntersectionPoint &cp, double param) {
        const Points                &contour        = this->boundary[cp.contour_idx];
        const std::vector<double>   &contour_params = this->boundary_params[cp.contour_idx];
        auto it = std::lower_bound(contour_params.begin(), contour_params.end(), param);
        if (*it != param) {
            assert(it != contour_params.begin());
            -- it;
        }
        size_t i = it - contour_params.begin();
        if (i == contour.size())
            i = 0;
        double t1 = contour_params[i];
        double t2 = next_value_modulo(i, contour_params);
        return lerp(contour[i], next_value_modulo(i, contour), (param - t1) / (t2 - t1));
    }

    enum Direction {
        Left,
        Right,
        Up,
        Down,
        Taken,
    };

    static Direction dir(const Point &p1, const Point &p2) {
        return p1.x() == p2.x() ?
            (p1.y() < p2.y() ? Up : Down) :
            (p1.x() < p2.x() ? Right : Left);
    }

    const Direction dir_prev(const ContourIntersectionPoint &cp) const {
        assert(cp.prev_on_contour);
        return cp.could_take_prev() ?
            dir(this->point(cp), this->point(*cp.prev_on_contour)) :
            Taken;
    }

    const Direction dir_next(const ContourIntersectionPoint &cp) const {
        assert(cp.next_on_contour);
        return cp.could_take_next() ?
            dir(this->point(cp), this->point(*cp.next_on_contour)) :
            Taken;
    }

    bool            first(const ContourIntersectionPoint &cp) const {
        return ((&cp - this->map_infill_end_point_to_boundary.data()) & 1) == 0;
    }

    const ContourIntersectionPoint& other(const ContourIntersectionPoint &cp) const {
        return this->map_infill_end_point_to_boundary[((&cp - this->map_infill_end_point_to_boundary.data()) ^ 1)];
    }

    ContourIntersectionPoint& other(const ContourIntersectionPoint &cp) {
        return this->map_infill_end_point_to_boundary[((&cp - this->map_infill_end_point_to_boundary.data()) ^ 1)];
    }

    bool            prev_vertical(const ContourIntersectionPoint &cp) const {
        return this->point(cp).x() == this->point(*cp.prev_on_contour).x();
    }

    bool            next_vertical(const ContourIntersectionPoint &cp) const {
        return this->point(cp).x() == this->point(*cp.next_on_contour).x();
    }

};


static inline void mark_boundary_segments_overlapping_infill(
    BoundaryInfillGraph                                    &graph,
    const Polylines                                        &infill,
    const double                                            spacing)
{
    for (ContourIntersectionPoint &cp : graph.map_infill_end_point_to_boundary) {
        const Points                &contour         = graph.boundary[cp.contour_idx];
        const std::vector<double>   &contour_params  = graph.boundary_params[cp.contour_idx];
        const Polyline              &infill_polyline = infill[(&cp - graph.map_infill_end_point_to_boundary.data()) / 2];
        const double                 radius          = 0.5 * (spacing + SCALED_EPSILON);
        assert(infill_polyline.size() == 2);
        const Linef                  infill_line { infill_polyline.points.front().cast<double>(), infill_polyline.points.back().cast<double>() };
        if (cp.could_take_next()) {
            bool inside = true;
            for (size_t i = cp.point_idx; i != cp.next_on_contour->point_idx; ) {
                size_t j = next_idx_modulo(i, contour);
                const Vec2d seg_pt2 = contour[j].cast<double>();
                if (line_alg::distance_to_squared(infill_line, seg_pt2) < radius * radius) {
                } else {
                    std::pair<double, double> interval;
                    line_rounded_thick_segment_collision(contour[i].cast<double>(), seg_pt2, infill_line.a, infill_line.b, radius, interval);
                    assert(interval.first == 0.);
                    double len_out = closed_contour_distance_ccw(contour_params[cp.point_idx], contour_params[i], contour_params.back()) + interval.second;
                    if (len_out < cp.contour_not_taken_length_next) {
                        inside = false;
                        break;
                    }
                }
                if (closed_contour_distance_ccw(contour_params[cp.point_idx], contour_params[j], contour_params.back()) >= cp.contour_not_taken_length_next)
                    break;
                i = j;
            }
            if (inside) {
                if (! cp.next_trimmed)
                    cp.next_on_contour->trim_prev(0);
                cp.trim_next(0);
            }
        } else
            cp.trim_next(0);
        if (cp.could_take_prev()) {
            bool inside = true;
            for (size_t i = cp.point_idx; i != cp.prev_on_contour->point_idx; ) {
                size_t j = prev_idx_modulo(i, contour);
                const Vec2d seg_pt2 = contour[j].cast<double>();
                if (line_alg::distance_to_squared(infill_line, seg_pt2) < radius * radius) {
                } else {
                    std::pair<double, double> interval;
                    line_rounded_thick_segment_collision(contour[i].cast<double>(), seg_pt2, infill_line.a, infill_line.b, radius, interval);
                    assert(interval.first == 0.);
                    double len_out = closed_contour_distance_cw(contour_params[cp.point_idx], contour_params[i], contour_params.back()) + interval.second;
                    if (len_out < cp.contour_not_taken_length_prev) {
                        inside = false;
                        break;
                    }
                }
                if (closed_contour_distance_cw(contour_params[cp.point_idx], contour_params[j], contour_params.back()) >= cp.contour_not_taken_length_prev)
                    break;
                i = j;
            }
            if (inside) {
                if (! cp.prev_trimmed)
                    cp.prev_on_contour->trim_next(0);
                cp.trim_prev(0);
            }
        } else
            cp.trim_prev(0);
    }
}

BoundaryInfillGraph create_boundary_infill_graph(const Polylines &infill_ordered, const std::vector<const Polygon*> &boundary_src, const BoundingBox &bbox, const double spacing)
{
    BoundaryInfillGraph out;
    out.boundary.assign(boundary_src.size(), Points());
    out.boundary_params.assign(boundary_src.size(), std::vector<double>());
    out.map_infill_end_point_to_boundary.assign(infill_ordered.size() * 2, ContourIntersectionPoint{ boundary_idx_unconnected, boundary_idx_unconnected });
    {
        std::vector<std::pair<EdgeGrid::Grid::ClosestPointResult, size_t>> intersection_points;
        {
            EdgeGrid::Grid grid;
            grid.set_bbox(bbox.inflated(SCALED_EPSILON));
            grid.create(boundary_src, coord_t(scale_(10.)));
            intersection_points.reserve(infill_ordered.size() * 2);
            for (const Polyline &pl : infill_ordered)
                for (const Point *pt : { &pl.points.front(), &pl.points.back() }) {
                    EdgeGrid::Grid::ClosestPointResult cp = grid.closest_point_signed_distance(*pt, coord_t(SCALED_EPSILON));
                    if (cp.valid()) {
                        assert(cp.distance <= 3.);
                        intersection_points.emplace_back(cp, (&pl - infill_ordered.data()) * 2 + (pt == &pl.points.front() ? 0 : 1));
                    }
                }
            std::sort(intersection_points.begin(), intersection_points.end(), [](const std::pair<EdgeGrid::Grid::ClosestPointResult, size_t> &cp1, const std::pair<EdgeGrid::Grid::ClosestPointResult, size_t> &cp2) {
                return   cp1.first.contour_idx < cp2.first.contour_idx ||
                        (cp1.first.contour_idx == cp2.first.contour_idx &&
                            (cp1.first.start_point_idx < cp2.first.start_point_idx ||
                                (cp1.first.start_point_idx == cp2.first.start_point_idx && cp1.first.t < cp2.first.t)));
            });
        }
        auto it = intersection_points.begin();
        auto it_end = intersection_points.end();
        std::vector<std::vector<ContourIntersectionPoint*>> boundary_intersection_points(out.boundary.size(), std::vector<ContourIntersectionPoint*>());
        for (size_t idx_contour = 0; idx_contour < boundary_src.size(); ++ idx_contour) {
            const Polygon &contour_src = *boundary_src[idx_contour];
            Points        &contour_dst = out.boundary[idx_contour];
            std::vector<ContourIntersectionPoint*> &contour_intersection_points = boundary_intersection_points[idx_contour];
            ContourIntersectionPoint *pfirst = nullptr;
            ContourIntersectionPoint *pprev  = nullptr;
            {
                size_t n_intersection_points = 0;
                for (auto itx = it; itx != it_end && itx->first.contour_idx == idx_contour; ++ itx)
                    ++ n_intersection_points;
                contour_intersection_points.reserve(n_intersection_points);
            }
            for (size_t idx_point = 0; idx_point < contour_src.points.size(); ++ idx_point) {
                const Point &ipt = contour_src.points[idx_point];
                if (contour_dst.empty() || contour_dst.back() != ipt)
                    contour_dst.emplace_back(ipt);
                for (; it != it_end && it->first.contour_idx == idx_contour && it->first.start_point_idx == idx_point; ++ it) {
                    const Polyline  &infill_line = infill_ordered[it->second / 2];
                    const Point     &pt          = (it->second & 1) ? infill_line.points.back() : infill_line.points.front();
                    size_t idx_tjoint_pt = 0;
                    if (idx_point + 1 < contour_src.size() || pt != contour_dst.front()) {
                        if (pt != contour_dst.back())
                            contour_dst.emplace_back(pt);
                        idx_tjoint_pt = contour_dst.size() - 1;
                    }
                    out.map_infill_end_point_to_boundary[it->second] = ContourIntersectionPoint{  idx_contour, idx_tjoint_pt };
                    ContourIntersectionPoint *pthis = &out.map_infill_end_point_to_boundary[it->second];
                    if (pprev) {
                        pprev->next_on_contour = pthis;
                        pthis->prev_on_contour = pprev;
                    } else
                        pfirst = pthis;
                    contour_intersection_points.emplace_back(pthis);
                    pprev = pthis;
                }
                if (pfirst) {
                    pprev->next_on_contour = pfirst;
                    pfirst->prev_on_contour = pprev;
                }
            }
            std::vector<double> &contour_params = out.boundary_params[idx_contour];
            contour_params.assign(contour_dst.size() + 1, 0.);
            for (size_t i = 1; i < contour_dst.size(); ++i) {
                contour_params[i] = contour_params[i - 1] + (contour_dst[i].cast<double>() - contour_dst[i - 1].cast<double>()).norm();
                assert(contour_params[i] > contour_params[i - 1]);
            }
            contour_params.back() = contour_params[contour_params.size() - 2] + (contour_dst.back().cast<double>() - contour_dst.front().cast<double>()).norm();
            assert(contour_params.back() > contour_params[contour_params.size() - 2]);
            for (ContourIntersectionPoint *ip : contour_intersection_points)
                ip->param = contour_params[ip->point_idx];
            const double contour_length = contour_params.back();
            for (ContourIntersectionPoint *ip : contour_intersection_points)
                if (ip->next_on_contour == ip) {
                    assert(ip->prev_on_contour == ip);
                    ip->contour_not_taken_length_prev = ip->contour_not_taken_length_next = contour_length;
                } else {
                    assert(ip->prev_on_contour != ip);
                    ip->contour_not_taken_length_prev = closed_contour_distance_ccw(ip->prev_on_contour->param, ip->param, contour_length);
                    ip->contour_not_taken_length_next = closed_contour_distance_ccw(ip->param, ip->next_on_contour->param, contour_length);
                }
        }

        assert(out.boundary.size() == boundary_src.size());
#if 0
        assert(std::all_of(out.map_infill_end_point_to_boundary.begin(), out.map_infill_end_point_to_boundary.end(),
            [&out.boundary](const ContourIntersectionPoint &contour_point) {
                return contour_point.contour_idx < out.boundary.size() && contour_point.point_idx < out.boundary[contour_point.contour_idx].size();
            }));
#endif

        {
            const double clip_distance      = 1.7 * scale_(spacing);
            const double distance_colliding = 0.8 * scale_(spacing);
            mark_boundary_segments_touching_infill(out.boundary, out.boundary_params, boundary_intersection_points, bbox, infill_ordered, clip_distance, distance_colliding);
        }
    }

    return out;
}

BoundingBox Fill::extended_object_bounding_box() const
{
    const Point c   = this->bounding_box.center();
    BoundingBox out = this->bounding_box;
    out.translate(-c.x(), -c.y());
    out.merge(Point(out.min.y(), out.min.x()));
    out.merge(Point(out.max.y(), out.max.x()));

    out = out.scaled(sqrt(2.));
    out.translate(c.x(), c.y());
    return out;
}

void Fill::connect_infill(Polylines &&infill_ordered, const std::vector<const Polygon*> &boundary_src, const BoundingBox &bbox, Polylines &polylines_out, const double spacing, const FillParams &params)
{
	assert(! infill_ordered.empty());
    assert(params.anchor_length     >= 0.);
    assert(params.anchor_length_max >= 0.01f);
    assert(params.anchor_length_max >= params.anchor_length);
    const double anchor_length     = scale_(params.anchor_length);
    const double anchor_length_max = scale_(params.anchor_length_max);

#if 0
    append(polylines_out, infill_ordered);
    return;
#endif

    BoundaryInfillGraph graph = create_boundary_infill_graph(infill_ordered, boundary_src, bbox, spacing);

    std::vector<size_t> merged_with(infill_ordered.size());
    std::iota(merged_with.begin(), merged_with.end(), 0);

    auto get_and_update_merged_with = [&merged_with](size_t polyline_idx) -> size_t {
        for (size_t last = polyline_idx;;) {
            size_t lower = merged_with[last];
            assert(lower <= last);
            if (lower == last) {
                merged_with[polyline_idx] = last;
                return last;
            }
            last = lower;
        }
        assert(false);
        return std::numeric_limits<size_t>::max();
    };

    const double line_half_width = 0.5 * scale_(spacing);

#if 0
    const auto length_max = double(scale_((1000. / params.density) * spacing));
    struct ConnectionCost {
        ConnectionCost(size_t idx_first, double cost, bool reversed) : idx_first(idx_first), cost(cost), reversed(reversed) {}
        size_t  idx_first;
        double  cost;
        bool    reversed;
    };
    std::vector<ConnectionCost> connections_sorted;
    connections_sorted.reserve(infill_ordered.size() * 2 - 2);
    for (size_t idx_chain = 1; idx_chain < infill_ordered.size(); ++ idx_chain) {
        const ContourIntersectionPoint      *cp1            = &graph.map_infill_end_point_to_boundary[(idx_chain - 1) * 2 + 1];
        const ContourIntersectionPoint      *cp2            = &graph.map_infill_end_point_to_boundary[idx_chain * 2];
        if (cp1->contour_idx != boundary_idx_unconnected && cp1->contour_idx == cp2->contour_idx) {
            std::pair<double, double> len = path_lengths_along_contour(cp1, cp2, graph.boundary_params[cp1->contour_idx].back());
            if (len.first < length_max)
                connections_sorted.emplace_back(idx_chain - 1, len.first, false);
            if (len.second < length_max)
                connections_sorted.emplace_back(idx_chain - 1, len.second, true);
        }
    }
    std::sort(connections_sorted.begin(), connections_sorted.end(), [](const ConnectionCost& l, const ConnectionCost& r) { return l.cost < r.cost; });

    for (ConnectionCost &connection_cost : connections_sorted) {
		ContourIntersectionPoint *cp1    = &graph.map_infill_end_point_to_boundary[connection_cost.idx_first * 2 + 1];
		ContourIntersectionPoint *cp2    = &graph.map_infill_end_point_to_boundary[(connection_cost.idx_first + 1) * 2];
        assert(cp1 != cp2);
        assert(cp1->contour_idx == cp2->contour_idx && cp1->contour_idx != boundary_idx_unconnected);
        if (cp1->consumed || cp2->consumed)
            continue;
        const double              length = connection_cost.cost;
        bool                      could_connect;
        {
            ContourIntersectionPoint *cp_low  = connection_cost.reversed ? cp2 : cp1;
            ContourIntersectionPoint *cp_high = connection_cost.reversed ? cp1 : cp2;
            assert(std::abs(length - closed_contour_distance_ccw(cp_low->param, cp_high->param, graph.boundary_params[cp1->contour_idx].back())) < SCALED_EPSILON);
            could_connect = ! cp_low->next_trimmed && ! cp_high->prev_trimmed;
            if (could_connect && cp_low->next_on_contour != cp_high) {
                const ContourIntersectionPoint *cp1prev = cp1 - 1;
                const ContourIntersectionPoint *cp2next = cp2 + 1;
                for (auto *cp = cp_low->next_on_contour; cp != cp_high; cp = cp->next_on_contour)
                    if (cp->consumed || cp == cp1prev || cp == cp2next || cp->prev_trimmed || cp->next_trimmed) {
                        could_connect = false;
                        break;
                    }
            }
        }
        size_t idx_first  = connection_cost.idx_first;
        size_t idx_second = idx_first + 1;
        idx_first = get_and_update_merged_with(idx_first);
        assert(idx_first < idx_second);
        assert(idx_second == merged_with[idx_second]);
        if (could_connect && length < anchor_length_max) {
            take(infill_ordered[idx_first], infill_ordered[idx_second], graph.boundary[cp1->contour_idx], cp1, cp2, connection_cost.reversed);
            merged_with[idx_second] = merged_with[idx_first];
            infill_ordered[idx_second].points.clear();
        } else {
            take_limited(infill_ordered[idx_first],  graph.boundary[cp1->contour_idx], graph.boundary_params[cp1->contour_idx], cp1, cp2, connection_cost.reversed, anchor_length, line_half_width);
            take_limited(infill_ordered[idx_second], graph.boundary[cp1->contour_idx], graph.boundary_params[cp1->contour_idx], cp2, cp1, ! connection_cost.reversed, anchor_length, line_half_width);
        }
	}
#endif

    struct Arc {
        ContourIntersectionPoint    *intersection;
        double                       arc_length;
    };
    std::vector<Arc> arches;
    if (!params.dont_sort) {
        arches.reserve(graph.map_infill_end_point_to_boundary.size());
        for (ContourIntersectionPoint& cp : graph.map_infill_end_point_to_boundary)
            if (cp.contour_idx != boundary_idx_unconnected && cp.next_on_contour != &cp && cp.could_connect_next())
                arches.push_back({ &cp, path_length_along_contour_ccw(&cp, cp.next_on_contour, graph.boundary_params[cp.contour_idx].back()) });
        std::sort(arches.begin(), arches.end(), [](const auto& l, const auto& r) { return l.arc_length < r.arc_length; });
    }

    for (Arc &arc : arches)
        if (! arc.intersection->consumed && ! arc.intersection->next_on_contour->consumed) {
            ContourIntersectionPoint *cp1            = arc.intersection;
            ContourIntersectionPoint *cp2            = arc.intersection->next_on_contour;
            size_t                    polyline_idx1  = get_and_update_merged_with(((cp1 - graph.map_infill_end_point_to_boundary.data()) / 2));
            size_t                    polyline_idx2  = get_and_update_merged_with(((cp2 - graph.map_infill_end_point_to_boundary.data()) / 2));
            const Points             &contour        = graph.boundary[cp1->contour_idx];

            if (params.multiline > 1 && arc.arc_length < scale_(spacing) * params.multiline) {
                continue;
            }

            const std::vector<double> &contour_params = graph.boundary_params[cp1->contour_idx];
            if (polyline_idx1 != polyline_idx2) {
                Polyline &polyline1 = infill_ordered[polyline_idx1];
                Polyline &polyline2 = infill_ordered[polyline_idx2];
                if (arc.arc_length < anchor_length_max) {
                    assert(contour[cp1->point_idx] == polyline1.points.front() || contour[cp1->point_idx] == polyline1.points.back());
                    if (contour[cp1->point_idx] == polyline1.points.front())
                        polyline1.reverse();
                    assert(contour[cp2->point_idx] == polyline2.points.front() || contour[cp2->point_idx] == polyline2.points.back());
                    if (contour[cp2->point_idx] == polyline2.points.back())
                        polyline2.reverse();
                    take(polyline1, polyline2, contour, cp1, cp2, false);
                    if (polyline_idx2 < polyline_idx1) {
                        polyline2 = std::move(polyline1);
                        polyline1.points.clear();
                        merged_with[polyline_idx1] = merged_with[polyline_idx2];
                    } else {
                        polyline2.points.clear();
                        merged_with[polyline_idx2] = merged_with[polyline_idx1];
                    }
                } else if (anchor_length > SCALED_EPSILON) {
                    take_limited(polyline1, contour, contour_params, cp1, cp2, false, anchor_length, line_half_width);
                    take_limited(polyline2, contour, contour_params, cp2, cp1, true,  anchor_length, line_half_width);
                }
            }
        }

    for (ContourIntersectionPoint &contour_point : graph.map_infill_end_point_to_boundary)
        if (! contour_point.consumed && contour_point.contour_idx != boundary_idx_unconnected) {
            const Points              &contour        = graph.boundary[contour_point.contour_idx];
            const std::vector<double> &contour_params = graph.boundary_params[contour_point.contour_idx];

            double    lprev         = contour_point.could_connect_prev() ?
                path_length_along_contour_ccw(contour_point.prev_on_contour, &contour_point, contour_params.back()) :
                std::numeric_limits<double>::max();
            double    lnext         = contour_point.could_connect_next() ?
                path_length_along_contour_ccw(&contour_point, contour_point.next_on_contour, contour_params.back()) :
                std::numeric_limits<double>::max();
            size_t    polyline_idx  = get_and_update_merged_with(((&contour_point - graph.map_infill_end_point_to_boundary.data()) / 2));
            Polyline &polyline      = infill_ordered[polyline_idx];
            assert(! polyline.empty());
            assert(contour[contour_point.point_idx] == polyline.points.front() || contour[contour_point.point_idx] == polyline.points.back());
            bool connected = false;
            for (double l : { std::min(lprev, lnext), std::max(lprev, lnext) }) {
                if (l == std::numeric_limits<double>::max() || l > anchor_length_max)
                    break;
                bool      reversed      = l == lprev;
                ContourIntersectionPoint *cp2 = reversed ? contour_point.prev_on_contour : contour_point.next_on_contour;
                size_t    polyline_idx2 = get_and_update_merged_with(((cp2 - graph.map_infill_end_point_to_boundary.data()) / 2));
                if (polyline_idx == polyline_idx2)
                    continue;
                if (contour[contour_point.point_idx] == polyline.points.front())
                    polyline.reverse();
                Polyline &polyline2 = infill_ordered[polyline_idx2];
                assert(! polyline.empty());
                assert(contour[cp2->point_idx] == polyline2.points.front() || contour[cp2->point_idx] == polyline2.points.back());
                if (contour[cp2->point_idx] == polyline2.points.back())
                    polyline2.reverse();
                take(polyline, polyline2, contour, &contour_point, cp2, reversed);
                if (polyline_idx < polyline_idx2) {
                    merged_with[polyline_idx2] = polyline_idx;
                    polyline2.points.clear();
                } else {
                    merged_with[polyline_idx] = polyline_idx2;
                    polyline2 = std::move(polyline);
                    polyline.points.clear();
                }
                connected = true;
                break;
            }
            if (! connected && anchor_length > SCALED_EPSILON) {
                double l = std::max(contour_point.contour_not_taken_length_prev, contour_point.contour_not_taken_length_next);
                if (l > SCALED_EPSILON) {
                    if (contour_point.contour_not_taken_length_prev > contour_point.contour_not_taken_length_next)
                        take_limited(polyline, contour, contour_params, &contour_point, contour_point.prev_on_contour, true, anchor_length, line_half_width);
                    else
                        take_limited(polyline, contour, contour_params, &contour_point, contour_point.next_on_contour, false, anchor_length, line_half_width);
                }
            }
        }

    polylines_out.reserve(polylines_out.size() + std::count_if(infill_ordered.begin(), infill_ordered.end(), [](const Polyline &pl) { return ! pl.empty(); }));
	for (Polyline &pl : infill_ordered)
		if (! pl.empty())
			polylines_out.emplace_back(std::move(pl));
}

void Fill::chain_or_connect_infill(Polylines &&infill_ordered, const ExPolygon &boundary, Polylines &polylines_out, const double spacing, const FillParams &params)
{
    if (!infill_ordered.empty()) {
        if (params.dont_connect()) {
            if (infill_ordered.size() > 1)
                infill_ordered = chain_polylines(std::move(infill_ordered));
            append(polylines_out, std::move(infill_ordered));
        } else
            connect_infill(std::move(infill_ordered), boundary, polylines_out, spacing, params);
    }
}

static inline void base_support_extend_infill_lines(Polylines &infill, BoundaryInfillGraph &graph, const double spacing, const FillParams &params)
{

    const double    line_spacing    = scale_(spacing) / params.density;
    const auto      dist_max_x      = coord_t(line_spacing * 0.33);
    const auto      dist_min_y      = coord_t(line_spacing * 0.5);

    for (ContourIntersectionPoint &cp : graph.map_infill_end_point_to_boundary) {
        const Points                &contour         = graph.boundary[cp.contour_idx];
        const std::vector<double>   &contour_param   = graph.boundary_params[cp.contour_idx];
        const Point                 &pt              = contour[cp.point_idx];
        const bool                   first           = graph.first(cp);
        int                          extend_next_idx = -1;
        int                          extend_prev_idx = -1;
        coord_t                      dist_y_prev     = 0;
        coord_t                      dist_y_next     = 0;
        double                       arc_len_prev    = 0;
        double                       arc_len_next    = 0;

        if (! graph.next_vertical(cp)) {
            size_t i = cp.point_idx;
            size_t j = next_idx_modulo(i, contour);
            while (j != cp.next_on_contour->point_idx) {
                const Point &p2 = contour[j];
                if (std::abs(p2.x() - pt.x()) > dist_max_x)
                    break;
                i = j;
                j = next_idx_modulo(j, contour);
            }
            if (i != cp.point_idx) {
                const Point &p2 = contour[i];
                coord_t      dist_y = p2.y() - pt.y();
                if (first)
                    dist_y = - dist_y;
                if (dist_y > dist_min_y) {
                    arc_len_next    = closed_contour_distance_ccw(contour_param[cp.point_idx], contour_param[i], contour_param.back());
                    if (arc_len_next < cp.contour_not_taken_length_next) {
                        extend_next_idx = i;
                        dist_y_next     = dist_y;
                    }
                }
            }
        }

        if (! graph.prev_vertical(cp)) {
            size_t i = cp.point_idx;
            size_t j = prev_idx_modulo(i, contour);
            while (j != cp.prev_on_contour->point_idx) {
                const Point &p2 = contour[j];
                if (std::abs(p2.x() - pt.x()) > dist_max_x)
                    break;
                i = j;
                j = prev_idx_modulo(j, contour);
            }
            if (i != cp.point_idx) {
                const Point &p2 = contour[i];
                coord_t      dist_y = p2.y() - pt.y();
                if (first)
                    dist_y = - dist_y;
                if (dist_y > dist_min_y) {
                    arc_len_prev = closed_contour_distance_ccw(contour_param[i], contour_param[cp.point_idx], contour_param.back());
                    if (arc_len_prev < cp.contour_not_taken_length_prev) {
                        extend_prev_idx = i;
                        dist_y_prev     = dist_y;
                    }
                }
            }
        }

        if (extend_prev_idx >= 0 && extend_next_idx >= 0)
            dist_y_prev < dist_y_next ? extend_prev_idx : extend_next_idx = -1;

        assert(cp.prev_trimmed == cp.prev_on_contour->next_trimmed);
        assert(cp.next_trimmed == cp.next_on_contour->prev_trimmed);
        Polyline &infill_line = infill[(&cp - graph.map_infill_end_point_to_boundary.data()) / 2];
        if (extend_prev_idx >= 0) {
            if (first)
                infill_line.reverse();
            take_cw_full(infill_line, contour, cp.point_idx, extend_prev_idx);
            if (first)
                infill_line.reverse();
            cp.point_idx = extend_prev_idx;
            if (cp.prev_trimmed)
                cp.contour_not_taken_length_prev -= arc_len_prev;
            else
                cp.contour_not_taken_length_prev = cp.prev_on_contour->contour_not_taken_length_next =
                    closed_contour_distance_ccw(contour_param[cp.prev_on_contour->point_idx], contour_param[cp.point_idx], contour_param.back());
            cp.trim_next(0);
            cp.next_on_contour->prev_trimmed = true;
        } else if (extend_next_idx >= 0) {
            if (first)
                infill_line.reverse();
            take_ccw_full(infill_line, contour, cp.point_idx, extend_next_idx);
            if (first)
                infill_line.reverse();
            cp.point_idx = extend_next_idx;
            cp.trim_prev(0);
            cp.prev_on_contour->next_trimmed = true;
            if (cp.next_trimmed)
                cp.contour_not_taken_length_next -= arc_len_next;
            else
                cp.contour_not_taken_length_next = cp.next_on_contour->contour_not_taken_length_prev =
                    closed_contour_distance_ccw(contour_param[cp.point_idx], contour_param[cp.next_on_contour->point_idx], contour_param.back());
        }
    }
}

static inline void emit_loops_in_band(
    coord_t                      left,
    coord_t                      right,
    const Points                &contour,
    const std::vector<double>   &contour_params,
    double                       tbegin,
    double                       tend,
    double                       min_length,
    Polylines                   &polylines_out)
{
    assert(left < right);
    assert(contour.size() + 1 == contour_params.size());
    assert(contour.size() >= 3);
#ifndef NDEBUG
    double contour_length = contour_params.back();
    assert(tbegin >= 0 && tbegin < contour_length);
    assert(tend   >= 0 && tend   < contour_length);
    assert(min_length > 0);
#endif
    size_t ibegin, iend;
    {
        auto it_begin = std::lower_bound(contour_params.begin(), contour_params.end(), tbegin);
        auto it_end   = std::lower_bound(contour_params.begin(), contour_params.end(), tend);
        assert(it_begin != contour_params.end());
        assert(it_end   != contour_params.end());
        if (*it_begin != tbegin) {
            assert(it_begin != contour_params.begin());
            -- it_begin;
        }
        ibegin = it_begin - contour_params.begin();
        iend   = it_end   - contour_params.begin();
    }

    if (ibegin == contour.size())
        ibegin = 0;
    if (iend == contour.size())
        iend = 0;
    assert(ibegin != iend);

    Point pbegin, pend;
    {
        double t1 = contour_params[ibegin];
        double t2 = next_value_modulo(ibegin, contour_params);
        pbegin = lerp(contour[ibegin], next_value_modulo(ibegin, contour), (tbegin - t1) / (t2 - t1));
        t1 = contour_params[iend];
        t2 = prev_value_modulo(iend, contour_params);
        pend = lerp(contour[iend], prev_value_modulo(iend, contour), (tend - t1) / (t2 - t1));
    }

    enum Side {
        Left,
        Right,
        Mid,
        Unknown
    };

    enum InOutBand {
        Entering,
        Leaving,
    };

    class State {
    public:
        State(coord_t left, coord_t right, double min_length, Polylines &polylines_out) :
            m_left(left), m_right(right), m_min_length(min_length), m_polylines_out(polylines_out) {}

        void add_inner_point(const Point* p)
        {
            m_polyline.points.emplace_back(*p);
        }

        void add_outer_point(const Point* p)
        {
            if (m_polyline_end > 0)
                m_polyline.points.emplace_back(*p);
        }

        void add_interpolated_point(const Point* p1, const Point* p2, Side side, InOutBand inout)
        {
            assert(side == Left || side == Right);

            coord_t x = side == Left ? m_left : m_right;
            coord_t y = p1->y() + coord_t(double(x - p1->x()) * double(p2->y() - p1->y()) / double(p2->x() - p1->x()));

            if (inout == Leaving) {
                assert(m_polyline_end == 0);
                m_polyline_end = m_polyline.size();
                m_polyline.points.emplace_back(x, y);
            } else {
                assert(inout == Entering);
                if (m_polyline_end > 0) {
                    if ((this->side1 == Left) == (y - m_polyline.points[m_polyline_end].y() < 0)) {
                        m_polyline.points.erase(m_polyline.points.begin() + m_polyline_end);
                    } else {
                        this->finalize();
                        m_polyline.points.emplace_back(x, y);
                    }
                    m_polyline_end = 0;
                } else
                    m_polyline.points.emplace_back(x, y);
            }
        };

        void finalize()
        {
            m_polyline.points.erase(m_polyline.points.begin() + m_polyline_end, m_polyline.points.end());
            if (! m_polyline.empty()) {
                if (! m_polylines_out.empty() && (m_polylines_out.back().points.back() - m_polyline.points.front()).cast<int64_t>().squaredNorm() < SCALED_EPSILON)
                    m_polylines_out.back().points.insert(m_polylines_out.back().points.end(), m_polyline.points.begin() + 1, m_polyline.points.end());
                else if (m_polyline.length() > m_min_length)
                    m_polylines_out.emplace_back(std::move(m_polyline));
                m_polyline.clear();
            }
        };

    private:
        coord_t      m_left;
        coord_t      m_right;
        double       m_min_length;
        Polylines   &m_polylines_out;

        Polyline     m_polyline;
        size_t       m_polyline_end { 0 };
        Polyline     m_overlapping;

    public:
        Side         side1 { Unknown };
        Side         side2 { Unknown };
    };

    State state { left, right, min_length, polylines_out };

    const Point *p1 = &pbegin;
    auto side = [left, right](const Point* p) {
        coord_t x = p->x();
        return x < left ? Left : x > right ? Right : Mid;
    };
    state.side1 = side(p1);
    if (state.side1 == Mid)
        state.add_inner_point(p1);

    for (size_t i = ibegin; i != iend; ) {
        size_t inext = i + 1;
        if (inext == contour.size())
            inext = 0;
        const Point *p2 = inext == iend ? &pend : &contour[inext];
        state.side2 = side(p2);
        if (state.side1 == Mid) {
            if (state.side2 == Mid) {
                state.add_inner_point(p2);
            } else {
                state.add_interpolated_point(p1, p2, state.side2, Leaving);
                state.add_outer_point(p2);
            }
        } else if (state.side2 == Mid) {
            state.add_interpolated_point(p1, p2, state.side1, Entering);
            state.add_inner_point(p2);
        } else if (state.side1 != state.side2) {
            state.add_interpolated_point(p1, p2, state.side1, Entering);
            state.add_interpolated_point(p1, p2, state.side2, Leaving);
        } else {
            assert((state.side1 == Left && state.side2 == Left) || (state.side1 == Right && state.side2 == Right));
            state.add_outer_point(p2);
        }
        state.side1 = state.side2;
        p1 = p2;
        i  = inext;
    }
    state.finalize();
}

#ifdef INFILL_DEBUG_OUTPUT
static void export_partial_infill_to_svg(const std::string &path, const BoundaryInfillGraph &graph, const Polylines &infill, const Polylines &emitted)
{
    Polygons polygons;
    for (const Points &pts : graph.boundary)
        polygons.emplace_back(pts);
    BoundingBox bbox = get_extents(polygons);
    bbox.merge(get_extents(infill));
    ::Slic3r::SVG svg(path, bbox);
    svg.draw(union_ex(polygons));
    svg.draw(infill, "blue");
    svg.draw(emitted, "darkblue");
    for (const ContourIntersectionPoint &cp : graph.map_infill_end_point_to_boundary)
        svg.draw(graph.point(cp), cp.consumed ? "red" : "green", scaled(0.2));
    for (const ContourIntersectionPoint &cp : graph.map_infill_end_point_to_boundary) {
        assert(cp.next_trimmed == cp.next_on_contour->prev_trimmed);
        assert(cp.prev_trimmed == cp.prev_on_contour->next_trimmed);
        if (cp.contour_not_taken_length_next > SCALED_EPSILON) {
            Polyline pl { graph.point(cp) };
            take_ccw_limited(pl, graph.boundary[cp.contour_idx], graph.boundary_params[cp.contour_idx], cp.point_idx, cp.next_on_contour->point_idx, cp.contour_not_taken_length_next);
            svg.draw(pl, cp.could_take_next() ? "lime" : "magenta", scaled(0.1));
        }
        if (cp.contour_not_taken_length_prev > SCALED_EPSILON) {
            Polyline pl { graph.point(cp) };
            take_cw_limited(pl, graph.boundary[cp.contour_idx], graph.boundary_params[cp.contour_idx], cp.point_idx, cp.prev_on_contour->point_idx, cp.contour_not_taken_length_prev);
            svg.draw(pl, cp.could_take_prev() ? "lime" : "magenta", scaled(0.1));
        }
    }
}
#endif
struct SupportArcCost
{
    bool    self_loop { false };
    bool    open { false };
    double  cost { 0 };
};

static double evaluate_support_arch_cost(const Polyline &pl)
{
    Point front = pl.points.front();
    Point back  = pl.points.back();

    coord_t ymin = front.y();
    coord_t ymax = back.y();
    if (ymin > ymax)
        std::swap(ymin, ymax);

    double dmax = 0;
    Linef line { front.cast<double>(), back.cast<double>() };
    for (const Point &pt : pl.points)
        dmax = std::max<double>(std::max(dmax, line_alg::distance_to(line, Vec2d(pt.cast<double>()))), std::max(pt.y() - ymax, ymin - pt.y()));
    return dmax;
}

static inline std::vector<SupportArcCost> evaluate_support_arches(Polylines &infill, BoundaryInfillGraph &graph, const double spacing, const FillParams &params)
{
    std::vector<SupportArcCost> arches(graph.map_infill_end_point_to_boundary.size() * 2);

    Polyline pl;
    for (ContourIntersectionPoint &cp : graph.map_infill_end_point_to_boundary) {
        assert(cp.next_on_contour != &cp);
        const size_t                    infill_line_idx = &cp - graph.map_infill_end_point_to_boundary.data();
        const bool                      first           = (infill_line_idx & 1) == 0;
        const ContourIntersectionPoint *other_end       = first ? &cp + 1 : &cp - 1;

        SupportArcCost &out_prev = arches[infill_line_idx * 2];
        SupportArcCost &out_next = *(&out_prev + 1);
        out_prev.self_loop = cp.prev_on_contour == other_end;
        out_prev.open      = cp.prev_trimmed;
        out_next.self_loop = cp.next_on_contour == other_end;
        out_next.open      = cp.next_trimmed;

        if (cp.contour_not_taken_length_next > SCALED_EPSILON) {
            pl.clear();
            pl.points.emplace_back(graph.point(cp));
            if (cp.next_trimmed)
                take_ccw_limited(pl, graph.boundary[cp.contour_idx], graph.boundary_params[cp.contour_idx], cp.point_idx, cp.next_on_contour->point_idx, cp.contour_not_taken_length_next);
            else
                take_ccw_full(pl, graph.boundary[cp.contour_idx], cp.point_idx, cp.next_on_contour->point_idx);
            out_next.cost = evaluate_support_arch_cost(pl);
        }

        if (cp.contour_not_taken_length_prev > SCALED_EPSILON) {
            pl.clear();
            pl.points.emplace_back(graph.point(cp));
            if (cp.prev_trimmed)
                take_cw_limited(pl, graph.boundary[cp.contour_idx], graph.boundary_params[cp.contour_idx], cp.point_idx, cp.prev_on_contour->point_idx, cp.contour_not_taken_length_prev);
            else
                take_cw_full(pl, graph.boundary[cp.contour_idx], cp.point_idx, cp.prev_on_contour->point_idx);
            out_prev.cost = evaluate_support_arch_cost(pl);
        }
    }

    return arches;
}

void Fill::connect_base_support(Polylines &&infill_ordered, const std::vector<const Polygon*> &boundary_src, const BoundingBox &bbox, Polylines &polylines_out, const double spacing, const FillParams &params)
{
    assert(params.anchor_length     >= 0.);
    assert(params.anchor_length_max >= 0.01f);
    assert(params.anchor_length_max >= params.anchor_length);

    BoundaryInfillGraph graph = create_boundary_infill_graph(infill_ordered, boundary_src, bbox, spacing);

#ifdef INFILL_DEBUG_OUTPUT
    static int iRun = 0;
    ++ iRun;
    export_partial_infill_to_svg(debug_out_path("connect_base_support-initial-%03d.svg", iRun), graph, infill_ordered, polylines_out);
#endif

    const double        line_half_width = 0.5 * scale_(spacing);
    const double        line_spacing    = scale_(spacing) / params.density;
    const double        min_arch_length = 1.3 * line_spacing;
    const double        trim_length     = line_half_width * 0.3;

    mark_boundary_segments_overlapping_infill(graph, infill_ordered, scale_(spacing));

#ifdef INFILL_DEBUG_OUTPUT
    export_partial_infill_to_svg(debug_out_path("connect_base_support-marked-%03d.svg", iRun), graph, infill_ordered, polylines_out);
#endif
    {
        std::vector<size_t> num_boundary_contour_infill_points(graph.boundary.size(), 0);
        for (ContourIntersectionPoint &cp : graph.map_infill_end_point_to_boundary)
            ++ num_boundary_contour_infill_points[cp.contour_idx];
        for (size_t i = 0; i < num_boundary_contour_infill_points.size(); ++ i)
            if (num_boundary_contour_infill_points[i] == 0 && graph.boundary_params[i].back() > trim_length + 0.5 * line_spacing) {
                Polyline pl(graph.boundary[i]);
                pl.points.emplace_back(pl.points.front());
                pl.clip_end(trim_length);
                if (pl.size() > 1)
                    polylines_out.emplace_back(std::move(pl));
            }
    }

    for (ContourIntersectionPoint &cp : graph.map_infill_end_point_to_boundary) {
        if (cp.next_on_contour && cp.next_trimmed && cp.next_on_contour->prev_trimmed) {
            bool    first    = graph.first(cp);
            coord_t left     = graph.point(cp).x();
            coord_t right    = left;
            if (first) {
                left  += line_half_width;
                right += line_spacing - line_half_width;
            } else {
                left  -= line_spacing - line_half_width;
                right -= line_half_width;
            }
            double param_start    = cp.param + cp.contour_not_taken_length_next;
            double param_end      = cp.next_on_contour->param - cp.next_on_contour->contour_not_taken_length_prev;
            double contour_length = graph.boundary_params[cp.contour_idx].back();
            if (param_start >= contour_length)
                param_start -= contour_length;
            if (param_end < 0)
                param_end += contour_length;
            assert(cyclic_interval_inside_interval(cp.param, cp.next_on_contour->param, param_start, param_end, contour_length));
            emit_loops_in_band(left, right, graph.boundary[cp.contour_idx], graph.boundary_params[cp.contour_idx], param_start, param_end, 0.5 * line_spacing, polylines_out);
        }
    }
#ifdef INFILL_DEBUG_OUTPUT
    export_partial_infill_to_svg(debug_out_path("connect_base_support-excess-%03d.svg", iRun), graph, infill_ordered, polylines_out);
#endif

    base_support_extend_infill_lines(infill_ordered, graph, spacing, params);

#ifdef INFILL_DEBUG_OUTPUT
    export_partial_infill_to_svg(debug_out_path("connect_base_support-extended-%03d.svg", iRun), graph, infill_ordered, polylines_out);
#endif

    std::vector<size_t> merged_with(infill_ordered.size());
    std::iota(merged_with.begin(), merged_with.end(), 0);
    auto get_and_update_merged_with = [&graph, &merged_with](const ContourIntersectionPoint *cp) -> size_t {
        size_t polyline_idx = (cp - graph.map_infill_end_point_to_boundary.data()) / 2;
        for (size_t last = polyline_idx;;) {
            size_t lower = merged_with[last];
            assert(lower <= last);
            if (lower == last) {
                merged_with[polyline_idx] = last;
                return last;
            }
            last = lower;
        }
        assert(false);
        return std::numeric_limits<size_t>::max();
    };

    auto vertical = [](BoundaryInfillGraph::Direction dir) {
        return dir == BoundaryInfillGraph::Up || dir == BoundaryInfillGraph::Down;
    };
    auto take_vertical_prev = [](const ContourIntersectionPoint &cp) {
        return cp.prev_trimmed == cp.next_trimmed ?
            cp.contour_not_taken_length_prev > cp.contour_not_taken_length_next :
            ! cp.prev_trimmed && cp.next_trimmed;
    };

    auto take_next = [&graph, &infill_ordered, &merged_with, get_and_update_merged_with, line_half_width, trim_length](ContourIntersectionPoint &cp, bool take_first) {
        ContourIntersectionPoint  *cp1            = &cp;
        ContourIntersectionPoint  *cp2            = cp.next_on_contour;
        assert(cp1->next_trimmed == cp2->prev_trimmed);
        if (take_first ? cp1->consumed : cp2->consumed)
            return;
        size_t                     polyline_idx1  = get_and_update_merged_with(cp1);
        size_t                     polyline_idx2  = get_and_update_merged_with(cp2);
        Polyline                  &polyline1      = infill_ordered[polyline_idx1];
        Polyline                  &polyline2      = infill_ordered[polyline_idx2];
        const Points              &contour        = graph.boundary[cp1->contour_idx];
        const std::vector<double> &contour_params = graph.boundary_params[cp1->contour_idx];
        assert(cp1->consumed || contour[cp1->point_idx] == polyline1.points.front() || contour[cp1->point_idx] == polyline1.points.back());
        assert(cp2->consumed || contour[cp2->point_idx] == polyline2.points.front() || contour[cp2->point_idx] == polyline2.points.back());
        bool trimmed = take_first ? cp1->next_trimmed : cp2->prev_trimmed;
        if (! trimmed) {
            trimmed = cp1 == cp2 || polyline_idx1 == polyline_idx2 || (take_first ? cp2->consumed : cp1->consumed);
            if (! trimmed) {
                const bool                      cp1_first = ((cp1 - graph.map_infill_end_point_to_boundary.data()) & 1) == 0;
                const ContourIntersectionPoint* cp1_other = cp1_first ? cp1 + 1 : cp1 - 1;
                trimmed = cp2 == cp1_other;
            }
            if (trimmed)  {
                double len = cp1 == cp2 ? contour_params.back() : path_length_along_contour_ccw(cp1, cp2, contour_params.back());
                if (take_first) {
                    cp1->trim_next(std::max(0., len - trim_length - SCALED_EPSILON));
                    cp2->trim_prev(0);
                } else {
                    cp1->trim_next(0);
                    cp2->trim_prev(std::max(0., len - trim_length - SCALED_EPSILON));
                }
            }
        }
        if (trimmed) {
            if (take_first)
                take_limited(polyline1, contour, contour_params, cp1, cp2, false, 1e10, line_half_width);
            else
                take_limited(polyline2, contour, contour_params, cp2, cp1, true, 1e10, line_half_width);
        } else if (! cp1->consumed && ! cp2->consumed) {
            if (contour[cp1->point_idx] == polyline1.points.front())
                polyline1.reverse();
            if (contour[cp2->point_idx] == polyline2.points.back())
                polyline2.reverse();
            take(polyline1, polyline2, contour, cp1, cp2, false);
            if (polyline_idx2 < polyline_idx1) {
                polyline2 = std::move(polyline1);
                polyline1.points.clear();
                merged_with[polyline_idx1] = merged_with[polyline_idx2];
            } else {
                polyline2.points.clear();
                merged_with[polyline_idx2] = merged_with[polyline_idx1];
            }
        }
    };

    for (ContourIntersectionPoint &cp : graph.map_infill_end_point_to_boundary) {
        assert(cp.contour_idx != boundary_idx_unconnected);
        if (cp.consumed)
            continue;
        const ContourIntersectionPoint &cp_other = graph.other(cp);
        assert((cp.next_on_contour == &cp_other) == (cp_other.prev_on_contour == &cp));
        assert((cp.prev_on_contour == &cp_other) == (cp_other.next_on_contour == &cp));
        BoundaryInfillGraph::Direction dir_prev = graph.dir_prev(cp);
        BoundaryInfillGraph::Direction dir_next = graph.dir_next(cp);
        assert((cp.next_on_contour == &cp) == (cp.prev_on_contour == &cp));
        bool can_take_prev = vertical(dir_prev) && ! cp.prev_on_contour->consumed && cp.prev_on_contour != &cp_other;
        bool can_take_next = vertical(dir_next) && ! cp.next_on_contour->consumed && cp.next_on_contour != &cp_other;
        if (can_take_prev && (! can_take_next || take_vertical_prev(cp))) {
            if (! cp.prev_trimmed || cp.contour_not_taken_length_prev > min_arch_length)
                take_next(*cp.prev_on_contour, false);
        } else if (can_take_next) {
            if (! cp.next_trimmed || cp.contour_not_taken_length_next > min_arch_length)
                take_next(cp, true);
        }
    }

#ifdef INFILL_DEBUG_OUTPUT
    export_partial_infill_to_svg(debug_out_path("connect_base_support-vertical-%03d.svg", iRun), graph, infill_ordered, polylines_out);
#endif

    const std::vector<SupportArcCost> arches = evaluate_support_arches(infill_ordered, graph, spacing, params);
    const double cost_low      = line_spacing * 1.3;
    const double cost_high     = line_spacing * 2.;
    const double cost_veryhigh = line_spacing * 3.;

    {
        std::vector<const SupportArcCost*> selected;
        selected.reserve(graph.map_infill_end_point_to_boundary.size());
        for (ContourIntersectionPoint &cp : graph.map_infill_end_point_to_boundary) {
            if (cp.consumed)
                continue;
            const SupportArcCost &cost_prev = arches[(&cp - graph.map_infill_end_point_to_boundary.data()) * 2];
            const SupportArcCost &cost_next = *(&cost_prev + 1);
            double                cost_min = cost_prev.cost;
            double                cost_max = cost_next.cost;
            if (cost_min > cost_max)
                std::swap(cost_min, cost_max);
            if (cost_max < cost_low || cost_min > cost_high)
                continue;
            const double           cost_diff_relative = (cost_max - cost_min) / cost_max;
            if (cost_diff_relative < 0.25)
                continue;
            if (cost_prev.cost > cost_low)
                selected.emplace_back(&cost_prev);
            if (cost_next.cost > cost_low)
                selected.emplace_back(&cost_next);
        }
        std::sort(selected.begin(), selected.end(), [](const auto *l, const auto *r) { return l->cost > r->cost; });
        for (const SupportArcCost *arc : selected) {
            ContourIntersectionPoint &cp = graph.map_infill_end_point_to_boundary[(arc - arches.data()) / 2];
            if (! cp.consumed) {
                bool prev = ((arc - arches.data()) & 1) == 0;
                if (prev)
                    take_next(*cp.prev_on_contour, false);
                else
                    take_next(cp, true);
            }
        }
    }

#if 0
    {
        struct Arc {
            ContourIntersectionPoint    *intersection;
            double                       arc_length;
            bool                         take_next;
        };
        std::vector<Arc> arches;
        arches.reserve(graph.map_infill_end_point_to_boundary.size());
        for (ContourIntersectionPoint &cp : graph.map_infill_end_point_to_boundary) {
            if (cp.consumed)
                continue;
            assert(cp.next_on_contour != &cp);
            const bool                      first     = ((&cp - graph.map_infill_end_point_to_boundary.data()) & 1) == 0;
            const ContourIntersectionPoint *other_end = first ? &cp + 1 : &cp - 1;
            const bool                      loop_next = cp.next_on_contour == other_end;
            if (! loop_next && cp.could_connect_next()) {
                if (cp.contour_not_taken_length_next > min_arch_length) {
                    arches.push_back({ &cp, cp.contour_not_taken_length_next, true });
                    arches.push_back({ cp.next_on_contour, cp.contour_not_taken_length_next, false });
                }
            } else {
                if (cp.prev_trimmed && cp.could_take_prev()) {
                    const Point &pt   = graph.point(cp);
                    const Point &prev = graph.point(*cp.prev_on_contour);
                    if (std::abs(pt.x() - prev.x()) < coord_t(0.5 * line_spacing)) {
                        if (cp.contour_not_taken_length_prev > 0.5 * line_spacing)
                            arches.push_back({ &cp, cp.contour_not_taken_length_prev, false });
                    } else {
                        if (cp.contour_not_taken_length_prev > min_arch_length)
                            arches.push_back({ &cp, cp.contour_not_taken_length_prev, false });
                    }
                }
                if (cp.next_trimmed && cp.could_take_next()) {
                    const Point &pt   = graph.point(cp);
                    const Point &next = graph.point(*cp.next_on_contour);
                    if (std::abs(pt.x() - next.x()) < coord_t(0.5 * line_spacing)) {
                        if (cp.contour_not_taken_length_next > 0.5 * line_spacing)
                            arches.push_back({ &cp, cp.contour_not_taken_length_next, true });
                    } else {
                        if (cp.contour_not_taken_length_next > min_arch_length)
                            arches.push_back({ &cp, cp.contour_not_taken_length_next, true });
                    }
                }
            }
        }
        std::sort(arches.begin(), arches.end(), [](const auto &l, const auto &r) { return l.arc_length > r.arc_length; });
        for (Arc &arc : arches)
            if (arc.take_next)
                take_next(*arc.intersection, true);
            else
                take_next(*arc.intersection->prev_on_contour, false);
    }
#endif

#ifdef INFILL_DEBUG_OUTPUT
    export_partial_infill_to_svg(debug_out_path("connect_base_support-arches-%03d.svg", iRun), graph, infill_ordered, polylines_out);
#endif
    for (ContourIntersectionPoint &cp : graph.map_infill_end_point_to_boundary) {
        assert(cp.contour_idx != boundary_idx_unconnected);
        if (cp.consumed)
            continue;
        bool first = ((&cp - graph.map_infill_end_point_to_boundary.data()) & 1) == 0;
        if (first) {
            if (get_and_update_merged_with(&cp) != get_and_update_merged_with(cp.next_on_contour))
                take_next(cp, true);
        } else {
            if (get_and_update_merged_with(&cp) != get_and_update_merged_with(cp.prev_on_contour))
                take_next(*cp.prev_on_contour, false);
        }
    }

#ifdef INFILL_DEBUG_OUTPUT
    export_partial_infill_to_svg(debug_out_path("connect_base_support-zigzag-%03d.svg", iRun), graph, infill_ordered, polylines_out);
#endif
    for (ContourIntersectionPoint &cp : graph.map_infill_end_point_to_boundary) {
        const bool                      first = ((&cp - graph.map_infill_end_point_to_boundary.data()) & 1) == 0;
        const ContourIntersectionPoint *other_end = first ? &cp + 1 : &cp - 1;
        const bool                      loop_next = cp.next_on_contour == other_end;
        const bool                      loop_prev = other_end->next_on_contour == &cp;
#ifndef NDEBUG
        const SupportArcCost           &cost_prev = arches[(&cp - graph.map_infill_end_point_to_boundary.data()) * 2];
        const SupportArcCost           &cost_next = *(&cost_prev + 1);
        assert(cost_prev.self_loop == loop_prev);
        assert(cost_next.self_loop == loop_next);
#endif
        if (loop_prev && cp.could_take_prev())
            take_next(*cp.prev_on_contour, false);
        if (loop_next && cp.could_take_next())
            take_next(cp, true);
    }

#ifdef INFILL_DEBUG_OUTPUT
    export_partial_infill_to_svg(debug_out_path("connect_base_support-caps-%03d.svg", iRun), graph, infill_ordered, polylines_out);
#endif
    {
        std::vector<const SupportArcCost*> candidates;
        for (ContourIntersectionPoint &cp : graph.map_infill_end_point_to_boundary) {
            if (cp.could_take_prev())
                candidates.emplace_back(&arches[(&cp - graph.map_infill_end_point_to_boundary.data()) * 2]);
            if (cp.could_take_next())
                candidates.emplace_back(&arches[(&cp - graph.map_infill_end_point_to_boundary.data()) * 2 + 1]);
        }
        std::sort(candidates.begin(), candidates.end(), [](auto *c1, auto *c2) { return c1->cost > c2->cost; });
        for (const SupportArcCost *candidate : candidates) {
            ContourIntersectionPoint &cp   = graph.map_infill_end_point_to_boundary[(candidate - arches.data()) / 2];
            bool                      prev = ((candidate - arches.data()) & 1) == 0;
            if (prev) {
                if (cp.could_take_prev() && (get_and_update_merged_with(&cp) != get_and_update_merged_with(cp.prev_on_contour) || candidate->cost > cost_high))
                    take_next(*cp.prev_on_contour, false);
            } else {
                if (cp.could_take_next() && (get_and_update_merged_with(&cp) != get_and_update_merged_with(cp.next_on_contour) || candidate->cost > cost_high))
                    take_next(cp, true);
            }
        }
    }

#ifdef INFILL_DEBUG_OUTPUT
    export_partial_infill_to_svg(debug_out_path("connect_base_support-Tjoints-%03d.svg", iRun), graph, infill_ordered, polylines_out);
#endif
    const double cap_cost = 0.5 * line_spacing;
    for (ContourIntersectionPoint &cp : graph.map_infill_end_point_to_boundary) {
        const SupportArcCost &cost_prev = arches[(&cp - graph.map_infill_end_point_to_boundary.data()) * 2];
        const SupportArcCost &cost_next = *(&cost_prev + 1);
        if (cp.contour_not_taken_length_prev > SCALED_EPSILON &&
            (cost_prev.self_loop ?
                cost_prev.cost > cap_cost :
                cost_prev.cost > cost_veryhigh)) {
            assert(cp.consumed && (cp.prev_on_contour->consumed || cp.prev_trimmed));
            Polyline pl { graph.point(cp) };
            if (! cp.prev_trimmed) {
                cp.trim_prev(cp.contour_not_taken_length_prev - line_half_width);
                cp.prev_on_contour->trim_next(0);
            }
            if (cp.contour_not_taken_length_prev > SCALED_EPSILON) {
                take_cw_limited(pl, graph.boundary[cp.contour_idx], graph.boundary_params[cp.contour_idx], cp.point_idx, cp.prev_on_contour->point_idx, cp.contour_not_taken_length_prev);
                cp.trim_prev(0);
                pl.clip_start(line_half_width);
                polylines_out.emplace_back(std::move(pl));
            }
        }
        if (cp.contour_not_taken_length_next > SCALED_EPSILON &&
            (cost_next.self_loop ?
                cost_next.cost > cap_cost :
                cost_next.cost > cost_veryhigh)) {
            assert(cp.consumed && (cp.next_on_contour->consumed || cp.next_trimmed));
            Polyline pl { graph.point(cp) };
            if (! cp.next_trimmed) {
                cp.trim_next(cp.contour_not_taken_length_next - line_half_width);
                cp.next_on_contour->trim_prev(0);
            }
            if (cp.contour_not_taken_length_next > SCALED_EPSILON) {
                take_ccw_limited(pl, graph.boundary[cp.contour_idx], graph.boundary_params[cp.contour_idx], cp.point_idx, cp.next_on_contour->point_idx, cp.contour_not_taken_length_next);
                cp.trim_next(0);
                pl.clip_start(line_half_width);
                polylines_out.emplace_back(std::move(pl));
            }
        }
    }

#ifdef INFILL_DEBUG_OUTPUT
    export_partial_infill_to_svg(debug_out_path("connect_base_support-final-%03d.svg", iRun), graph, infill_ordered, polylines_out);
#endif

    polylines_out.reserve(polylines_out.size() + std::count_if(infill_ordered.begin(), infill_ordered.end(), [](const Polyline &pl) { return ! pl.empty(); }));
    for (Polyline &pl : infill_ordered)
        if (! pl.empty())
            polylines_out.emplace_back(std::move(pl));
}

void Fill::connect_base_support(Polylines &&infill_ordered, const Polygons &boundary_src, const BoundingBox &bbox, Polylines &polylines_out, const double spacing, const FillParams &params)
{
    auto polygons_src = reserve_vector<const Polygon*>(boundary_src.size());
    for (const Polygon &polygon : boundary_src)
        polygons_src.emplace_back(&polygon);

    connect_base_support(std::move(infill_ordered), polygons_src, bbox, polylines_out, spacing, params);
}

void multiline_fill(Polylines& polylines, const FillParams& params, float spacing)
{
    if (params.multiline <= 1)
        return;

    const int n_lines     = params.multiline;
    const int n_polylines = static_cast<int>(polylines.size());
    Polylines all_polylines;
    all_polylines.reserve(n_lines * n_polylines);

    polylines.erase(std::remove_if(polylines.begin(), polylines.end(),
                              [](const Polyline& p) { return p.size() < 2; }),
               polylines.end());

    if (polylines.empty())
    return;

    const int rings = n_lines / 2;

    std::vector<double> offsets;
    offsets.reserve(n_lines);

    if (n_lines % 2 != 0) {
        offsets.push_back(0.0);

        for (int i = 1; i <= rings; ++i)
            offsets.push_back(i * spacing);
    } else {
        double start = 0.5 * spacing;
        for (int i = 0; i < rings; ++i)
            offsets.push_back(start + i * spacing);
    }

    for (double t : offsets) {
        if (t == 0.0) {
            all_polylines.insert(all_polylines.end(), polylines.begin(), polylines.end());
            continue;
        }

        const float delta = float(scale_(t));
        for (const Polygon &ring : offset(polylines, delta, jtRound, 0.002 * delta, etOpenRound))
            if (ring.size() >= 3)
                all_polylines.emplace_back(ring.split_at_first_point());
    }

    polylines = std::move(all_polylines);
}

}
