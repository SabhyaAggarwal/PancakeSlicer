#include "BeadModel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Slic3r::Nonplanar {

double charge_length(const Vec3d &a, const Vec3d &b, const Vec3d &tool_axis)
{
    const Vec3d d = b - a;
    return (d - d.dot(tool_axis) * tool_axis).norm();
}

double bead_area(double width, double height)
{
    return height * (width - height * (1. - 0.25 * M_PI));
}

double segment_volume(const Vec3d &a, const Vec3d &b, double width, float height_a, float height_b,
                      const Vec3d &tool_axis)
{
    const double h = 0.5 * (double(height_a) + double(height_b));
    return std::max(0., bead_area(width, h)) * charge_length(a, b, tool_axis);
}

std::vector<double> limit_feedrates(const std::vector<Vec3d> &pts, double nominal,
                                    const MotionLimits &limits, double quantum)
{
    return limit_feedrates(pts, std::vector<double>(pts.size() < 2 ? 0 : pts.size() - 1, nominal), limits, quantum);
}

std::vector<double> limit_feedrates(const std::vector<Vec3d> &pts, const std::vector<double> &nominal,
                                    const MotionLimits &limits, double quantum)
{
    if (pts.size() < 2)
        return {};
    const size_t n = pts.size() - 1;
    std::vector<double> length(n), slope(n), v = nominal;
    for (size_t i = 0; i < n; ++i) {
        const Vec3d d = pts[i + 1] - pts[i];
        length[i]     = d.norm();
        slope[i]      = length[i] > 0. ? d.z() / length[i] : 0.;
        if (length[i] <= 0.)
            continue;
        if (limits.max_v_z > 0. && std::abs(slope[i]) > 0.)
            v[i] = std::min(v[i], limits.max_v_z / std::abs(slope[i]));
        if (limits.max_v_xy > 0.) {
            const double xy = d.head<2>().norm() / length[i];
            if (xy > 0.)
                v[i] = std::min(v[i], limits.max_v_xy / xy);
        }
    }

    if (limits.max_a_z > 0.)
        for (size_t i = 1; i < n; ++i) {
            const double ds = 0.5 * (length[i - 1] + length[i]);
            if (ds <= 0.)
                continue;
            const double curvature = std::abs(slope[i] - slope[i - 1]) / ds;
            if (curvature <= 0.)
                continue;
            const double cap = std::sqrt(limits.max_a_z / curvature);
            v[i - 1]         = std::min(v[i - 1], cap);
            v[i]             = std::min(v[i], cap);
        }

    if (limits.segment_rate > 0.)
        for (size_t i = 0; i < n; ++i)
            if (length[i] > 1e-3)
                v[i] = std::min(v[i], limits.segment_rate * length[i]);

    const double cos_shallow = std::cos(M_PI / 4.);
    for (size_t i = 1; i < n; ++i) {
        if (length[i - 1] <= 0. || length[i] <= 0.)
            continue;
        const Vec3d u0 = (pts[i] - pts[i - 1]) / length[i - 1];
        const Vec3d u1 = (pts[i + 1] - pts[i]) / length[i];
        const double c = u0.dot(u1);
        if (c < cos_shallow || c > 1. - 1e-12)
            continue;
        double cap = std::numeric_limits<double>::infinity();
        const double sin_half = std::sqrt(0.5 * (1. - c));
        if (limits.junction_deviation > 0. && limits.max_a_xy > 0.) {
            const double s = std::min(std::sqrt(0.5 * (1. + c)), 1. - 1e-9);
            cap = std::sqrt(limits.max_a_xy * limits.junction_deviation * s / (1. - s));
        }
        else {
            const Vec3d dv = (u1 - u0).cwiseAbs();
            if (limits.max_jerk_xy > 0.)
                cap = std::min(cap, limits.max_jerk_xy / std::max({ dv.x(), dv.y(), 1e-12 }));
            if (limits.max_jerk_z > 0. && dv.z() > 1e-12)
                cap = std::min(cap, limits.max_jerk_z / dv.z());
        }
        if (limits.max_a_xy > 0. && sin_half > 1e-9)
            cap = std::min(cap, std::sqrt(limits.max_a_xy * 0.25 * (length[i - 1] + length[i]) / sin_half));
        v[i - 1] = std::min(v[i - 1], cap);
        v[i]     = std::min(v[i], cap);
    }

    auto tangential_accel = [&](size_t i) {
        double a = limits.max_a_xy > 0. ? limits.max_a_xy : std::numeric_limits<double>::infinity();
        if (limits.max_a_z > 0. && std::abs(slope[i]) > 0.)
            a = std::min(a, limits.max_a_z / std::abs(slope[i]));
        return a;
    };
    for (size_t i = 1; i < n; ++i) {
        const double a = tangential_accel(i);
        if (std::isfinite(a))
            v[i] = std::min(v[i], std::sqrt(v[i - 1] * v[i - 1] + 2. * a * length[i - 1]));
    }
    for (size_t i = n - 1; i > 0; --i) {
        const double a = tangential_accel(i - 1);
        if (std::isfinite(a))
            v[i - 1] = std::min(v[i - 1], std::sqrt(v[i] * v[i] + 2. * a * length[i]));
    }

    if (quantum > 0. && quantum < 1.) {
        const double step = std::log(1. - quantum);
        for (size_t i = 0; i < n; ++i) {
            double &s = v[i];
            if (nominal[i] <= 0.)
                continue;
            if (s >= nominal[i] * (1. - 1e-9)) {
                s = nominal[i];
                continue;
            }
            const double k = std::ceil(std::log(std::max(s, 1e-6) / nominal[i]) / step - 1e-9);
            s              = nominal[i] * std::exp(k * step);
        }
    }
    return v;
}

}
