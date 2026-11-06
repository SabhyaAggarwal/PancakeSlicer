#include "../ClipperUtils.hpp"
#include "../MarchingSquares.hpp"
#include <cmath>
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>
#include "libslic3r/BoundingBox.hpp"
#include <vector>
#include "libslic3r/Execution/ExecutionTBB.hpp"
#include <math.h>
#include <utility>
#include "libslic3r/ExPolygon.hpp"
#include "FillBase.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Polyline.hpp"
#include "FillGyroid.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "FillTpmsAdaptive.hpp"

namespace Slic3r {

static float gyroid(float x, float y, float z)
{
    return std::sin(x) * std::cos(y) + std::sin(y) * std::cos(z) + std::sin(z) * std::cos(x);
}

}














namespace marchsq {
using namespace Slic3r;

using coordr_t = long;
using Pointf   = Vec2d;

struct GyroidField
{
    static constexpr float gsizef = 0.40f;
    static constexpr float rsizef = 0.004f;
    const coord_t          rsize  = scaled(rsizef);
    const coordr_t         gsize  = std::round(gsizef / rsizef);
    Point                  size;
    Point                  offs;
    coordf_t               z;
    float                  fx;
    float                  fy;
    float                  fz;
    float                  isoval = 0.0f;

    explicit GyroidField(const BoundingBox bb, const coordf_t z, const float period, const float omega = 1.0f)
        : size{bb.size()}, offs{bb.min}, z{z}
    {
        const float baseline = float(2.0 * PI) / std::max(period, 1e-3f);
        fx = baseline;
        fy = baseline;
        fz = omega * baseline;
    }

    float get_scalar(coordf_t x, coordf_t y, coordf_t z_arg) const
    {
        return gyroid(fx * float(x), fy * float(y), fz * float(z_arg));
    }

    float get_scalar(Coord p) const
    {
        Pointf pf = to_Pointf(p);
        return get_scalar(pf.x(), pf.y(), z);
    }

    inline coord_t  to_coord (const coordr_t& x) const { return x * rsize; }
    inline coordr_t to_coordr(const coord_t& x)  const { return x / rsize; }
    inline Point  to_Point (const Coord& p) const { return Point(to_coord(p.c) + offs.x(), to_coord(p.r) + offs.y()); }
    inline Coord  to_Coord (const Point& p) const { return Coord(to_coordr(p.y() - offs.y()), to_coordr(p.x() - offs.x())); }
    inline Pointf to_Pointf(const Point& p) const { return Pointf(unscaled(p.x()), unscaled(p.y())); }
    inline Pointf to_Pointf(const Coord& p) const { return to_Pointf(to_Point(p)); }
};

template<> struct _RasterTraits<GyroidField>
{
    using ValueType = float;
    static float  get (const GyroidField& sf, size_t row, size_t col) { return sf.get_scalar(Coord(row, col)); }
    static size_t rows(const GyroidField& sf) { return sf.to_coordr(sf.size.y()); }
    static size_t cols(const GyroidField& sf) { return sf.to_coordr(sf.size.x()); }
};

inline Polylines get_gyroid_polylines(const GyroidField& sf, const double tolerance = SCALED_EPSILON)
{
    std::vector<Ring> rings = execute_with_policy(ex_tbb, sf, sf.isoval, {sf.gsize, sf.gsize});
    Polylines polys;
    polys.reserve(rings.size());
    for (const Ring& ring : rings) {
        Polyline poly;
        Points&  pts = poly.points;
        pts.reserve(ring.size() + 1);
        for (const Coord& crd : ring)
            pts.emplace_back(sf.to_Point(crd));
        pts.push_back(pts.front());
        if (tolerance >= 0.0)
            poly.simplify(tolerance);
        polys.emplace_back(poly);
    }
    return polys;
}

}

namespace Slic3r {

static inline double f(double x, double z_sin, double z_cos, bool vertical, bool flip)
{
    if (vertical) {
        double phase_offset = (z_cos < 0 ? M_PI : 0) + M_PI;
        double a   = sin(x + phase_offset);
        double b   = - z_cos;
        double res = z_sin * cos(x + phase_offset + (flip ? M_PI : 0.));
        double r   = sqrt(sqr(a) + sqr(b));
        return asin(a/r) + asin(res/r) + M_PI;
    }
    else {
        double phase_offset = z_sin < 0 ? M_PI : 0.;
        double a   = cos(x + phase_offset);
        double b   = - z_sin;
        double res = z_cos * sin(x + phase_offset + (flip ? 0 : M_PI));
        double r   = sqrt(sqr(a) + sqr(b));
        return (asin(a/r) + asin(res/r) + 0.5 * M_PI);
    }
}


static inline Polyline make_wave(
    const std::vector<Vec2d>& one_period, double x_min, double x_max, double offset, double scaleFactor, bool vertical)
{
    const double period = one_period.back().x();

    const size_t n  = one_period.size() - 1;
    double       x0 = std::floor(x_min / period) * period;
    size_t       i  = 0;
    while (i + 1 < n && x0 + one_period[i + 1].x() <= x_min)
        ++i;

    Polyline polyline;
    polyline.points.reserve(size_t((x_max - x0) / period + 1.) * n + 1);
    for (;;) {
        Vec2d      point(x0 + one_period[i].x(), one_period[i].y() + offset);
        const bool last = point.x() >= x_max;
        if (vertical)
            std::swap(point(0), point(1));
        polyline.points.emplace_back((point * scaleFactor).cast<coord_t>());
        if (last)
            break;
        if (++i == n) {
            i = 0;
            x0 += period;
        }
    }
    return polyline;
}

static std::vector<Vec2d> make_one_period(double scaleFactor, double z_cos, double z_sin, bool vertical, bool flip, double tolerance)
{
    std::vector<Vec2d> points;
    double dx = M_PI_2;
    double limit = 2*M_PI;
    points.reserve(coord_t(ceil(limit / tolerance / 3)));

    for (double x = 0.; x < limit - EPSILON; x += dx) {
        points.emplace_back(Vec2d(x, f(x, z_sin, z_cos, vertical, flip)));
    }
    points.emplace_back(Vec2d(limit, f(limit, z_sin, z_cos, vertical, flip)));


    for(;;)
    {
        size_t size = points.size();
        for (unsigned int i = 1;i < size; ++i) {
            auto& lp = points[i-1];
            auto& rp = points[i];
            double x = lp(0) + (rp(0) - lp(0)) / 2;
            double y = f(x, z_sin, z_cos, vertical, flip);
            Vec2d ip = {x, y};
            if (std::abs(cross2(Vec2d(ip - lp), Vec2d(ip - rp))) > sqr(tolerance)) {
                points.emplace_back(std::move(ip));
            }
        }

        if (size == points.size())
            break;
        else
        {

            std::sort(points.begin(), points.end(),
                      [](const Vec2d &lhs, const Vec2d &rhs) { return lhs(0) < rhs(0); });
        }
    }

    return points;
}




































static inline double compute_omega_factor(double density_adjusted, double line_spacing, double layer_height)
{
    double lh_ratio   = (line_spacing > 0.) ? layer_height / line_spacing : 0.5;
    double correction = 1.0 / std::sqrt(1.0 + lh_ratio);
    double raw        = std::sqrt(1.0 / std::max(density_adjusted, 0.1)) * correction;
    return std::clamp(raw, 1.0, 2.0);
}


static Polylines make_gyroid_waves(double gridZ, double density_adjusted, double line_spacing, const BoundingBox &bbox, const Point &origin)
{
    const double scaleFactor = scale_(line_spacing) / density_adjusted;



    const double tolerance = std::min(line_spacing / 2, FillGyroid::PatternTolerance) / unscale<double>(scaleFactor);



    const double z     = gridZ / scaleFactor;
    const double z_sin = sin(z);
    const double z_cos = cos(z);

    bool vertical = (std::abs(z_sin) <= std::abs(z_cos));

    Vec2d lo = (bbox.min - origin).cast<double>() / scaleFactor;
    Vec2d hi = (bbox.max - origin).cast<double>() / scaleFactor;
    double lower_bound = 0.;
    bool flip = true;
    if (vertical) {
        flip = false;
        lower_bound = -M_PI;
        std::swap(lo(0), lo(1));
        std::swap(hi(0), hi(1));
    }

    std::vector<Vec2d> one_period_odd = make_one_period(scaleFactor, z_cos, z_sin, vertical, flip, tolerance);
    flip = !flip;
    std::vector<Vec2d> one_period_even = make_one_period(scaleFactor, z_cos, z_sin, vertical, flip, tolerance);


    double f_min = std::numeric_limits<double>::max();
    double f_max = std::numeric_limits<double>::lowest();
    for (const std::vector<Vec2d> *one_period : { &one_period_odd, &one_period_even })
        for (const Vec2d &point : *one_period) {
            f_min = std::min(f_min, point.y());
            f_max = std::max(f_max, point.y());
        }

    Polylines result;
    for (int i = int(std::ceil((lo.y() - f_max - lower_bound) / M_PI)); lower_bound + i * M_PI + f_min <= hi.y(); ++i) {
        Polyline &wave = result.emplace_back(make_wave(i % 2 == 0 ? one_period_odd : one_period_even, lo.x(), hi.x(),
                                                       lower_bound + i * M_PI, scaleFactor, vertical));
        wave.translate(origin);
    }

    return result;
}


constexpr double FillGyroid::PatternTolerance;

void FillGyroid::_fill_surface_single(
    const FillParams                &params,
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction,
    ExPolygon                        expolygon,
    Polylines                       &polylines_out)
{
    if (params.tpms_adaptive == TpmsAdaptiveMode::SteppedShells && this->tpms_radial_field != nullptr) {
        fill_tpms_shells(*this->tpms_radial_field, expolygon, this->z - 0.5 * params.layer_height, params, this->spacing,
                         [&](const FillParams &shell_params, const ExPolygon &shell) {
                             this->_fill_surface_single(shell_params, thickness_layers, direction, shell, polylines_out);
                         });
        return;
    }

    auto infill_angle = float(this->angle + (CorrectionAngle * 2*M_PI) / 360.);
    if(std::abs(infill_angle) >= EPSILON)
        expolygon.rotate(-infill_angle);

    BoundingBox bb = expolygon.contour.bounding_box();

    double      density_adjusted = std::max(0., params.density * DensityAdjust / params.multiline);

    coord_t     distance = coord_t(scale_(this->spacing) / density_adjusted);


    const coord_t shift  = coord_t(10 * scale_(this->spacing));
    const Point   origin = align_to_grid(bb.min, Point(2*M_PI*distance, 2*M_PI*distance)) - Point(shift, shift);


    bb.offset(scale_(this->spacing * params.multiline));


    Polylines polylines;
    if (params.tpms_adaptive != TpmsAdaptiveMode::Disabled && this->tpms_radial_field != nullptr) {

        auto frequency = [&params, this](double density) { return density * DensityAdjust / (params.multiline * this->spacing); };
        polylines = make_adaptive_tpms({gyroid, frequency(params.density), frequency(params.tpms_interior_density), params.tpms_adaptive_gradient},
                                       *this->tpms_radial_field, bb, this->z, params.layer_height, this->spacing, infill_angle);
    } else if (params.gyroid_optimized) {









        const double lh = (params.layer_height > 0.) ? double(params.layer_height) : double(this->spacing);
        const double omega = compute_omega_factor(density_adjusted, this->spacing * params.multiline, lh);

        const float density_factor = std::max(0.001f, float(params.density * DensityAdjust / params.multiline));
        const float period         = float(2.0 * M_PI) * float(this->spacing) / density_factor;


        const coord_t cell = scaled(marchsq::GyroidField::gsizef);
        bb.offset(cell);
        bb.merge(align_to_grid(bb.min, Point(cell, cell)));
        marchsq::GyroidField sf(bb, this->z, period, float(omega));
        polylines = marchsq::get_gyroid_polylines(sf, SCALED_SPARSE_INFILL_RESOLUTION);
    } else {
        polylines = make_gyroid_waves(scale_(this->z), density_adjusted, this->spacing, bb, origin);
    }


    multiline_fill(polylines, params, spacing);

	polylines = intersection_pl(std::move(polylines), expolygon);

    if (! polylines.empty()) {


        const double minlength = scale_(0.8 * this->spacing);
		polylines.erase(
			std::remove_if(polylines.begin(), polylines.end(), [minlength](const Polyline &pl) { return pl.length() < minlength; }),
			polylines.end());
    }

	if (! polylines.empty()) {

		size_t polylines_out_first_idx = polylines_out.size();
        chain_or_connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);


        if (std::abs(infill_angle) >= EPSILON) {
	        for (auto it = polylines_out.begin() + polylines_out_first_idx; it != polylines_out.end(); ++ it)
                it->rotate(infill_angle);
	    }
    }
}

}
