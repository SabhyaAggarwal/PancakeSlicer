#include "ConformalEmit.hpp"
#include "ClearanceVerifier.hpp"

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCodeWriter.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Slic3r::Nonplanar {

std::vector<ConformalPoint> conformal_points(const ExtrusionPathConformal &path, const ToWriterFrame &to_writer)
{
    const Points3              &pts = path.polyline.points;
    std::vector<ConformalPoint> out(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        out[i].pose.tip     = to_writer(pts[i]);
        out[i].pose.axis    = i < path.tool_axis.size() ? Vec3d(path.tool_axis[i].cast<double>().normalized()) : Vec3d::UnitZ();
        out[i].bead_height  = i < path.bead_height.size() ? path.bead_height[i] : path.height;
        if (out[i].bead_height <= 0.f && i > 0 && i + 1 < pts.size())
            out[i].pose.tip += GlideLift * out[i].pose.axis;
    }
    return out;
}

namespace {

ConformalPoint interpolate(const ConformalPoint &a, const ConformalPoint &b, double t)
{
    ConformalPoint p;
    p.pose.tip    = a.pose.tip + t * (b.pose.tip - a.pose.tip);
    p.pose.axis   = (a.pose.axis + t * (b.pose.axis - a.pose.axis)).normalized();
    p.bead_height = a.bead_height + float(t) * (b.bead_height - a.bead_height);
    return p;
}

std::vector<ConformalPoint> taper_runs(const std::vector<ConformalPoint> &points, double length, std::vector<double> &flow)
{
    const size_t n = points.size();
    std::vector<ConformalPoint> out;
    flow.clear();
    out.reserve(n + 16);
    out.emplace_back(points.front());
    auto extruded = [&points](size_t i) { return points[i].bead_height > 0.f && points[i + 1].bead_height > 0.f; };
    for (size_t i = 0; i + 1 < n;) {
        if (!extruded(i)) {
            out.emplace_back(points[i + 1]);
            flow.emplace_back(1.);
            ++i;
            continue;
        }
        size_t j   = i;
        double run = 0.;
        while (j + 1 < n && extruded(j)) {
            run += (points[j + 1].pose.tip - points[j].pose.tip).norm();
            ++j;
        }
        const double taper = std::min(length, 0.25 * run);
        const double piece = taper / 5.;
        double       left  = run;
        for (size_t k = i; k < j; ++k) {
            const ConformalPoint &a = points[k], &b = points[k + 1];
            const double          l = (b.pose.tip - a.pose.tip).norm();
            if (taper <= 0. || left - l >= taper || l <= 0.) {
                out.emplace_back(b);
                flow.emplace_back(1.);
                left -= l;
                continue;
            }
            const double s0    = std::max(0., left - taper);
            double       s     = 0.;
            const int    count = std::max(1, int(std::ceil((l - s0) / piece)));
            if (s0 > 0.) {
                out.emplace_back(interpolate(a, b, s0 / l));
                flow.emplace_back(1.);
                s = s0;
            }
            for (int m = 1; m <= count; ++m) {
                const double e   = s0 + (l - s0) * double(m) / count;
                const double mid = left - 0.5 * (s + e);
                out.emplace_back(m == count ? b : interpolate(a, b, e / l));
                flow.emplace_back(std::clamp(mid / taper, 0., 1.));
                s = e;
            }
            left -= l;
        }
        i = j;
    }
    return out;
}

}

double emit_conformal_extrusion(GCodeWriter &writer, const std::vector<ConformalPoint> &points_in, double width,
                                const ConformalEmitParams &params, float &last_height, std::string &out,
                                const DepositFn &deposit)
{
    if (points_in.size() < 2)
        return 0.;
    const bool rotary = writer.tool_kinematics().rotary_count() > 0;
    std::vector<double>                flow;
    std::vector<ConformalPoint>        tapered;
    if (params.taper_length > 0.)
        tapered = taper_runs(points_in, params.taper_length, flow);
    const std::vector<ConformalPoint> &points = params.taper_length > 0. ? tapered : points_in;

    std::vector<Vec3d> tips(points.size());
    for (size_t i = 0; i < points.size(); ++i)
        tips[i] = points[i].pose.tip;
    const std::vector<double> speeds = limit_feedrates(tips, params.nominal_speed, params.limits);

    double volume = 0.;
    char   buf[64];
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const ConformalPoint &a = points[i];
        const ConformalPoint &b = points[i + 1];
        if ((b.pose.tip - a.pose.tip).norm() < EPSILON)
            continue;
        const Vec3d axis   = (a.pose.axis + b.pose.axis).normalized();
        const float height = 0.5f * (a.bead_height + b.bead_height);
        if (std::abs(height - last_height) > 0.01f) {
            last_height = height;
            std::snprintf(buf, sizeof(buf), ";%s%g\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height).c_str(), last_height);
            out += buf;
        }
        const double v = segment_volume(a.pose.tip, b.pose.tip, width, a.bead_height, b.bead_height, axis) *
                         (flow.empty() ? 1. : flow[i]);
        volume += v;
        const double F = speeds[i] * 60.;
        if (std::abs(writer.get_current_speed() - F) > EPSILON) {
            if (rotary)
                writer.set_pose_speed(F);
            else
                writer.set_speed(out, F, "", params.speed_marker);
        }
        writer.extrude_to_pose(out, b.pose, v * params.e_per_mm3, params.comment);
        if (deposit)
            deposit(a.pose.tip, b.pose.tip, width);
    }
    return volume;
}

bool emit_conformal_wipe(GCodeWriter &writer, const std::vector<ConformalPoint> &points, const ConformalWipeParams &params,
                         std::string &out, Vec3d &end)
{
    end = points.empty() ? writer.get_position() : points.back().pose.tip;
    if (points.size() < 2 || params.distance <= 0. || params.speed <= 0.)
        return false;
    std::vector<ConformalPoint> route{ points.back() };
    double                      length = 0.;
    for (size_t i = points.size() - 1; i > 0 && length < params.distance; --i) {
        const ConformalPoint &a = points[i], &b = points[i - 1];
        if (a.bead_height <= 0.f || b.bead_height <= 0.f)
            break;
        const double l = (b.pose.tip - a.pose.tip).norm();
        if (length + l > params.distance) {
            route.emplace_back(interpolate(a, b, (params.distance - length) / l));
            length = params.distance;
            break;
        }
        route.emplace_back(b);
        length += l;
    }
    if (length <= EPSILON)
        return false;
    const bool rotary = writer.tool_kinematics().rotary_count() > 0;
    out += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Start) + "\n";
    for (size_t i = 0; i + 1 < route.size(); ++i) {
        const Vec3d  d = route[i + 1].pose.tip - route[i].pose.tip;
        const double l = d.norm();
        if (l < EPSILON)
            continue;
        double v = params.speed;
        if (params.limits.max_v_z > 0. && std::abs(d.z()) > EPSILON)
            v = std::min(v, params.limits.max_v_z * l / std::abs(d.z()));
        const double F = v * 60.;
        if (std::abs(writer.get_current_speed() - F) > EPSILON) {
            if (rotary)
                writer.set_pose_speed(F);
            else
                writer.set_speed(out, F, "", params.speed_marker);
        }
        writer.extrude_to_pose(out, route[i + 1].pose, -params.retract * l / length, "wipe on the surface");
    }
    out += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_End) + "\n";
    end = route.back().pose.tip;
    return true;
}

void deposit_material_path(MaterialColumns &columns, const ExtrusionPath &path, const ToWriterFrame &to_writer,
                           const std::function<double(double)> &z_at_fraction)
{
    const double length = z_at_fraction ? path.polyline.length() * SCALING_FACTOR : 0.;
    double distance = 0.;
    Vec3d previous;
    for (size_t i = 0; i < path.polyline.points.size(); ++i) {
        Vec3d current = to_writer(path.polyline.points[i]);
        if (i != 0) distance += (current.head<2>() - previous.head<2>()).norm();
        if (z_at_fraction) current.z() = z_at_fraction(length > 0. ? distance / length : 0.);
        if (i != 0) {
            const int count = std::max(1, int(std::ceil((current - previous).norm() / 0.25)));
            for (int k = 0; k < count; ++k)
                columns.deposit(previous + double(k) / count * (current - previous),
                                previous + double(k + 1) / count * (current - previous), path.width);
        }
        previous = current;
    }
}

namespace {

double periodic_lookup(const std::vector<double> &table, double phi)
{
    const size_t n = table.size();
    double       u = phi / (2. * M_PI) * double(n);
    u -= std::floor(u / double(n)) * double(n);
    const size_t i = size_t(u) % n;
    const double t = u - std::floor(u);
    return (1. - t) * table[i] + t * table[(i + 1) % n];
}

Vec3d rotate_toward(const Vec3d &from, const Vec3d &to, double max_angle)
{
    const double angle = std::acos(std::clamp(from.dot(to), -1., 1.));
    if (angle <= max_angle || angle <= 0.)
        return to;
    const Vec3d axis = from.cross(to);
    if (axis.norm() < 1e-15)
        return from;
    return Eigen::AngleAxisd(max_angle, axis.normalized()) * from;
}

}

void apply_minimal_tilt(std::vector<ConformalPoint> &points, const std::function<Vec2d(const Vec2d &)> &gradient,
                        const ToolKinematics &kinematics, const MinimalTiltParams &params)
{
    if (points.empty() || kinematics.rotary_count() == 0)
        return;

    constexpr int       n_reach = 72;
    std::vector<double> reach(n_reach);
    for (int k = 0; k < n_reach; ++k)
        reach[k] = kinematics.max_tilt(2. * M_PI * k / n_reach);
    auto reachable = [&reach](double phi) {
        double u = phi / (2. * M_PI) * n_reach;
        u -= std::floor(u / n_reach) * n_reach;
        const int i = int(u) % n_reach;
        return std::min(reach[i], reach[(i + 1) % n_reach]);
    };

    const size_t       n = points.size();
    std::vector<Vec3d> normals(n), axes(n);
    for (size_t i = 0; i < n; ++i) {
        const Vec2d  g     = gradient(points[i].pose.tip.head<2>());
        normals[i]         = Vec3d(-g.x(), -g.y(), 1.).normalized();
        const double beta  = std::atan(g.norm());
        if (beta <= 0.) {
            axes[i] = Vec3d::UnitZ();
            continue;
        }
        const double phi   = std::atan2(g.y(), g.x());
        const double alpha = params.slope_limit.empty() ? 0.5 * M_PI : periodic_lookup(params.slope_limit, phi);
        const double tilt  = std::clamp(beta - alpha + params.margin, 0., std::min(beta, reachable(phi + M_PI)));
        axes[i]            = Vec3d(-std::sin(tilt) * std::cos(phi), -std::sin(tilt) * std::sin(phi), std::cos(tilt));
    }

    std::vector<double> s(n, 0.);
    for (size_t i = 1; i < n; ++i)
        s[i] = s[i - 1] + (points[i].pose.tip - points[i - 1].pose.tip).norm();
    if (params.smoothing > 0.) {
        std::vector<Vec3d> smoothed(n);
        size_t             lo = 0, hi = 0;
        Vec3d              sum = Vec3d::Zero();
        for (size_t i = 0; i < n; ++i) {
            while (hi < n && s[hi] <= s[i] + 0.5 * params.smoothing)
                sum += axes[hi++];
            while (s[lo] < s[i] - 0.5 * params.smoothing)
                sum -= axes[lo++];
            smoothed[i] = sum.normalized();
        }
        axes.swap(smoothed);
    }

    if (params.max_rate > 0.) {
        for (size_t i = 1; i < n; ++i)
            axes[i] = rotate_toward(axes[i - 1], axes[i], params.max_rate * (s[i] - s[i - 1]));
        for (size_t i = n - 1; i > 0; --i)
            axes[i - 1] = rotate_toward(axes[i], axes[i - 1], params.max_rate * (s[i] - s[i - 1]));
    }

    for (size_t i = 0; i < n; ++i) {
        const double along   = normals[i].z() / std::max(axes[i].dot(normals[i]), 1e-6);
        points[i].pose.axis  = axes[i];
        points[i].bead_height = float(points[i].bead_height * along);
    }
}

}
