#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "test_helpers.hpp"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Test;
using Catch::Matchers::WithinAbs;

namespace {

uint64_t gcode_commands_hash(const std::string &gcode)
{
    uint64_t           hash = 1469598103934665603ull;
    std::istringstream in(gcode);
    std::string        line;
    while (std::getline(in, line)) {
        line = line.substr(0, line.find(';'));
        if (line.find_first_not_of(" \r\t") == std::string::npos)
            continue;
        for (unsigned char c : line + '\n') {
            hash ^= c;
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

std::string hex(uint64_t v)
{
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string slice_part(const std::string &part, const DynamicPrintConfig &config)
{
    if (part == "20mm_cube")
        return slice({ cube(20) }, config);
    return slice({ part == "overhang" ? TestMesh::overhang : TestMesh::pyramid }, config);
}

}

TEST_CASE("Default non-planar settings leave G-code commands unchanged", "[Nonplanar][Regression]")
{
    const auto [part, reference] = GENERATE(table<std::string, std::string>({
        { "20mm_cube", "85f9f67088de9aa2" },
        { "overhang",  "ccc4fcbb503f1788" },
        { "pyramid",   "d06bc0518e4529ce" },
    }));
    CAPTURE(part);

    const DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    const std::string        hash   = hex(gcode_commands_hash(slice_part(part, config)));
    UNSCOPED_INFO("G-code command hash: " << hash);
#if defined(__linux__) && defined(__x86_64__) && defined(__GNUC__) && !defined(__clang__)
    CHECK(hash == reference);
#endif

    DynamicPrintConfig explicit_defaults = config;
    explicit_defaults.set_deserialize_strict({
        { "nonplanar_top_shells", 0 },
        { "nonplanar_bottom_shells", 0 },
        { "nonplanar_head", "" },
        { "nonplanar_kinematics", "none" },
        { "nonplanar_fallback", "none" },
        { "nonplanar_collision_check", "warn" },
        { "nonplanar_atom_toolpath", "" },
    });
    CHECK(hex(gcode_commands_hash(slice_part(part, explicit_defaults))) == hash);
}

TEST_CASE("Non-planar keys exist with defaults that disable the feature", "[Nonplanar][Config]")
{
    const DynamicPrintConfig config = DynamicPrintConfig::full_print_config();

    CHECK(config.opt_serialize("nonplanar_mode") == "off");
    CHECK(config.opt_serialize("nonplanar_head").empty());
    CHECK(config.opt_serialize("nonplanar_kinematics") == "none");
    CHECK(config.opt_serialize("rotary_axis_limits") == "-90,90,-180,180");
    CHECK(config.opt_serialize("rotary_axis_continuous") == "0,0");
    CHECK(config.opt_serialize("rotary_max_speed") == "0,0");
    CHECK(config.opt_serialize("rotary_pivot") == "0,0,0");
    CHECK_THAT(config.opt_float("rotary_tool_length"), WithinAbs(0., 1e-12));
    CHECK_FALSE(config.opt_bool("rotary_firmware_rtcp"));
    CHECK_FALSE(config.opt_bool("rotary_inverse_time_feed"));
    CHECK_THAT(config.opt_float("nonplanar_max_tilt"), WithinAbs(0., 1e-12));
    CHECK(config.opt_int("nonplanar_top_shells") == 0);
    CHECK(config.opt_int("nonplanar_bottom_shells") == 0);
    CHECK_THAT(config.opt_float("nonplanar_shell_layer_height"), WithinAbs(0., 1e-12));
    CHECK_THAT(config.opt_float("nonplanar_max_slope"), WithinAbs(0., 1e-12));
    CHECK_THAT(config.opt_float("nonplanar_max_surface_error"), WithinAbs(0.05, 1e-12));
    CHECK_THAT(config.opt_float("nonplanar_min_patch_area"), WithinAbs(16., 1e-12));
    CHECK(config.opt_serialize("nonplanar_top_pattern") == "contour");
    CHECK_FALSE(config.opt_bool("nonplanar_ironing"));
    CHECK(config.opt_serialize("nonplanar_fallback") == "none");
    CHECK_THAT(config.opt_float("nonplanar_surface_resolution"), WithinAbs(0.2, 1e-12));
    CHECK_THAT(config.opt_float("nonplanar_max_patch_height"), WithinAbs(0., 1e-12));
    CHECK(config.opt_serialize("nonplanar_atom_toolpath").empty());
    CHECK(config.opt_serialize("nonplanar_collision_check") == "warn");

    PrintConfig print_config;
    print_config.apply(config, true);
    CHECK(print_config.nonplanar_kinematics.value == RotaryLayout::None);
    CHECK(print_config.nonplanar_collision_check.value == NonplanarCollisionCheck::Warn);
    PrintRegionConfig region_config;
    region_config.apply(config, true);
    CHECK(region_config.nonplanar_top_pattern.value == NonplanarTopPattern::Contour);
    CHECK(region_config.nonplanar_fallback.value == NonplanarFallback::None);
}

TEST_CASE("Print::validate enforces the non-planar configuration rules", "[Nonplanar][Config]")
{
    using Items = std::vector<ConfigBase::SetDeserializeItem>;
    const auto [settings, blamed] = GENERATE(table<Items, std::string>({
        { Items{ { "nonplanar_top_shells", 3 } }, "" },
        { Items{ { "nonplanar_top_shells", 3 }, { "spiral_mode", 1 } }, "" },
        { Items{ { "nonplanar_top_shells", 3 }, { "nonplanar_shell_layer_height", 0.3 } }, "" },
        { Items{ { "nonplanar_top_shells", 3 }, { "nonplanar_shell_layer_height", 0.31 } }, "nonplanar_shell_layer_height" },
        { Items{ { "nonplanar_kinematics", "table_bc" } }, "rotary_tool_length" },
        { Items{ { "nonplanar_kinematics", "table_bc" }, { "rotary_tool_length", 60 } }, "" },
        { Items{ { "nonplanar_kinematics", "table_bc" }, { "rotary_firmware_rtcp", 1 } }, "" },
        { Items{ { "nonplanar_kinematics", "table_bc" }, { "rotary_tool_length", 60 }, { "gcode_remap_x", "neg_x" } },
          "nonplanar_kinematics" },
    }));

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({ { "nozzle_diameter", "0.4" }, { "nonplanar_head", "generic-conservative" },
                                    { "layer_change_gcode", "G92 E0\n" }, { "nonplanar_mode", "manual" } });
    for (const ConfigBase::SetDeserializeItem &item : settings)
        config.set_deserialize_strict({ item });

    Print print;
    Model model;
    init_print({ cube(20) }, print, model, config);
    const StringObjectException err = print.validate();
    INFO(err.string);
    CHECK(err.opt_key == blamed);
}
