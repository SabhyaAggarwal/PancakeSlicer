#include <algorithm>
#include <catch2/catch_all.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include <cstddef>
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "test_helpers.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/TriangleMesh.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

static std::vector<GCodeFlavor> non_klipper_flavors()
{
    std::vector<GCodeFlavor> flavors;
    for (const auto &[name, value] : ConfigOptionEnum<GCodeFlavor>::get_enum_values())
        if (GCodeFlavor(value) != gcfKlipper)
            flavors.push_back(GCodeFlavor(value));
    return flavors;
}

static std::string flavor_name(GCodeFlavor flavor)
{
    return ConfigOptionEnum<GCodeFlavor>::get_enum_names()[int(flavor)];
}

TEST_CASE("Klipper flushes the wipe tower planner queue with M400", "[WipeTower]")
{
    CHECK(std::string(flush_planner_queue_command(gcfKlipper)) == "M400\n");
}

TEST_CASE("Other flavors flush the wipe tower planner queue with a zero dwell", "[WipeTower]")
{
    const GCodeFlavor flavor = GENERATE(from_range(non_klipper_flavors()));
    INFO("gcode flavor: " << flavor_name(flavor));
    CHECK(std::string(flush_planner_queue_command(flavor)) == "G4 S0\n");
}

TEST_CASE("Klipper waits in the wipe tower with a millisecond dwell", "[WipeTower]")
{
    CHECK(wait_command(gcfKlipper, 1.5f) == "G4 P1500\n");
}

TEST_CASE("Other flavors wait in the wipe tower with a seconds dwell", "[WipeTower]")
{
    const GCodeFlavor flavor = GENERATE(from_range(non_klipper_flavors()));
    INFO("gcode flavor: " << flavor_name(flavor));
    CHECK(wait_command(flavor, 1.5f) == "G4 S1.500\n");
}

TEST_CASE("The wipe tower placement clamp follows a non-rectangular bed outline", "[WipeTower]")
{
    const coord_t margin = scaled<coord_t>(1.);
    auto square_at = [](double x, double y, double side) {
        return BoundingBox(Point::new_scale(x, y), Point::new_scale(x + side, y + side));
    };
    auto lands_inside = [](BoundingBox box, const Polygons &bed, const Vec2f &move, coord_t pad) {
        box.translate(Point::new_scale(move.x(), move.y()));
        return diff(Polygons{box.inflated(pad).polygon()}, bed).empty();
    };

    const Polygons hex_bed{make_circle_num_segments(scaled<double>(100.), 6)};
    const Polygons square_bed{Polygon::new_scale(Pointfs{{0., 0.}, {200., 0.}, {200., 200.}, {0., 200.}})};

    SECTION("a rectangular bed is left to the bounding box clamp") {
        const Vec2f move = WipeTower::move_box_inside_polygon(square_at(50., 50., 30.), square_bed, margin);
        CHECK_THAT(move.x(), Catch::Matchers::WithinAbs(0., 1e-6));
        CHECK_THAT(move.y(), Catch::Matchers::WithinAbs(0., 1e-6));
    }

    SECTION("only the violated axis is clamped") {
        const Vec2f move = WipeTower::move_box_inside_polygon(square_at(185., 50., 30.), square_bed, margin);
        CHECK_THAT(move.x(), Catch::Matchers::WithinAbs(-16., 1e-6));
        CHECK_THAT(move.y(), Catch::Matchers::WithinAbs(0., 1e-6));
    }

    SECTION("a footprint already inside the outline is left alone") {
        const Vec2f move = WipeTower::move_box_inside_polygon(square_at(-15., -15., 30.), hex_bed, margin);
        CHECK_THAT(move.x(), Catch::Matchers::WithinAbs(0., 1e-6));
        CHECK_THAT(move.y(), Catch::Matchers::WithinAbs(0., 1e-6));
    }

    SECTION("a footprint in the bounding box corner is pulled onto the bed") {
        const BoundingBox box = square_at(55., 50., 30.);
        REQUIRE_FALSE(lands_inside(box, hex_bed, Vec2f::Zero(), margin));
        CHECK(lands_inside(box, hex_bed, WipeTower::move_box_inside_polygon(box, hex_bed, margin), margin));
    }

    SECTION("a negative margin still lands the footprint inside the outline") {
        const BoundingBox box = square_at(55., 50., 30.);
        const coord_t     brim = scaled<coord_t>(-0.5);
        CHECK(lands_inside(box, hex_bed, WipeTower::move_box_inside_polygon(box, hex_bed, brim), 0));
    }

    SECTION("a footprint too large for the bed is left alone") {
        const Vec2f move = WipeTower::move_box_inside_polygon(square_at(-200., -200., 400.), hex_bed, margin);
        CHECK_THAT(move.x(), Catch::Matchers::WithinAbs(0., 1e-6));
        CHECK_THAT(move.y(), Catch::Matchers::WithinAbs(0., 1e-6));
    }
}

static std::string wipe_tower_regions(const std::string &gcode)
{
    const std::string &start_tag = GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_Start);
    const std::string &end_tag   = GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_End);
    std::string regions;
    size_t pos = 0;
    while (true) {
        size_t start = gcode.find(start_tag, pos);
        if (start == std::string::npos)
            break;
        size_t end = gcode.find(end_tag, start);
        if (end == std::string::npos)
            break;
        regions.append(gcode, start, end - start);
        pos = end + 1;
    }
    return regions;
}

static DynamicPrintConfig wipe_tower_toolchange_config(const std::string &gcode_flavor)
{
    return multifilament_config(2, {
        { "sparse_infill_filament_id",  1 },
        { "internal_solid_filament_id", 1 },
        { "top_surface_filament_id",    1 },
        { "bottom_surface_filament_id", 1 },
        { "outer_wall_filament_id",     2 },
        { "inner_wall_filament_id",     2 },
        { "enable_prime_tower",         true },
        { "wipe_tower_x",               50 },
        { "wipe_tower_y",               50 },
        { "layer_height",               0.3 },
        { "gcode_flavor",               gcode_flavor },
    });
}

static std::string slice_with_prime_tower(const DynamicPrintConfig &config)
{
    Print print;
    Model model;
    init_print({ cube(10) }, print, model, config);
    print.apply(model, config);
    return gcode(print);
}

TEST_CASE("The wipe tower's toolchange planner flush follows the gcode flavor", "[WipeTower]")
{
    auto [flavor, expected, unexpected] = GENERATE(table<std::string, std::string, std::string>({
        { "klipper", "M400",  "G4 S0" },
        { "marlin",  "G4 S0", "M400"  } }));
    DYNAMIC_SECTION(flavor) {
        const std::string tower = wipe_tower_regions(slice_with_prime_tower(wipe_tower_toolchange_config(flavor)));
        REQUIRE_FALSE(tower.empty());
        CHECK_THAT(tower, Catch::Matchers::ContainsSubstring(expected));
        CHECK_THAT(tower, !Catch::Matchers::ContainsSubstring(unexpected));
    }
}

static DynamicPrintConfig tower_estimate_config(const char *wall_type, unsigned int filaments = 2)
{
    return multifilament_config(filaments, {
        { "outer_wall_filament_id",         filaments == 2 ? "2" : "1" },
        { "enable_prime_tower",             "1"       },
        { "wipe_tower_wall_type",           wall_type },
        { "prime_tower_width",              "50"      },
        { "prime_volume",                   "100"     },
        { "prime_tower_infill_gap",         "100%"    },
        { "prime_tower_brim_width",         "3"       },
        { "purge_in_prime_tower",           "0"       },
        { "single_extruder_multi_material", "0"       },
        { "timelapse_type",                 "0"       },
        { "layer_height",                   "0.2"     },
        { "enable_wrapping_detection",      "0"       },
        { "raft_layers",                    "0"       } });
}

TEST_CASE("The tower is sized for the thinnest layer any object on the plate is sliced at", "[WipeTower]")
{
    const DynamicPrintConfig config = tower_estimate_config("rectangle");
    const std::vector<std::vector<ConfigBase::SetDeserializeItem>> overrides = {
        {}, { { "layer_height", "0.1" } } };

    Print print;
    Model model;
    init_print({ cube(20), cube(20) }, print, model, config, &overrides);

    const float floor_20mm = WipeTower::get_limit_depth_by_height(20.f);
    REQUIRE(floor_20mm < 10.f);
    CHECK_THAT(print.wipe_tower_data(2).depth, Catch::Matchers::WithinAbs(20., 1e-4));
}

TEST_CASE("Validation is given the tower's effective width, not the configured one", "[WipeTower]")
{
    Print print;
    Model model;

    SECTION("a rectangle wall keeps the configured width") {
        const DynamicPrintConfig config = tower_estimate_config("rectangle");
        init_print({ cube(20) }, print, model, config);
        const WipeTowerData &data = print.wipe_tower_data(2);
        CHECK_THAT(data.width, Catch::Matchers::WithinAbs(50., 1e-4));
        CHECK(data.depth < data.width);
    }

    SECTION("a rib wall reports the squared footprint") {
        const DynamicPrintConfig config = tower_estimate_config("rib");
        init_print({ cube(20) }, print, model, config);
        const WipeTowerData &data = print.wipe_tower_data(2);
        CHECK_THAT(data.width, Catch::Matchers::WithinAbs(data.depth, 1e-4));
        CHECK(data.width > 0.f);
    }
}

TEST_CASE("Generating the tower keeps its reported width current", "[WipeTower]")
{
    const DynamicPrintConfig config = wipe_tower_toolchange_config("marlin");
    Print print;
    Model model;
    init_print({ cube(10) }, print, model, config);
    print.apply(model, config);
    REQUIRE(print.wipe_tower_data(2).width > 0.f);

    print.process();
    REQUIRE(print.is_step_done(psWipeTower));
    const WipeTowerData &data = print.wipe_tower_data();
    CHECK(data.width > 0.f);
    CHECK(data.width < 50.f);
    CHECK(data.width <= data.depth + EPSILON);
}

TEST_CASE("A single-filament plate reserves a tower only when one is actually printed", "[WipeTower]")
{
    Print print;
    Model model;

    SECTION("no tool change and nothing else that prints one") {
        const DynamicPrintConfig config = tower_estimate_config("rib", 1);
        init_print({ cube(20) }, print, model, config);
        REQUIRE_FALSE(print.has_wipe_tower());
        CHECK_THAT(print.wipe_tower_data(1).depth, Catch::Matchers::WithinAbs(0., 1e-6));
    }

    SECTION("a raft alone does not print one") {
        DynamicPrintConfig config = tower_estimate_config("rib", 1);
        config.set_deserialize_strict({ { "raft_layers", "3" } });
        init_print({ cube(20) }, print, model, config);
        REQUIRE_FALSE(print.config().enable_prime_tower.value);
        REQUIRE_FALSE(print.has_wipe_tower());
        CHECK_THAT(print.wipe_tower_data(1).depth, Catch::Matchers::WithinAbs(0., 1e-6));
    }

    SECTION("smooth timelapse prints one, and keeps enable_prime_tower on") {
        DynamicPrintConfig config = tower_estimate_config("rib", 1);
        config.set_deserialize_strict({ { "timelapse_type", "1" } });
        init_print({ cube(20) }, print, model, config);
        REQUIRE(print.has_wipe_tower());
        CHECK(print.wipe_tower_data(1).depth > 0.f);
    }
}

static DynamicPrintConfig sparse_run_config(double layer_height, const char *max_layer_height, bool combine)
{
    DynamicPrintConfig config = multifilament_config(2, {
        { "top_surface_filament_id",        2     },
        { "enable_prime_tower",             true  },
        { "wipe_tower_x",                   50    },
        { "wipe_tower_y",                   50    },
        { "prime_tower_width",              35    },
        { "min_layer_height",               "0.08"},
        { "single_extruder_multi_material", true  },
        { "timelapse_type",                 "0"   },
        { "enable_wrapping_detection",      false },
        { "raft_layers",                    "0"   } });
    config.set_deserialize_strict({ { "layer_height",               std::to_string(layer_height) },
                                    { "initial_layer_print_height", std::to_string(layer_height) },
                                    { "max_layer_height", max_layer_height },
                                    { "wipe_tower_sparse_layers_combination", combine ? "1" : "0" } });
    return config;
}

struct SparseRunResult { size_t planned, sparse, folded; float tallest_printed, printed_height; std::string gcode; };

static SparseRunResult slice_sparse_run(const DynamicPrintConfig &config)
{
    Print print;
    Model model;
    init_print({ cube(10) }, print, model, config);
    print.apply(model, config);
    print.process();
    REQUIRE(print.is_step_done(psWipeTower));

    SparseRunResult r{};
    for (const std::vector<WipeTower::ToolChangeResult> &layer : print.wipe_tower_data().tool_changes) {
        if (layer.empty())
            continue;
        ++r.planned;
        if (wipe_tower_layer_is_sparse(layer))
            ++r.sparse;
        if (wipe_tower_layer_is_combined_away(layer)) {
            ++r.folded;
        } else {
            r.tallest_printed = std::max(r.tallest_printed, layer.front().layer_height);
            r.printed_height += layer.front().layer_height;
        }
    }
    r.gcode = Slic3r::Test::gcode(print);
    return r;
}

static size_t count_height_tags(const std::string &gcode, const char *height)
{
    const std::string tag = ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height) + height + "\n";
    size_t n = 0;
    for (size_t p = gcode.find(tag); p != std::string::npos; p = gcode.find(tag, p + 1))
        ++n;
    return n;
}

TEST_CASE("Combining sparse layers folds a run into whole layers the nozzle can lay down", "[WipeTower]")
{
    const SparseRunResult plain    = slice_sparse_run(sparse_run_config(0.1, "0.32", false));
    const SparseRunResult combined = slice_sparse_run(sparse_run_config(0.1, "0.32", true));

    REQUIRE(plain.planned == combined.planned);
    REQUIRE(plain.sparse > 10);
    CHECK(plain.folded == 0);
    CHECK_THAT(plain.tallest_printed, Catch::Matchers::WithinAbs(0.1f, 1e-4f));

    CHECK(combined.folded > 0);
    CHECK_THAT(combined.tallest_printed, Catch::Matchers::WithinAbs(0.3f, 1e-4f));
    CHECK(combined.folded <= plain.sparse);
    CHECK(combined.folded >= plain.sparse / 2);
    CHECK_THAT(combined.printed_height, Catch::Matchers::WithinAbs(plain.printed_height, 1e-3f));
}

TEST_CASE("A run too thin to reach the nozzle's layer height is left alone", "[WipeTower]")
{
    const SparseRunResult plain    = slice_sparse_run(sparse_run_config(0.2, "0.32", false));
    const SparseRunResult combined = slice_sparse_run(sparse_run_config(0.2, "0.32", true));

    REQUIRE(plain.sparse > 10);
    CHECK(combined.folded == 0);
    CHECK(combined.planned == plain.planned);
    CHECK_THAT(combined.tallest_printed, Catch::Matchers::WithinAbs(0.2f, 1e-4f));
}

TEST_CASE("A merged tower layer declares its own height to the G-code processor", "[WipeTower]")
{
    const SparseRunResult plain    = slice_sparse_run(sparse_run_config(0.2, "0.42", false));
    const SparseRunResult combined = slice_sparse_run(sparse_run_config(0.2, "0.42", true));

    REQUIRE(combined.folded > 0);
    CHECK_THAT(combined.tallest_printed, Catch::Matchers::WithinAbs(0.4f, 1e-4f));
    CHECK(count_height_tags(combined.gcode, "0.4") - count_height_tags(plain.gcode, "0.4") == combined.folded);
}

TEST_CASE("A tower printed without a tool change is still validated against the bed", "[WipeTower]")
{
    Print print;
    Model model;
    DynamicPrintConfig config = tower_estimate_config("rectangle", 1);
    config.set_deserialize_strict({ { "enable_wrapping_detection", "1" },
                                    { "wrapping_exclude_area", "180x180,190x180,190x190,180x190" },
                                    { "wipe_tower_x", "500" }, { "wipe_tower_y", "500" },                                    { "use_relative_e_distances", "0" } });

    init_print({ cube(20) }, print, model, config);
    REQUIRE(print.extruders(true).size() == 1);
    REQUIRE(print.has_wipe_tower());
    CHECK(print.wipe_tower_data(1).depth > 0.f);
    CHECK_THAT(print.validate().string, Catch::Matchers::ContainsSubstring("printable area"));
}
