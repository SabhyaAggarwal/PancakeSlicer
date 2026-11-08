#pragma once
#include <string>

namespace Slic3r::Nonplanar {

struct SlantChoice
{
    double      angle_deg = 0., direction_deg = 0., budget_deg = 0.;
    double      start_mm = 0.;
    int         morph_layers = 0;
    int         lowered = 0;
    double      full_tilt_mm = 0.;
    double      min_gap_mm = 0., max_gap_mm = 0.;
    double      steps = 0., overhang = 0., planar_steps = 0., planar_overhang = 0.;
    std::string planar_limit, reason;
};

}
