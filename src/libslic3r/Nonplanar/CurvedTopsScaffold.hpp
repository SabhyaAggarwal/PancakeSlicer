#pragma once
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Layer.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace Slic3r::Nonplanar {




struct CurvedTopsScaffold {
    struct Region {
        LayerRegion *region;
        ExtrusionEntityCollection perimeters, fills, thin_fills;
    };
    const PrintObject *owner = nullptr;
    std::array<std::uint64_t, 3> generation{};
    std::vector<Region> regions;
    bool restored = false;

    void remember(LayerRegion &region)
    {
        if (std::none_of(regions.begin(), regions.end(), [&region](const Region &saved) { return saved.region == &region; }))
            regions.push_back({&region, region.perimeters, region.fills, region.thin_fills});
    }
    void restore()
    {
        restored = true;
        for (const Region &saved : regions) {
            saved.region->perimeters = saved.perimeters;
            saved.region->fills = saved.fills;
            saved.region->thin_fills = saved.thin_fills;
        }
    }
};
}
