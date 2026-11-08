#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "TextureBakeDisplace.hpp"
#include "TextureBakeIndex.hpp"

namespace Slic3r {
namespace TextureBake {

struct RelocateSettings
{
    int iterations = 3;

    double max_move_fraction = 0.35;

    double min_gradient_fraction = 0.05;

    double contour_level = 0.5;

    double gradient_step_fraction = 0.25;
};

struct RelocateResult
{
    TriSoup geometry;
    size_t  moved = 0;
    size_t  rejected = 0;
};

RelocateResult relocate_to_contours(const TriSoup &geometry, const HeightSampleFn &sample,
                                    const RelocateSettings &settings = {},
                                    const std::vector<uint8_t> &locked = {});

}
}
