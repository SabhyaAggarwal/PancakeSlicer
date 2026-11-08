#include <catch2/catch_all.hpp>

#include "catch2/matchers/catch_matchers_floating_point.hpp"
#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"

#include "test_helpers.hpp"
#include "test_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <libslic3r/PrintConfig.hpp>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Test;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kBedWidth  = 300.0;
constexpr double kBedDepth  = 200.0;
const Vec2d      kPrimaryZoneCentre(kBedWidth / 4.0, kBedDepth / 2.0);

struct XYBounds
{
    double min_x = std::numeric_limits<double>::max();
    double min_y = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double max_y = std::numeric_limits<double>::lowest();

    bool   empty() const { return min_x > max_x; }
    double size_x() const { return max_x - min_x; }
    double size_y() const { return max_y - min_y; }
};

static XYBounds perimeter_bounds(const std::string &gcode, double z_max = std::numeric_limits<double>::max())
{
    XYBounds    bounds;
    GCodeReader reader;
    reader.parse_buffer(gcode, [&bounds, z_max](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (!line.extruding(self))
            return;
        if (line.comment().find("perimeter") == std::string_view::npos)
            return;
        if (double(line.new_Z(self)) > z_max)
            return;
        const double x = line.new_X(self);
        const double y = line.new_Y(self);
        bounds.min_x   = std::min(bounds.min_x, x);
        bounds.max_x   = std::max(bounds.max_x, x);
        bounds.min_y   = std::min(bounds.min_y, y);
        bounds.max_y   = std::max(bounds.max_y, y);
    });
    return bounds;
}

static bool read_marker(const std::string &gcode, const std::string &tag, Vec2d &out)
{
    for (size_t at = gcode.find(tag); at != std::string::npos; at = gcode.find(tag, at + 1)) {
        const size_t      from    = at + tag.size();
        const size_t      to      = gcode.find('\n', from);
        const std::string payload = gcode.substr(from, to == std::string::npos ? std::string::npos : to - from);
        const size_t      comma   = payload.find(',');
        if (comma == std::string::npos)
            continue;
        try {
            out = Vec2d(std::stod(payload.substr(0, comma)), std::stod(payload.substr(comma + 1)));
        } catch (const std::exception &) {
            continue;
        }
        return true;
    }
    return false;
}

static void imex_printer(DynamicPrintConfig &config)
{
    config.set_deserialize_strict({
        { "nozzle_diameter",           "0.4,0.4,0.4,0.4,0.4,0.4,0.4" },
        { "printer_extruder_id",       "1,2,3,4,5,6,7" },
        { "printer_extruder_variant",  "Direct Drive Standard,Direct Drive Standard,Direct Drive Standard,"
                                       "Direct Drive Standard,Direct Drive Standard,Direct Drive Standard,"
                                       "Direct Drive Standard" },
        { "extruder_printable_height", "0,0,0,0,0,0,0" },
        { "physical_extruder_map",     "0,0,0,0,1,2,3" },
        { "printable_area",            "0x0,300x0,300x200,0x200" },
        { "is_imex",                   "1" },
        { "imex_gantry_count",         "1" },
        { "imex_tools_per_gantry",     "2" },
        { "imex_tool_layout",          "front-left" },
        { "imex_mode_names",           "primary;copy" },
        { "imex_mode_active_tools",    "0:P;0:P,1:C" },
        { "imex_parallel_mode",        "copy" },
        { "skirt_loops",               "0" },
        { "brim_type",                 "no_brim" },
        { "enable_prime_tower",        "0" },
        { "sparse_infill_density",     "0%" },
        { "top_shell_layers",          "0" },
        { "bottom_shell_layers",       "0" },
        { "wall_loops",                "2" },
        { "layer_height",              "0.2" },
        { "initial_layer_print_height","0.2" },
        { "gcode_flavor",              "klipper" },
    });
    size_flush_to_nozzles(config);
}

static void all_regions_on_filament(DynamicPrintConfig &config, int filament_1based)
{
    for (const char *key : { "outer_wall_filament_id", "inner_wall_filament_id",
                             "sparse_infill_filament_id", "internal_solid_filament_id",
                             "top_surface_filament_id", "bottom_surface_filament_id" })
        config.set_deserialize_strict({ { key, std::to_string(filament_1based) } });
}

static DynamicPrintConfig imex_config(bool firmware_managed)
{
    DynamicPrintConfig config = multifilament_config(7);
    imex_printer(config);
    all_regions_on_filament(config, 1);
    config.set_deserialize_strict({ { "imex_firmware_managed_zones", firmware_managed ? "1" : "0" } });
    return config;
}

static Vec2d derived_offset(const DynamicPrintConfig &config, const Vec3d &plate_origin = Vec3d::Zero())
{
    Print print;
    Model model;
    init_print({ cube(20) }, print, model, config);
    print.set_plate_origin(plate_origin);
    print.update_imex_slice_offset();
    return print.get_imex_slice_offset();
}

static std::string slice_cube(const DynamicPrintConfig &config, const Vec3d &plate_origin = Vec3d::Zero())
{
    Print print;
    Model model;
    init_print({ cube(20) }, print, model, config);
    print.set_plate_origin(plate_origin);
    return gcode(print);
}

}
TEST_CASE("A firmware-managed IMEX plate derives its slice offset with nothing pushed in",
          "[IMEXSliceOffset][IMEX]")
{
    const Vec2d offset = derived_offset(imex_config(true));

    CHECK_THAT(offset.x(), WithinAbs(kPrimaryZoneCentre.x(), 1e-9));
    CHECK_THAT(offset.y(), WithinAbs(kPrimaryZoneCentre.y(), 1e-9));
}

TEST_CASE("The derived IMEX slice offset is plate-local and does not move with the plate origin",
          "[IMEXSliceOffset][IMEX]")
{
    const DynamicPrintConfig config = imex_config(true);

    const Vec2d at_origin = derived_offset(config);
    const Vec2d on_plate3 = derived_offset(config, Vec3d(kBedWidth * 2.0, -kBedDepth * 2.0, 0.0));

    CHECK_THAT(on_plate3.x(), WithinAbs(at_origin.x(), 1e-9));
    CHECK_THAT(on_plate3.y(), WithinAbs(at_origin.y(), 1e-9));
}

TEST_CASE("The derived IMEX slice offset follows the printable area", "[IMEXSliceOffset][IMEX]")
{
    DynamicPrintConfig config = imex_config(true);
    config.set_deserialize_strict({ { "printable_area", "0x0,150x0,150x100,0x100" } });

    const Vec2d offset = derived_offset(config);

    CHECK_THAT(offset.x(), WithinAbs(kPrimaryZoneCentre.x() / 2.0, 1e-9));
    CHECK_THAT(offset.y(), WithinAbs(kPrimaryZoneCentre.y() / 2.0, 1e-9));
}

TEST_CASE("The derived IMEX slice offset is zero unless firmware-managed zones are in play",
          "[IMEXSliceOffset][IMEX]")
{
    Print fresh;
    REQUIRE_THAT(fresh.get_imex_slice_offset().x(), WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(fresh.get_imex_slice_offset().y(), WithinAbs(0.0, 1e-12));

    Vec2d offset = Vec2d::Zero();

    SECTION("an ordinary printer with no IMEX configuration at all") {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.set_deserialize_strict({
            { "sparse_infill_density",      "0%" },
            { "layer_height",               "0.2" },
            { "initial_layer_print_height", "0.2" },
        });
        offset = derived_offset(config);
    }
    SECTION("an IMEX printer in a parallel mode, but firmware-managed zones off") {
        offset = derived_offset(imex_config(false));
    }
    SECTION("firmware-managed zones on, but the plate is in Primary mode") {
        DynamicPrintConfig config = imex_config(true);
        config.set_deserialize_strict({ { "imex_parallel_mode", "primary" } });
        offset = derived_offset(config);
    }
    SECTION("firmware-managed zones on, but the plate has no mode at all") {
        DynamicPrintConfig config = imex_config(true);
        config.set_deserialize_strict({ { "imex_parallel_mode", "" } });
        offset = derived_offset(config);
    }
    SECTION("firmware-managed zones on, but the plate's mode is not one this printer defines") {
        DynamicPrintConfig config = imex_config(true);
        config.set_deserialize_strict({ { "imex_parallel_mode", "renamed-since" } });
        offset = derived_offset(config);
    }
    SECTION("firmware-managed zones on, but the tool grid holds a single tool") {
        DynamicPrintConfig config = imex_config(true);
        config.set_deserialize_strict({ { "imex_tools_per_gantry",  "1" },
                                        { "imex_mode_active_tools", "0:P;0:P" } });
        offset = derived_offset(config);
    }

    CHECK_THAT(offset.x(), WithinAbs(0.0, 1e-12));
    CHECK_THAT(offset.y(), WithinAbs(0.0, 1e-12));
}

TEST_CASE("A firmware-managed IMEX plate emits coordinates shifted by the offset it derived",
          "[IMEXSliceOffset][IMEX]")
{
    const XYBounds baseline = perimeter_bounds(slice_cube(imex_config(false)));
    const XYBounds shifted  = perimeter_bounds(slice_cube(imex_config(true)));
    REQUIRE_FALSE(baseline.empty());
    REQUIRE_FALSE(shifted.empty());

    CHECK_THAT(shifted.min_x, WithinAbs(baseline.min_x - kPrimaryZoneCentre.x(), 1e-3));
    CHECK_THAT(shifted.max_x, WithinAbs(baseline.max_x - kPrimaryZoneCentre.x(), 1e-3));
    CHECK_THAT(shifted.min_y, WithinAbs(baseline.min_y - kPrimaryZoneCentre.y(), 1e-3));
    CHECK_THAT(shifted.max_y, WithinAbs(baseline.max_y - kPrimaryZoneCentre.y(), 1e-3));

    CHECK_THAT(shifted.size_x(), WithinAbs(baseline.size_x(), 1e-3));
    CHECK_THAT(shifted.size_y(), WithinAbs(baseline.size_y(), 1e-3));
}

TEST_CASE("An IMEX slice offset composes with the plate origin rather than replacing it",
          "[IMEXSliceOffset][IMEX]")
{
    const Vec3d plate_origin(kBedWidth * 1.1, -kBedDepth * 1.1, 0.0);

    const XYBounds baseline = perimeter_bounds(slice_cube(imex_config(false)));
    const XYBounds shifted  = perimeter_bounds(slice_cube(imex_config(true), plate_origin));
    REQUIRE_FALSE(baseline.empty());
    REQUIRE_FALSE(shifted.empty());

    const double expected_x = plate_origin.x() + kPrimaryZoneCentre.x();
    const double expected_y = plate_origin.y() + kPrimaryZoneCentre.y();
    CHECK_THAT(shifted.min_x, WithinAbs(baseline.min_x - expected_x, 1e-3));
    CHECK_THAT(shifted.max_x, WithinAbs(baseline.max_x - expected_x, 1e-3));
    CHECK_THAT(shifted.min_y, WithinAbs(baseline.min_y - expected_y, 1e-3));
    CHECK_THAT(shifted.max_y, WithinAbs(baseline.max_y - expected_y, 1e-3));
}

TEST_CASE("Turning on firmware-managed zones changes nothing on a non-IMEX printer",
          "[IMEXSliceOffset][IMEX]")
{
    auto ordinary_printer = [](bool firmware_managed) {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.set_deserialize_strict({
            { "sparse_infill_density",         "0%" },
            { "top_shell_layers",              "0" },
            { "bottom_shell_layers",           "0" },
            { "wall_loops",                    "2" },
            { "layer_height",                  "0.2" },
            { "initial_layer_print_height",    "0.2" },
            { "skirt_loops",                   "0" },
            { "brim_type",                     "no_brim" },
            { "enable_prime_tower",            "0" },
            { "imex_firmware_managed_zones",   firmware_managed ? "1" : "0" },
        });
        return config;
    };

    const Vec2d offset = derived_offset(ordinary_printer(true));
    CHECK_THAT(offset.x(), WithinAbs(0.0, 1e-12));
    CHECK_THAT(offset.y(), WithinAbs(0.0, 1e-12));

    const XYBounds baseline  = perimeter_bounds(slice_cube(ordinary_printer(false)));
    const XYBounds with_flag = perimeter_bounds(slice_cube(ordinary_printer(true)));
    REQUIRE_FALSE(baseline.empty());
    REQUIRE_FALSE(with_flag.empty());

    CHECK_THAT(with_flag.min_x, WithinAbs(baseline.min_x, 1e-3));
    CHECK_THAT(with_flag.max_x, WithinAbs(baseline.max_x, 1e-3));
    CHECK_THAT(with_flag.min_y, WithinAbs(baseline.min_y, 1e-3));
    CHECK_THAT(with_flag.max_y, WithinAbs(baseline.max_y, 1e-3));
}

TEST_CASE("first_layer_print_min/max track the derived IMEX slice offset",
          "[IMEXSliceOffset][IMEX]")
{
    auto with_markers = [](bool firmware_managed) {
        DynamicPrintConfig config = imex_config(firmware_managed);
        config.set_deserialize_strict({
            { "machine_start_gcode",
              ";FLMIN:{first_layer_print_min[0]},{first_layer_print_min[1]}\n"
              ";FLMAX:{first_layer_print_max[0]},{first_layer_print_max[1]}\n" },
        });
        return config;
    };

    const std::string baseline_gcode = slice_cube(with_markers(false));
    const std::string shifted_gcode  = slice_cube(with_markers(true));

    Vec2d baseline_min, baseline_max, shifted_min, shifted_max;
    REQUIRE(read_marker(baseline_gcode, ";FLMIN:", baseline_min));
    REQUIRE(read_marker(baseline_gcode, ";FLMAX:", baseline_max));
    REQUIRE(read_marker(shifted_gcode, ";FLMIN:", shifted_min));
    REQUIRE(read_marker(shifted_gcode, ";FLMAX:", shifted_max));

    CHECK_THAT(shifted_min.x(), WithinAbs(baseline_min.x() - kPrimaryZoneCentre.x(), 1e-3));
    CHECK_THAT(shifted_min.y(), WithinAbs(baseline_min.y() - kPrimaryZoneCentre.y(), 1e-3));
    CHECK_THAT(shifted_max.x(), WithinAbs(baseline_max.x() - kPrimaryZoneCentre.x(), 1e-3));
    CHECK_THAT(shifted_max.y(), WithinAbs(baseline_max.y() - kPrimaryZoneCentre.y(), 1e-3));

    const XYBounds baseline_layer = perimeter_bounds(baseline_gcode, 0.3);
    const XYBounds shifted_layer  = perimeter_bounds(shifted_gcode, 0.3);
    REQUIRE_FALSE(baseline_layer.empty());
    REQUIRE_FALSE(shifted_layer.empty());

    CHECK(shifted_min.x() <= shifted_layer.min_x + 1e-3);
    CHECK(shifted_min.y() <= shifted_layer.min_y + 1e-3);
    CHECK(shifted_max.x() >= shifted_layer.max_x - 1e-3);
    CHECK(shifted_max.y() >= shifted_layer.max_y - 1e-3);

    CHECK_THAT(shifted_layer.min_x - shifted_min.x(),
               WithinAbs(baseline_layer.min_x - baseline_min.x(), 1e-3));
    CHECK_THAT(shifted_layer.min_y - shifted_min.y(),
               WithinAbs(baseline_layer.min_y - baseline_min.y(), 1e-3));
    CHECK_THAT(shifted_max.x() - shifted_layer.max_x,
               WithinAbs(baseline_max.x() - baseline_layer.max_x, 1e-3));
    CHECK_THAT(shifted_max.y() - shifted_layer.max_y,
               WithinAbs(baseline_max.y() - baseline_layer.max_y, 1e-3));
}
