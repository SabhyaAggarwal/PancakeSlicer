#pragma once

#include "Types.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace libvgcode {

inline Color clearance_color(float clearance, float margin, float head_floor)
{
    if (std::isnan(clearance))
        return { 128, 128, 128 };
    const Color red{ 220, 55, 45 }, amber{ 245, 180, 45 }, green{ 35, 175, 100 };
    if (clearance <= 0.f)
        return red;
    head_floor = std::max(head_floor, 0.001f);
    if (clearance >= head_floor)
        return green;
    margin = std::clamp(margin, 0.001f, head_floor);
    const bool low = clearance < margin;
    const Color &a = low ? red : amber, &b = low ? amber : green;
    const float t = low ? clearance / margin : (clearance - margin) / (head_floor - margin);
    Color color{};
    for (std::size_t i = 0; i < color.size(); ++i)
        color[i] = static_cast<uint8_t>(a[i] + t * (float(b[i]) - a[i]));
    return color;
}

}
