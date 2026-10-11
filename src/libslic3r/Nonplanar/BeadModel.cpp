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
    if (pts.size() < 2)
        return {};
    const size_t n = pts.size() - 1;
    std::vector<double> length(n), slope(n), v(n, nominal);
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

    if (quantum > 0. && quantum < 1. && nominal > 0.) {
        const double step = std::log(1. - quantum);
        for (double &s : v) {
            if (s >= nominal * (1. - 1e-9)) {
                s = nominal;
                continue;
            }
            const double k = std::ceil(std::log(std::max(s, 1e-6) / nominal) / step - 1e-9);
            s              = nominal * std::exp(k * step);
        }
    }
    return v;
}

}
