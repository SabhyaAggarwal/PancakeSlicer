#pragma once
#include "PatchTools.hpp"

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <vector>

namespace Slic3r { class Flow; }

namespace Slic3r::Nonplanar {

class HeightField;
struct ConformalPatch;

constexpr double MaxGlide = 20.;

struct PathTilt
{
    double max  = 0.;
    double mean = 0.;
};
PathTilt path_tilt(const Polylines &polylines, const HeightField &surface, double step);

struct ShellFill
{
    bool   spiral  = false;
    bool   outward = false;
    double angle   = 0.;
    bool   outline = true;
};

std::vector<ShellFill> plan_shell_fills(const ConformalPatch &patch, const PrintRegionConfig &config, double spacing);

Polylines fill_spiral(const ExPolygons &area, double spacing, const Point &seam, const Polyline *seam_line = nullptr);

Polylines fill_surface_spaced(const ExPolygons &area, const HeightField &surface, InfillPattern pattern, double spacing, double angle,
                              bool monotonic, const Flow &flow, double z);

float surface_spaced_bead(float height, double width, const Vec2d &gradient, double angle);

constexpr double ContourStripHeight = 2.;

Polylines contour_fill(const HeightField &surface, const ExPolygons &area, double drop, double spacing, const Flow &flow, bool monotonic,
                       double z, double cross = 0., double strip_height = ContourStripHeight);

Polylines chain_nearest(Polylines &&polylines, const Point &start);

std::vector<Polylines> shell_polylines(const ExPolygons &area, const ShellFill &fill, InfillPattern line_pattern, double spacing,
                                       bool monotonic, const Flow &flow, double z, const Point &start, double max_glide,
                                       const HeightField *surface_spaced = nullptr, const Polyline *seam_line = nullptr);

std::vector<Polylines> link_paths(Polylines &&paths, const ExPolygons &area, double spacing, double max_glide);

void lift_linked(const std::vector<Polylines> &linked, const ExtrusionPath &proto, const LiftFn &at, double host_z, double resample,
                 int patch_id, int shell, ExtrusionEntitiesPtr &out);

bool prepend_glide(ExtrusionPathConformal &path, const Point3 &from, const ExPolygons &area, const LiftFn &at, double host_z,
                   double resample, double max_glide);

}
