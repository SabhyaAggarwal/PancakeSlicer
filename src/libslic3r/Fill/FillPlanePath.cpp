#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "FillCornerSmoothing.hpp"
#include "libslic3r/Point.hpp"
#include <utility>
#include "libslic3r/Polyline.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"
#include <cmath>
#include <algorithm>
#include <math.h>
#include <cstddef>
#include <type_traits>
#include "FillPlanePath.hpp"
#include "libslic3r/Config.hpp"

namespace Slic3r {

class InfillPolylineClipper : public InfillPolylineOutput {
public:
    InfillPolylineClipper(const BoundingBox bbox, const double scale_out) : InfillPolylineOutput(scale_out), m_bbox(bbox) {}

    void            add_point(const Vec2d &pt);
    Points&&        result() { return std::move(m_out); }
    bool            clips() const override { return true; }

private:
    enum class Side {
        Left   = 1,
        Right  = 2,
        Top    = 4,
        Bottom = 8
    };

    int sides(const Point &p) const {
        return int(p.x() < m_bbox.min.x()) * int(Side::Left) +
               int(p.x() > m_bbox.max.x()) * int(Side::Right) +
               int(p.y() < m_bbox.min.y()) * int(Side::Bottom) +
               int(p.y() > m_bbox.max.y()) * int(Side::Top);
    };

    BoundingBox m_bbox;

    int         m_sides_prev;
    int         m_sides_this;
};

void InfillPolylineClipper::add_point(const Vec2d &fpt)
{
    const Point pt{ this->scaled(fpt) };

    if (m_out.size() < 2) {
        (m_out.empty() ? m_sides_prev : m_sides_this) = sides(pt);
        m_out.emplace_back(pt);
    } else {
        int sides_next = sides(pt);
        if (
            m_sides_this == 0 ||
            (m_sides_prev & m_sides_this & sides_next) == 0) {
            m_sides_prev = m_sides_this;
        } else {
            m_out.pop_back();
        }
        m_out.emplace_back(pt);
        m_sides_this = sides_next;
    }
}

void FillPlanePath::_fill_surface_single(
    const FillParams                &params, 
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction, 
    ExPolygon                        expolygon,
    Polylines                       &polylines_out)
{
    expolygon.rotate(-direction.first);

    bool        align = params.extrusion_role == ExtrusionRole::erInternalInfill;
    BoundingBox bounding_box;
    BoundingBox snug_bounding_box = get_extents(expolygon).inflated(SCALED_EPSILON);

    snug_bounding_box.offset(scale_(this->spacing)*params.multiline);

    if (align)
        bounding_box = this->bounding_box.rotated(-direction.first);
    else if (params.center_of_surface_pattern == CenterOfSurfacePattern::Each_Surface)
        bounding_box = snug_bounding_box;
    else if (params.center_of_surface_pattern == CenterOfSurfacePattern::Each_Model)
        bounding_box = this->bounding_box.rotated(-direction.first);
    else
        bounding_box = extended_object_bounding_box();

    Point shift = this->centered() ? 
        bounding_box.center() :
        bounding_box.min;
    expolygon.translate(-shift.x(), -shift.y());
    bounding_box.translate(-shift.x(), -shift.y());

    Polyline polyline;
    {
        auto distance_between_lines = scaled<double>(this->spacing) * params.multiline / params.density;
        auto min_x = coord_t(ceil(coordf_t(bounding_box.min.x()) / distance_between_lines));
        auto min_y = coord_t(ceil(coordf_t(bounding_box.min.y()) / distance_between_lines));
        auto max_x = coord_t(ceil(coordf_t(bounding_box.max.x()) / distance_between_lines));
        auto max_y = coord_t(ceil(coordf_t(bounding_box.max.y()) / distance_between_lines));
        auto resolution = scaled<double>(params.resolution) / distance_between_lines;
        if (align) {
            snug_bounding_box.translate(-shift.x(), -shift.y());
            InfillPolylineClipper output(snug_bounding_box, distance_between_lines);
            this->generate(min_x, min_y, max_x, max_y, resolution, params, output);
            polyline.points = std::move(output.result());
        } else {
            InfillPolylineOutput output(distance_between_lines);
            this->generate(min_x, min_y, max_x, max_y, resolution, params, output);
            polyline.points = std::move(output.result());
        }
    }

    if (params.extrusion_role == erIroning && dynamic_cast<FillArchimedeanChords*>(this) && polyline.size() > 1) {
        const double start_radius = 3. * scaled<double>(this->spacing) / params.density;
        auto first = std::find_if(polyline.points.begin(), polyline.points.end(),
                                  [start_radius](const Point &p) { return p.cast<double>().squaredNorm() >= start_radius * start_radius; });
        if (first != polyline.points.end())
            polyline.points.erase(polyline.points.begin() + 1, first);
    }

    Polylines polylines = {polyline};

    multiline_fill(polylines, params, spacing);

    if (polyline.size() >= 2) {
        polylines = intersection_pl(std::move(polylines), expolygon);
        if (!polylines.empty()) {
            Polylines chained;
            if (params.dont_connect() || params.density > 0.5) {
                auto is_flow_calib = params.fill_order == SurfaceFillOrder::Default &&
                                     params.extrusion_role == erTopSolidInfill &&
                                     this->print_object_config->has("calib_flowrate_topinfill_special_order") &&
                                     this->print_object_config->option("calib_flowrate_topinfill_special_order")->getBool() &&
                                     dynamic_cast<FillArchimedeanChords*>(this);
                if (is_flow_calib) {
                    auto     it            = std::max_element(polylines.begin(), polylines.end(),
                                                              [](const Polyline& a, const Polyline& b) { return a.length() < b.length(); });
                    Polyline center_spiral = std::move(*it);

                    if (center_spiral.first_point().squaredNorm() > center_spiral.last_point().squaredNorm()) {
                        center_spiral.reverse();
                    }

                    polylines.erase(it);
                    chained = chain_polylines(std::move(polylines), nullptr);

                    chained.push_back(std::move(center_spiral));
                } else if (params.fill_order != SurfaceFillOrder::Default) {
                    restore_source_path_order(polyline, polylines);
                    chained = std::move(polylines);
                    if (params.fill_order == SurfaceFillOrder::Inward) {
                        std::reverse(chained.begin(), chained.end());
                        for (Polyline &pl : chained)
                            pl.reverse();
                    }
                } else {
                    chained = chain_polylines(std::move(polylines), nullptr);
                }
            } else
                connect_infill(std::move(polylines), expolygon, chained, this->spacing, params);
            for (Polyline& pl : chained) {
                pl.translate(shift.x(), shift.y());
                pl.rotate(direction.first);
            }
            append(polylines_out, std::move(chained));
        }
    }
}

template<typename Output>
static void generate_archimedean_chords(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution, Output &output)
{
    coordf_t rmax = std::sqrt(coordf_t(max_x)*coordf_t(max_x)+coordf_t(max_y)*coordf_t(max_y)) * std::sqrt(2.) + 1.5;
    coordf_t a = 1.;
    coordf_t b = 1./(2.*M_PI);
    coordf_t theta = 0.;
    coordf_t r = 1;
    Pointfs out;
    output.add_point({ 0, 0 });
    output.add_point({ 1, 0 });
    while (r < rmax) {
        theta += 2. * acos(1. - resolution / r);
        r = a + b * theta;
        output.add_point({ r * cos(theta), r * sin(theta) });
    }
}

void FillArchimedeanChords::generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution, InfillPolylineOutput &output)
{
    if (output.clips())
        generate_archimedean_chords(min_x, min_y, max_x, max_y, resolution, static_cast<InfillPolylineClipper&>(output));
    else
        generate_archimedean_chords(min_x, min_y, max_x, max_y, resolution, output);
}

static inline Point hilbert_n_to_xy(const size_t n)
{
    static constexpr const int next_state[16] { 4,0,0,12, 0,4,4,8, 12,8,8,4, 8,12,12,0 };
    static constexpr const int digit_to_x[16] { 0,1,1,0, 0,0,1,1, 1,0,0,1, 1,1,0,0 };
    static constexpr const int digit_to_y[16] { 0,0,1,1, 0,1,1,0, 1,1,0,0, 1,0,0,1 };

    size_t ndigits = 0;
    {
        size_t nc = n;
        while(nc > 0) {
            nc >>= 2;
            ++ ndigits;
        }
    }
    int state = (ndigits & 1) ? 4 : 0;
    coord_t x = 0;
    coord_t y = 0;
    for (int i = (int)ndigits - 1; i >= 0; -- i) {
        int digit = (n >> (i * 2)) & 3;
        state += digit;
        x |= digit_to_x[state] << i;
        y |= digit_to_y[state] << i;
        state = next_state[state];
    }
    return Point(x, y);
}

template<typename Output>
static void generate_hilbert_curve(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, Output &output)
{
    size_t sz = 2;
    size_t pw = 1;
    {
        size_t sz0 = std::max(max_x + 1 - min_x, max_y + 1 - min_y);
        while (sz < sz0) {
            sz = sz << 1;
            ++ pw;
        }
    }

    size_t sz2 = sz * sz;
    output.reserve(sz2);
    for (size_t i = 0; i < sz2; ++ i) {
        Point p = hilbert_n_to_xy(i);
        output.add_point({ p.x() + min_x, p.y() + min_y });
    }
}

template<typename Output>
class SmoothingPolylineOutput
{
public:
    SmoothingPolylineOutput(Output &output, const double smooth_factor, const double tolerance)
        : m_output(output), m_smoother(smooth_factor, tolerance) {}

    void reserve(size_t n) { m_output.reserve(n); }
    void add_point(const Vec2d &pt) { auto emit = emitter(); m_smoother.push(pt, emit); }
    void finish() { auto emit = emitter(); m_smoother.flush(emit); }

private:
    auto emitter()
    {
        return [this](const Vec2d &pt) {
            const Point snapped = m_output.scaled(pt);
            if (m_has_last_snapped && snapped == m_last_snapped)
                return;
            m_last_snapped     = snapped;
            m_has_last_snapped = true;
            m_output.add_point(pt);
        };
    }

    Output        &m_output;
    CornerSmoother m_smoother;
    Point          m_last_snapped { Point::Zero() };
    bool           m_has_last_snapped { false };
};

template<typename GenerateFn>
static void generate_path(InfillPolylineOutput &output, const FillParams &params, const double resolution, GenerateFn generate)
{
    const double smooth_factor = sanitize_smooth_factor(params.smooth_factor);
    auto run = [smooth_factor, resolution, &generate](auto &out) {
        if (smooth_factor == 0.) {
            generate(out);
        } else {
            SmoothingPolylineOutput<std::remove_reference_t<decltype(out)>> smoothing(out, smooth_factor, resolution);
            generate(smoothing);
            smoothing.finish();
        }
    };

    if (output.clips())
        run(static_cast<InfillPolylineClipper&>(output));
    else
        run(output);
}

void FillHilbertCurve::generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double , InfillPolylineOutput &output)
{
    if (output.clips())
        generate_hilbert_curve(min_x, min_y, max_x, max_y, static_cast<InfillPolylineClipper&>(output));
    else
        generate_hilbert_curve(min_x, min_y, max_x, max_y, output);
}

void FillHilbertCurve::generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution,
    const FillParams &params, InfillPolylineOutput &output)
{
    generate_path(output, params, resolution,
        [min_x, min_y, max_x, max_y](auto &out) { generate_hilbert_curve(min_x, min_y, max_x, max_y, out); });
}

template<typename Output>
static void generate_octagram_spiral(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, Output &output)
{
    coordf_t rmax = std::sqrt(coordf_t(max_x)*coordf_t(max_x)+coordf_t(max_y)*coordf_t(max_y)) * std::sqrt(2.) + 1.5;
    coordf_t r = 0;
    coordf_t r_inc = sqrt(2.);
    output.add_point({ 0., 0. });
    while (r < rmax) {
        r += r_inc;
        coordf_t rx = r / sqrt(2.);
        coordf_t r2 = r + rx;
        output.add_point({ r,   0. });
        output.add_point({ r2,  rx });
        output.add_point({ rx,  rx });
        output.add_point({ rx,  r2 });
        output.add_point({ 0.,  r  });
        output.add_point({-rx,  r2 });
        output.add_point({-rx,  rx });
        output.add_point({-r2,  rx });
        output.add_point({- r,  0. });
        output.add_point({-r2, -rx });
        output.add_point({-rx, -rx });
        output.add_point({-rx, -r2 });
        output.add_point({ 0., -r  });
        output.add_point({ rx, -r2 });
        output.add_point({ rx, -rx });
        output.add_point({ r2+r_inc, -rx });
    }
}

void FillOctagramSpiral::generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double , InfillPolylineOutput &output)
{
    if (output.clips())
        generate_octagram_spiral(min_x, min_y, max_x, max_y, static_cast<InfillPolylineClipper&>(output));
    else
        generate_octagram_spiral(min_x, min_y, max_x, max_y, output);
}

void FillOctagramSpiral::generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution,
    const FillParams &params, InfillPolylineOutput &output)
{
    generate_path(output, params, resolution,
        [min_x, min_y, max_x, max_y](auto &out) { generate_octagram_spiral(min_x, min_y, max_x, max_y, out); });
}

}
