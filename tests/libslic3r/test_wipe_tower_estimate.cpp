#include <algorithm>
#include <catch2/catch_all.hpp>

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/GCode/WipeTower2.hpp"
#include "libslic3r/GCode/WipeTowerEstimate.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/FilamentMixer.hpp"

#include <cmath>
#include "libslic3r/Config.hpp"
#include <cstddef>
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"
#include <numeric>
#include <string>
#include <vector>
#include "libslic3r/clonable_ptr.hpp"

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

static DynamicPrintConfig preset_shaped_defaults()
{
    DynamicPrintConfig config;
    config.apply(FullPrintConfig::defaults());
    return config;
}

static DynamicPrintConfig make_config(const char *wall_type = "rectangle")
{
    DynamicPrintConfig config = preset_shaped_defaults();
    config.set_key_value("prime_tower_width", new ConfigOptionFloat(50.));
    config.set_key_value("prime_volume", new ConfigOptionFloat(100.));
    config.set_key_value("filament_prime_volume", new ConfigOptionFloats({100.}));
    config.set_key_value("filament_adhesiveness_category", new ConfigOptionInts({0}));
    config.set_key_value("prime_tower_infill_gap", new ConfigOptionPercent(100.));
    config.set_key_value("wipe_tower_extra_spacing", new ConfigOptionPercent(100.));
    config.set_key_value("prime_tower_brim_width", new ConfigOptionFloat(3.));
    config.set_deserialize_strict("wipe_tower_wall_type", wall_type);
    config.set_key_value("wipe_tower_rib_width", new ConfigOptionFloat(8.));
    config.set_key_value("wipe_tower_extra_rib_length", new ConfigOptionFloat(0.));
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
    config.set_key_value("initial_layer_print_height", new ConfigOptionFloat(0.2));
    config.set_deserialize_strict("timelapse_type", "0");
    config.set_key_value("enable_wrapping_detection", new ConfigOptionBool(false));
    config.set_key_value("raft_layers", new ConfigOptionInt(0));
    config.set_key_value("purge_in_prime_tower", new ConfigOptionBool(false));
    config.set_key_value("single_extruder_multi_material", new ConfigOptionBool(false));
    return config;
}

static std::vector<unsigned int> filaments(size_t count)
{
    std::vector<unsigned int> ids(count);
    std::iota(ids.begin(), ids.end(), 0u);
    return ids;
}

static WipeTowerFootprint estimate(const ConfigBase &config, size_t count, double layer_height, double height, WipeTowerType type = WipeTowerType::Type2)
{
    return estimate_wipe_tower_footprint(config, type, filaments(count), layer_height, height);
}

static double printed_brim(double configured, WipeTowerType type)
{
    return WipeTower::estimate_brim_real_width(float(configured), 0.4f, 0.2f, type == WipeTowerType::Type2);
}

TEST_CASE("A rectangle wall tower is sized by the purge volume", "[WipeTowerEstimate]") {
    const DynamicPrintConfig config = make_config();
    const WipeTowerFootprint fp = estimate(config, 3, 0.2, 5.);
    CHECK_THAT(fp.width, WithinAbs(50., 1e-9));
    CHECK_THAT(fp.depth, WithinAbs(20., 1e-9));
    CHECK_THAT(fp.height, WithinAbs(5., 1e-9));
    CHECK_THAT(fp.brim_width, WithinAbs(printed_brim(3., WipeTowerType::Type2), 1e-6));
    CHECK_THAT(estimate(config, 3, 0.1, 5.).depth, WithinAbs(40., 1e-9));
}

TEST_CASE("Each planner spaces its purge lines by its own option", "[WipeTowerEstimate]") {
    DynamicPrintConfig config = make_config();
    config.set_key_value("wipe_tower_extra_flow", new ConfigOptionPercent(250.));
    CHECK_THAT(estimate(config, 3, 0.2, 5.).depth, WithinAbs(20., 1e-9));
    config.set_key_value("wipe_tower_extra_spacing", new ConfigOptionPercent(150.));
    CHECK_THAT(estimate(config, 3, 0.2, 5.).depth, WithinAbs(30., 1e-9));
    const double type1_spaced = estimate(config, 3, 0.2, 5., WipeTowerType::Type1).depth;
    config.set_key_value("prime_tower_infill_gap", new ConfigOptionPercent(150.));
    CHECK_THAT(estimate(config, 3, 0.2, 5.).depth, WithinAbs(30., 1e-9));
    CHECK_THAT(estimate(config, 3, 0.2, 5., WipeTowerType::Type1).depth - 0.5, WithinAbs(1.5 * (type1_spaced - 0.5), 1e-6));
}

TEST_CASE("Type1 sizes the tower from each filament's own prime volume", "[WipeTowerEstimate]") {
    DynamicPrintConfig config = make_config();
    config.set_key_value("prime_tower_width", new ConfigOptionFloat(35.));
    config.set_key_value("prime_tower_infill_gap", new ConfigOptionPercent(150.));
    config.set_key_value("initial_layer_print_height", new ConfigOptionFloat(0.21));
    config.set_key_value("filament_prime_volume", new ConfigOptionFloats({30., 45.}));
    config.set_key_value("filament_adhesiveness_category", new ConfigOptionInts({100, 0}));
    const std::vector<WipeTower::PurgeEstimate> purges{{30.f, 100}, {45.f, 0}};
    const double blocks = WipeTower::estimate_tower_blocks_depth(purges, 35.f, 0.21f, 0.4f, 1.5f);
    REQUIRE_THAT(blocks, WithinAbs(18.5, 0.01));
    CHECK_THAT(estimate(config, 2, 0.21, 5., WipeTowerType::Type1).depth, WithinAbs(blocks, 1e-4));
    CHECK_THAT(estimate_wipe_tower_footprint(config, WipeTowerType::Type1, {1, 0}, 0.21, 5.).depth, WithinAbs(blocks, 1e-4));
    CHECK_THAT(estimate(config, 1, 0.21, 5., WipeTowerType::Type1).depth, WithinAbs(0., 1e-9));
    config.set_key_value("filament_adhesiveness_category", new ConfigOptionInts({0, 0}));
    CHECK_THAT(estimate(config, 2, 0.21, 5., WipeTowerType::Type1).depth, WithinAbs(11., 0.01));
    config.set_deserialize_strict("wipe_tower_wall_type", "rib");
    const WipeTowerFootprint rib = estimate(config, 2, 0.21, 5., WipeTowerType::Type1);
    CHECK_THAT(rib.width, WithinAbs(rib.depth, 1e-9));
    CHECK_THAT(rib.depth, WithinAbs(WipeTower::estimate_rib_tower_bbox_side({{30.f, 0}, {45.f, 0}}, 35.f, 0.21f, 0.4f, 1.5f, 8.f, 0.f, 5.f), 1e-4));
}

TEST_CASE("A second nozzle adds the ramming of one nozzle change per layer", "[WipeTowerEstimate]") {
    DynamicPrintConfig config = make_config();
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4, 0.4}));
    config.set_key_value("filament_change_length", new ConfigOptionFloats({10., 10.}));
    config.set_key_value("filament_diameter", new ConfigOptionFloats({1.75, 1.75}));
    config.set_key_value("filament_map", new ConfigOptionInts({1, 1}));
    const double same_nozzle = estimate(config, 2, 0.2, 5., WipeTowerType::Type1).depth;
    config.set_key_value("filament_map", new ConfigOptionInts({1, 2}));
    CHECK_THAT(estimate(config, 2, 0.2, 5., WipeTowerType::Type1).depth - same_nozzle, WithinAbs(3., 1e-4));
}

TEST_CASE("The tower is sized for the first layer when it is the thinnest", "[WipeTowerEstimate]") {
    DynamicPrintConfig config = make_config();
    const double at_thinnest = estimate(config, 3, 0.2, 5.).depth;
    CHECK_THAT(estimate(config, 3, 0.28, 5.).depth, WithinAbs(at_thinnest, 1e-9));
    config.set_key_value("initial_layer_print_height", new ConfigOptionFloat(0.3));
    CHECK(estimate(config, 3, 0.28, 5.).depth < at_thinnest);
}

TEST_CASE("Object height sets the stability floor and the auto brim", "[WipeTowerEstimate]") {
    DynamicPrintConfig config = make_config();
    CHECK_THAT(estimate(config, 2, 0.2, 100.).depth, WithinAbs(20., 1e-9));
    config.set_key_value("prime_tower_brim_width", new ConfigOptionFloat(-1.));
    const double auto_brim = WipeTower::get_auto_brim_by_height(50.f);
    CHECK_THAT(estimate(config, 2, 0.2, 50.).brim_width, WithinAbs(printed_brim(auto_brim, WipeTowerType::Type2), 1e-6));
    CHECK_THAT(estimate(config, 2, 0.2, 50., WipeTowerType::Type1).brim_width, WithinAbs(printed_brim(auto_brim, WipeTowerType::Type1), 1e-6));
}

TEST_CASE("A single filament only gets a tower when one is printed anyway", "[WipeTowerEstimate]") {
    DynamicPrintConfig config = make_config();
    CHECK_THAT(estimate(config, 1, 0.2, 100.).depth, WithinAbs(0., 1e-9));
    CHECK_THAT(estimate(config, 0, 0.2, 100.).width, WithinAbs(0., 1e-9));

    config.set_key_value("enable_wrapping_detection", new ConfigOptionBool(true));
    CHECK_THAT(estimate(config, 1, 0.2, 100.).depth, WithinAbs(20., 1e-9));
    CHECK_THAT(estimate(config, 1, 0.2, 100., WipeTowerType::Type1).depth, WithinAbs(WipeTower::get_wrapping_detection_depth(), 1e-9));
    config.set_key_value("enable_wrapping_detection", new ConfigOptionBool(false));

    config.set_key_value("raft_layers", new ConfigOptionInt(3));
    CHECK_THAT(estimate(config, 1, 0.2, 100.).depth, WithinAbs(0., 1e-9));
    config.set_key_value("raft_layers", new ConfigOptionInt(0));

    config.set_deserialize_strict("timelapse_type", "1");
    CHECK_THAT(estimate(config, 1, 0.2, 100.).depth, WithinAbs(20., 1e-9));
    CHECK_THAT(estimate(config, 1, 0.2, 5.).depth, WithinAbs(WipeTower::get_limit_depth_by_height(5.f), 1e-9));
}

TEST_CASE("A tool change reserves a tower even with nothing to purge", "[WipeTowerEstimate]") {
    const double     height = GENERATE(5., 100.);
    const float      floor  = WipeTower::get_limit_depth_by_height(float(height));
    const char      *wall   = GENERATE("rectangle", "rib");
    DynamicPrintConfig config = make_config(wall);
    config.set_key_value("prime_volume", new ConfigOptionFloat(0.));
    config.set_key_value("filament_prime_volume", new ConfigOptionFloats({0.}));

    CHECK(estimate(config, 3, 0.2, height, WipeTowerType::Type2).depth >= floor);
    CHECK(estimate(config, 3, 0.2, height, WipeTowerType::Type1).depth >= floor);
    CHECK_THAT(estimate(config, 1, 0.2, height, WipeTowerType::Type2).depth, WithinAbs(0., 1e-9));
    CHECK_THAT(estimate(config, 1, 0.2, height, WipeTowerType::Type1).depth, WithinAbs(0., 1e-9));
}

TEST_CASE("Both wall types agree on whether there is a tower at all", "[WipeTowerEstimate]") {
    const double height = GENERATE(5., 100.);
    DynamicPrintConfig rect = make_config();
    DynamicPrintConfig rib  = make_config("rib");

    CHECK_THAT(estimate(rect, 1, 0.2, height).depth, WithinAbs(0., 1e-9));
    CHECK_THAT(estimate(rib, 1, 0.2, height).depth, WithinAbs(0., 1e-9));

    rect.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4, 0.4}));
    rib.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4, 0.4}));
    CHECK_THAT(estimate(rect, 1, 0.2, height).depth, WithinAbs(0., 1e-9));
    CHECK_THAT(estimate(rib, 1, 0.2, height).depth, WithinAbs(0., 1e-9));

    CHECK(estimate(rect, 2, 0.2, height).depth >= WipeTower::get_limit_depth_by_height(float(height)));
    CHECK(estimate(rib, 2, 0.2, height).depth >= WipeTower::get_limit_depth_by_height(float(height)));
}

TEST_CASE("A rib wall squares the tower and caps the rib width", "[WipeTowerEstimate]") {
    DynamicPrintConfig config = make_config("rib");
    const double body = std::sqrt(1000.);
    WipeTowerFootprint fp = estimate(config, 3, 0.2, 5.);
    CHECK_THAT(fp.depth, WithinAbs(8. / std::sqrt(2.) + body, 1e-5));
    CHECK_THAT(fp.width, WithinAbs(fp.depth, 1e-9));
    config.set_key_value("wipe_tower_extra_rib_length", new ConfigOptionFloat(4.));
    CHECK_THAT(estimate(config, 3, 0.2, 5.).depth, WithinAbs((8. + 4.) / std::sqrt(2.) + body, 1e-5));
    config.set_key_value("wipe_tower_extra_rib_length", new ConfigOptionFloat(0.));
    config.set_key_value("prime_volume", new ConfigOptionFloat(5.));
    CHECK_THAT(estimate(config, 2, 0.2, 5.).depth, WithinAbs(2.5 / std::sqrt(2.) + 5., 1e-5));
}

TEST_CASE("Every wall and tower type is read the same from a preset and a static config", "[WipeTowerEstimate]") {
    const char *wall_type  = GENERATE("rectangle", "cone", "rib");
    const char *tower_type = GENERATE("type1", "type2");
    DynamicPrintConfig preset = make_config(wall_type);
    preset.set_deserialize_strict("wipe_tower_type", tower_type);
    REQUIRE(dynamic_cast<const ConfigOptionEnumGeneric *>(preset.option("wipe_tower_wall_type")) != nullptr);

    FullPrintConfig static_config;
    static_config.apply(preset, true);
    REQUIRE(static_config.wipe_tower_wall_type.serialize() == wall_type);
    REQUIRE(static_config.wipe_tower_type.serialize() == tower_type);

    const WipeTowerType type = resolve_wipe_tower_type(preset);
    CHECK(type == (std::string(tower_type) == "type1" ? WipeTowerType::Type1 : WipeTowerType::Type2));
    CHECK(resolve_wipe_tower_type(static_config) == type);

    const WipeTowerFootprint fp          = estimate(preset, 3, 0.2, 5., type);
    const WipeTowerFootprint from_static = estimate(static_config, 3, 0.2, 5., type);
    CHECK(fp.depth > 0.);
    if (std::string(wall_type) == "rib")
        CHECK_THAT(fp.width, WithinAbs(fp.depth, 1e-9));
    else
        CHECK_THAT(fp.width, WithinAbs(50., 1e-9));
    CHECK_THAT(from_static.width, WithinAbs(fp.width, 1e-9));
    CHECK_THAT(from_static.depth, WithinAbs(fp.depth, 1e-9));
    CHECK_THAT(from_static.brim_width, WithinAbs(fp.brim_width, 1e-9));

    preset.set_deserialize_strict("timelapse_type", "1");
    static_config.apply(preset, true);
    CHECK(estimate(preset, 1, 0.2, 5., type).depth > 0.);
    CHECK(estimate(static_config, 1, 0.2, 5., type).depth > 0.);
}

TEST_CASE("The first-layer outline bulges only for a Type2 cone wall", "[WipeTowerEstimate]") {
    DynamicPrintConfig config = make_config("cone");
    config.set_key_value("wipe_tower_cone_angle", new ConfigOptionFloat(25.));
    REQUIRE(dynamic_cast<const ConfigOptionEnumGeneric *>(config.option("wipe_tower_wall_type")) != nullptr);
    const Polygon box = Polygon::new_scale({{0., 0.}, {35., 0.}, {35., 20.}, {0., 20.}});
    auto is_box = [&box](const Polygon &outline) { return diff(Polygons{outline}, Polygons{box}).empty(); };

    const Polygon cone = estimate_wipe_tower_first_layer_outline(config, WipeTowerType::Type2, 35., 20., 100.);
    CHECK(unscaled(get_extents(cone).max.y()) > 20. + 1.);
    CHECK(diff(Polygons{box}, Polygons{cone}).empty());
    CHECK(is_box(estimate_wipe_tower_first_layer_outline(config, WipeTowerType::Type1, 35., 20., 100.)));
    for (const char *wall_type : {"rectangle", "rib"}) {
        config.set_deserialize_strict("wipe_tower_wall_type", wall_type);
        CHECK(is_box(estimate_wipe_tower_first_layer_outline(config, WipeTowerType::Type2, 35., 20., 100.)));
    }
    config.set_deserialize_strict("wipe_tower_wall_type", "cone");
    FullPrintConfig static_config;
    static_config.apply(config, true);
    const Polygon from_static = estimate_wipe_tower_first_layer_outline(static_config, WipeTowerType::Type2, 35., 20., 100.);
    CHECK(from_static.points == cone.points);
}

TEST_CASE("A Bambu Lab printer always gets the Type1 planner", "[WipeTowerEstimate]") {
    DynamicPrintConfig config = make_config();
    config.set_deserialize_strict("wipe_tower_type", "type2");
    config.set_key_value("printer_model", new ConfigOptionString("Bambu Lab X1 Carbon"));
    CHECK(resolve_wipe_tower_type(config) == WipeTowerType::Type1);
    config.set_key_value("printer_model", new ConfigOptionString("Voron 2.4"));
    CHECK(resolve_wipe_tower_type(config) == WipeTowerType::Type2);
    config.erase("wipe_tower_type");
    CHECK(resolve_wipe_tower_type(config) == WipeTowerType::Type2);
}

TEST_CASE("A dual nozzle purges every filament plus the filament change", "[WipeTowerEstimate]") {
    DynamicPrintConfig config = make_config();
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4, 0.4}));
    config.set_key_value("filament_change_length", new ConfigOptionFloats({10., 10.}));
    config.set_key_value("filament_diameter", new ConfigOptionFloats({1.75, 1.75}));
    const double change_volume = 10. * PI * 1.75 * 1.75 / 4.;
    CHECK_THAT(estimate(config, 2, 0.2, 5.).depth, WithinAbs((200. + change_volume) / 10., 1e-9));
}

TEST_CASE("The shipped defaults size the tower from the flush matrix", "[WipeTowerEstimate]") {
    DynamicPrintConfig config = preset_shaped_defaults();
    REQUIRE(config.opt_bool("purge_in_prime_tower"));
    REQUIRE(config.opt_bool("single_extruder_multi_material"));
    config.set_key_value("prime_tower_width", new ConfigOptionFloat(50.));
    config.set_deserialize_strict("wipe_tower_wall_type", "rectangle");
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));

    const double flush_volume = WipeTower2::estimate_semm_flush_volume(config, 2);
    const double expected     = std::max(double(WipeTower::get_limit_depth_by_height(5.f)), flush_volume / (0.2 * 50.));
    CHECK_THAT(estimate(config, 2, 0.2, 5.).depth, WithinAbs(expected, 1e-6));

    REQUIRE(WipeTower2::estimate_semm_flush_volume(config, 1) > 0.);
    CHECK_THAT(estimate(config, 1, 0.2, 5.).depth, WithinAbs(0., 1e-9));
}

TEST_CASE("A config missing a tower key falls back to that key's default", "[WipeTowerEstimate]") {
    const DynamicPrintConfig full = make_config();
    DynamicPrintConfig       partial = full;
    partial.erase("wipe_tower_extra_spacing");
    REQUIRE(partial.option("wipe_tower_extra_spacing") == nullptr);

    DynamicPrintConfig defaulted = full;
    defaulted.set_key_value("wipe_tower_extra_spacing",
                            print_config_def.get("wipe_tower_extra_spacing")->default_value->clone());
    CHECK_THAT(estimate(partial, 3, 0.2, 5.).depth, WithinAbs(estimate(defaulted, 3, 0.2, 5.).depth, 1e-9));
}

TEST_CASE("prime_tower_is_printed answers exactly what normalize_fdm_2 decides", "[WipeTowerEstimate]") {
    const int  used_filaments = GENERATE(1, 2, 3);
    const bool has_mixed      = GENERATE(false, true);
    const int  num_objects    = GENERATE(1, 2);
    const bool by_object      = GENERATE(false, true);
    const bool smooth         = GENERATE(false, true);
    const bool wrapping       = GENERATE(false, true);

    DynamicPrintConfig config = preset_shaped_defaults();
    config.set_key_value("enable_prime_tower", new ConfigOptionBool(true));
    config.set_deserialize_strict("print_sequence", by_object ? "by object" : "by layer");
    config.set_deserialize_strict("timelapse_type", smooth ? "1" : "0");
    config.set_key_value("enable_wrapping_detection", new ConfigOptionBool(wrapping));
    config.set_key_value("filament_is_mixed", new ConfigOptionBools(
        has_mixed ? std::vector<unsigned char>{0, 1} : std::vector<unsigned char>{0, 0}));
    REQUIRE(has_any_mixed_filament(config.option<ConfigOptionBools>("filament_is_mixed")->values) == has_mixed);

    DynamicPrintConfig normalized = config;
    normalized.normalize_fdm_2(num_objects, used_filaments);
    const bool slicer_prints_one = normalized.opt_bool("enable_prime_tower");

    CHECK(prime_tower_is_printed(config, used_filaments, num_objects, has_mixed) == slicer_prints_one);
}

TEST_CASE("prime_tower_is_printed follows the option the user set", "[WipeTowerEstimate]") {
    DynamicPrintConfig config = preset_shaped_defaults();
    config.set_key_value("enable_prime_tower", new ConfigOptionBool(false));
    CHECK_FALSE(prime_tower_is_printed(config, 2, 1, false));

    config.set_key_value("enable_prime_tower", new ConfigOptionBool(true));
    CHECK(prime_tower_is_printed(config, 0, 1, false));
}
