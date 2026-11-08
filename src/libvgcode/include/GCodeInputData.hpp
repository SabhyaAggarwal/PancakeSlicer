///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966
///|/ libvgcode is released under the terms of the AGPLv3 or higher
#ifndef VGCODE_GCODEINPUTDATA_HPP
#define VGCODE_GCODEINPUTDATA_HPP

#include "PathVertex.hpp"
#include <vector>
#include "Types.hpp"

namespace libvgcode {

struct GCodeInputData
{
    bool spiral_vase_mode{ false };
    float clearance_margin{ 0.3f };
    float clearance_head_floor{ 5.f };
    std::vector<PathVertex> vertices;
    Palette tools_colors;
    Palette color_print_colors;
};

}

#endif
