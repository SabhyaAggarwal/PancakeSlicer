#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <cmath>
#include <memory>
#include <random>
#include <vector>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

namespace {

RotaryGeometry prodcan3_geometry()
{
    RotaryGeometry geo;
    geo.axes[0]            = { 'B', -60., 60., false, 28.6 };
    geo.axes[1]            = { 'C', -180., 180., false, 57.3 };
    geo.pivot              = Vec3d(91., 55.3 - 117.95, 102.);
    geo.table_height       = -6.5 + 13.;
    geo.tool_mount         = Vec3d(-3., 35., 82.05);
    geo.tool_length        = 60.;
    geo.linear_moves_table = { false, true, false };
    geo.max_tilt           = 60. * M_PI / 180.;
    geo.inverse_time_feed  = true;
    return geo;
}

std::unique_ptr<RotaryKinematics> make_layout(RotaryLayout layout)
{
    RotaryGeometry geo = prodcan3_geometry();
    switch (layout) {
    case RotaryLayout::TableBC: return std::make_unique<RotaryTableBC>(geo);
    case RotaryLayout::TableAC:
        geo.axes[0].letter = 'A';
        return std::make_unique<RotaryTableAC>(geo);
    case RotaryLayout::HeadAB:
        geo.axes    = { RotaryAxis{ 'A', -70., 70., false, 0. }, RotaryAxis{ 'B', -70., 70., false, 0. } };
        geo.tool_length = 45.;
        return std::make_unique<RotaryHeadAB>(geo);
    default:
        geo.axes        = { RotaryAxis{ 'B', -150., -30., false, 0. }, RotaryAxis{ 'C', 0., 0., true, 0. } };
        geo.home        = { -90., 0. };
        geo.reversed    = { true, false };
        geo.tool_length = 45.;
        return std::make_unique<RotaryHeadBC>(geo);
    }
}

Vec3d tilted(double tilt_deg, double azimuth_deg)
{
    const double t = tilt_deg * M_PI / 180., p = azimuth_deg * M_PI / 180.;
    return Vec3d(std::sin(t) * std::cos(p), std::sin(t) * std::sin(p), std::cos(t));
}

void check_close(const Vec3d &a, const Vec3d &b, double tol)
{
    CHECK_THAT(a.x(), WithinAbs(b.x(), tol));
    CHECK_THAT(a.y(), WithinAbs(b.y(), tol));
    CHECK_THAT(a.z(), WithinAbs(b.z(), tol));
}

}

TEST_CASE("Table BC kinematics reproduce the Prodcan3 joint-space model", "[Nonplanar][ToolKinematics]")
{
    const double reference[][11] = {
        { 94., 97.65, 121.45, 0., 0., 0., 0., 35., 0., 0., 1. },
        { 80.5, 110.2, 95., 25., -40., -6.178040875689, -21.566853301309, 1.434585661402, -0.323744370967, -0.271653782274, 0.906307787037 },
        { 101.3, 88., 130.7, -47.5, 135., -23.121425383475, -36.768586260376, 22.404078477781, -0.521333804474, -0.521333804474, 0.675590207616 },
        { 60., 140., 100., 10., 170., 29.049526166006, 48.125532253225, 7.341357407219, 0.171010071663, 0.030153689607, 0.984807753012 },
    };
    const RotaryTableBC kin(prodcan3_geometry());
    for (const auto &row : reference) {
        JointPose joints;
        joints.xyz = Vec3d(row[0], row[1], row[2]);
        joints.rot = { row[3], row[4] };
        const ToolPose pose = kin.forward(joints);
        check_close(pose.tip, Vec3d(row[5], row[6], row[7]), 1e-8);
        check_close(pose.axis, Vec3d(row[8], row[9], row[10]), 1e-11);

        JointPose back;
        REQUIRE(kin.inverse(pose, &joints, back));
        check_close(back.xyz, joints.xyz, 1e-8);
        CHECK_THAT(back.rot[0], WithinAbs(row[3], 1e-9));
        CHECK_THAT(back.rot[1], WithinAbs(row[4], 1e-9));
    }
}

TEST_CASE("Forward kinematics inverts inverse kinematics on every rotary layout", "[Nonplanar][ToolKinematics]")
{
    const RotaryLayout layout = GENERATE(RotaryLayout::TableBC, RotaryLayout::TableAC, RotaryLayout::HeadAB, RotaryLayout::HeadBC);
    const bool         rtcp   = GENERATE(false, true);
    std::unique_ptr<RotaryKinematics> kin = make_layout(layout);
    RotaryGeometry geo = kin->geometry();
    geo.firmware_rtcp  = rtcp;
    switch (layout) {
    case RotaryLayout::TableBC: kin = std::make_unique<RotaryTableBC>(geo); break;
    case RotaryLayout::TableAC: kin = std::make_unique<RotaryTableAC>(geo); break;
    case RotaryLayout::HeadAB: kin = std::make_unique<RotaryHeadAB>(geo); break;
    default: kin = std::make_unique<RotaryHeadBC>(geo); break;
    }

    std::mt19937                           rng(1234 + int(layout));
    std::uniform_real_distribution<double> pos(-35., 35.), tilt(0., 55.), azimuth(-180., 180.);
    int reached = 0;
    for (int i = 0; i < 500; ++i) {
        ToolPose pose;
        pose.tip  = Vec3d(pos(rng), pos(rng), 30. + pos(rng) / 2.);
        pose.axis = tilted(tilt(rng), azimuth(rng));
        JointPose joints;
        if (!kin->inverse(pose, nullptr, joints))
            continue;
        ++reached;
        for (int r = 0; r < 2; ++r) {
            const RotaryAxis &axis = kin->rotary_axes()[r];
            if (!axis.continuous) {
                CHECK(joints.rot[r] >= axis.min_deg - 1e-9);
                CHECK(joints.rot[r] <= axis.max_deg + 1e-9);
            }
        }
        const ToolPose back = kin->forward(joints);
        check_close(back.tip, pose.tip, 1e-9);
        check_close(back.axis, pose.axis, 1e-9);
    }
    CHECK(reached > 250);
}

TEST_CASE("Inverse kinematics never flips the turntable along a smooth tool path", "[Nonplanar][ToolKinematics]")
{
    const RotaryLayout layout = GENERATE(RotaryLayout::TableBC, RotaryLayout::TableAC, RotaryLayout::HeadBC);
    const std::unique_ptr<RotaryKinematics> kin = make_layout(layout);
    const double start = layout == RotaryLayout::TableBC ? 10. : layout == RotaryLayout::TableAC ? -80. : -170.;
    std::vector<Vec3d> axes;
    for (double az = start; az <= start + 340.; az += 1.)
        axes.push_back(tilted(30., az));
    for (double t = 30.; t >= -30.; t -= 1.)
        axes.push_back(tilted(t, start + 340.));

    JointPose seed;
    seed.rot = { 30., 170. };
    JointPose prev;
    REQUIRE(kin->inverse({ Vec3d(0., 0., 10.), axes.front() }, layout == RotaryLayout::HeadBC ? nullptr : &seed, prev));
    for (size_t i = 1; i < axes.size(); ++i) {
        JointPose next;
        REQUIRE(kin->inverse({ Vec3d(0., 0., 10.), axes[i] }, &prev, next));
        INFO("step " << i);
        CHECK(std::abs(next.rot[0] - prev.rot[0]) < 2.);
        CHECK(std::abs(next.rot[1] - prev.rot[1]) < 5.);
        prev = next;
    }
}

TEST_CASE("An upright tool keeps the previous turntable angle", "[Nonplanar][ToolKinematics]")
{
    const RotaryLayout layout = GENERATE(RotaryLayout::TableBC, RotaryLayout::TableAC, RotaryLayout::HeadBC);
    const std::unique_ptr<RotaryKinematics> kin = make_layout(layout);
    JointPose tilted_joints, upright;
    REQUIRE(kin->inverse({ Vec3d(5., 5., 10.), tilted(20., 63.) }, nullptr, tilted_joints));
    REQUIRE(kin->inverse({ Vec3d(5., 5., 10.), Vec3d::UnitZ() }, &tilted_joints, upright));
    CHECK_THAT(upright.rot[1], WithinAbs(tilted_joints.rot[1], 1e-12));
    CHECK_THAT(kin->kinematic_angles(upright)[0], WithinAbs(0., 1e-12));
}

TEST_CASE("Poses outside the rotary limits are unreachable", "[Nonplanar][ToolKinematics]")
{
    const RotaryTableBC kin(prodcan3_geometry());
    JointPose           joints;
    CHECK(kin.inverse({ Vec3d::Zero(), tilted(59., 10.) }, nullptr, joints));
    CHECK_FALSE(kin.inverse({ Vec3d::Zero(), tilted(61., 10.) }, nullptr, joints));
    CHECK_THAT(kin.max_tilt(0.3), WithinAbs(M_PI / 3., 1e-9));

    RotaryGeometry geo = prodcan3_geometry();
    geo.axes[1].min_deg = 0.;
    geo.axes[1].max_deg = 90.;
    const RotaryTableBC quarter(geo);
    CHECK(quarter.max_tilt(135. * M_PI / 180.) > 1.);
    CHECK(quarter.max_tilt(-45. * M_PI / 180.) > 1.);
    CHECK_THAT(quarter.max_tilt(45. * M_PI / 180.), WithinAbs(0., 1e-6));
}

TEST_CASE("Upright-at-minus-90 heads write their home angle", "[Nonplanar][ToolKinematics]")
{
    const std::unique_ptr<RotaryKinematics> kin = make_layout(RotaryLayout::HeadBC);
    JointPose joints;
    REQUIRE(kin->inverse({ Vec3d(1., 2., 3.), Vec3d::UnitZ() }, nullptr, joints));
    CHECK_THAT(joints.rot[0], WithinAbs(-90., 1e-12));
    check_close(joints.xyz, Vec3d(1., 2., 3.), 1e-12);
    REQUIRE(kin->inverse({ Vec3d(1., 2., 3.), tilted(20., 0.) }, &joints, joints));
    CHECK_THAT(joints.rot[0], WithinAbs(-110., 1e-9));
    CHECK_THAT(joints.rot[1], WithinAbs(0., 1e-9));
}

TEST_CASE("Linearize keeps the tip on the segment during a 30 degree B sweep", "[Nonplanar][ToolKinematics]")
{
    const RotaryTableBC kin(prodcan3_geometry());
    const ToolPose      pa { Vec3d(-10., 0., 5.), Vec3d::UnitZ() };
    const ToolPose      pb { Vec3d(10., 0., 8.), tilted(30., 180.) };
    JointPose           ja;
    REQUIRE(kin.inverse(pa, nullptr, ja));
    const double           tol = 0.02;
    std::vector<JointPose> out;
    kin.linearize(ja, pa, pb, tol, out);
    REQUIRE(out.size() > 1);
    CHECK_THAT(kin.kinematic_angles(out.back())[0], WithinAbs(M_PI / 6., 1e-9));

    const Vec3d dir = (pb.tip - pa.tip).normalized();
    double      worst = 0.;
    JointPose   prev = ja;
    for (const JointPose &next : out) {
        for (double u = 0.; u <= 1.; u += 0.05) {
            JointPose j;
            j.xyz = prev.xyz + u * (next.xyz - prev.xyz);
            for (int r = 0; r < 2; ++r)
                j.rot[r] = prev.rot[r] + u * (next.rot[r] - prev.rot[r]);
            const Vec3d d = kin.forward(j).tip - pa.tip;
            worst         = std::max(worst, (d - d.dot(dir) * dir).norm());
        }
        prev = next;
    }
    CHECK(worst <= tol);

    std::vector<JointPose> one;
    RotaryGeometry         rtcp = prodcan3_geometry();
    rtcp.firmware_rtcp          = true;
    RotaryTableBC(rtcp).linearize(ja, pa, pb, tol, one);
    CHECK(one.size() == 1);
}

TEST_CASE("Tool kinematics follow the printer configuration", "[Nonplanar][ToolKinematics]")
{
    PrintConfig config;
    CHECK(make_tool_kinematics(config)->layout() == RotaryLayout::None);
    CHECK(make_tool_kinematics(config)->rotary_count() == 0);

    config.nonplanar_kinematics.value = RotaryLayout::TableAC;
    config.rotary_axis_limits.values  = { -45., 30., -360., 360. };
    config.rotary_axis_home.values    = { 0., 90. };
    config.nonplanar_max_tilt.value   = 40.;
    const std::unique_ptr<ToolKinematics> kin = make_tool_kinematics(config);
    REQUIRE(kin->layout() == RotaryLayout::TableAC);
    CHECK(kin->rotary_axes()[0].letter == 'A');
    CHECK(kin->rotary_axes()[1].letter == 'C');
    CHECK_THAT(kin->rotary_axes()[0].min_deg, WithinAbs(-45., 1e-12));
    CHECK_THAT(kin->max_tilt(0.), WithinAbs(40. * M_PI / 180., 1e-9));
    JointPose joints;
    REQUIRE(kin->inverse({ Vec3d::Zero(), Vec3d::UnitZ() }, nullptr, joints));
    CHECK_THAT(joints.rot[1], WithinAbs(90., 1e-12));
}
