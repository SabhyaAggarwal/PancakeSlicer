#ifndef slic3r_BeltBrim_hpp_
#define slic3r_BeltBrim_hpp_

#include "ExPolygon.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Point.hpp"
#include "Polyline.hpp"
#include "libslic3r.h"

#include <cmath>
#include <vector>






























namespace Slic3r {






inline constexpr double BELT_BRIM_MIN_TILT_DEG = 1.;
inline constexpr double BELT_BRIM_MAX_TILT_DEG = 85.;


struct BeltBrimFrame
{

    double shear     = 0.;

    int    from_axis = 1;



    double u_stretch() const { return std::sqrt(1. + shear * shear); }

    double cos_tilt()  const { return 1. / this->u_stretch(); }


    int    downhill_sign() const { return shear > 0. ? -1 : +1; }
};






ExPolygons belt_scale_u(const ExPolygons &src, const BeltBrimFrame &frame, double factor);
Polylines  belt_scale_u(const Polylines  &src, const BeltBrimFrame &frame, double factor);


inline ExPolygons belt_flatten(const ExPolygons &src, const BeltBrimFrame &frame)
    { return belt_scale_u(src, frame, frame.u_stretch()); }
inline ExPolygons belt_unflatten(const ExPolygons &src, const BeltBrimFrame &frame)
    { return belt_scale_u(src, frame, frame.cos_tilt()); }









ExPolygons sweep_ex(const ExPolygons &src, const Point &t);







ExPolygons belt_brim_clip_leading_edge(const ExPolygons &region, const BeltBrimFrame &frame, coordf_t u_cut);

















ExPolygons belt_brim_region(const ExPolygons   &footprint_flat,
                            bool                has_outer,
                            bool                has_inner,
                            coord_t             brim_width,
                            coord_t             object_gap,
                            coord_t             leading,
                            coord_t             lateral,
                            const BeltBrimFrame &frame);












std::vector<coord_t> belt_brim_line_positions(coord_t u_lo,
                                              coord_t u_hi,
                                              coord_t pitch_u,
                                              coord_t u_anchor);















struct BeltBrimBand
{
    coordf_t                  print_z = 0.;
    coordf_t                  height  = 0.;

    ExtrusionEntityCollection fills;

    ExPolygons                areas;
};

class PrintObject;






void make_belt_brim(PrintObject &object);

}

#endif
