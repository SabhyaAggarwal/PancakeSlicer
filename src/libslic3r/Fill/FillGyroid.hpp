#ifndef slic3r_FillGyroid_hpp_
#define slic3r_FillGyroid_hpp_


#include "libslic3r/ExPolygon.hpp"
#include "FillBase.hpp"
#include <utility>
#include "libslic3r/Polyline.hpp"

namespace Slic3r { class Point; }

namespace Slic3r {

class FillGyroid : public Fill
{
public:
    FillGyroid() {}
    Fill* clone() const override { return new FillGyroid(*this); }


    bool use_bridge_flow() const override { return false; }
    bool is_self_crossing() override { return false; }
    bool aligned_to_origin() const override { return true; }



    static constexpr float CorrectionAngle = -45.;


    static constexpr double DensityAdjust = 2.44;


    static constexpr double PatternTolerance = 0.2;


protected:
    void _fill_surface_single(
        const FillParams                &params,
        unsigned int                     thickness_layers,
        const std::pair<float, Point>   &direction,
        ExPolygon                        expolygon,
        Polylines                       &polylines_out) override;
};

}

#endif
