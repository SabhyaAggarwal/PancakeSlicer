#pragma once

#include "libslic3r/ExPolygon.hpp"

#include <cstddef>
#include <map>
#include <mutex>
#include <vector>

namespace Slic3r::Nonplanar {
struct RegionMaskCache {
    std::mutex mutex;
    bool ready = false, has_enforce = false, detected = false;
    ExPolygons enforce, block;
    struct DetectedBand { double min_z, max_z; Polygons projection; };
    std::vector<DetectedBand> detected_bands;
    struct LayerPaint {
        ExPolygons enforce, block, auto_domain;
        bool restrict_auto = false;
    };
    std::map<size_t, LayerPaint> paint;
    std::map<size_t, ExPolygons> tops;
    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        ready = has_enforce = detected = false;
        enforce.clear(); block.clear(); detected_bands.clear();
        paint.clear(); tops.clear();
    }
};
}
