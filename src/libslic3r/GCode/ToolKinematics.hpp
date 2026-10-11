#pragma once
#include "libslic3r/GCode/MachineKinematics.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <array>
#include <memory>
#include <vector>

namespace Slic3r {

struct ToolPose
{
    Vec3d tip  { Vec3d::Zero() };
    Vec3d axis { Vec3d::UnitZ() };
};

struct JointPose
{
    Vec3d                 xyz { Vec3d::Zero() };
    std::array<double, 2> rot { 0., 0. };
};

struct RotaryAxis
{
    char   letter = 'B';
    double min_deg = -90., max_deg = 90.;
    bool   continuous = false;
    double max_speed_deg_s = 0.;
};

inline bool layout_is_table(RotaryLayout layout) { return layout == RotaryLayout::TableBC || layout == RotaryLayout::TableAC; }

class ToolKinematics
{
public:
    virtual ~ToolKinematics() = default;

    virtual RotaryLayout layout() const = 0;
    int  rotary_count() const { return this->layout() == RotaryLayout::None ? 0 : 2; }
    virtual const std::array<RotaryAxis, 2> &rotary_axes() const = 0;

    virtual bool inverse(const ToolPose &pose, const JointPose *prev, JointPose &out) const = 0;
    virtual ToolPose forward(const JointPose &joints) const = 0;

    virtual Matrix3d head_frame(const JointPose &joints) const = 0;

    virtual double max_tilt(double phi) const = 0;

    virtual void linearize(const JointPose &ja, const ToolPose &pa, const ToolPose &pb, double tol,
                           std::vector<JointPose> &out) const;

    virtual bool firmware_rtcp() const = 0;
    virtual bool inverse_time_feed() const = 0;
};

class ThreeAxisKinematics : public ToolKinematics
{
public:
    RotaryLayout                     layout() const override { return RotaryLayout::None; }
    const std::array<RotaryAxis, 2> &rotary_axes() const override { return m_axes; }
    bool                             inverse(const ToolPose &pose, const JointPose *prev, JointPose &out) const override;
    ToolPose                         forward(const JointPose &joints) const override;
    Matrix3d                         head_frame(const JointPose &) const override { return Matrix3d::Identity(); }
    double                           max_tilt(double) const override { return 0.; }
    bool                             firmware_rtcp() const override { return false; }
    bool                             inverse_time_feed() const override { return false; }

private:
    std::array<RotaryAxis, 2> m_axes;
};

struct RotaryGeometry
{
    std::array<RotaryAxis, 2> axes;
    std::array<double, 2> home { 0., 0. };
    std::array<bool, 2>   reversed { false, false };
    Vec3d                pivot      { Vec3d::Zero() };
    Vec3d                tool_mount { Vec3d::Zero() };
    double               tool_length  = 0.;
    double               table_height = 0.;
    std::array<bool, 3>  linear_moves_table { false, false, false };
    double               max_tilt = 0.5 * M_PI;
    bool                 firmware_rtcp = false;
    bool                 inverse_time_feed = false;
};

class RotaryKinematics : public ToolKinematics
{
public:
    explicit RotaryKinematics(const RotaryGeometry &geometry) : m_geo(geometry) {}

    const std::array<RotaryAxis, 2> &rotary_axes() const override { return m_geo.axes; }
    const RotaryGeometry            &geometry() const { return m_geo; }
    bool                             inverse(const ToolPose &pose, const JointPose *prev, JointPose &out) const override;
    ToolPose                         forward(const JointPose &joints) const override;
    Matrix3d                         head_frame(const JointPose &joints) const override;
    double                           max_tilt(double phi) const override;
    bool                             firmware_rtcp() const override { return m_geo.firmware_rtcp; }
    bool                             inverse_time_feed() const override { return m_geo.inverse_time_feed; }

    std::array<double, 2> kinematic_angles(const JointPose &joints) const;
    double                machine_angle(int axis, double kinematic_rad) const;

protected:
    virtual Matrix3d rotation(double r1, double r2) const = 0;
    virtual bool     is_table() const = 0;
    virtual void     solve_axis(const Vec3d &axis, double free_angle, std::vector<std::array<double, 2>> &out) const = 0;
    virtual int      free_axis() const = 0;

    RotaryGeometry m_geo;
};

class RotaryTableBC : public RotaryKinematics
{
public:
    using RotaryKinematics::RotaryKinematics;
    RotaryLayout layout() const override { return RotaryLayout::TableBC; }

protected:
    Matrix3d rotation(double b, double c) const override;
    bool     is_table() const override { return true; }
    void     solve_axis(const Vec3d &axis, double free_angle, std::vector<std::array<double, 2>> &out) const override;
    int      free_axis() const override { return 1; }
};

class RotaryTableAC : public RotaryKinematics
{
public:
    using RotaryKinematics::RotaryKinematics;
    RotaryLayout layout() const override { return RotaryLayout::TableAC; }

protected:
    Matrix3d rotation(double a, double c) const override;
    bool     is_table() const override { return true; }
    void     solve_axis(const Vec3d &axis, double free_angle, std::vector<std::array<double, 2>> &out) const override;
    int      free_axis() const override { return 1; }
};

class RotaryHeadAB : public RotaryKinematics
{
public:
    using RotaryKinematics::RotaryKinematics;
    RotaryLayout layout() const override { return RotaryLayout::HeadAB; }

protected:
    Matrix3d rotation(double a, double b) const override;
    bool     is_table() const override { return false; }
    void     solve_axis(const Vec3d &axis, double free_angle, std::vector<std::array<double, 2>> &out) const override;
    int      free_axis() const override { return -1; }
};

class RotaryHeadBC : public RotaryKinematics
{
public:
    using RotaryKinematics::RotaryKinematics;
    RotaryLayout layout() const override { return RotaryLayout::HeadBC; }

protected:
    Matrix3d rotation(double b, double c) const override;
    bool     is_table() const override { return false; }
    void     solve_axis(const Vec3d &axis, double free_angle, std::vector<std::array<double, 2>> &out) const override;
    int      free_axis() const override { return 1; }
};

class RotaryUprightKinematics : public MachineKinematics
{
public:
    explicit RotaryUprightKinematics(std::unique_ptr<RotaryKinematics> kinematics);

    Vec3d to_machine(const Vec3d &p) const override;
    bool  must_emit_all_axes() const override { return true; }
    bool  suppress_lift_at_unknown_position() const override { return true; }
    bool  supports_arc_moves() const override { return false; }
    void  set_axis_remap(int, int, int) override {}
    void  set_build_volume_max(const Vec3d &) override {}

private:
    std::unique_ptr<RotaryKinematics> m_kinematics;
    JointPose                         m_home;
};

RotaryGeometry                  rotary_geometry(const PrintConfig &config);
std::unique_ptr<ToolKinematics> make_tool_kinematics(const PrintConfig &config);
std::unique_ptr<MachineKinematics> make_upright_machine_kinematics(const PrintConfig &config);

}
