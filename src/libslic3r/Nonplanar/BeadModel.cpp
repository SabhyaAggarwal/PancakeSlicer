#include "BeadModel.hpp"

#include "libslic3r/libslic3r.h"

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

double bead_height_for_area(double width, double area)
{
    const double a    = 1. - 0.25 * M_PI;
    const double disc = width * width - 4. * a * area;
    return (width - std::sqrt(std::max(0., disc))) / (2. * a);
}

double segment_volume(const Vec3d &a, const Vec3d &b, double width, float height_a, float height_b,
                      const Vec3d &tool_axis)
{
    const double h = 0.5 * (double(height_a) + double(height_b));
    return std::max(0., bead_area(width, h)) * charge_length(a, b, tool_axis);
}

std::vector<double> limit_feedrates(const std::vector<Vec3d> &pts, double nominal,
                                    const MotionLimits &limits, double quantum, const std::vector<double> *caps)
{
    return limit_feedrates(pts, std::vector<double>(pts.size() < 2 ? 0 : pts.size() - 1, nominal), limits, quantum, caps);
}

namespace {

std::vector<double> shallow_junction_caps(const std::vector<Vec3d> &pts, const std::vector<double> &length,
                                          const MotionLimits &limits)
{
    const size_t        n = length.size();
    std::vector<double> cap(n + 1, std::numeric_limits<double>::infinity());
    const double        cos_shallow = std::cos(M_PI / 4.);
    for (size_t i = 1; i < n; ++i) {
        if (length[i - 1] <= 0. || length[i] <= 0.)
            continue;
        const Vec3d  u0 = (pts[i] - pts[i - 1]) / length[i - 1];
        const Vec3d  u1 = (pts[i + 1] - pts[i]) / length[i];
        const double c  = u0.dot(u1);
        if (c < cos_shallow || c > 1. - 1e-12)
            continue;
        double       v        = std::numeric_limits<double>::infinity();
        const double sin_half = std::sqrt(0.5 * (1. - c));
        if (limits.junction_deviation > 0. && limits.max_a_xy > 0.) {
            const double s = std::min(std::sqrt(0.5 * (1. + c)), 1. - 1e-9);
            v = std::sqrt(limits.max_a_xy * limits.junction_deviation * s / (1. - s));
        } else {
            const Vec3d dv = (u1 - u0).cwiseAbs();
            if (limits.max_jerk_xy > 0.)
                v = std::min(v, limits.max_jerk_xy / std::max({ dv.x(), dv.y(), 1e-12 }));
            if (limits.max_jerk_z > 0. && dv.z() > 1e-12)
                v = std::min(v, limits.max_jerk_z / dv.z());
        }
        if (limits.max_a_xy > 0. && sin_half > 1e-9)
            v = std::min(v, std::sqrt(limits.max_a_xy * 0.25 * (length[i - 1] + length[i]) / sin_half));
        cap[i] = v;
    }
    return cap;
}

double road_speed(const std::vector<double> &soft, const std::vector<double> &length, size_t begin, size_t end, double fraction)
{
    std::vector<std::pair<double, double>> v;
    double                                 total = 0.;
    for (size_t i = begin; i < end; ++i)
        if (length[i] > 0.) {
            v.emplace_back(soft[i], length[i]);
            total += length[i];
        }
    if (v.empty())
        return soft[begin];
    std::sort(v.begin(), v.end());
    double below = 0.;
    for (const auto &[speed, l] : v)
        if ((below += l) >= (1. - fraction) * total)
            return speed;
    return v.back().first;
}

}

std::vector<double> limit_feedrates(const std::vector<Vec3d> &pts, const std::vector<double> &nominal,
                                    const MotionLimits &limits, double quantum, const std::vector<double> *caps,
                                    const std::vector<bool> *glides)
{
    if (pts.size() < 2)
        return {};
    const size_t n = pts.size() - 1;
    auto glide = [glides](size_t i) { return glides != nullptr && i < glides->size() && (*glides)[i]; };
    std::vector<double> length(n), slope(n), hard = nominal, soft = nominal;
    for (size_t i = 0; i < n; ++i) {
        const Vec3d d = pts[i + 1] - pts[i];
        length[i]     = d.norm() < EPSILON ? 0. : d.norm();
        slope[i]      = length[i] > 0. ? d.z() / length[i] : 0.;
        if (caps != nullptr && i < caps->size() && !glide(i))
            hard[i] = std::min(hard[i], (*caps)[i]);
        if (length[i] <= 0.)
            continue;
        if (limits.max_v_z > 0. && std::abs(slope[i]) > 0.)
            hard[i] = std::min(hard[i], limits.max_v_z / std::abs(slope[i]));
        if (limits.max_v_xy > 0.) {
            const double xy = d.head<2>().norm() / length[i];
            if (xy > 0.)
                hard[i] = std::min(hard[i], limits.max_v_xy / xy);
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
            hard[i - 1]      = std::min(hard[i - 1], cap);
            hard[i]          = std::min(hard[i], cap);
        }

    if (limits.segment_rate > 0.)
        for (size_t i = 0; i < n; ++i)
            soft[i] = std::min(soft[i], limits.segment_rate * length[i]);
    const std::vector<double> junction = shallow_junction_caps(pts, length, limits);
    for (size_t i = 0; i < n; ++i)
        if (!glide(i))
            soft[i] = std::min({ soft[i], junction[i], junction[i + 1] });

    std::vector<double> v(n);
    for (size_t begin = 0; begin < n;) {
        size_t end = begin + 1;
        while (end < n && nominal[end] == nominal[begin])
            ++end;
        const double road = road_speed(soft, length, begin, end, 0.5);
        for (size_t i = begin; i < end; ++i)
            v[i] = std::min(road, hard[i]);
        begin = end;
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
