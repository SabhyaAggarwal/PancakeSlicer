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
#include <string>
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
enum class PatchKind : unsigned char;

using Mask = std::vector<uint8_t>;

void throw_if_canceled(const PrintObject &object);

HeightField object_grid(const PrintObject &object, double pitch, Polygons *silhouette = nullptr);

Mask rasterize(const ExPolygons &area, const HeightField &grid);
ExPolygons mask_to_expolygons(const Mask &mask, const HeightField &grid);
HeightField crop(const HeightField &src, int ix0, int iy0, int ix1, int iy1);
void pad_lowest(HeightField &field, int iterations);
std::vector<int> label_components(const Mask &mask, int nx, int ny, int &count);

void drop_steep_nodes(const HeightField &surface, const HeadEnvelope &head, double gap, const HeightField &grid, Mask &mask);

void drop_unsafe_nodes(const HeightField &surface, const HeadEnvelope &head, double gap, double max_error,
                       Mask &mask, std::vector<int> *limiting_components = nullptr);
std::string most_limiting_component(const HeadEnvelope &head, const std::vector<int> &limiting_components,
                                    const Mask &area);

void clip_extrusions(ExtrusionEntityCollection &collection, const ExPolygons &area, const BoundingBox &area_bbox);

int planar_transition_layer(const std::vector<double> &boundaries, double shell_face, double shell_height,
                            double h_min, double h_max, PatchKind kind);

double top_shell_angle(const ConformalPatch &patch, const PrintRegionConfig &config);
bool contour_line_angle(const HeightField &surface, double &angle, double min_coherence = 0.1);

Polylines fill_solid(const ExPolygons &area, InfillPattern pattern, double spacing, double angle, bool monotonic,
                     const Flow &flow, double z);
Polylines fill_sparse(const ExPolygons &area, InfillPattern pattern, double spacing, double density, double angle, bool monotonic,
                      const Flow &flow, double z);

using LiftFn = std::function<bool(const Vec2d &p, double &z, float &bead_height)>;

void lift_polylines(const Polylines &polylines, const ExtrusionPath &proto, const LiftFn &at, double host_z, double resample,
                    int patch_id, int shell, ExtrusionEntitiesPtr &out);

double head_band_height(const HeadEnvelope &head);

void cut_into_bands(const ExtrusionEntitiesPtr &paths, double pitch, double tolerance,
                    const std::function<void(int band, ExtrusionPathConformal *piece)> &sink,
                    const std::function<int(const Point3 &)> &cut_rank = {});

void band_schedule(const HeadEnvelope &head, double shell_height, double max_patch_height, double &pitch, double &tolerance);

struct BandSeams
{
    const std::vector<ExPolygons>                            &areas;
    const std::vector<Point>                                 &seams;
    const std::vector<Polyline>                              &seam_lines;
    double                                                    spacing;
    double                                                    max_glide;
    std::function<bool(const HeightField &, const Vec2d &)>   wall;
    int                                                       hidden = 0;
    const std::vector<HeightField>                           *layers = nullptr;
};

struct ConformalResult;
std::vector<ConformalPatch> split_into_bands(ConformalResult &result, const HeightField &S, const std::vector<double> &layer_z,
                                             double pitch, double tolerance, const BandSeams *seams = nullptr);

}
