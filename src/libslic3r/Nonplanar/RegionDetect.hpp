#pragma once

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"

#include <cstddef>
#include <vector>

namespace Slic3r {
class ModelVolume;
class PrintObject;
namespace Nonplanar {
struct HeadEnvelope;
struct RegionMaskCache;

enum class RegionKind { CurvedTop, Dome, SlopedLid, ReliefTop };
const char *region_kind_name(RegionKind kind);

struct Region {
    RegionKind kind = RegionKind::CurvedTop;
    double area = 0.;
    double min_slope = 0., max_slope = 0.;
    double height_span = 0.;
    double min_z = 0., max_z = 0.;
    Vec3d centroid = Vec3d::Zero();
    std::vector<int> facets;
    Polygons projection;
};

std::vector<Region> detect_nonplanar_regions(const ModelVolume &volume, const Transform3d &transform,
                                            const HeadEnvelope &head);

ExPolygons nonplanar_region_mask(const PrintObject &object, size_t layer_id);

ExPolygons nonplanar_paint_mask(const PrintObject &object, size_t layer_id, const ExPolygons &domain);

}
}
