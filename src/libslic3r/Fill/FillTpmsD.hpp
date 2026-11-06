#ifndef slic3r_FillTpmsD_hpp_
#define slic3r_FillTpmsD_hpp_

#include <utility>

#include "FillBase.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Polyline.hpp"

namespace Slic3r {
class Point;

class FillTpmsD : public Fill
{
public:
    FillTpmsD() {}
    Fill* clone() const override { return new FillTpmsD(*this); }


    bool use_bridge_flow() const override { return false; }




    static constexpr float CorrectionAngle = -45.;

    void _fill_surface_single(const FillParams&              params,
                              unsigned int                   thickness_layers,
                              const std::pair<float, Point>& direction,
                              ExPolygon                      expolygon,
                              Polylines&                     polylines_out) override;

    bool is_self_crossing() override { return false; }
    bool aligned_to_origin() const override { return true; }


    static constexpr double DensityAdjust = 2.1;


    static constexpr double PatternTolerance = 0.1;

};

}

#endif
