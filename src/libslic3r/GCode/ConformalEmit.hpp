#pragma once
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/Nonplanar/BeadModel.hpp"
#include "libslic3r/Point.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Slic3r {

class ExtrusionPath;
class ExtrusionPathConformal;
class MaterialColumns;
class GCodeWriter;

namespace Nonplanar {

struct ConformalPoint
{
    ToolPose pose;
    float    bead_height = 0.f;
};

using ToWriterFrame = std::function<Vec3d(const Point3 &)>;

constexpr double GlideLift = 0.1;

std::vector<ConformalPoint> conformal_points(const ExtrusionPathConformal &path, const ToWriterFrame &to_writer);

constexpr double ConformalTaper = 1.;
constexpr double ConformalWipe  = 1.;

struct ConformalEmitParams
{
    double       nominal_speed = 0.;
    double       e_per_mm3     = 0.;
    MotionLimits limits;
    std::string  comment;
    std::string  speed_marker;
    double       taper_length  = 0.;
};

using DepositFn = std::function<void(const Vec3d &, const Vec3d &, double)>;
double emit_conformal_extrusion(GCodeWriter &writer, const std::vector<ConformalPoint> &points, double width,
                                const ConformalEmitParams &params, float &last_height, std::string &out,
                                const DepositFn &deposit = {});

struct ConformalWipeParams
{
    double       distance = 0.;
    double       retract  = 0.;
    double       speed    = 0.;
    MotionLimits limits;
    std::string  speed_marker;
};
bool emit_conformal_wipe(GCodeWriter &writer, const std::vector<ConformalPoint> &points, const ConformalWipeParams &params,
                           std::string &out, Vec3d &end);

void deposit_material_path(MaterialColumns &columns, const ExtrusionPath &path, const ToWriterFrame &to_writer,
                           const std::function<double(double)> &z_at_fraction = {});

struct MinimalTiltParams
{
    std::vector<double> slope_limit;
    double              margin    = 3. * M_PI / 180.;
    double              smoothing = 2.;
    double              max_rate  = 0.;
};
void apply_minimal_tilt(std::vector<ConformalPoint> &points, const std::function<Vec2d(const Vec2d &)> &gradient,
                        const ToolKinematics &kinematics, const MinimalTiltParams &params);

}
}
