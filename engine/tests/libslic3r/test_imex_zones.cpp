#include <catch2/catch_all.hpp>

#include <libslic3r/BoundingBox.hpp>
#include <libslic3r/Config.hpp>
#include <string>
#include <vector>

#include "catch2/matchers/catch_matchers_floating_point.hpp"
#include "catch2/matchers/catch_matchers.hpp"
#include "catch2/catch_test_macros.hpp"
#include "libslic3r/IMEXHelpers.hpp"
#include "libslic3r/IMEXZones.hpp"
#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

namespace {

const BoundingBoxf kBed{ Vec2d(0.0, 0.0), Vec2d(200.0, 200.0) };

constexpr const char* kMode = "parallel";

struct PrinterCfg
{
    int            gantry_count     = 1;
    int            tools_per_gantry = 2;
    ImexToolLayout tool_layout      = ImexToolLayout::FrontLeft;
    std::string    active_tools;
    double         clearance_x      = 0.0;
    double         clearance_y      = 0.0;
    double         margin           = 0.0;
    bool           is_imex          = true;
};

DynamicPrintConfig make_cfg(const PrinterCfg& p)
{
    DynamicPrintConfig cfg;
    cfg.set_key_value("is_imex", new ConfigOptionBool(p.is_imex));
    cfg.set_key_value("imex_gantry_count", new ConfigOptionInt(p.gantry_count));
    cfg.set_key_value("imex_tools_per_gantry", new ConfigOptionInt(p.tools_per_gantry));
    cfg.set_deserialize_strict("imex_tool_layout", ConfigOptionEnum<ImexToolLayout>(p.tool_layout).serialize());
    cfg.set_key_value("imex_mode_names",
                      new ConfigOptionStrings(std::vector<std::string>{ kImexPrimaryMode, kMode }));
    cfg.set_key_value("imex_mode_active_tools",
                      new ConfigOptionStrings(std::vector<std::string>{ std::string(), p.active_tools }));
    cfg.set_key_value("imex_nozzle_clearance_x", new ConfigOptionFloat(p.clearance_x));
    cfg.set_key_value("imex_nozzle_clearance_y", new ConfigOptionFloat(p.clearance_y));
    cfg.set_key_value("imex_carriage_margin", new ConfigOptionFloat(p.margin));
    return cfg;
}

ImexZoneLayout layout_for(const PrinterCfg& p,
                          const std::string& plate_mode   = kMode,
                          const std::string& process_mode = std::string())
{
    return compute_imex_zone_layout(make_cfg(p), plate_mode, process_mode, kBed);
}

void check_rect(const BoundingBoxf& b, double x0, double y0, double x1, double y1)
{
    CHECK_THAT(b.min.x(), WithinAbs(x0, 1e-9));
    CHECK_THAT(b.min.y(), WithinAbs(y0, 1e-9));
    CHECK_THAT(b.max.x(), WithinAbs(x1, 1e-9));
    CHECK_THAT(b.max.y(), WithinAbs(y1, 1e-9));
}

void check_box_xy(const BoundingBoxf3& b, double x0, double y0, double x1, double y1)
{
    CHECK_THAT(b.min.x(), WithinAbs(x0, 1e-9));
    CHECK_THAT(b.min.y(), WithinAbs(y0, 1e-9));
    CHECK_THAT(b.max.x(), WithinAbs(x1, 1e-9));
    CHECK_THAT(b.max.y(), WithinAbs(y1, 1e-9));
}

void check_point(const Vec2d& p, double x, double y)
{
    CHECK_THAT(p.x(), WithinAbs(x, 1e-9));
    CHECK_THAT(p.y(), WithinAbs(y, 1e-9));
}

bool is_empty(const ImexZoneLayout& l)
{
    return l.primary_head == -1
        && !l.primary_zone_box.has_value()
        && l.head_zone_centers.empty()
        && l.copy_zones.empty()
        && l.mirror_zones.empty()
        && l.secondary_zone_boxes.empty()
        && l.collision_zones.empty()
        && l.margin_bands.empty();
}

}
TEST_CASE("a config missing the nozzle clearance keys falls back to the registered clearance",
          "[IMEXZones]")
{
    DynamicPrintConfig cfg;
    cfg.set_key_value("is_imex", new ConfigOptionBool(true));
    cfg.set_key_value("imex_gantry_count", new ConfigOptionInt(1));
    cfg.set_key_value("imex_tools_per_gantry", new ConfigOptionInt(2));
    cfg.set_key_value("imex_mode_names", new ConfigOptionStrings({ kMode }));
    cfg.set_key_value("imex_mode_active_tools", new ConfigOptionStrings({ "0:P,1:M" }));

    const ImexZoneLayout l = compute_imex_zone_layout(cfg, kMode, std::string(), kBed);

    REQUIRE(l.primary_head == 0);
    REQUIRE(l.primary_zone_box.has_value());
    check_rect(*l.primary_zone_box, 0.0, 0.0, 100.0, 200.0);

    REQUIRE(l.collision_zones.size() == 1);
    check_box_xy(l.collision_zones[0], 70.0, 0.0, 100.0, 200.0);

    CHECK(l.margin_bands.empty());
}

TEST_CASE("an IDEX mirror pair splits the bed into two columns", "[IMEXZones]")
{
    PrinterCfg p;
    p.gantry_count     = 1;
    p.tools_per_gantry = 2;
    p.active_tools     = "0:P,1:M";
    p.clearance_x      = 5.0;
    p.margin           = 2.0;

    const ImexZoneLayout l = layout_for(p);

    REQUIRE(l.primary_head == 0);
    REQUIRE(l.primary_zone_box.has_value());
    check_rect(*l.primary_zone_box, 0.0, 0.0, 100.0, 200.0);

    CHECK(l.copy_zones.empty());
    REQUIRE(l.mirror_zones.size() == 1);
    check_rect(l.mirror_zones[0], 100.0, 0.0, 200.0, 200.0);

    REQUIRE(l.head_zone_centers.size() == 2);
    check_point(l.head_zone_centers.at(0), 50.0, 100.0);
    check_point(l.head_zone_centers.at(1), 150.0, 100.0);

    REQUIRE(l.secondary_zone_boxes.size() == 1);
    check_box_xy(l.secondary_zone_boxes[0], 100.0, 0.0, 200.0, 200.0);
    CHECK_THAT(l.secondary_zone_boxes[0].min.z(), WithinAbs(-1.0, 1e-9));
    CHECK(l.secondary_zone_boxes[0].max.z() > 1000.0);

    REQUIRE(l.collision_zones.size() == 1);
    check_box_xy(l.collision_zones[0], 95.0, 0.0, 100.0, 200.0);

    REQUIRE(l.margin_bands.size() == 1);
    check_rect(l.margin_bands[0], 93.0, 0.0, 95.0, 200.0);
}

TEST_CASE("a copy secondary claims a zone but raises no collision strip", "[IMEXZones]")
{
    PrinterCfg p;
    p.gantry_count     = 1;
    p.tools_per_gantry = 2;
    p.active_tools     = "0:P,1:C";
    p.clearance_x      = 5.0;
    p.margin           = 2.0;

    const ImexZoneLayout l = layout_for(p);

    REQUIRE(l.copy_zones.size() == 1);
    check_rect(l.copy_zones[0], 100.0, 0.0, 200.0, 200.0);
    CHECK(l.mirror_zones.empty());
    CHECK(l.secondary_zone_boxes.size() == 1);
    CHECK(l.collision_zones.empty());
    CHECK(l.margin_bands.empty());
}

TEST_CASE("the tool layout decides which corner the primary zone occupies", "[IMEXZones]")
{
    struct Case { ImexToolLayout layout; const char* name; double x0, y0, x1, y1; };
    const Case cases[] = {
        { ImexToolLayout::FrontLeft,  "FrontLeft",    0.0,   0.0, 100.0, 100.0 },
        { ImexToolLayout::FrontRight, "FrontRight", 100.0,   0.0, 200.0, 100.0 },
        { ImexToolLayout::RearLeft,   "RearLeft",     0.0, 100.0, 100.0, 200.0 },
        { ImexToolLayout::RearRight,  "RearRight",  100.0, 100.0, 200.0, 200.0 },
    };

    for (const Case& c : cases) {
        DYNAMIC_SECTION(c.name) {
            PrinterCfg p;
            p.gantry_count     = 2;
            p.tools_per_gantry = 2;
            p.tool_layout      = c.layout;
            p.active_tools     = "0:P,1:C,2:M,3:M";

            const ImexZoneLayout l = layout_for(p);

            REQUIRE(l.primary_zone_box.has_value());
            check_rect(*l.primary_zone_box, c.x0, c.y0, c.x1, c.y1);
            REQUIRE(l.head_zone_centers.count(0) == 1);
            check_point(l.head_zone_centers.at(0), 0.5 * (c.x0 + c.x1), 0.5 * (c.y0 + c.y1));
            CHECK(l.secondary_zone_boxes.size() == 3);
        }
    }
}

TEST_CASE("a 2x2 IQEX with independent secondaries gives one quadrant per tool", "[IMEXZones]")
{
    PrinterCfg p;
    p.gantry_count     = 2;
    p.tools_per_gantry = 2;
    p.active_tools     = "0:P,1:C,2:M,3:M";
    p.clearance_x      = 6.0;
    p.clearance_y      = 4.0;

    const ImexZoneLayout l = layout_for(p);

    REQUIRE(l.primary_zone_box.has_value());
    check_rect(*l.primary_zone_box, 0.0, 0.0, 100.0, 100.0);

    REQUIRE(l.copy_zones.size() == 1);
    check_rect(l.copy_zones[0], 100.0, 0.0, 200.0, 100.0);

    REQUIRE(l.mirror_zones.size() == 2);
    check_rect(l.mirror_zones[0], 0.0, 100.0, 100.0, 200.0);
    check_rect(l.mirror_zones[1], 100.0, 100.0, 200.0, 200.0);

    REQUIRE(l.secondary_zone_boxes.size() == 3);
    check_box_xy(l.secondary_zone_boxes[0], 100.0, 0.0, 200.0, 100.0);
    check_box_xy(l.secondary_zone_boxes[1], 0.0, 100.0, 100.0, 200.0);
    check_box_xy(l.secondary_zone_boxes[2], 100.0, 100.0, 200.0, 200.0);

    REQUIRE(l.collision_zones.size() == 1);
    check_box_xy(l.collision_zones[0], 0.0, 96.0, 100.0, 100.0);
    CHECK(l.margin_bands.empty());
}

TEST_CASE("a Span partner collapses the far gantry into one full-width strip", "[IMEXZones]")
{
    PrinterCfg p;
    p.gantry_count     = 2;
    p.tools_per_gantry = 2;
    p.active_tools     = "0:P,1:S,2:M,3:M";
    p.clearance_x      = 6.0;
    p.clearance_y      = 4.0;

    const ImexZoneLayout l = layout_for(p);

    REQUIRE(l.primary_head == 0);
    REQUIRE(l.primary_zone_box.has_value());
    check_rect(*l.primary_zone_box, 0.0, 0.0, 200.0, 100.0);

    REQUIRE(l.mirror_zones.size() == 1);
    check_rect(l.mirror_zones[0], 0.0, 100.0, 200.0, 200.0);
    CHECK(l.copy_zones.empty());
    CHECK(l.secondary_zone_boxes.size() == 1);

    CHECK(l.head_zone_centers.count(1) == 0);
    check_point(l.head_zone_centers.at(0), 50.0, 50.0);
    check_point(l.head_zone_centers.at(2), 50.0, 150.0);
    check_point(l.head_zone_centers.at(3), 150.0, 150.0);

    REQUIRE(l.collision_zones.size() == 1);
    check_box_xy(l.collision_zones[0], 0.0, 96.0, 200.0, 100.0);
}

TEST_CASE("inactive tools donate their bed share to the active ones", "[IMEXZones]")
{
    PrinterCfg p;
    p.gantry_count     = 1;
    p.tools_per_gantry = 3;
    p.active_tools     = "0:P,2:M";
    p.clearance_x      = 5.0;

    const ImexZoneLayout l = layout_for(p);

    REQUIRE(l.primary_zone_box.has_value());
    check_rect(*l.primary_zone_box, 0.0, 0.0, 100.0, 200.0);
    CHECK_THAT(l.primary_zone_box->size().x(), WithinAbs(100.0, 1e-9));

    REQUIRE(l.mirror_zones.size() == 1);
    check_rect(l.mirror_zones[0], 100.0, 0.0, 200.0, 200.0);

    REQUIRE(l.head_zone_centers.size() == 2);
    check_point(l.head_zone_centers.at(0), 50.0, 100.0);
    check_point(l.head_zone_centers.at(2), 150.0, 100.0);

    REQUIRE(l.collision_zones.size() == 1);
    check_box_xy(l.collision_zones[0], 95.0, 0.0, 100.0, 200.0);
}

TEST_CASE("the plate mode falls back to the process preset only when it is Primary", "[IMEXZones]")
{
    PrinterCfg p;
    p.gantry_count     = 1;
    p.tools_per_gantry = 2;
    p.active_tools     = "0:P,1:M";

    SECTION("a plate still on Primary picks up the process preset's mode") {
        const ImexZoneLayout l = layout_for(p, kImexPrimaryMode, kMode);
        REQUIRE(l.primary_zone_box.has_value());
        check_rect(*l.primary_zone_box, 0.0, 0.0, 100.0, 200.0);
    }

    SECTION("a plate with its own mode ignores the process preset") {
        const ImexZoneLayout l = layout_for(p, kMode, "no-such-mode");
        REQUIRE(l.primary_zone_box.has_value());
        check_rect(*l.primary_zone_box, 0.0, 0.0, 100.0, 200.0);
    }

    SECTION("Primary on both sides yields no zones at all") {
        CHECK(is_empty(layout_for(p, kImexPrimaryMode, std::string())));
        CHECK(is_empty(layout_for(p, kImexPrimaryMode, kImexPrimaryMode)));
    }
}

TEST_CASE("configurations with nothing to divide yield an empty layout", "[IMEXZones]")
{
    PrinterCfg base;
    base.gantry_count     = 1;
    base.tools_per_gantry = 2;
    base.active_tools     = "0:P,1:M";

    SECTION("a non-IMEX printer") {
        PrinterCfg p = base;
        p.is_imex = false;
        CHECK(is_empty(layout_for(p)));
    }

    SECTION("an empty plate mode") {
        CHECK(is_empty(layout_for(base, std::string(), std::string())));
    }

    SECTION("a single-tool grid has no second zone to dim") {
        PrinterCfg p = base;
        p.gantry_count     = 1;
        p.tools_per_gantry = 1;
        p.active_tools     = "0:P";
        CHECK(is_empty(layout_for(p)));
    }

    SECTION("a mode name that the printer does not define") {
        CHECK(is_empty(layout_for(base, "carried-over-mode")));
    }

    SECTION("a mode whose tool roster is empty") {
        PrinterCfg p = base;
        p.active_tools = "";
        CHECK(is_empty(layout_for(p)));
    }
}

TEST_CASE("a Primary that is not on the grid yields no zones at all", "[IMEXZones]")
{
    PrinterCfg p;
    p.gantry_count     = 1;
    p.tools_per_gantry = 2;
    p.active_tools     = "3:P,0:M";
    p.clearance_x      = 5.0;
    p.margin           = 2.0;

    const ImexZoneLayout l = layout_for(p);

    CHECK(is_empty(l));

    CHECK(l.primary_head == -1);
    CHECK_FALSE(l.primary_zone_box.has_value());
    CHECK(l.secondary_zone_boxes.empty());
    CHECK(l.mirror_zones.empty());
    CHECK(l.head_zone_centers.empty());
    check_point(compute_imex_slice_offset(true, kMode, l.primary_zone_box), 0.0, 0.0);
}

TEST_CASE("a mode with no Primary marker at all yields no zones", "[IMEXZones]")
{
    PrinterCfg p;
    p.gantry_count     = 1;
    p.tools_per_gantry = 2;
    p.active_tools     = "0:M,1:M";
    p.clearance_x      = 5.0;

    CHECK(is_empty(layout_for(p)));
}

TEST_CASE("a Primary on the last tool of the grid lays out normally", "[IMEXZones]")
{
    SECTION("last column of a 1x2 IDEX") {
        PrinterCfg p;
        p.gantry_count     = 1;
        p.tools_per_gantry = 2;
        p.active_tools     = "1:P,0:M";
        p.clearance_x      = 5.0;
        p.margin           = 2.0;

        const ImexZoneLayout l = layout_for(p);

        REQUIRE(l.primary_head == 1);
        REQUIRE(l.primary_zone_box.has_value());
        check_rect(*l.primary_zone_box, 100.0, 0.0, 200.0, 200.0);

        REQUIRE(l.mirror_zones.size() == 1);
        check_rect(l.mirror_zones[0], 0.0, 0.0, 100.0, 200.0);

        REQUIRE(l.head_zone_centers.size() == 2);
        check_point(l.head_zone_centers.at(0), 50.0, 100.0);
        check_point(l.head_zone_centers.at(1), 150.0, 100.0);

        REQUIRE(l.collision_zones.size() == 1);
        check_box_xy(l.collision_zones[0], 100.0, 0.0, 105.0, 200.0);
        REQUIRE(l.margin_bands.size() == 1);
        check_rect(l.margin_bands[0], 105.0, 0.0, 107.0, 200.0);
    }

    SECTION("last tool of a 2x2 IQEX") {
        PrinterCfg p;
        p.gantry_count     = 2;
        p.tools_per_gantry = 2;
        p.active_tools     = "3:P,0:M";
        p.clearance_x      = 5.0;
        p.clearance_y      = 5.0;

        const ImexZoneLayout l = layout_for(p);

        REQUIRE(l.primary_head == 3);
        REQUIRE(l.primary_zone_box.has_value());
        check_rect(*l.primary_zone_box, 100.0, 100.0, 200.0, 200.0);

        REQUIRE(l.mirror_zones.size() == 1);
        check_rect(l.mirror_zones[0], 0.0, 0.0, 100.0, 100.0);

        CHECK(l.collision_zones.empty());
    }
}

TEST_CASE("zero clearance and zero margin suppress the strips they size", "[IMEXZones]")
{
    PrinterCfg p;
    p.gantry_count     = 1;
    p.tools_per_gantry = 2;
    p.active_tools     = "0:P,1:M";

    SECTION("no clearance, no strip") {
        const ImexZoneLayout l = layout_for(p);
        CHECK(l.collision_zones.empty());
        CHECK(l.margin_bands.empty());
    }

    SECTION("clearance without margin gives the strip but no advisory band") {
        p.clearance_x = 5.0;
        const ImexZoneLayout l = layout_for(p);
        CHECK(l.collision_zones.size() == 1);
        CHECK(l.margin_bands.empty());
    }
}

TEST_CASE("an aggregated gantry raises its collision strip from the zone it paints", "[IMEXZones]")
{
    struct Case { const char* name; int tools_per_gantry; const char* active_tools; };
    const Case cases[] = {
        { "two tools per gantry, mirrors on T2 and T3",   2, "0:P,1:S,2:M,3:M" },
        { "three tools per gantry, mirrors on T4 and T5", 3, "0:P,1:S,4:M,5:M" },
    };

    for (const Case& c : cases) {
        DYNAMIC_SECTION(c.name) {
            PrinterCfg p;
            p.gantry_count     = 2;
            p.tools_per_gantry = c.tools_per_gantry;
            p.active_tools     = c.active_tools;
            p.clearance_x      = 6.0;
            p.clearance_y      = 4.0;
            p.margin           = 2.0;

            const ImexZoneLayout l = layout_for(p);

            REQUIRE(l.primary_head == 0);
            REQUIRE(l.primary_zone_box.has_value());
            check_rect(*l.primary_zone_box, 0.0, 0.0, 200.0, 100.0);

            CHECK(l.copy_zones.empty());
            REQUIRE(l.mirror_zones.size() == 1);
            check_rect(l.mirror_zones[0], 0.0, 100.0, 200.0, 200.0);
            REQUIRE(l.secondary_zone_boxes.size() == 1);
            check_box_xy(l.secondary_zone_boxes[0], 0.0, 100.0, 200.0, 200.0);

            REQUIRE(l.collision_zones.size() == 1);
            check_box_xy(l.collision_zones[0], 0.0, 96.0, 200.0, 100.0);

            REQUIRE(l.margin_bands.size() == 1);
            check_rect(l.margin_bands[0], 0.0, 94.0, 200.0, 96.0);
        }
    }
}
