#include "ToolKinematics.hpp"

#include "libslic3r/Exception.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/format.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace Slic3r {

namespace {

constexpr double SingularXY = 1e-12;

Matrix3d rot_x(double t) { return Eigen::AngleAxisd(t, Vec3d::UnitX()).toRotationMatrix(); }
Matrix3d rot_y(double t) { return Eigen::AngleAxisd(t, Vec3d::UnitY()).toRotationMatrix(); }
Matrix3d rot_z(double t) { return Eigen::AngleAxisd(t, Vec3d::UnitZ()).toRotationMatrix(); }

double wrap_pi(double a) { return std::remainder(a, 2. * M_PI); }

JointPose lerp(const JointPose &a, const JointPose &b, double t)
{
    JointPose out;
    out.xyz = a.xyz + t * (b.xyz - a.xyz);
    for (int i = 0; i < 2; ++i)
        out.rot[i] = a.rot[i] + t * (b.rot[i] - a.rot[i]);
    return out;
}

}

void ToolKinematics::linearize(const JointPose &ja, const ToolPose &pa, const ToolPose &pb, double tol,
                               std::vector<JointPose> &out) const
{
    auto solve = [this](const ToolPose &pose, const JointPose &prev) {
        JointPose j;
        if (!this->inverse(pose, &prev, j))
            throw RuntimeError(Slic3r::format("Non-planar: the tool cannot reach the pose at X%.3f Y%.3f Z%.3f "
                                              "with this printer's rotary axis limits.",
                                              pose.tip.x(), pose.tip.y(), pose.tip.z()));
        return j;
    };
    const JointPose jb = solve(pb, ja);
    if (this->rotary_count() == 0 || this->firmware_rtcp()) {
        out.push_back(jb);
        return;
    }
    constexpr int max_depth = 16;
    std::function<void(const JointPose &, const ToolPose &, const JointPose &, const ToolPose &, int)> subdivide =
        [&](const JointPose &j0, const ToolPose &p0, const JointPose &j1, const ToolPose &p1, int depth) {
            bool ok = true;
            for (double u : { 0.25, 0.5, 0.75 })
                if ((this->forward(lerp(j0, j1, u)).tip - (p0.tip + u * (p1.tip - p0.tip))).norm() > tol) {
                    ok = false;
                    break;
                }
            if (ok) {
                out.push_back(j1);
                return;
            }
            if (depth >= max_depth)
                throw RuntimeError("Non-planar: a rotary move needs too many subdivisions to stay on the tool path.");
            JointPose turned = lerp(j0, j1, 0.5);
            ToolPose  pm;
            pm.tip  = 0.5 * (p0.tip + p1.tip);
            pm.axis = this->forward(turned).axis;
            const JointPose jm = solve(pm, turned);
            subdivide(j0, p0, jm, pm, depth + 1);
            subdivide(jm, pm, j1, p1, depth + 1);
        };
    subdivide(ja, pa, jb, pb, 0);
}

bool ThreeAxisKinematics::inverse(const ToolPose &pose, const JointPose *, JointPose &out) const
{
    out.xyz = pose.tip;
    out.rot = { 0., 0. };
    return pose.axis.z() > 1. - 1e-9;
}

ToolPose ThreeAxisKinematics::forward(const JointPose &joints) const
{
    ToolPose pose;
    pose.tip = joints.xyz;
    return pose;
}

double RotaryKinematics::machine_angle(int axis, double kinematic_rad) const
{
    return m_geo.home[axis] + (m_geo.reversed[axis] ? -1. : 1.) * Geometry::rad2deg(kinematic_rad);
}

std::array<double, 2> RotaryKinematics::kinematic_angles(const JointPose &joints) const
{
    std::array<double, 2> out;
    for (int i = 0; i < 2; ++i)
        out[i] = Geometry::deg2rad((joints.rot[i] - m_geo.home[i]) * (m_geo.reversed[i] ? -1. : 1.));
    return out;
}

bool RotaryKinematics::inverse(const ToolPose &pose, const JointPose *prev, JointPose &out) const
{
    const Vec3d axis = pose.axis.normalized();
    if (std::atan2(axis.head<2>().norm(), axis.z()) > m_geo.max_tilt + 1e-9)
        return false;

    JointPose upright;
    for (int i = 0; i < 2; ++i)
        upright.rot[i] = this->machine_angle(i, 0.);
    const JointPose            &from      = prev ? *prev : upright;
    const std::array<double, 2> from_kin  = this->kinematic_angles(from);
    const int                   free      = this->free_axis();

    std::vector<std::array<double, 2>> solutions;
    this->solve_axis(axis, free >= 0 ? from_kin[free] : 0., solutions);

    double                best_cost = std::numeric_limits<double>::infinity();
    std::array<double, 2> best_rot { 0., 0. };
    for (const std::array<double, 2> &kin : solutions) {
        std::array<std::vector<double>, 2> options;
        for (int i = 0; i < 2; ++i) {
            const RotaryAxis &ra = m_geo.axes[i];
            const double      m  = this->machine_angle(i, kin[i]);
            if (ra.continuous)
                options[i].push_back(m + 360. * std::round((from.rot[i] - m) / 360.));
            else {
                const double first_turn = std::ceil((ra.min_deg - m - 1e-9) / 360.);
                const double last_turn  = std::floor((ra.max_deg - m + 1e-9) / 360.);
                if (first_turn <= last_turn) {
                    const double turn = std::clamp(std::round((from.rot[i] - m) / 360.), first_turn, last_turn);
                    options[i].push_back(m + 360. * turn);
                }
            }
        }
        for (double m0 : options[0])
            for (double m1 : options[1]) {
                const std::array<double, 2> m { m0, m1 };
                double worst = 0., sum = 0.;
                for (int i = 0; i < 2; ++i) {
                    const double speed = m_geo.axes[i].max_speed_deg_s > 0. ? m_geo.axes[i].max_speed_deg_s : 1.;
                    const double t     = std::abs(m[i] - from.rot[i]) / speed;
                    worst              = std::max(worst, t);
                    sum += t;
                }
                const double cost = worst + 1e-3 * sum;
                if (cost < best_cost) {
                    best_cost = cost;
                    best_rot  = m;
                }
            }
    }
    if (!std::isfinite(best_cost))
        return false;

    out.rot             = best_rot;
    const auto     kin  = this->kinematic_angles(out);
    const Matrix3d R    = this->rotation(kin[0], kin[1]);
    if (m_geo.firmware_rtcp) {
        out.xyz = pose.tip;
    } else if (this->is_table()) {
        const Vec3d r    = R * (pose.tip + Vec3d(0., 0., m_geo.table_height));
        const Vec3d tip0 = m_geo.tool_mount - m_geo.tool_length * Vec3d::UnitZ();
        for (int i = 0; i < 3; ++i)
            out.xyz[i] = (m_geo.linear_moves_table[i] ? -1. : 1.) * (r[i] + m_geo.pivot[i] - tip0[i]);
    } else {
        out.xyz = pose.tip + m_geo.tool_length * (R * Vec3d::UnitZ() - Vec3d::UnitZ());
    }
    return true;
}

ToolPose RotaryKinematics::forward(const JointPose &joints) const
{
    const auto     kin = this->kinematic_angles(joints);
    const Matrix3d R   = this->rotation(kin[0], kin[1]);
    ToolPose       pose;
    if (this->is_table()) {
        pose.axis = R.transpose() * Vec3d::UnitZ();
        if (m_geo.firmware_rtcp) {
            pose.tip = joints.xyz;
        } else {
            const Vec3d tip0 = m_geo.tool_mount - m_geo.tool_length * Vec3d::UnitZ();
            Vec3d       r;
            for (int i = 0; i < 3; ++i)
                r[i] = (m_geo.linear_moves_table[i] ? -1. : 1.) * joints.xyz[i] - m_geo.pivot[i] + tip0[i];
            pose.tip = R.transpose() * r - Vec3d(0., 0., m_geo.table_height);
        }
    } else {
        pose.axis = R * Vec3d::UnitZ();
        pose.tip  = m_geo.firmware_rtcp ? joints.xyz : Vec3d(joints.xyz - m_geo.tool_length * (pose.axis - Vec3d::UnitZ()));
    }
    return pose;
}

Matrix3d RotaryKinematics::head_frame(const JointPose &joints) const
{
    const auto     kin = this->kinematic_angles(joints);
    const Matrix3d R   = this->rotation(kin[0], kin[1]);
    return this->is_table() ? R : Matrix3d(R.transpose());
}

double RotaryKinematics::max_tilt(double phi) const
{
    auto reachable = [this, phi](double tilt) {
        ToolPose  pose;
        pose.axis = Vec3d(std::sin(tilt) * std::cos(phi), std::sin(tilt) * std::sin(phi), std::cos(tilt));
        JointPose j;
        return this->inverse(pose, nullptr, j);
    };
    double lo = 0., hi = std::min(m_geo.max_tilt, 0.5 * M_PI - 1e-6);
    if (hi <= 0.)
        return 0.;
    if (reachable(hi))
        return hi;
    for (int i = 0; i < 50; ++i) {
        const double mid = 0.5 * (lo + hi);
        (reachable(mid) ? lo : hi) = mid;
    }
    return lo;
}

Matrix3d RotaryTableBC::rotation(double b, double c) const { return rot_y(b) * rot_z(c); }

void RotaryTableBC::solve_axis(const Vec3d &a, double free_angle, std::vector<std::array<double, 2>> &out) const
{
    const double xy = a.head<2>().norm();
    if (xy < SingularXY) {
        out.push_back({ 0., free_angle });
        return;
    }
    const double tilt = std::atan2(xy, a.z());
    out.push_back({ tilt, std::atan2(a.y(), -a.x()) });
    out.push_back({ -tilt, std::atan2(-a.y(), a.x()) });
}

Matrix3d RotaryTableAC::rotation(double a, double c) const { return rot_x(a) * rot_z(c); }

void RotaryTableAC::solve_axis(const Vec3d &a, double free_angle, std::vector<std::array<double, 2>> &out) const
{
    const double xy = a.head<2>().norm();
    if (xy < SingularXY) {
        out.push_back({ 0., free_angle });
        return;
    }
    const double tilt = std::atan2(xy, a.z());
    out.push_back({ tilt, std::atan2(a.x(), a.y()) });
    out.push_back({ -tilt, std::atan2(-a.x(), -a.y()) });
}

Matrix3d RotaryHeadAB::rotation(double a, double b) const { return rot_y(b) * rot_x(a); }

void RotaryHeadAB::solve_axis(const Vec3d &v, double, std::vector<std::array<double, 2>> &out) const
{
    const double a = std::asin(std::clamp(-v.y(), -1., 1.));
    const double b = std::hypot(v.x(), v.z()) < SingularXY ? 0. : std::atan2(v.x(), v.z());
    out.push_back({ a, b });
    out.push_back({ wrap_pi(M_PI - a), wrap_pi(b + M_PI) });
}

Matrix3d RotaryHeadBC::rotation(double b, double c) const { return rot_z(c) * rot_y(b); }

void RotaryHeadBC::solve_axis(const Vec3d &a, double free_angle, std::vector<std::array<double, 2>> &out) const
{
    const double xy = a.head<2>().norm();
    if (xy < SingularXY) {
        out.push_back({ 0., free_angle });
        return;
    }
    const double tilt = std::atan2(xy, a.z());
    out.push_back({ tilt, std::atan2(a.y(), a.x()) });
    out.push_back({ -tilt, std::atan2(-a.y(), -a.x()) });
}

namespace {

constexpr RotaryLayout HybridAC = RotaryLayout(5);

class RotaryHybridAC : public RotaryKinematics
{
public:
    using RotaryKinematics::RotaryKinematics;
    RotaryLayout layout() const override { return HybridAC; }

    bool inverse(const ToolPose &pose, const JointPose *prev, JointPose &out) const override
    {
        if (!RotaryKinematics::inverse(pose, prev, out))
            return false;
        if (!m_geo.firmware_rtcp) {
            const auto  kin  = this->kinematic_angles(out);
            const Vec3d tool = rot_x(kin[0]) * Vec3d::UnitZ();
            out.xyz          = this->turn_work(pose.tip, kin[1]) + m_geo.tool_length * (tool - Vec3d::UnitZ());
        }
        return true;
    }

    ToolPose forward(const JointPose &joints) const override
    {
        const auto  kin  = this->kinematic_angles(joints);
        const Vec3d tool = rot_x(kin[0]) * Vec3d::UnitZ();
        ToolPose    pose;
        pose.axis = rot_z(-kin[1]) * tool;
        pose.tip  = m_geo.firmware_rtcp ? joints.xyz : this->turn_work(joints.xyz - m_geo.tool_length * (tool - Vec3d::UnitZ()), -kin[1]);
        return pose;
    }

protected:
    Matrix3d rotation(double a, double c) const override { return rot_z(-c) * rot_x(a); }
    bool     is_table() const override { return false; }
    int      free_axis() const override { return 1; }

    void solve_axis(const Vec3d &v, double free_angle, std::vector<std::array<double, 2>> &out) const override
    {
        const double xy = v.head<2>().norm();
        if (xy < SingularXY) {
            out.push_back({ 0., free_angle });
            return;
        }
        const double tilt = std::atan2(xy, v.z());
        out.push_back({ tilt, std::atan2(-v.x(), -v.y()) });
        out.push_back({ -tilt, std::atan2(v.x(), v.y()) });
    }

private:
    Vec3d turn_work(const Vec3d &p, double c) const
    {
        const Vec3d centre(m_geo.pivot.x(), m_geo.pivot.y(), 0.);
        return centre + rot_z(c) * (p - centre);
    }
};

}

RotaryGeometry rotary_geometry(const PrintConfig &config)
{
    RotaryGeometry geo;
    const char    *letters = "BC";
    switch (config.nonplanar_kinematics.value) {
    case RotaryLayout::TableAC: letters = "AC"; break;
    case RotaryLayout::HeadAB: letters = "AB"; break;
    case HybridAC: letters = "AC"; break;
    default: break;
    }
    auto at = [](const auto &opt, size_t i, auto fallback) { return i < opt.values.size() ? opt.values[i] : fallback; };
    for (size_t i = 0; i < 2; ++i) {
        RotaryAxis &axis     = geo.axes[i];
        axis.letter          = letters[i];
        axis.min_deg         = at(config.rotary_axis_limits, 2 * i, i == 0 ? -90. : -180.);
        axis.max_deg         = at(config.rotary_axis_limits, 2 * i + 1, i == 0 ? 90. : 180.);
        axis.continuous      = at(config.rotary_axis_continuous, i, (unsigned char) false) != 0;
        axis.max_speed_deg_s = at(config.rotary_max_speed, i, 0.);
        geo.home[i]          = at(config.rotary_axis_home, i, 0.);
        geo.reversed[i]      = at(config.rotary_axis_reversed, i, (unsigned char) false) != 0;
    }
    for (size_t i = 0; i < 3; ++i) {
        geo.pivot[i]              = at(config.rotary_pivot, i, 0.);
        geo.tool_mount[i]         = at(config.rotary_tool_mount, i, 0.);
        geo.linear_moves_table[i] = at(config.rotary_linear_moves_table, i, (unsigned char) false) != 0;
    }
    geo.tool_length       = config.rotary_tool_length.value;
    geo.table_height      = config.rotary_table_height.value;
    geo.max_tilt          = Geometry::deg2rad(config.nonplanar_max_tilt.value);
    geo.firmware_rtcp     = config.rotary_firmware_rtcp.value;
    geo.inverse_time_feed = config.rotary_inverse_time_feed.value;
    return geo;
}

RotaryUprightKinematics::RotaryUprightKinematics(std::unique_ptr<RotaryKinematics> kinematics)
    : m_kinematics(std::move(kinematics))
{
    for (int i = 0; i < 2; ++i)
        m_home.rot[i] = m_kinematics->machine_angle(i, 0.);
}

Vec3d RotaryUprightKinematics::to_machine(const Vec3d &p) const
{
    JointPose joints;
    m_kinematics->inverse({ p, Vec3d::UnitZ() }, &m_home, joints);
    return joints.xyz;
}

std::unique_ptr<ToolKinematics> make_tool_kinematics(const PrintConfig &config)
{
    switch (config.nonplanar_kinematics.value) {
    case RotaryLayout::TableBC: return std::make_unique<RotaryTableBC>(rotary_geometry(config));
    case RotaryLayout::TableAC: return std::make_unique<RotaryTableAC>(rotary_geometry(config));
    case RotaryLayout::HeadAB: return std::make_unique<RotaryHeadAB>(rotary_geometry(config));
    case RotaryLayout::HeadBC: return std::make_unique<RotaryHeadBC>(rotary_geometry(config));
    case HybridAC: return std::make_unique<RotaryHybridAC>(rotary_geometry(config));
    default: return std::make_unique<ThreeAxisKinematics>();
    }
}

std::unique_ptr<MachineKinematics> make_upright_machine_kinematics(const PrintConfig &config)
{
    if (config.nonplanar_kinematics.value == RotaryLayout::None)
        return nullptr;
    std::unique_ptr<ToolKinematics> kin = make_tool_kinematics(config);
    return std::make_unique<RotaryUprightKinematics>(std::unique_ptr<RotaryKinematics>(static_cast<RotaryKinematics *>(kin.release())));
}

}
