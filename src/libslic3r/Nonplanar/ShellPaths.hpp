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
};

std::vector<ShellFill> plan_shell_fills(const ConformalPatch &patch, const PrintRegionConfig &config, double spacing);

Point shell_seam(const ExPolygons &area, const HeightField &surface, SeamPosition seam, unsigned salt);

Polylines fill_spiral(const ExPolygons &area, double spacing, const Point &seam);

Polylines fill_surface_spaced(const ExPolygons &area, const HeightField &surface, InfillPattern pattern, double spacing, double angle,
                              bool monotonic, const Flow &flow, double z);

float surface_spaced_bead(float height, double width, const Vec2d &gradient, double angle);

std::vector<Polylines> shell_polylines(const ExPolygons &area, const ShellFill &fill, InfillPattern line_pattern, double spacing,
                                       bool monotonic, const Flow &flow, double z, const Point &start, double max_glide,
                                       const HeightField *surface_spaced = nullptr);

void lift_linked(const std::vector<Polylines> &linked, const ExtrusionPath &proto, const LiftFn &at, double host_z, double resample,
                 int patch_id, int shell, ExtrusionEntitiesPtr &out);

bool prepend_glide(ExtrusionPathConformal &path, const Point3 &from, const ExPolygons &area, const LiftFn &at, double host_z,
                   double resample, double max_glide);

}
