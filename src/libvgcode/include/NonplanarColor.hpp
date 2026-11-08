#pragma once

#include "Types.hpp"
#include <array>
#include <cstddef>

namespace libvgcode {

static const Color NONPLANAR_PLANAR_COLOR{ 140, 140, 140 };

inline Color nonplanar_index_color(int index)
{
    static const std::array<Color, 7> palette{ {
        { 230, 159, 0 }, { 86, 180, 233 }, { 0, 158, 115 }, { 240, 228, 66 },
        { 0, 114, 178 }, { 213, 94, 0 }, { 204, 121, 167 } } };
    return index < 0 ? Color{ 230, 230, 230 } : palette[static_cast<std::size_t>(index) % palette.size()];
}

}
