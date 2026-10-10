#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "TextureBakeIndex.hpp"

namespace Slic3r {
namespace TextureBake {

static constexpr double DECIMATE_FLIP_DOT = 0.2;
static constexpr double DECIMATE_CREASE_COS = 0.5;
static constexpr double DECIMATE_CREASE_WEIGHT = 1e4;
static constexpr int DECIMATE_MAX_VALENCE = 64;

static constexpr double DECIMATE_DEFAULT_HARVEST_TOL = 0.005;

using DecimateProgressFn = std::function<bool(double fraction)>;

struct DecimateResult
{
    TriSoup geometry;
    std::vector<int> face_color;
    bool locked_over_budget = false;
    bool target_cost_detail = false;
};

DecimateResult decimate(const TriSoup &geometry, size_t target_triangles, bool harvest_flat = true,
                        double harvest_tol = DECIMATE_DEFAULT_HARVEST_TOL,
                        const std::vector<uint8_t> &locked_faces = {},
                        const DecimateProgressFn   &on_progress  = {},
                        const std::vector<int>     &face_color   = {});

}
}
