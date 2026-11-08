#pragma once
#include <string>

namespace Slic3r::Nonplanar {

struct SlantChoice
{
    double      angle_deg = 0., direction_deg = 0., budget_deg = 0.;
    int         transition_chunks = 0;
    double      steps = 0., overhang = 0., planar_steps = 0., planar_overhang = 0.;
    std::string planar_limit, reason;
};

}
