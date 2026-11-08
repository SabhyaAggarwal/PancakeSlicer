#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "libslic3r/Point.hpp"
#include "TextureBakeIndex.hpp"

namespace Slic3r {
namespace TextureBake {

using HeightSampleFn = std::function<float(const Vec3f &position, const Vec3f &smooth_normal,
                                           const Vec3f &blend_normal)>;

struct DisplaceSettings
{
    float amplitude = 0.4f;

    bool symmetric = false;

    float bottom_angle_limit = 5.f;
    float top_angle_limit    = 0.f;

    bool no_downward_z = false;

    float boundary_falloff = 0.f;

    int blend_normal_smoothing = 32;
};

struct DisplaceBounds
{
    Vec3f min = Vec3f::Zero();
    Vec3f max = Vec3f::Zero();
};

using DisplaceProgressFn = std::function<bool(double fraction)>;

TriSoup apply_displacement(const TriSoup &geometry, const HeightSampleFn &sample,
                           const DisplaceSettings &settings, const DisplaceBounds &bounds,
                           const DisplaceProgressFn &on_progress = {});

}
}
