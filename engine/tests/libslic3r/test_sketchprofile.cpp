#include <TopoDS_Face.hxx>
#include <catch2/catch_all.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include "libslic3r/CAD/SketchEngine.hpp"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include "libslic3r/Point.hpp"
#include <vector>
#include <math.h>
#include <cstddef>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

namespace {

SketchPlane xy_plane() { return SketchPlane::XY(); }

SketchEntity line(const Vec2d& a, const Vec2d& b)
{
    SketchEntity e;
    e.type = SketchEntity::Type::Line;
    e.p0 = a; e.p1 = b;
    return e;
}

std::vector<SketchEntity> rect(double w, double h)
{
    return { line({0, 0}, {w, 0}), line({w, 0}, {w, h}),
             line({w, h}, {0, h}), line({0, h}, {0, 0}) };
}

int closed_wires(const std::vector<SketchEntity>& ents)
{
    const auto ws = SketchEngine::entities_to_wires(ents, xy_plane());
    int n = 0;
    for (const auto& w : ws)
        if (!w.IsNull() && w.Closed()) ++n;
    return n;
}

double profile_area(const std::vector<SketchEntity>& ents)
{
    const auto ws = SketchEngine::entities_to_wires(ents, xy_plane());
    if (ws.size() != 1 || ws[0].IsNull() || !ws[0].Closed()) return -1.0;
    const TopoDS_Face f = SketchEngine::wires_to_face(ws, xy_plane());
    GProp_GProps props;
    BRepGProp::SurfaceProperties(f, props);
    return props.Mass();
}

SketchEntity arc(const Vec2d& c, double r, double a0, double a1)
{
    SketchEntity e;
    e.type        = SketchEntity::Type::Arc;
    e.center      = c;
    e.radius      = r;
    e.start_angle = a0;
    e.end_angle   = a1;
    e.p0 = c + r * Vec2d(std::cos(a0), std::sin(a0));
    e.p1 = c + r * Vec2d(std::cos(a1), std::sin(a1));
    return e;
}

}

TEST_CASE("profile baseline: a hand-built rectangle is one closed loop", "[SketchProfile]")
{
    REQUIRE(closed_wires(rect(40, 20)) == 1);
}

TEST_CASE("profile: mirroring a closed rectangle keeps it closed", "[SketchProfile]")
{
    const auto m = SketchEngine::mirror_entities(rect(40, 20), Vec2d(-10, 0), Vec2d(-10, 1));
    REQUIRE(m.size() == 4);
    REQUIRE(closed_wires(m) == 1);
}

TEST_CASE("profile: mirroring an open half-profile closes it against the axis", "[SketchProfile]")
{
    const std::vector<SketchEntity> half = {
        line({0, 0}, {20, 0}), line({20, 0}, {20, 10}), line({20, 10}, {0, 10}) };
    auto all = half;
    for (const auto& e : SketchEngine::mirror_entities(half, Vec2d(0, 0), Vec2d(0, 1)))
        all.push_back(e);
    REQUIRE(all.size() == 6);
    REQUIRE(closed_wires(all) == 1);
}

TEST_CASE("profile: offsetting a closed rectangle keeps it closed", "[SketchProfile]")
{
    const auto out = SketchEngine::offset_entities(rect(40, 20), 5.0);
    REQUIRE(out.size() == 4);
    REQUIRE(closed_wires(out) == 1);
}

TEST_CASE("profile: offset outward grows the enclosed area by the right amount", "[SketchProfile]")
{
    const double w = 40, h = 20, d = 5;
    const auto out = SketchEngine::offset_entities(rect(w, h), d);
    const auto ws  = SketchEngine::entities_to_wires(out, xy_plane());
    REQUIRE(ws.size() == 1);
    REQUIRE(ws[0].Closed());
}

TEST_CASE("profile: offset sign is left-of-travel, so +d shrinks a CCW rectangle", "[SketchProfile]")
{
    const double w = 40, h = 20, d = 5;
    REQUIRE_THAT(profile_area(SketchEngine::offset_entities(rect(w, h), d)),
                 WithinAbs((w - 2 * d) * (h - 2 * d), 1e-6));
    REQUIRE_THAT(profile_area(SketchEngine::offset_entities(rect(w, h), -d)),
                 WithinAbs((w + 2 * d) * (h + 2 * d), 1e-6));
}

TEST_CASE("profile: offsetting a stadium (two lines + two arcs) stays closed", "[SketchProfile]")
{
    const double L = 30, r = 8, d = 3;
    const std::vector<SketchEntity> slot = {
        line({0, -r}, {L, -r}),
        arc({L, 0}, r, -M_PI / 2, M_PI / 2),
        line({L, r}, {0, r}),
        arc({0, 0}, r, M_PI / 2, 3 * M_PI / 2),
    };
    REQUIRE(closed_wires(slot) == 1);
    const auto out = SketchEngine::offset_entities(slot, -d);
    REQUIRE(closed_wires(out) == 1);
    const double rr = r + d;
    REQUIRE_THAT(profile_area(out), WithinAbs(L * 2 * rr + M_PI * rr * rr, 1e-6));
}

TEST_CASE("profile: an open chain offsets without being forced closed", "[SketchProfile]")
{
    const std::vector<SketchEntity> open_chain = {
        line({0, 0}, {20, 0}), line({20, 0}, {20, 10}) };
    const auto out = SketchEngine::offset_entities(open_chain, 4.0);
    REQUIRE(out.size() == 2);
    REQUIRE(closed_wires(out) == 0);
    REQUIRE_THAT((out[0].p1 - out[1].p0).norm(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("profile: a mirrored half offsets as one loop, not two", "[SketchProfile]")
{
    const double L = 30, R = 15, d = 4;
    const std::vector<SketchEntity> half = {
        line({0, -R}, {L, -R}),
        arc({L, 0}, R, -M_PI / 2, M_PI / 2),
        line({L, R}, {0, R}),
    };
    std::vector<SketchEntity> all = half;
    for (const auto& e : SketchEngine::mirror_entities(half, Vec2d(0, 0), Vec2d(0, 1)))
        all.push_back(e);
    REQUIRE(closed_wires(all) == 1);

    const auto out = SketchEngine::offset_entities(all, -d);
    REQUIRE(closed_wires(out) == 1);
    for (const auto& o : out)
        if (o.type == SketchEntity::Type::Arc)
            REQUIRE_THAT(o.radius, WithinAbs(R + d, 1e-9));
}

TEST_CASE("profile: a mirrored half continues the original chain", "[SketchProfile]")
{
    const std::vector<SketchEntity> half = {
        line({0, -15}, {50, -15}), line({50, -15}, {50, 15}), line({50, 15}, {0, 15}) };
    const auto m = SketchEngine::mirror_entities(half, Vec2d(0, 0), Vec2d(0, 1));
    REQUIRE(m.size() == 3);

    REQUIRE_THAT((half.back().p1 - m.front().p0).norm(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT((m.back().p1   - half.front().p0).norm(), WithinAbs(0.0, 1e-9));

    for (size_t i = 0; i + 1 < m.size(); ++i)
        REQUIRE_THAT((m[i].p1 - m[i + 1].p0).norm(), WithinAbs(0.0, 1e-9));

    auto all = half;
    for (const auto& e : m) all.push_back(e);
    REQUIRE(closed_wires(all) == 1);
}
