#pragma once
#include "libslic3r/Point.hpp"
#include <vector>

namespace Slic3r::Nonplanar {

double charge_length(const Vec3d &a, const Vec3d &b, const Vec3d &tool_axis = Vec3d::UnitZ());

double bead_area(double width, double height);
double bead_height_for_area(double width, double area);

double segment_volume(const Vec3d &a, const Vec3d &b, double width, float height_a, float height_b,
                      const Vec3d &tool_axis = Vec3d::UnitZ());

constexpr double BridgeSagCoefficient = 0.01;
constexpr double BridgeSagMax         = 0.4;
constexpr double BridgedSagFactor     = 0.5;
constexpr double BridgeSagLayerFactor = 0.4;
constexpr double BridgeSpanAir        = 10.;
double bridge_sag(double span, bool bridged = false);
inline bool past_bridging_limit(double span, double height) { return bridge_sag(span) > 0.25 * height; }

struct MotionLimits
{
    double max_v_xy = 0., max_a_xy = 0.;
    double max_v_z  = 0., max_a_z  = 0.;
    double max_jerk_xy = 0., max_jerk_z = 0., junction_deviation = 0.;
    double segment_rate = 0.;
};

std::vector<double> limit_feedrates(const std::vector<Vec3d> &pts, double nominal,
                                    const MotionLimits &limits, double quantum = 0.05,
                                    const std::vector<double> *caps = nullptr);
std::vector<double> limit_feedrates(const std::vector<Vec3d> &pts, const std::vector<double> &nominal,
                                    const MotionLimits &limits, double quantum = 0.05,
                                    const std::vector<double> *caps = nullptr, const std::vector<bool> *glides = nullptr);

}
