#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "libslic3r/Flow.hpp"
#include "libslic3r/Nonplanar/BeadModel.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Nonplanar;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("Charge length is the XY length for a vertical tool axis", "[Nonplanar][BeadModel]")
{
    const Vec3d a(1., 2., 3.);
    const Vec3d b(4., 6., 3. + std::tan(30. * M_PI / 180.) * 5.);
    CHECK_THAT(charge_length(a, b), WithinAbs(5., 1e-12));
    CHECK_THAT((b - a).norm() / charge_length(a, b) - 1., WithinAbs(0.1547, 1e-4));
}

TEST_CASE("Charge length projects onto the plane normal to a tilted tool axis", "[Nonplanar][BeadModel]")
{
    const double t    = 25. * M_PI / 180.;
    const Vec3d  axis(std::sin(t), 0., std::cos(t));
    CHECK_THAT(charge_length(Vec3d::Zero(), 3. * axis, axis), WithinAbs(0., 1e-12));
    CHECK_THAT(charge_length(Vec3d::Zero(), 2. * Vec3d::UnitY(), axis), WithinAbs(2., 1e-12));
}

TEST_CASE("Bead area equals Flow::mm3_per_mm for the same width and height", "[Nonplanar][BeadModel]")
{
    const float w = GENERATE(0.42f, 0.45f, 0.6f);
    const float h = GENERATE(0.1f, 0.2f, 0.28f);
    const Flow  flow(w, h, 0.4f);
    CHECK_THAT(bead_area(w, h), WithinRel(flow.mm3_per_mm(), 1e-6));
}

TEST_CASE("Segment volume averages the endpoint heights over the charged length", "[Nonplanar][BeadModel]")
{
    const Vec3d a(0., 0., 0.), b(10., 0., 2.);
    CHECK_THAT(segment_volume(a, b, 0.45, 0.1f, 0.3f), WithinRel(bead_area(0.45, 0.2) * 10., 1e-6));
}

TEST_CASE("Feedrate limiting keeps a sine wave inside the Z speed and acceleration limits", "[Nonplanar][BeadModel]")
{
    const double lambda  = GENERATE(3., 6., 12., 24.);
    const double A       = 0.25;
    const double k       = 2. * M_PI / lambda;
    const double nominal = 60.;
    MotionLimits limits;
    limits.max_v_z = 10.;
    limits.max_a_z = 100.;

    std::vector<Vec3d> pts;
    for (double x = 0.; x <= 4. * lambda + 1e-9; x += 0.02)
        pts.emplace_back(x, 0., A * std::sin(k * x));
    const std::vector<double> v = limit_feedrates(pts, nominal, limits);
    REQUIRE(v.size() == pts.size() - 1);

    double max_vz = 0.;
    for (size_t i = 0; i < v.size(); ++i) {
        const Vec3d d = pts[i + 1] - pts[i];
        CHECK(v[i] <= nominal);
        max_vz = std::max(max_vz, v[i] * std::abs(d.z()) / d.norm());
    }
    CHECK(max_vz <= limits.max_v_z + 1e-9);

    const double expected = std::min({ nominal, std::sqrt(limits.max_a_z / (A * k * k)), limits.max_v_z / (A * k) });
    const double slowest  = *std::min_element(v.begin(), v.end());
    CHECK(slowest <= expected * (1. + 1e-6));
    CHECK(slowest >= expected * 0.94);
}

TEST_CASE("Feedrate limiting leaves a flat path at the nominal speed", "[Nonplanar][BeadModel]")
{
    MotionLimits limits;
    limits.max_v_z = 5.;
    limits.max_a_z = 50.;
    const std::vector<Vec3d>  pts { { 0., 0., 1. }, { 10., 0., 1. }, { 10., 10., 1. } };
    const std::vector<double> v = limit_feedrates(pts, 80., limits);
    REQUIRE(v.size() == 2);
    CHECK_THAT(v[0], WithinAbs(80., 1e-12));
    CHECK_THAT(v[1], WithinAbs(80., 1e-12));
}
