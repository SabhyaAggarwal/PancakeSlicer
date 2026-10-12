#include "ConformalEmit.hpp"
#include "ClearanceVerifier.hpp"

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCodeWriter.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace Slic3r::Nonplanar {

std::vector<ConformalPoint> conformal_points(const ExtrusionPathConformal &path, const ToWriterFrame &to_writer)
{
    const Points3              &pts = path.polyline.points;
    std::vector<ConformalPoint> out(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        out[i].pose.tip     = to_writer(pts[i]);
        out[i].pose.axis    = i < path.tool_axis.size() ? Vec3d(path.tool_axis[i].cast<double>().normalized()) : Vec3d::UnitZ();
        out[i].bead_height  = i < path.bead_height.size() ? path.bead_height[i] : path.height;
        out[i].width        = path.bead_width.size() == pts.size() ? path.bead_width[i] : 0.f;
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
    p.width       = a.width + float(t) * (b.width - a.width);
    return p;
}

std::vector<ConformalPoint> taper_runs(const std::vector<ConformalPoint> &points, double length, double min_piece,
                                       std::vector<double> &flow)
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
        const double piece = std::min(taper, std::max(taper / 5., min_piece));
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

double planner_segment_rate(double configured, GCodeFlavor flavor, bool bbl_printer)
{
    if (configured > 0.)
        return configured;
    if (flavor == gcfKlipper)
        return 400.;
    if (flavor == gcfRepRapFirmware)
        return 250.;
    if (bbl_printer)
        return 200.;
    if (flavor == gcfMarlinFirmware)
        return 120.;
    return 100.;
}

constexpr double ConditionWidthTolerance = 0.02;

void condition_path(std::vector<ConformalPoint> &points, const ConditionParams &params)
{
    const size_t n = points.size();
    if (n < 3)
        return;

    auto chord_ok = [&](size_t i, size_t j) {
        const Vec3d  a = points[i].pose.tip, b = points[j].pose.tip;
        const Vec3d  d = b - a;
        const double len = d.norm();
        const bool extruded = points[i].bead_height > 0.f;
        for (size_t k = i + 1; k <= j; ++k)
            if ((points[k].bead_height > 0.f) != extruded)
                return false;
        if (std::abs(d.z()) > 1e-4 && len > params.max_length)
            return false;
        if (std::acos(std::clamp(points[i].pose.axis.dot(points[j].pose.axis), -1., 1.)) > params.axis_tolerance)
            return false;
        const bool   loose = len < params.target_length;
        const double tol   = loose ? params.min_tolerance : params.tolerance;
        const double ztol  = extruded ? (loose ? params.min_z_tolerance : params.z_tolerance) : params.glide_z_tolerance;
        const Vec2d  dxy   = d.head<2>();
        const double l2    = dxy.squaredNorm();
        for (size_t k = i + 1; k < j; ++k) {
            const Vec3d &p = points[k].pose.tip;
            const double t = l2 > 0. ? std::clamp((p - a).head<2>().dot(dxy) / l2, 0., 1.) : 0.;
            const Vec3d  q = a + t * d;
            if ((p - q).head<2>().norm() > tol || std::abs(p.z() - q.z()) > ztol)
                return false;
            const double bead = (1. - t) * points[i].bead_height + t * points[j].bead_height;
            if (std::abs(points[k].bead_height - bead) > params.bead_tolerance)
                return false;
            const Vec3d axis = ((1. - t) * points[i].pose.axis + t * points[j].pose.axis).normalized();
            if (std::acos(std::clamp(axis.dot(points[k].pose.axis), -1., 1.)) > params.axis_tolerance)
                return false;
            if (std::abs(double(points[k].width) - ((1. - t) * double(points[i].width) + t * double(points[j].width))) > ConditionWidthTolerance)
                return false;
        }
        return true;
    };
    std::vector<ConformalPoint> out;
    out.reserve(n);
    out.push_back(points.front());
    for (size_t i = 0; i + 1 < n;) {
        size_t j = i + 1;
        while (j + 1 < n && chord_ok(i, j + 1))
            ++j;
        out.push_back(points[j]);
        i = j;
    }
    points.swap(out);
}

double emit_conformal_extrusion(GCodeWriter &writer, const std::vector<ConformalPoint> &points_in, double width,
                                const ConformalEmitParams &params, float &last_height, std::string &out,
                                const DepositFn &deposit)
{
    if (points_in.size() < 2)
        return 0.;
    std::vector<ConformalPoint> conditioned;
    if (params.condition.target_length > 0.) {
        conditioned = points_in;
        condition_path(conditioned, params.condition);
    }
    const std::vector<ConformalPoint> &merged = params.condition.target_length > 0. ? conditioned : points_in;
    const bool rotary = writer.tool_kinematics().rotary_count() > 0;
    std::vector<double>                flow;
    std::vector<ConformalPoint>        tapered;
    if (params.taper_length > 0.)
        tapered = taper_runs(merged, params.taper_length, params.condition.target_length, flow);
    const std::vector<ConformalPoint> &staged = params.taper_length > 0. ? tapered : merged;

    constexpr double            StubLength = 0.01;
    std::vector<ConformalPoint> points;
    std::vector<double>         stub_flow;
    points.reserve(staged.size());
    points.emplace_back(staged.front());
    for (size_t i = 1; i < staged.size(); ++i) {
        const bool last = i + 1 == staged.size();
        if ((staged[i].pose.tip - points.back().pose.tip).norm() >= StubLength) {
            points.emplace_back(staged[i]);
            if (!flow.empty())
                stub_flow.emplace_back(flow[i - 1]);
        } else if (last && points.size() > 1) {
            points.back() = staged[i];
            if (!flow.empty())
                stub_flow.back() = std::min(stub_flow.back(), flow[i - 1]);
        }
    }
    if (points.size() < 2) {
        points.emplace_back(staged.back());
        if (!flow.empty())
            stub_flow.emplace_back(flow.back());
    }
    flow.swap(stub_flow);

    std::vector<Vec3d> tips(points.size());
    for (size_t i = 0; i < points.size(); ++i)
        tips[i] = (points[i].pose.tip * 1000.).array().round() / 1000.;
    auto glide = [&points](size_t i) { return points[i].bead_height <= 0.f && points[i + 1].bead_height <= 0.f; };
    std::vector<double> nominal(points.size() - 1, params.nominal_speed);
    for (size_t i = 0; i < nominal.size(); ++i)
        if (glide(i))
            nominal[i] = std::max(params.nominal_speed, params.glide_speed);
    const std::vector<double> caps   = params.speed_caps ? params.speed_caps(points) : std::vector<double>();
    const std::vector<double> speeds = limit_feedrates(tips, nominal, params.limits, 0.05, caps.empty() ? nullptr : &caps);

    double volume = 0.;
    char   buf[64];
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const ConformalPoint &a = points[i];
        const ConformalPoint &b = points[i + 1];
        if ((b.pose.tip - a.pose.tip).norm() < EPSILON)
            continue;
        const Vec3d axis   = (a.pose.axis + b.pose.axis).normalized();
        const float height = 0.5f * (a.bead_height + b.bead_height);
        const double seg_width = a.width > 0.f && b.width > 0.f ? 0.5 * (double(a.width) + double(b.width)) : width;
        if (params.last_width != nullptr && std::abs(float(seg_width) - *params.last_width) > 0.01f) {
            *params.last_width = float(seg_width);
            std::snprintf(buf, sizeof(buf), ";%s%g\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Width).c_str(), *params.last_width);
            out += buf;
        }
        if (std::abs(height - last_height) > 0.01f) {
            last_height = height;
            std::snprintf(buf, sizeof(buf), ";%s%g\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height).c_str(), last_height);
            out += buf;
        }
        const double v = segment_volume(a.pose.tip, b.pose.tip, seg_width, a.bead_height, b.bead_height, axis) *
                         (flow.empty() ? 1. : flow[i]);
        volume += v;
        double speed = speeds[i];
        if (params.max_volume_per_second > 0. && v > 0.)
            speed = std::min(speed, params.max_volume_per_second * (b.pose.tip - a.pose.tip).norm() / v);
        const double F = speed * 60.;
        if (std::abs(writer.get_current_speed() - F) > EPSILON) {
            if (rotary)
                writer.set_pose_speed(F);
            else
                writer.set_speed(out, F, "", glide(i) ? std::string() : params.speed_marker);
        }
        writer.extrude_to_pose(out, b.pose, v * params.e_per_mm3, params.comment);
        if (deposit && !glide(i))
            deposit(a.pose.tip, b.pose.tip, seg_width);
    }
    return volume;
}

std::vector<double> conformal_speed_caps(const std::vector<ConformalPoint> &points, double width,
                                         const std::function<Vec2d(const Vec2d &)> &gradient, const ToolKinematics &kinematics,
                                         const JointPose &start, const Vec3d &writer_offset, const ConformalSpeedParams &params)
{
    constexpr double    inf = std::numeric_limits<double>::infinity();
    std::vector<double> caps(points.size() < 2 ? 0 : points.size() - 1, inf);
    const bool          rotary = kinematics.rotary_count() > 0;
    JointPose           prev   = start;
    bool                joints = rotary;
    for (size_t i = 0; i < caps.size(); ++i) {
        const ConformalPoint &a = points[i], &b = points[i + 1];
        const double          len = (b.pose.tip - a.pose.tip).norm();
        if (len < EPSILON)
            continue;
        const Vec3d  axis   = (a.pose.axis + b.pose.axis).normalized();
        const double volume = segment_volume(a.pose.tip, b.pose.tip, width, a.bead_height, b.bead_height, axis);
        if (params.max_volumetric > 0. && volume > 0.)
            caps[i] = std::min(caps[i], params.max_volumetric * len / volume);
        if (params.reference_height > 0. && volume > 0. && gradient) {
            const Vec2d  g      = gradient(0.5 * (a.pose.tip + b.pose.tip).head<2>());
            const Vec3d  normal = Vec3d(-g.x(), -g.y(), 1.).normalized();
            const double normal_thickness = 0.5 * (a.bead_height + b.bead_height) * std::max(0., axis.dot(normal));
            caps[i] = std::min(caps[i], params.nominal_speed * std::clamp(normal_thickness / params.reference_height, params.min_ratio, 1.));
        }
        if (joints) {
            JointPose next;
            if (!kinematics.inverse({ b.pose.tip - writer_offset, b.pose.axis.normalized() }, &prev, next)) {
                joints = false;
                continue;
            }
            const std::array<RotaryAxis, 2> &axes = kinematics.rotary_axes();
            for (int k = 0; k < 2; ++k)
                if (const double d = std::abs(next.rot[k] - prev.rot[k]); axes[k].max_speed_deg_s > 0. && d > 1e-9)
                    caps[i] = std::min(caps[i], axes[k].max_speed_deg_s * len / d);
            for (int k = 0; k < 3; ++k)
                if (const double d = std::abs(next.xyz[k] - prev.xyz[k]); params.max_joint_speed[k] > 0. && d > 1e-9)
                    caps[i] = std::min(caps[i], params.max_joint_speed[k] * len / d);
            prev = next;
        }
    }
    return caps;
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
    for (size_t i = 1; i + 1 < route.size();)
        if ((route[i].pose.tip - route[i - 1].pose.tip).norm() < 0.01)
            route.erase(route.begin() + i);
        else
            ++i;
    if (route.size() > 2 && (route.back().pose.tip - route[route.size() - 2].pose.tip).norm() < 0.01)
        route.erase(route.end() - 2);
    const bool rotary = writer.tool_kinematics().rotary_count() > 0;
    out += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Start) + "\n";
    for (size_t i = 0; i + 1 < route.size(); ++i) {
        const Vec3d  d = Vec3d((route[i + 1].pose.tip * 1000.).array().round() / 1000.) -
                         Vec3d((route[i].pose.tip * 1000.).array().round() / 1000.);
        const double l = (route[i + 1].pose.tip - route[i].pose.tip).norm();
        if (l < EPSILON)
            continue;
        double v = params.speed;
        if (params.limits.max_v_z > 0. && std::abs(d.z()) > EPSILON)
            v = std::min(v, params.limits.max_v_z * d.norm() / std::abs(d.z()));
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
