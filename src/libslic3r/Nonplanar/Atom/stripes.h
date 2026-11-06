#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace Slic3r::Nonplanar::Atom {
std::vector<std::vector<std::array<float, 3>>> continuous_stripes(const std::vector<float> &positions,
    const std::vector<uint32_t> &triangles, float spacing, float angle);
}
