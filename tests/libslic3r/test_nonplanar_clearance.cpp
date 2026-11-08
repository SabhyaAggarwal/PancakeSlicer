#include <catch2/catch_all.hpp>
#include "libslic3r/GCode/ClearanceVerifier.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Exception.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include <utility>

using namespace Slic3r;
using namespace Slic3r::Nonplanar;
using Catch::Matchers::WithinAbs;
namespace {
HeadEnvelope test_head()
{
    HeadEnvelope head;
    head.margin = 0.05; head.lateral_margin = 0.;
    head.boxes.push_back({ "heater_block", Vec3d(-2., -2., 2.), Vec3d(2., 2., 5.) });
    return head;
}
void move(GCodeProcessorResult &result, const Vec3d &p, bool extruding = false)
{
    GCodeProcessorResult::MoveVertex v;
    v.position = p.cast<float>();
    v.type = extruding ? EMoveType::Extrude : EMoveType::Travel;
    v.width = 0.45f; v.height = 0.2f; v.gcode_id = unsigned(result.moves.size() + 1);
    result.moves.push_back(v);
    result.joints.push_back({ p, { 0., 0. } });
}
ClearanceReport check(const GCodeProcessorResult &result, HeadEnvelope head = test_head())
{
    ClearanceParams params; params.head = std::move(head);
    return verify_clearance(result, params, ThreeAxisKinematics());
}
}

TEST_CASE("Column rectangle queries match brute force on random fields", "[Nonplanar][Clearance]")
{
    constexpr double pitch = 0.25;
    constexpr int n = 97;
    MaterialColumns cols(BoundingBoxf(Vec2d::Zero(), Vec2d(24., 24.)), pitch);
    std::vector<double> brute(n * n, -std::numeric_limits<double>::infinity());
    std::mt19937 rng(72841);
    std::uniform_real_distribution<double> xy(-2., 26.), height(-1., 30.), width(0., 1.);
    auto index = [=](double v, bool upper) { return std::clamp(int(std::floor(v / pitch)) + (upper ? 1 : 0), 0, n); };
    for (int k = 0; k < 250; ++k) {
        const Vec3d a(xy(rng), xy(rng), height(rng)), b(xy(rng), xy(rng), height(rng));
        const double w = width(rng);
        cols.deposit(a, b, w);
        Vec2d lo = a.head<2>().cwiseMin(b.head<2>()) - Vec2d::Constant(w / 2.);
        Vec2d hi = a.head<2>().cwiseMax(b.head<2>()) + Vec2d::Constant(w / 2.);
        for (int y = index(lo.y(), false); y < index(hi.y(), true); ++y)
            for (int x = index(lo.x(), false); x < index(hi.x(), true); ++x)
                brute[y * n + x] = std::max(brute[y * n + x], std::max(a.z(), b.z()));
        for (int query = 0; query < 10; ++query) {
            Vec2d p(xy(rng), xy(rng)), q(xy(rng), xy(rng));
            lo = p.cwiseMin(q); hi = p.cwiseMax(q);
            double expected = -std::numeric_limits<double>::infinity();
            for (int y = index(lo.y(), false); y < index(hi.y(), true); ++y)
                for (int x = index(lo.x(), false); x < index(hi.x(), true); ++x) expected = std::max(expected, brute[y * n + x]);
            const double actual = cols.top_in_rect(BoundingBoxf(lo, hi));
            if (std::isfinite(expected)) REQUIRE_THAT(actual, WithinAbs(expected, 1e-12));
            else REQUIRE(actual == expected);
        }
        HeadEnvelope head; head.margin = 0.1; head.lateral_margin = 0.2;
        head.boxes.push_back({ "random_box", Vec3d(-1., -2., 2.), Vec3d(2., 1., 5.) });
        const Vec3d p(xy(rng), xy(rng), height(rng)), q(xy(rng), xy(rng), height(rng));
        const Vec2d swept_lo = p.head<2>().cwiseMin(q.head<2>()) + head.boxes.front().min.head<2>() - Vec2d::Constant(head.lateral_margin);
        const Vec2d swept_hi = p.head<2>().cwiseMax(q.head<2>()) + head.boxes.front().max.head<2>() + Vec2d::Constant(head.lateral_margin);
        double top = -std::numeric_limits<double>::infinity();
        for (int y = index(swept_lo.y(), false); y < index(swept_hi.y(), true); ++y)
            for (int x = index(swept_lo.x(), false); x < index(swept_hi.x(), true); ++x) top = std::max(top, brute[y * n + x]);
        const double expected = std::min(p.z(), q.z()) + 2. - head.margin - top;
        if (std::isfinite(expected)) REQUIRE_THAT(cols.swept_clearance(p, q, head), WithinAbs(expected, 1e-12));
        else REQUIRE(std::isinf(cols.swept_clearance(p, q, head)));
    }
}

TEST_CASE("Safe travel uses the swept footprint and infinite beams", "[Nonplanar][Clearance]")
{
    MaterialColumns cols(BoundingBoxf(Vec2d(-20., -20.), Vec2d(20., 20.)), 0.25);
    cols.deposit(Vec3d(0., 0., 10.), Vec3d(0., 0., 10.), 1.);
    const auto head = test_head();
    const double z = safe_travel_z(cols, head, Vec2d(-5., 0.), Vec2d(5., 0.), 0.3);
    REQUIRE_THAT(z, WithinAbs(10. - 2. + head.margin + 0.3, 1e-12));
    REQUIRE(cols.swept_clearance(Vec3d(-5., 0., z), Vec3d(5., 0., z), head) >= 0.3 - 1e-12);
    auto beam = head; beam.boxes.front().infinite_x = true;
    REQUIRE(std::isfinite(safe_travel_z(cols, beam, Vec2d(15., -5.), Vec2d(15., 5.), 0.)));
    REQUIRE(std::isinf(safe_travel_z(cols, head, Vec2d(-5., 15.), Vec2d(5., 15.), 0.)));
}

TEST_CASE("A planar cube clears the first head box floor", "[Nonplanar][Clearance]")
{
    GCodeProcessorResult result;
    move(result, Vec3d(0., 0., 0.2));
    for (int layer = 1; layer <= 100; ++layer) {
        const double z = layer * 0.2;
        move(result, Vec3d(0., 0., z));
        move(result, Vec3d(20., 0., z), true); move(result, Vec3d(20., 20., z), true);
        move(result, Vec3d(0., 20., z), true); move(result, Vec3d(0., 0., z), true);
    }
    auto head = test_head(); head.margin = 0.;
    const auto report = check(result, head);
    REQUIRE(report.passed());
    REQUIRE(report.min_clearance >= head.boxes.front().min.z() - 1e-6);
    REQUIRE(report.per_move.size() == result.moves.size());
}

TEST_CASE("Travel through an earlier pillar names the colliding component", "[Nonplanar][Clearance]")
{
    GCodeProcessorResult result;
    move(result, Vec3d(0., 0., 10.)); move(result, Vec3d(0., 1., 10.), true);
    move(result, Vec3d(-5., 0., 2.)); move(result, Vec3d(5., 0., 2.));
    const auto report = check(result);
    REQUIRE(report.hit_moves >= 1);
    REQUIRE(report.worst.front().component == "heater_block");
    REQUIRE(report.worst.front().penetration > 0.);
    REQUIRE(report.per_move.back() < 0.);
}

TEST_CASE("Material deposited later never obstructs an earlier move", "[Nonplanar][Clearance]")
{
    GCodeProcessorResult early;
    move(early, Vec3d(-5., 0., 2.)); move(early, Vec3d(5., 0., 2.));
    move(early, Vec3d(0., 0., 10.)); move(early, Vec3d(0., 1., 10.), true);
    REQUIRE(check(early).passed());
    GCodeProcessorResult late;
    move(late, Vec3d(0., 0., 10.)); move(late, Vec3d(0., 1., 10.), true);
    move(late, Vec3d(-5., 0., 2.)); move(late, Vec3d(5., 0., 2.));
    REQUIRE(check(late).hit_moves > 0);
}

TEST_CASE("A thirty degree head rejects a forty degree uphill sweep but clears twenty", "[Nonplanar][Clearance]")
{
    const double angle = GENERATE(20., 40.);
    GCodeProcessorResult result;
    const double slope = std::tan(angle * M_PI / 180.);
    move(result, Vec3d(0., 0., 1.));
    for (int i = 1; i <= 60; ++i) move(result, Vec3d(i * 0.25, 0., 1. + i * 0.25 * slope), true);
    move(result, Vec3d(0., 0., 1.2));
    const size_t start = result.moves.size();
    for (int i = 1; i <= 60; ++i) move(result, Vec3d(i * 0.25, 0., 1.2 + i * 0.25 * slope), true);
    HeadEnvelope head; head.margin = 0.; head.lateral_margin = 0.;
    head.boxes.push_back({ "shoulder", Vec3d(1., -0.1, std::tan(M_PI / 6.)), Vec3d(1., 0.1, 10.) });
    const auto report = check(result, head);
    const bool hit = std::any_of(report.per_move.begin() + start, report.per_move.end(), [](float gap) { return gap < -1e-5f; });
    REQUIRE(hit == (angle == 40.));
}

TEST_CASE("A BC table rotation brings earlier material into the heater block", "[Nonplanar][Clearance]")
{
    RotaryGeometry geo; geo.firmware_rtcp = true;
    RotaryTableBC kin(geo);
    GCodeProcessorResult result;
    move(result, Vec3d(-4., -0.5, 0.)); move(result, Vec3d(-4., 0.5, 0.), true);
    move(result, Vec3d(0., 0., 0.));
    move(result, Vec3d(0., 0., 0.)); result.joints.back().rot[0] = 90.;
    ClearanceParams params; params.head = test_head();
    const auto report = verify_clearance(result, params, kin);
    REQUIRE(report.per_move.back() < 0.);
    REQUIRE(report.worst.front().component == "heater_block");
}

TEST_CASE("Clearance results survive copy move and reset", "[Nonplanar][Clearance]")
{
    GCodeProcessorResult source, copy, moved;
    source.nonplanar = true; source.clearance = { 1.f, -2.f }; source.clearance_report.ran = true;
    source.clearance_report.omissions = { "test omission" };
    copy = source;
    REQUIRE(copy.nonplanar); REQUIRE(copy.clearance == source.clearance);
    moved = std::move(copy);
    REQUIRE(moved.clearance == source.clearance); REQUIRE(moved.clearance_report.ran);
    REQUIRE(moved.clearance_report.omissions == source.clearance_report.omissions);
    moved.reset(); REQUIRE_FALSE(moved.nonplanar); REQUIRE(moved.clearance.empty()); REQUIRE_FALSE(moved.clearance_report.ran);
}

TEST_CASE("Clearance verification responds to cancellation and invalid resolution", "[Nonplanar][Clearance]")
{
    GCodeProcessorResult result; move(result, Vec3d::Zero()); move(result, Vec3d(1., 0., 0.));
    ClearanceParams params; params.head = test_head();
    REQUIRE_THROWS_AS(verify_clearance(result, params, ThreeAxisKinematics(), [] { throw RuntimeError("canceled"); }), RuntimeError);
    params.sweep = 0.;
    REQUIRE_THROWS_AS(verify_clearance(result, params, ThreeAxisKinematics()), RuntimeError);
}
