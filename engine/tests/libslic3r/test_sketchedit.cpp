#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS_Wire.hxx>
#include <catch2/catch_all.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include "libslic3r/CAD/SketchEngine.hpp"
#include <cmath>
#include <algorithm>
#include <TopExp_Explorer.hxx>
#include <TopAbs.hxx>
#include <math.h>
#include <vector>
#include "libslic3r/Point.hpp"

using namespace Slic3r;

using Catch::Matchers::WithinAbs;

TEST_CASE("Mirror Line across Y axis (reversed: p0 is the reflection of the source p1)", "[SketchEdit]")
{
    SketchEntity e;
    e.type = SketchEntity::Type::Line;
    e.p0   = Vec2d(3, 2);
    e.p1   = Vec2d(5, 4);

    Vec2d a(0, -1);
    Vec2d b(0, 1);

    auto result = SketchEngine::mirror_entities({e}, a, b);
    REQUIRE(result.size() == 1);

    const auto& m = result[0];
    REQUIRE(m.type == SketchEntity::Type::Line);
    REQUIRE_THAT(m.p0.x(), WithinAbs(-5.0, 1e-9));
    REQUIRE_THAT(m.p0.y(), WithinAbs(4.0, 1e-9));
    REQUIRE_THAT(m.p1.x(), WithinAbs(-3.0, 1e-9));
    REQUIRE_THAT(m.p1.y(), WithinAbs(2.0, 1e-9));
}

TEST_CASE("Mirror Circle across Y axis", "[SketchEdit]")
{
    SketchEntity e;
    e.type   = SketchEntity::Type::Circle;
    e.center = Vec2d(5, 0);
    e.p0     = Vec2d(5, 0);
    e.radius = 3;

    Vec2d a(0, -1);
    Vec2d b(0, 1);

    auto result = SketchEngine::mirror_entities({e}, a, b);
    REQUIRE(result.size() == 1);

    const auto& m = result[0];
    REQUIRE(m.type == SketchEntity::Type::Circle);
    REQUIRE_THAT(m.center.x(), WithinAbs(-5.0, 1e-9));
    REQUIRE_THAT(m.center.y(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(m.radius, WithinAbs(3.0, 1e-9));
    REQUIRE_THAT(m.p0.x(), WithinAbs(-5.0, 1e-9));
    REQUIRE_THAT(m.p0.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Mirror Arc across X axis", "[SketchEdit]")
{
    SketchEntity e;
    e.type        = SketchEntity::Type::Arc;
    e.center      = Vec2d(0, 0);
    e.radius      = 1.0;
    e.start_angle = 0.0;
    e.end_angle   = M_PI / 2.0;
    e.p0          = Vec2d(1, 0);
    e.p1          = Vec2d(0, 1);

    Vec2d a(-1, 0);
    Vec2d b(1, 0);

    auto result = SketchEngine::mirror_entities({e}, a, b);
    REQUIRE(result.size() == 1);

    const auto& m = result[0];
    REQUIRE(m.type == SketchEntity::Type::Arc);

    REQUIRE_THAT(m.p0.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(m.p0.y(), WithinAbs(-1.0, 1e-9));
    REQUIRE_THAT(m.p1.x(), WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(m.p1.y(), WithinAbs(0.0, 1e-9));

    double sweep = m.end_angle - m.start_angle;
    double orig_sweep = e.end_angle - e.start_angle;
    REQUIRE(orig_sweep > 0.0);
    REQUIRE(sweep > 0.0);
}

TEST_CASE("Offset Line by positive d", "[SketchEdit]")
{
    SketchEntity e;
    e.type = SketchEntity::Type::Line;
    e.p0   = Vec2d(0, 0);
    e.p1   = Vec2d(10, 0);

    auto result = SketchEngine::offset_entities({e}, 2.0);
    REQUIRE(result.size() == 1);

    const auto& o = result[0];
    REQUIRE(o.type == SketchEntity::Type::Line);
    REQUIRE_THAT(o.p0.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(o.p0.y(), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(o.p1.x(), WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(o.p1.y(), WithinAbs(2.0, 1e-9));
}

TEST_CASE("Offset Circle: +d shrinks (a circle is CCW), -d grows, too far collapses", "[SketchEdit]")
{
    SketchEntity e;
    e.type   = SketchEntity::Type::Circle;
    e.center = Vec2d(0, 0);
    e.p0     = Vec2d(0, 0);
    e.radius = 5;

    auto inward = SketchEngine::offset_entities({e}, 2.0);
    REQUIRE(inward.size() == 1);
    REQUIRE_THAT(inward[0].radius, WithinAbs(3.0, 1e-9));

    auto outward = SketchEngine::offset_entities({e}, -2.0);
    REQUIRE(outward.size() == 1);
    REQUIRE_THAT(outward[0].radius, WithinAbs(7.0, 1e-9));

    auto collapsed = SketchEngine::offset_entities({e}, 5.0);
    REQUIRE(collapsed.empty());
}

TEST_CASE("Offset: a full circle and the same CCW outline drawn as arcs go the same way", "[SketchEdit]")
{
    SketchEntity c;
    c.type = SketchEntity::Type::Circle; c.center = Vec2d(0, 0); c.p0 = c.center; c.radius = 5;
    SketchEntity a0, a1;
    a0.type = a1.type = SketchEntity::Type::Arc;
    a0.center = a1.center = Vec2d(0, 0);
    a0.radius = a1.radius = 5;
    a0.start_angle = 0.0;  a0.end_angle = M_PI;       a0.p0 = Vec2d(5, 0);  a0.p1 = Vec2d(-5, 0);
    a1.start_angle = M_PI; a1.end_angle = 2.0 * M_PI; a1.p0 = Vec2d(-5, 0); a1.p1 = Vec2d(5, 0);
    const auto oc = SketchEngine::offset_entities({ c }, 1.0);
    const auto oa = SketchEngine::offset_entities({ a0, a1 }, 1.0);
    REQUIRE(oc.size() == 1);
    REQUIRE(oa.size() == 2);
    CHECK_THAT(oc[0].radius, WithinAbs(oa[0].radius, 1e-9));
    CHECK_THAT(oc[0].radius, WithinAbs(oa[1].radius, 1e-9));
}

TEST_CASE("Offset Arc by positive d (left of travel: a CCW arc shrinks)", "[SketchEdit]")
{
    SketchEntity e;
    e.type        = SketchEntity::Type::Arc;
    e.center      = Vec2d(0, 0);
    e.radius      = 4.0;
    e.start_angle = 0.0;
    e.end_angle   = M_PI / 2.0;
    e.p0          = Vec2d(4, 0);
    e.p1          = Vec2d(0, 4);

    auto result = SketchEngine::offset_entities({e}, 1.0);
    REQUIRE(result.size() == 1);

    const auto& o = result[0];
    REQUIRE(o.type == SketchEntity::Type::Arc);
    REQUIRE_THAT(o.radius, WithinAbs(3.0, 1e-9));
    REQUIRE_THAT(o.p0.x(), WithinAbs(3.0, 1e-9));
    REQUIRE_THAT(o.p0.y(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(o.p1.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(o.p1.y(), WithinAbs(3.0, 1e-9));
}

TEST_CASE("Fillet right-angle corner", "[SketchEdit]")
{
    SketchEntity a;
    a.type = SketchEntity::Type::Line;
    a.p0   = Vec2d(0, 0);
    a.p1   = Vec2d(10, 0);

    SketchEntity b;
    b.type = SketchEntity::Type::Line;
    b.p0   = Vec2d(10, 0);
    b.p1   = Vec2d(10, 10);

    SketchEntity a_out, b_out, arc_out;
    bool ok = SketchEngine::fillet_lines(a, b, 2.0, a_out, b_out, arc_out);
    REQUIRE(ok);

    REQUIRE_THAT(a_out.p0.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(a_out.p0.y(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(a_out.p1.x(), WithinAbs(8.0, 1e-9));
    REQUIRE_THAT(a_out.p1.y(), WithinAbs(0.0, 1e-9));

    REQUIRE_THAT(b_out.p0.x(), WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(b_out.p0.y(), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(b_out.p1.x(), WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(b_out.p1.y(), WithinAbs(10.0, 1e-9));

    REQUIRE(arc_out.type == SketchEntity::Type::Arc);
    REQUIRE_THAT(arc_out.radius, WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(arc_out.center.x(), WithinAbs(8.0, 1e-9));
    REQUIRE_THAT(arc_out.center.y(), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT((arc_out.p0 - arc_out.center).norm(), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT((arc_out.p1 - arc_out.center).norm(), WithinAbs(2.0, 1e-9));
}

TEST_CASE("Fillet parallel lines returns false", "[SketchEdit]")
{
    SketchEntity a;
    a.type = SketchEntity::Type::Line;
    a.p0   = Vec2d(0, 0);
    a.p1   = Vec2d(10, 0);

    SketchEntity b;
    b.type = SketchEntity::Type::Line;
    b.p0   = Vec2d(0, 5);
    b.p1   = Vec2d(10, 5);

    SketchEntity a_out, b_out, arc_out;
    REQUIRE_FALSE(SketchEngine::fillet_lines(a, b, 1.0, a_out, b_out, arc_out));
}

TEST_CASE("Fillet arc too big returns false", "[SketchEdit]")
{
    SketchEntity a;
    a.type = SketchEntity::Type::Line;
    a.p0   = Vec2d(0, 0);
    a.p1   = Vec2d(1, 0);

    SketchEntity b;
    b.type = SketchEntity::Type::Line;
    b.p0   = Vec2d(1, 0);
    b.p1   = Vec2d(1, 1);

    SketchEntity a_out, b_out, arc_out;
    REQUIRE_FALSE(SketchEngine::fillet_lines(a, b, 5.0, a_out, b_out, arc_out));
}

TEST_CASE("Trim right arm", "[SketchEdit]")
{
    SketchEntity e;
    e.type = SketchEntity::Type::Line;
    e.p0   = Vec2d(-5, 0);
    e.p1   = Vec2d(5, 0);

    SketchEntity vc;
    vc.type = SketchEntity::Type::Line;
    vc.p0   = Vec2d(0, -5);
    vc.p1   = Vec2d(0, 5);

    bool ok = SketchEngine::trim_entity(e, {vc}, Vec2d(3, 0));
    REQUIRE(ok);
    REQUIRE_THAT(e.p0.x(), WithinAbs(-5.0, 1e-9));
    REQUIRE_THAT(e.p0.y(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(e.p1.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(e.p1.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Trim left arm", "[SketchEdit]")
{
    SketchEntity e;
    e.type = SketchEntity::Type::Line;
    e.p0   = Vec2d(-5, 0);
    e.p1   = Vec2d(5, 0);

    SketchEntity vc;
    vc.type = SketchEntity::Type::Line;
    vc.p0   = Vec2d(0, -5);
    vc.p1   = Vec2d(0, 5);

    bool ok = SketchEngine::trim_entity(e, {vc}, Vec2d(-3, 0));
    REQUIRE(ok);
    REQUIRE_THAT(e.p0.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(e.p0.y(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(e.p1.x(), WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(e.p1.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Trim no cut (u out of range)", "[SketchEdit]")
{
    SketchEntity e;
    e.type = SketchEntity::Type::Line;
    e.p0   = Vec2d(-5, 0);
    e.p1   = Vec2d(5, 0);

    SketchEntity other;
    other.type = SketchEntity::Type::Line;
    other.p0   = Vec2d(0, 3);
    other.p1   = Vec2d(0, 8);

    REQUIRE_FALSE(SketchEngine::trim_entity(e, {other}, Vec2d(3, 0)));
}

TEST_CASE("Extend forward to line", "[SketchEdit]")
{
    SketchEntity e;
    e.type = SketchEntity::Type::Line;
    e.p0   = Vec2d(0, 0);
    e.p1   = Vec2d(2, 0);

    SketchEntity other;
    other.type = SketchEntity::Type::Line;
    other.p0   = Vec2d(5, -5);
    other.p1   = Vec2d(5, 5);

    bool ok = SketchEngine::extend_entity(e, {other}, Vec2d(2, 0));
    REQUIRE(ok);
    REQUIRE_THAT(e.p0.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(e.p0.y(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(e.p1.x(), WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(e.p1.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Extend forward to circle", "[SketchEdit]")
{
    SketchEntity e;
    e.type = SketchEntity::Type::Line;
    e.p0   = Vec2d(0, 0);
    e.p1   = Vec2d(2, 0);

    SketchEntity other;
    other.type   = SketchEntity::Type::Circle;
    other.center = Vec2d(10, 0);
    other.p0     = Vec2d(10, 0);
    other.radius = 3;

    bool ok = SketchEngine::extend_entity(e, {other}, Vec2d(2, 0));
    REQUIRE(ok);
    REQUIRE_THAT(e.p0.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(e.p0.y(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(e.p1.x(), WithinAbs(7.0, 1e-9));
    REQUIRE_THAT(e.p1.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Extend backward", "[SketchEdit]")
{
    SketchEntity e;
    e.type = SketchEntity::Type::Line;
    e.p0   = Vec2d(0, 0);
    e.p1   = Vec2d(2, 0);

    SketchEntity other;
    other.type = SketchEntity::Type::Line;
    other.p0   = Vec2d(-3, -5);
    other.p1   = Vec2d(-3, 5);

    bool ok = SketchEngine::extend_entity(e, {other}, Vec2d(0, 0));
    REQUIRE(ok);
    REQUIRE_THAT(e.p0.x(), WithinAbs(-3.0, 1e-9));
    REQUIRE_THAT(e.p0.y(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(e.p1.x(), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(e.p1.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Extend no target", "[SketchEdit]")
{
    SketchEntity e;
    e.type = SketchEntity::Type::Line;
    e.p0   = Vec2d(0, 0);
    e.p1   = Vec2d(2, 0);

    SketchEntity other;
    other.type = SketchEntity::Type::Line;
    other.p0   = Vec2d(5, -5);
    other.p1   = Vec2d(5, -1);

    REQUIRE_FALSE(SketchEngine::extend_entity(e, {other}, Vec2d(2, 0)));
}

TEST_CASE("Trim arc drops the picked (start) side", "[SketchEdit]")
{
    SketchEntity e;
    e.type        = SketchEntity::Type::Arc;
    e.center      = Vec2d(0, 0);
    e.radius      = 5;
    e.start_angle = 0.0;
    e.end_angle   = M_PI;

    SketchEntity cut;
    cut.type = SketchEntity::Type::Line;
    cut.p0   = Vec2d(0, -10);
    cut.p1   = Vec2d(0, 10);

    bool ok = SketchEngine::trim_entity(e, {cut}, Vec2d(5 * std::cos(M_PI/4), 5 * std::sin(M_PI/4)));
    REQUIRE(ok);
    REQUIRE(e.type == SketchEntity::Type::Arc);
    REQUIRE_THAT(e.radius, WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(e.start_angle, WithinAbs(M_PI / 2.0, 1e-9));
    REQUIRE_THAT(e.end_angle,   WithinAbs(M_PI, 1e-9));
    CHECK_THAT(e.p0.x(), WithinAbs(0.0, 1e-9));  CHECK_THAT(e.p0.y(), WithinAbs(5.0, 1e-9));
    CHECK_THAT(e.p1.x(), WithinAbs(-5.0, 1e-9)); CHECK_THAT(e.p1.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Trim arc drops the picked (end) side", "[SketchEdit]")
{
    SketchEntity e;
    e.type        = SketchEntity::Type::Arc;
    e.center      = Vec2d(0, 0);
    e.radius      = 5;
    e.start_angle = 0.0;
    e.end_angle   = M_PI;

    SketchEntity cut;
    cut.type = SketchEntity::Type::Line;
    cut.p0   = Vec2d(0, -10);
    cut.p1   = Vec2d(0, 10);

    bool ok = SketchEngine::trim_entity(e, {cut}, Vec2d(5 * std::cos(3*M_PI/4), 5 * std::sin(3*M_PI/4)));
    REQUIRE(ok);
    REQUIRE(e.type == SketchEntity::Type::Arc);
    REQUIRE_THAT(e.start_angle, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(e.end_angle,   WithinAbs(M_PI / 2.0, 1e-9));
    CHECK_THAT(e.p0.x(), WithinAbs(5.0, 1e-9)); CHECK_THAT(e.p0.y(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(e.p1.x(), WithinAbs(0.0, 1e-9)); CHECK_THAT(e.p1.y(), WithinAbs(5.0, 1e-9));
}

TEST_CASE("Trim circle opens into an arc excluding the pick", "[SketchEdit]")
{
    SketchEntity e;
    e.type   = SketchEntity::Type::Circle;
    e.center = Vec2d(0, 0);
    e.p0     = e.center;
    e.radius = 5;

    SketchEntity cut;
    cut.type = SketchEntity::Type::Line;
    cut.p0   = Vec2d(0, -10);
    cut.p1   = Vec2d(0, 10);

    bool ok = SketchEngine::trim_entity(e, {cut}, Vec2d(5, 0));
    REQUIRE(ok);
    REQUIRE(e.type == SketchEntity::Type::Arc);
    REQUIRE_THAT(e.radius, WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(e.end_angle - e.start_angle, WithinAbs(M_PI, 1e-9));
    double mid = 0.5 * (e.start_angle + e.end_angle);
    REQUIRE_THAT(5 * std::cos(mid), WithinAbs(-5.0, 1e-9));
    REQUIRE_THAT(5 * std::sin(mid), WithinAbs(0.0, 1e-9));
    CHECK_THAT((e.p0 - e.center).norm(), WithinAbs(5.0, 1e-9));
    CHECK_THAT((e.p1 - e.center).norm(), WithinAbs(5.0, 1e-9));
    CHECK_THAT(e.p0.x(), WithinAbs(5.0 * std::cos(e.start_angle), 1e-9));
    CHECK_THAT(e.p1.x(), WithinAbs(5.0 * std::cos(e.end_angle), 1e-9));
}

TEST_CASE("Extend arc forward (end) to a crossing", "[SketchEdit]")
{
    SketchEntity e;
    e.type        = SketchEntity::Type::Arc;
    e.center      = Vec2d(0, 0);
    e.radius      = 5;
    e.start_angle = 0.0;
    e.end_angle   = M_PI / 2.0;

    SketchEntity cut;
    cut.type = SketchEntity::Type::Line;
    cut.p0   = Vec2d(-10, 0);
    cut.p1   = Vec2d(0, 0);

    bool ok = SketchEngine::extend_entity(e, {cut}, Vec2d(0, 5));
    REQUIRE(ok);
    REQUIRE(e.type == SketchEntity::Type::Arc);
    REQUIRE_THAT(e.start_angle, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(e.end_angle,   WithinAbs(M_PI, 1e-9));
    CHECK_THAT(e.p1.x(), WithinAbs(-5.0, 1e-9)); CHECK_THAT(e.p1.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Extend arc backward (start) to a crossing", "[SketchEdit]")
{
    SketchEntity e;
    e.type        = SketchEntity::Type::Arc;
    e.center      = Vec2d(0, 0);
    e.radius      = 5;
    e.start_angle = M_PI / 2.0;
    e.end_angle   = M_PI;

    SketchEntity cut;
    cut.type = SketchEntity::Type::Line;
    cut.p0   = Vec2d(10, 0);
    cut.p1   = Vec2d(0, 0);

    bool ok = SketchEngine::extend_entity(e, {cut}, Vec2d(0, 5));
    REQUIRE(ok);
    REQUIRE_THAT(e.start_angle, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(e.end_angle,   WithinAbs(M_PI, 1e-9));
}

TEST_CASE("Extend circle returns false (closed)", "[SketchEdit]")
{
    SketchEntity e;
    e.type   = SketchEntity::Type::Circle;
    e.center = Vec2d(0, 0);
    e.p0     = e.center;
    e.radius = 5;

    SketchEntity cut;
    cut.type = SketchEntity::Type::Line;
    cut.p0   = Vec2d(0, -10);
    cut.p1   = Vec2d(0, 10);

    REQUIRE_FALSE(SketchEngine::extend_entity(e, {cut}, Vec2d(5, 0)));
}

TEST_CASE("Trim arc with no crossing returns false", "[SketchEdit]")
{
    SketchEntity e;
    e.type        = SketchEntity::Type::Arc;
    e.center      = Vec2d(0, 0);
    e.radius      = 5;
    e.start_angle = 0.0;
    e.end_angle   = M_PI / 2.0;

    SketchEntity cut;
    cut.type = SketchEntity::Type::Line;
    cut.p0   = Vec2d(20, -5);
    cut.p1   = Vec2d(20, 5);

    REQUIRE_FALSE(SketchEngine::trim_entity(e, {cut}, Vec2d(5 * std::cos(M_PI/4), 5 * std::sin(M_PI/4))));
}

TEST_CASE("entities_to_wires keeps every edge of a loop drawn out of order", "[SketchEngine]")
{
    std::vector<SketchEntity> ents(6);

    ents[0].type = SketchEntity::Type::Line;
    ents[0].p0   = Vec2d(-0.537697713190522, -0.0009077462579133498);
    ents[0].p1   = Vec2d(99.46230228680926, -0.0009141694814321626);

    ents[1].type        = SketchEntity::Type::Arc;
    ents[1].p0          = Vec2d(-0.537697713190522, -0.0009077462579133498);
    ents[1].p1          = Vec2d(-100.14602636660666, -0.27673132181233495);
    ents[1].center      = Vec2d(-50.313868583115394, -10.248112903179617);
    ents[1].radius      = 50.81999999999999;
    ents[1].start_angle = 0.20302922018398933;
    ents[1].end_angle   = 2.944101582158999;

    ents[2].type = SketchEntity::Type::Line;
    ents[2].p0   = Vec2d(99.46228668626469, -39.22091416947833);
    ents[2].p1   = Vec2d(-0.537864366432629, -39.22420589376945);

    ents[3].type        = SketchEntity::Type::Arc;
    ents[3].p0          = Vec2d(-100.14602636694521, -38.94673132181234);
    ents[3].p1          = Vec2d(-0.5378420354900413, -39.22421049164698);
    ents[3].center      = Vec2d(-50.31377078666179, -28.975499083790503);
    ents[3].radius      = 50.82006659345552;
    ents[3].start_angle = -2.944104841286737;
    ents[3].end_angle   = -0.20305921095748136;

    ents[4].type = SketchEntity::Type::Line;
    ents[4].p0   = Vec2d(99.46228668626469, -39.22091416947833);
    ents[4].p1   = Vec2d(99.46230228680926, -0.0009141694814321626);

    ents[5].type = SketchEntity::Type::Line;
    ents[5].p0   = Vec2d(-100.14602636694521, -38.94673132181234);
    ents[5].p1   = Vec2d(-100.14602636660666, -0.27673132181233495);

    auto wires = SketchEngine::entities_to_wires(ents, SketchPlane::XY());

    REQUIRE(wires.size() == 1);

    int edge_count = 0;
    for (TopExp_Explorer ex(wires[0], TopAbs_EDGE); ex.More(); ex.Next())
        ++edge_count;
    REQUIRE(edge_count == 6);

    REQUIRE(wires[0].Closed());
}

TEST_CASE("a loop the viewport shades closed is buildable by the kernel", "[SketchEngine]")
{
    std::vector<SketchEntity> ents(4);

    ents[0].type = SketchEntity::Type::Line;
    ents[0].p0   = Vec2d(0, 0);
    ents[0].p1   = Vec2d(20, 0);

    ents[1].type = SketchEntity::Type::Line;
    ents[1].p0   = Vec2d(20, 10);
    ents[1].p1   = Vec2d(0, 10);

    ents[2].type = SketchEntity::Type::Line;
    ents[2].p0   = Vec2d(20, 0);
    ents[2].p1   = Vec2d(20, 10);

    ents[3].type = SketchEntity::Type::Line;
    ents[3].p0   = Vec2d(0, 10);
    ents[3].p1   = Vec2d(0.0009, 0);

    auto wires = SketchEngine::entities_to_wires(ents, SketchPlane::XY());

    REQUIRE(wires.size() == 1);

    int edge_count = 0;
    for (TopExp_Explorer ex(wires[0], TopAbs_EDGE); ex.More(); ex.Next())
        ++edge_count;
    REQUIRE(edge_count == 4);

    REQUIRE(wires[0].Closed());
}

TEST_CASE("auto-close off makes the kernel demand an exact joint", "[SketchEngine]")
{
    std::vector<SketchEntity> ents(4);

    ents[0].type = SketchEntity::Type::Line;
    ents[0].p0   = Vec2d(0, 0);
    ents[0].p1   = Vec2d(20, 0);

    ents[1].type = SketchEntity::Type::Line;
    ents[1].p0   = Vec2d(20, 10);
    ents[1].p1   = Vec2d(0, 10);

    ents[2].type = SketchEntity::Type::Line;
    ents[2].p0   = Vec2d(20, 0);
    ents[2].p1   = Vec2d(20, 10);

    ents[3].type = SketchEntity::Type::Line;
    ents[3].p0   = Vec2d(0, 10);
    ents[3].p1   = Vec2d(0.0009, 0);

    auto edge_count = [](const TopoDS_Wire& w) {
        int n = 0;
        for (TopExp_Explorer ex(w, TopAbs_EDGE); ex.More(); ex.Next()) ++n;
        return n;
    };

    Slic3r::set_sketch_auto_close(true);
    auto wires_on = SketchEngine::entities_to_wires(ents, SketchPlane::XY());
    REQUIRE(wires_on.size() == 1);
    REQUIRE(edge_count(wires_on[0]) == 4);
    REQUIRE(wires_on[0].Closed());

    Slic3r::set_sketch_auto_close(false);
    auto wires_off = SketchEngine::entities_to_wires(ents, SketchPlane::XY());
    REQUIRE(wires_off.size() == 1);
    REQUIRE(edge_count(wires_off[0]) == 4);
    REQUIRE_FALSE(wires_off[0].Closed());

    ents[3].p1 = Vec2d(0, 0);
    auto wires_exact = SketchEngine::entities_to_wires(ents, SketchPlane::XY());
    REQUIRE(wires_exact.size() == 1);
    REQUIRE(edge_count(wires_exact[0]) == 4);
    REQUIRE(wires_exact[0].Closed());

    Slic3r::set_sketch_auto_close(true);
}

TEST_CASE("a stray open segment does not break a closed profile", "[SketchEngine]")
{
    auto line = [](double x0, double y0, double x1, double y1) {
        SketchEntity e;
        e.type = SketchEntity::Type::Line;
        e.p0   = Vec2d(x0, y0);
        e.p1   = Vec2d(x1, y1);
        return e;
    };

    std::vector<SketchEntity> ents;
    ents.push_back(line(0, 0, 20, 0));
    ents.push_back(line(20, 0, 20, 10));
    ents.push_back(line(20, 10, 0, 10));
    ents.push_back(line(0, 10, 0, 0));
    ents.push_back(line(5, 5, 6, 5.2));

    auto edge_count = [](const TopoDS_Wire& w) {
        int n = 0;
        for (TopExp_Explorer ex(w, TopAbs_EDGE); ex.More(); ex.Next()) ++n;
        return n;
    };

    auto wires_all = SketchEngine::entities_to_wires(ents, SketchPlane::XY(), false);
    REQUIRE(wires_all.size() == 2);

    auto wires_closed = SketchEngine::entities_to_wires(ents, SketchPlane::XY(), true);
    REQUIRE(wires_closed.size() == 1);
    REQUIRE(edge_count(wires_closed[0]) == 4);
    REQUIRE(wires_closed[0].Closed());

    TopoDS_Wire w = SketchEngine::entities_to_wire(ents, SketchPlane::XY(), true);
    REQUIRE_FALSE(w.IsNull());
    REQUIRE(edge_count(w) == 4);
}

TEST_CASE("sketch_open_ends names where a chain fails to close", "[SketchEngine]")
{
    auto line = [](double x0, double y0, double x1, double y1) {
        SketchEntity e;
        e.type = SketchEntity::Type::Line;
        e.p0   = Vec2d(x0, y0);
        e.p1   = Vec2d(x1, y1);
        return e;
    };

    std::vector<SketchEntity> ents;
    ents.push_back(line(0, 0, 10, 0));
    ents.push_back(line(10, 0, 10, 10));
    ents.push_back(line(10, 10, 0, 10));

    auto got = sketch_open_ends(ents, SketchPlane::XY());
    REQUIRE(got.size() == 2);

    std::sort(got.begin(), got.end(), [](const Vec2d& a, const Vec2d& b) {
        if (a.x() < b.x()) return true;
        if (a.x() > b.x()) return false;
        return a.y() < b.y();
    });

    REQUIRE_THAT(got[0].x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(got[0].y(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(got[1].x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(got[1].y(), WithinAbs(10.0, 1e-9));
}

TEST_CASE("Mirror EllipseArc keeps the same arc, not its complement", "[SketchEdit]")
{
    SketchEntity e;
    e.type = SketchEntity::Type::EllipseArc;
    e.center = Vec2d(10, 0); e.radius = 6; e.rminor = 3; e.rotation = 0.0;
    e.start_angle = 0.0; e.end_angle = M_PI / 2.0;
    e.p0 = Vec2d(16, 0); e.p1 = Vec2d(10, 3);
    const auto out = SketchEngine::mirror_entities({ e }, Vec2d(0, -1), Vec2d(0, 1));
    REQUIRE(out.size() == 1);
    const SketchEntity& m = out[0];
    REQUIRE(m.type == SketchEntity::Type::EllipseArc);
    CHECK_THAT(m.end_angle - m.start_angle, WithinAbs(M_PI / 2.0, 1e-9));
    const double cr = std::cos(m.rotation), sr = std::sin(m.rotation);
    auto at = [&](double t) {
        return Vec2d(m.center.x() + m.radius * std::cos(t) * cr - m.rminor * std::sin(t) * sr,
                     m.center.y() + m.radius * std::cos(t) * sr + m.rminor * std::sin(t) * cr);
    };
    CHECK((at(m.start_angle) - m.p0).norm() < 1e-6);
    CHECK((at(m.end_angle)   - m.p1).norm() < 1e-6);
    CHECK(at(0.5 * (m.start_angle + m.end_angle)).y() > 1.0);
}

TEST_CASE("Bridge between two collinear lines is straight and stays between them", "[SketchEdit]")
{
    SketchEntity a, b;
    a.type = b.type = SketchEntity::Type::Line;
    a.p0 = Vec2d(-10, 0); a.p1 = Vec2d(0, 0);
    b.p0 = Vec2d(9, 0);   b.p1 = Vec2d(19, 0);
    const SketchEntity br = SketchEngine::make_bridge(a, 1, b, 0);
    REQUIRE(br.ctrl.size() == 4);
    CHECK(br.ctrl[1].x() > 0.0);
    CHECK(br.ctrl[2].x() < 9.0);
    for (const Vec2d& p : br.ctrl) CHECK_THAT(p.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Transform with a negative scale keeps an arc on its endpoints", "[SketchEdit]")
{
    SketchEntity e;
    e.type = SketchEntity::Type::Arc;
    e.center = Vec2d(3, 0); e.radius = 2; e.start_angle = 0.0; e.end_angle = M_PI / 2.0;
    e.p0 = Vec2d(5, 0); e.p1 = Vec2d(3, 2);
    const auto out = SketchEngine::transform_entities({ e }, Vec2d(0, 0), 0.0, -1.0, Vec2d(0, 0));
    REQUIRE(out.size() == 1);
    const SketchEntity& m = out[0];
    CHECK((m.center - Vec2d(-3, 0)).norm() < 1e-9);
    CHECK((m.p0 - Vec2d(-5, 0)).norm() < 1e-9);
    CHECK((m.p1 - Vec2d(-3, -2)).norm() < 1e-9);
}

TEST_CASE("A zero-radius circle does not build a wire (and does not throw)", "[SketchEdit]")
{
    SketchEntity c;
    c.type = SketchEntity::Type::Circle; c.center = Vec2d(0, 0); c.p0 = c.center; c.radius = 0.0;
    std::vector<TopoDS_Wire> w;
    REQUIRE_NOTHROW(w = SketchEngine::entities_to_wires({ c }, SketchPlane::XY(), true));
    CHECK(w.empty());
}
