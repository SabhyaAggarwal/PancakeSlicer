#include "ConformalEmit.hpp"
#include "ClearanceVerifier.hpp"

#include "libslic3r/Nonplanar/ShellSeams.hpp"

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCodeWriter.hpp"
#include "libslic3r/Nonplanar/TiltBudget.hpp"
#include "libslic3r/libslic3r.h"

#include <Eigen/Geometry>

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {
constexpr float TagStep = 0.002f;
}

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

double printed_length(const Vec3d &a, const Vec3d &b)
{
    return (Vec3d((b * 1000.).array().round() / 1000.) - Vec3d((a * 1000.).array().round() / 1000.)).norm();
}

double axis_limited_speed(const Vec3d &a, const Vec3d &b, double v, const MotionLimits &limits)
{
    const Vec3d  d = (Vec3d(GCodeFormatter::quantize_xyzf(b.x()), GCodeFormatter::quantize_xyzf(b.y()), GCodeFormatter::quantize_xyzf(b.z())) -
                      Vec3d(GCodeFormatter::quantize_xyzf(a.x()), GCodeFormatter::quantize_xyzf(a.y()), GCodeFormatter::quantize_xyzf(a.z())));
    const double l = d.norm();
    if (l < EPSILON)
        return v;
    if (limits.max_v_z > 0. && std::abs(d.z()) > EPSILON)
        v = std::min(v, limits.max_v_z * l / std::abs(d.z()));
    const double xy = std::max(std::abs(d.x()), std::abs(d.y()));
    if (limits.max_v_xy > 0. && xy > EPSILON)
        v = std::min(v, limits.max_v_xy * l / xy);
    return v;
}

ConformalPoint interpolate(const ConformalPoint &a, const ConformalPoint &b, double t)
{
    ConformalPoint p;
    p.pose.tip    = a.pose.tip + t * (b.pose.tip - a.pose.tip);
    p.pose.axis   = (a.pose.axis + t * (b.pose.axis - a.pose.axis)).normalized();
    p.bead_height = a.bead_height + float(t) * (b.bead_height - a.bead_height);
    p.vertical_height = a.vertical_height + float(t) * (b.vertical_height - a.vertical_height);
    p.width       = a.width + float(t) * (b.width - a.width);
    return p;
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
    std::vector<ConformalPoint>        shaped;
    const bool shape = params.ramp_length > 0. || params.taper_length > 0. || params.coast_length > 0.;
    if (shape)
        shaped = shape_runs(merged, { params.ramp_length, params.taper_length, params.coast_length, params.open_end, params.scored },
                            params.condition.target_length, flow);
    const std::vector<ConformalPoint> &staged = shape ? shaped : merged;

    std::vector<ConformalPoint> points;
    if (params.preserve_points)
        points = staged;
    else {
        constexpr double            StubLength = 0.01;
        std::vector<double>         stub_flow;
        points.reserve(staged.size());
        points.emplace_back(staged.front());
        for (size_t i = 1; i < staged.size(); ++i) {
            const bool last = i + 1 == staged.size();
            if ((staged[i].pose.tip - points.back().pose.tip).norm() >= StubLength) {
                points.emplace_back(staged[i]);
                if (!flow.empty())
                    stub_flow.emplace_back(flow[i - 1]);
            } else if (points.back().bead_height <= 0.f && staged[i].bead_height > 0.f) {
                points.back() = staged[i];
            } else if (points.size() > 1 &&
                       (last || (staged[i].bead_height > 0.f) != (staged[i + 1].bead_height > 0.f))) {
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

        {
            std::vector<ConformalPoint> moves{ points.front() };
            std::vector<double>         move_flow;
            for (size_t i = 0; i + 1 < points.size(); ++i) {
                const ConformalPoint &a = moves.back();
                const ConformalPoint &b = points[i + 1];
                const double          l = (b.pose.tip - a.pose.tip).norm();
                if (l < EPSILON || (l < ConformalMinSegment && i + 2 < points.size() && (points[i + 2].bead_height > 0.f) == (b.bead_height > 0.f) &&
                                    (a.bead_height > 0.f) == (b.bead_height > 0.f)))
                    continue;
                if (!flow.empty())
                    move_flow.emplace_back(flow[i]);
                moves.emplace_back(b);
            }
            if (moves.size() < 2)
                return 0.;
            points.swap(moves);
            flow.swap(move_flow);
        }

    }

    std::vector<Vec3d> tips(points.size());
    for (size_t i = 0; i < points.size(); ++i)
        tips[i] = (points[i].pose.tip * 1000.).array().round() / 1000.;
    auto glide = [&points](size_t i) { return points[i].bead_height <= 0.f && points[i + 1].bead_height <= 0.f; };
    std::vector<double> nominal(points.size() - 1, params.nominal_speed);
    std::vector<bool>   glides(points.size() - 1, false);
    for (size_t i = 0; i < nominal.size(); ++i)
        if (glide(i)) {
            nominal[i] = std::max(params.nominal_speed, params.glide_speed);
            glides[i]  = true;
        }
    const std::vector<double> caps   = params.speed_caps ? params.speed_caps(points) : std::vector<double>();
    const std::vector<double> speeds = limit_feedrates(tips, nominal, params.limits, 0.05, caps.empty() ? nullptr : &caps, &glides);

    const std::vector<std::string> tags = params.segment_tags ? params.segment_tags(points, speeds) : std::vector<std::string>();
    std::vector<double> prime(points.size() - 1, 0.);
    auto primes = [&](size_t i) {
        return points[i].bead_height > 0.f && points[i + 1].bead_height > 0.f && (points[i + 1].pose.tip - points[i].pose.tip).norm() >= 0.05;
    };
    if (params.prime > 0.) {
        double road = 0.;
        for (size_t i = 0; i + 1 < points.size(); ++i)
            if (primes(i))
                road += (points[i + 1].pose.tip - points[i].pose.tip).norm();
        const double length = std::min(road, std::max(params.prime_length, EPSILON));
        if (length <= EPSILON) {
            if (params.prime_speed > 0.) {
                if (rotary)
                    writer.set_pose_speed(params.prime_speed * 60.);
                else
                    writer.set_speed(out, params.prime_speed * 60.);
            }
            writer.extrude_to_pose(out, points.front().pose, params.prime, "prime");
        }
        else {
            double done = 0.;
            for (size_t i = 0; i + 1 < points.size() && done < length; ++i)
                if (primes(i)) {
                    const double run = std::min((points[i + 1].pose.tip - points[i].pose.tip).norm(), length - done);
                    prime[i] = params.prime * run / length;
                    done += run;
                }
        }
    }

    double volume = 0.;
    char   buf[64];
    float last_vertical_height = -1.f;
    bool   speed_block_open = ! params.speed_marker.empty();
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        double                speed = speeds[i];
        const ConformalPoint &a = points[i];
        const ConformalPoint &b = points[i + 1];
        if ((b.pose.tip - a.pose.tip).norm() < EPSILON)
            continue;
        if (i < tags.size())
            out += tags[i];
        const float height = 0.5f * (a.bead_height + b.bead_height);
        const double seg_width = a.width > 0.f && b.width > 0.f ? 0.5 * (double(a.width) + double(b.width)) : width;
        if (params.last_width != nullptr && std::abs(float(seg_width) - *params.last_width) > TagStep) {
            *params.last_width = float(seg_width);
            std::snprintf(buf, sizeof(buf), ";%s%g\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Width).c_str(), *params.last_width);
            out += buf;
        }
        if (std::abs(height - last_height) > TagStep) {
            last_height = height;
            std::snprintf(buf, sizeof(buf), ";%s%g\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height).c_str(), last_height);
            out += buf;
        }
        const float vertical_height = a.vertical_height > 0.f && b.vertical_height > 0.f ?
                                          0.5f * (a.vertical_height + b.vertical_height) : 0.f;
        if (rotary && std::abs(vertical_height - last_vertical_height) > TagStep) {
            last_vertical_height = vertical_height;
            std::snprintf(buf, sizeof(buf), ";_NONPLANAR_VERTICAL_HEIGHT:%g\n", vertical_height);
            out += buf;
        }
        if (params.pressure_advance != nullptr && !glide(i))
            params.pressure_advance->update(height, out);
        const Vec3d axis = (a.pose.axis + b.pose.axis).normalized();
        const double v = a.bead_height <= 0.f || b.bead_height <= 0.f ? 0. :
                         (a.vertical_height > 0.f && b.vertical_height > 0.f ?
                              segment_volume(a.pose.tip, b.pose.tip, seg_width, a.vertical_height, b.vertical_height) :
                              segment_volume(a.pose.tip, b.pose.tip, seg_width, a.bead_height, b.bead_height, axis)) *
                             (flow.empty() ? 1. : flow[i]);
        volume += v;
        if (params.max_volume_per_second > 0. && v > 0.)
            speed = std::min(speed, params.max_volume_per_second * (b.pose.tip - a.pose.tip).norm() / v);
        const double dE = v * params.e_per_mm3 + prime[i];
        if (prime[i] > 0. && params.prime_speed > 0.)
            speed = std::min(speed, params.prime_speed * printed_length(a.pose.tip, b.pose.tip) / dE);
        if (params.limits.segment_rate > 0.) {
            double length = (b.pose.tip - a.pose.tip).norm();
            if (params.preserve_points && !rotary)
                if (const double printed = printed_length(a.pose.tip, b.pose.tip); printed > EPSILON)
                    length = std::min(length, printed);
            speed = std::min(speed, params.limits.segment_rate * length);
        }
        const double F = (rotary ? speed : axis_limited_speed(a.pose.tip, b.pose.tip, speed, params.limits)) * 60.;
        if (std::abs(writer.get_current_speed() - F) > EPSILON) {
            if (rotary)
                writer.set_pose_speed(F);
            else {
                const std::string &marker = glide(i) ? std::string() : params.speed_marker;
                if (marker.empty() && speed_block_open)
                    out += ";_EXTRUDE_END\n";
                speed_block_open = ! marker.empty();
                writer.set_speed(out, F, "", marker);
            }
        }
        writer.extrude_to_pose(out, b.pose, dE, params.comment);
        if (deposit && v > 0.)
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
            const double thin = params.nominal_speed * std::clamp(normal_thickness / params.reference_height, params.min_ratio, 1.);
            caps[i] = std::min(caps[i], std::max(thin, std::min(params.min_speed, params.nominal_speed)));
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
    const bool                  rotary_route = writer.tool_kinematics().rotary_count() > 0;
    std::vector<ConformalPoint> kept{ route.front() };
    for (size_t i = 1; i < route.size(); ++i) {
        const Vec3d &a = kept.back().pose.tip, &p = route[i].pose.tip;
        if ((p - a).norm() < ConformalMinSegment) {
            if (i + 1 == route.size() && kept.size() > 1)
                kept.back() = route[i];
            continue;
        }
        if (i + 1 < route.size() && ! rotary_route) {
            const Vec3d  c = route[i + 1].pose.tip - a;
            const double t = c.squaredNorm() > 0. ? std::clamp((p - a).dot(c) / c.squaredNorm(), 0., 1.) : 0.;
            if ((a + t * c - p).norm() < 0.01)
                continue;
        }
        kept.emplace_back(route[i]);
    }
    route.swap(kept);
    if (route.size() < 2)
        return false;
    const bool rotary = writer.tool_kinematics().rotary_count() > 0;
    length = 0.;
    for (size_t i = 0; i + 1 < route.size(); ++i)
        length += rotary ? (route[i + 1].pose.tip - route[i].pose.tip).norm() : printed_length(route[i].pose.tip, route[i + 1].pose.tip);
    if (length < ConformalMinSegment)
        return false;
    out += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Start) + "\n";
    for (size_t i = 0; i + 1 < route.size(); ++i) {
        const Vec3d  d = Vec3d((route[i + 1].pose.tip * 1000.).array().round() / 1000.) -
                         Vec3d((route[i].pose.tip * 1000.).array().round() / 1000.);
        const double l = rotary ? (route[i + 1].pose.tip - route[i].pose.tip).norm() : d.norm();
        if (l < EPSILON)
            continue;
        double v = rotary ? params.speed : axis_limited_speed(route[i].pose.tip, route[i + 1].pose.tip, params.speed, params.limits);
        if (rotary && params.limits.max_v_z > 0. && std::abs(d.z()) > EPSILON)
            v = std::min(v, params.limits.max_v_z * l / std::abs(d.z()));
        if (params.max_v_e > 0. && params.retract > EPSILON)
            v = std::min(v, params.max_v_e * length / params.retract);
        if (params.limits.segment_rate > 0.)
            v = std::min(v, l * params.limits.segment_rate);
        const double F = v * 60.;
        if (i == 0 || std::abs(writer.get_current_speed() - F) > EPSILON) {
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

bool densify_conformal_path(const ExtrusionPathConformal &path, double max_segment, ExtrusionPathConformal &out)
{
    const Points3 &pts  = path.polyline.points;
    const double   step = scale_(max_segment);
    bool           long_segment = false;
    for (size_t i = 1; i < pts.size() && ! long_segment; ++i)
        long_segment = (pts[i] - pts[i - 1]).cast<double>().norm() > step;
    if (! long_segment)
        return false;
    out = path;
    Points3            &dense_pts = out.polyline.points;
    std::vector<float> &heights   = out.bead_height;
    std::vector<Vec3f> &axes      = out.tool_axis;
    dense_pts.clear(), heights.clear(), axes.clear();
    for (size_t i = 0; i < pts.size(); ++i) {
        const float h = i < path.bead_height.size() ? path.bead_height[i] : path.height;
        if (i > 0) {
            const int n = int(std::ceil((pts[i] - pts[i - 1]).cast<double>().norm() / step));
            const float previous_height = i - 1 < path.bead_height.size() ? path.bead_height[i - 1] : path.height;
            for (int k = 1; k < n; ++k) {
                const double t = double(k) / n;
                dense_pts.emplace_back((pts[i - 1].cast<double>() + t * (pts[i] - pts[i - 1]).cast<double>()).cast<coord_t>());
                heights.emplace_back(previous_height <= 0.f || h <= 0.f ? 0.f : float((1. - t) * previous_height + t * h));
                if (! path.tool_axis.empty())
                    axes.emplace_back(((1. - t) * path.tool_axis[i - 1] + t * path.tool_axis[i]).normalized());
            }
        }
        dense_pts.emplace_back(pts[i]);
        heights.emplace_back(h);
        if (! path.tool_axis.empty())
            axes.emplace_back(path.tool_axis[i]);
    }
    return true;
}

std::vector<RotaryRun> plan_rotary_runs(const ToolKinematics &kinematics, const std::vector<ConformalPoint> &points,
                                        const JointPose &from, double max_jump_deg)
{
    std::vector<RotaryRun> runs;
    RotaryRun              run;
    run.seed = from;
    run.end  = points.empty() ? 0 : points.size() - 1;
    const auto *rotary = dynamic_cast<const RotaryKinematics *>(&kinematics);
    JointPose   prev;
    if (rotary == nullptr || points.size() < 2 || ! kinematics.inverse(points.front().pose, &from, prev))
        return { run };
    auto other_branch = [rotary](const JointPose &j) {
        const std::array<double, 2> kin = rotary->kinematic_angles(j);
        JointPose                   out = j;
        out.rot[0] = rotary->machine_angle(0, -kin[0]);
        out.rot[1] = rotary->machine_angle(1, kin[1] + M_PI);
        return out;
    };
    auto jump = [](const JointPose &a, const JointPose &b) { return std::max(std::abs(a.rot[0] - b.rot[0]), std::abs(a.rot[1] - b.rot[1])); };
    for (size_t i = 1; i < points.size(); ++i) {
        JointPose next;
        if (! kinematics.inverse(points[i].pose, &prev, next))
            break;
        if (jump(prev, next) > max_jump_deg) {
            const JointPose seed = other_branch(prev);
            JointPose       start, after;
            if (kinematics.inverse(points[i - 1].pose, &seed, start) && kinematics.inverse(points[i].pose, &start, after) &&
                jump(start, after) <= max_jump_deg) {
                if (i - 1 == run.begin)
                    run.seed = seed;
                else {
                    run.end = i - 1;
                    runs.emplace_back(run);
                    run.begin = i - 1;
                    run.seed  = seed;
                }
                next = after;
            }
        }
        prev = next;
    }
    run.end = points.size() - 1;
    runs.emplace_back(run);
    return runs;
}

namespace {

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

    const TiltBudget   budget(kinematics, params.slope_limit, params.margin, params.max_tilt, params.max_residual, params.residual_from);
    const size_t       n = points.size();
    std::vector<Vec3d> normals(n), axes(n);
    for (size_t i = 0; i < n; ++i) {
        const Vec2d g = gradient(points[i].pose.tip.head<2>());
        if (!g.allFinite())
            throw RuntimeError("Non-planar rotary path has no finite surface gradient. Repair the model or regenerate its non-planar paths.");
        normals[i]    = Vec3d(-g.x(), -g.y(), 1.).normalized();
        axes[i]       = budget.axis(g);
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

    if (params.clearance) {
        auto tilt_of = [](const Vec3d &axis) { return std::acos(std::clamp(axis.z(), -1., 1.)); };
        auto shrink = [&tilt_of](const Vec3d &axis, double tilt) {
            const double full = tilt_of(axis);
            if (full <= tilt)
                return axis;
            return Vec3d(Eigen::Quaterniond::Identity().slerp(tilt / full, Eigen::Quaterniond::FromTwoVectors(Vec3d::UnitZ(), axis)) *
                         Vec3d::UnitZ());
        };
        std::vector<Vec3d> cleared(n, Vec3d::UnitZ());
        for (int round = 0; round < 16; ++round) {
            bool changed = false;
            for (size_t i = 0; i < n; ++i) {
                if (axes[i].z() > 1. - EPSILON || axes[i] == cleared[i])
                    continue;
                const Vec3d &tip = points[i].pose.tip;
                if (params.clearance(tip, axes[i]) >= params.clearance_margin) {
                    cleared[i] = axes[i];
                    continue;
                }
                const double floor = params.clearance_margin - 1e-3;
                const Vec3d  full  = axes[i];
                axes[i]            = Vec3d::UnitZ();
                for (int k = 3; k > 0; --k) {
                    const Vec3d axis = shrink(full, 0.25 * k * tilt_of(full));
                    if (params.clearance(tip, axis) >= floor) {
                        axes[i] = axis;
                        break;
                    }
                }
                cleared[i] = axes[i];
                changed    = true;
            }
            if (! changed || params.max_rate <= 0.)
                break;
            for (size_t i = 1; i < n; ++i)
                axes[i] = shrink(axes[i], tilt_of(axes[i - 1]) + params.max_rate * (s[i] - s[i - 1]));
            for (size_t i = n - 1; i > 0; --i)
                axes[i - 1] = shrink(axes[i - 1], tilt_of(axes[i]) + params.max_rate * (s[i] - s[i - 1]));
        }
        for (size_t i = 0; i < n; ++i)
            if (axes[i] != cleared[i])
                axes[i] = Vec3d::UnitZ();
    }

    for (size_t i = 0; i < n; ++i) {
        const double along   = std::min(normals[i].z() / std::max(axes[i].dot(normals[i]), 1e-6), 1. / std::max(axes[i].z(), 1e-6));
        points[i].vertical_height = points[i].bead_height;
        if (points[i].bead_height <= 0.f && i > 0 && i + 1 < n)
            points[i].pose.tip += GlideLift * (axes[i] - points[i].pose.axis);
        points[i].pose.axis  = axes[i];
        points[i].bead_height = float(points[i].bead_height * along);
    }
}

}
