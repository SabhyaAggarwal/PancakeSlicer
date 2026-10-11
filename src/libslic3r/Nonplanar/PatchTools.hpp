#pragma once
#include "HeightField.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace Slic3r {
class BoundingBox;
class ExtrusionEntityCollection;
class Flow;
class PrintObject;
class PrintRegionConfig;
}

namespace Slic3r::Nonplanar {

class HeadEnvelope;
struct ConformalPatch;

using Mask = std::vector<uint8_t>;

void throw_if_canceled(const PrintObject &object);

HeightField object_grid(const PrintObject &object, double pitch, Polygons *silhouette = nullptr);

Mask rasterize(const ExPolygons &area, const HeightField &grid);
ExPolygons mask_to_expolygons(const Mask &mask, const HeightField &grid);
HeightField crop(const HeightField &src, int ix0, int iy0, int ix1, int iy1);
void pad_lowest(HeightField &field, int iterations);
std::vector<int> label_components(const Mask &mask, int nx, int ny, int &count);

void drop_steep_nodes(const HeightField &surface, const HeadEnvelope &head, double gap, const HeightField &grid, Mask &mask);

void clip_extrusions(ExtrusionEntityCollection &collection, const ExPolygons &area, const BoundingBox &area_bbox);

double top_shell_angle(const ConformalPatch &patch, const PrintRegionConfig &config);

Polylines fill_solid(const ExPolygons &area, InfillPattern pattern, double spacing, double angle, bool monotonic,
                     const Flow &flow, double z);

using LiftFn = std::function<bool(const Vec2d &p, double &z, float &bead_height)>;

void lift_polylines(const Polylines &polylines, const ExtrusionPath &proto, const LiftFn &at, double host_z, double resample,
                    int patch_id, int shell, ExtrusionEntitiesPtr &out);

}
