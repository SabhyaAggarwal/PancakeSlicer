#include "../ClipperUtils.hpp"
#include "../MarchingSquares.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Execution/ExecutionTBB.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "FillTpmsFK.hpp"
#include "FillTpmsAdaptive.hpp"
#include <cmath>
#include <algorithm>
#include <cstddef>
#include <math.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {



static float fischer_koch(float x, float y, float z)
{
    return cosf(2 * x) * sinf(y) * cosf(z) + cosf(2 * y) * sinf(z) * cosf(x) + cosf(2 * z) * sinf(x) * cosf(y);
}

}

namespace marchsq {
using namespace Slic3r;

using coordr_t = long;

using Pointf = Vec2d;

struct ScalarField
{
    static constexpr float gsizef = 0.40;
    static constexpr float rsizef = 0.004;
    const coord_t          rsize  = scaled(rsizef);
    const coordr_t         gsize  = std::round(gsizef / rsizef);
    Point                  size;
    Point                  offs;
    coordf_t               z;
    float                  freq;
    float                  isoval = 0.0;

    explicit ScalarField(const BoundingBox bb, const coordf_t z = 0.0, const float period = 10.0)
        : size{bb.size()}, offs{bb.min}, z{z}, freq{float(2 * PI) / period}
    {}


    float get_scalar(coordf_t x, coordf_t y, coordf_t z) const { return fischer_koch(freq * x, freq * y, freq * z); }


    float get_scalar(Coord p) const
    {
        Pointf pf = to_Pointf(p);
        return get_scalar(pf.x(), pf.y(), z);
    }


    inline coord_t  to_coord(const coordr_t& x) const { return x * rsize; }
    inline coordr_t to_coordr(const coord_t& x) const { return x / rsize; }


    inline Point  to_Point(const Coord& p) const { return Point(to_coord(p.c) + offs.x(), to_coord(p.r) + offs.y()); }
    inline Coord  to_Coord(const Point& p) const { return Coord(to_coordr(p.y() - offs.y()), to_coordr(p.x() - offs.x())); }
    inline Pointf to_Pointf(const Point& p) const { return Pointf(unscaled(p.x()), unscaled(p.y())); }
    inline Pointf to_Pointf(const Coord& p) const { return to_Pointf(to_Point(p)); }
};


template<> struct _RasterTraits<ScalarField>
{

    using ValueType = float;


    static float get(const ScalarField& sf, size_t row, size_t col) { return sf.get_scalar(Coord(row, col)); }


    static size_t rows(const ScalarField& sf) { return sf.to_coordr(sf.size.y()); }
    static size_t cols(const ScalarField& sf) { return sf.to_coordr(sf.size.x()); }
};






Polylines get_polylines(const ScalarField& sf, const double tolerance = SCALED_EPSILON)
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

void FillTpmsFK::_fill_surface_single(const FillParams&              params,
                                      unsigned int                   thickness_layers,
                                      const std::pair<float, Point>& direction,
                                      ExPolygon                      expolygon,
                                      Polylines&                     polylines_out)
{
    if (params.tpms_adaptive == TpmsAdaptiveMode::SteppedShells && this->tpms_radial_field != nullptr) {
        fill_tpms_shells(*this->tpms_radial_field, expolygon, this->z - 0.5 * params.layer_height, params, this->spacing,
                         [&](const FillParams &shell_params, const ExPolygon &shell) {
                             this->_fill_surface_single(shell_params, thickness_layers, direction, shell, polylines_out);
                         });
        return;
    }

    auto infill_angle = float(this->angle + (CorrectionAngle * 2 * M_PI) / 360.);
    if (std::abs(infill_angle) >= EPSILON)
        expolygon.rotate(-infill_angle);


    auto period = [&params, this](float density) { return 4.18f * spacing * params.multiline / std::min(0.9f, density); };

    BoundingBox bbox = expolygon.contour.bounding_box();

    bbox.offset(scale_((params.multiline + 1) * spacing));
    Polylines polylines;
    if (params.tpms_adaptive != TpmsAdaptiveMode::Disabled && this->tpms_radial_field != nullptr) {
        polylines = make_adaptive_tpms({fischer_koch, 2. * PI / period(params.density), 2. * PI / period(params.tpms_interior_density),
                                        params.tpms_adaptive_gradient},
                                       *this->tpms_radial_field, bbox, this->z, params.layer_height, spacing, infill_angle);
    } else {
        marchsq::ScalarField sf = marchsq::ScalarField(bbox, this->z, period(params.density));

        polylines = marchsq::get_polylines(sf, SCALED_SPARSE_INFILL_RESOLUTION);
    }


    multiline_fill(polylines, params, spacing);


    polylines = intersection_pl(std::move(polylines), expolygon);

    if (!polylines.empty()) {


        const double minlength = scale_(0.8 * this->spacing);
        polylines.erase(std::remove_if(polylines.begin(), polylines.end(),
                                       [minlength](const Polyline& pl) { return pl.length() < minlength; }),
                        polylines.end());
    }

    if (!polylines.empty()) {

        size_t polylines_out_first_idx = polylines_out.size();



        connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);


        if (std::abs(infill_angle) >= EPSILON) {
            for (auto it = polylines_out.begin() + polylines_out_first_idx; it != polylines_out.end(); ++it)
                it->rotate(infill_angle);
        }
    }
}

}
