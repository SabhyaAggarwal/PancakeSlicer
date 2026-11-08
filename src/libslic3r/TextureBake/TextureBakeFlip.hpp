#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "TextureBakeDisplace.hpp"
#include "TextureBakeIndex.hpp"

namespace Slic3r {
namespace TextureBake {

struct FlipSettings
{
    int passes = 3;

    double min_gain_fraction = 0.02;

    double min_planar_cos = 0.985;
};

struct FlipResult
{
    TriSoup          geometry;
    std::vector<int> face_parent_id;
    size_t           flipped = 0;
};

FlipResult flip_edges_to_height(const TriSoup &geometry, const std::vector<int> &face_parent_id,
                                const HeightSampleFn &sample, const FlipSettings &settings,
                                const std::vector<uint8_t> &locked);

}
}
