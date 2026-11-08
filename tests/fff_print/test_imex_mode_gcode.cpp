#include <catch2/catch_all.hpp>

#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators.hpp"
#include "catch2/catch_message.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "test_helpers.hpp"
#include "test_utils.hpp"

#include <cstddef>
#include <string>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Test;

static std::size_t find_exact_line(const std::string &gcode, const std::string &expected)
{
    std::size_t pos = 0;
    while (pos <= gcode.size()) {
        const std::size_t eol = gcode.find('\n', pos);
        const std::size_t end = (eol == std::string::npos) ? gcode.size() : eol;
        std::string       line = gcode.substr(pos, end - pos);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (line == expected)
            return pos;
        if (eol == std::string::npos)
            break;
        pos = eol + 1;
    }
    return std::string::npos;
}

static bool has_line(const std::string &gcode, const std::string &expected)
{
    return find_exact_line(gcode, expected) != std::string::npos;
}

static bool has_command_containing(const std::string &gcode, const std::string &token)
{
    std::size_t pos = 0;
    while (pos < gcode.size()) {
        const std::size_t eol = gcode.find('\n', pos);
        const std::string line = gcode.substr(pos, (eol == std::string::npos ? gcode.size() : eol) - pos);
        if (!line.empty() && line.front() != ';' && line.find(token) != std::string::npos)
            return true;
        if (eol == std::string::npos)
            break;
        pos = eol + 1;
    }
    return false;
}

static const char *kCopyScript = "SET_DUAL_CARRIAGE MODE=COPY";
static const char *kQuadScript = "SET_QUAD_CARRIAGE MODE=COPY";

static const char *kReportStartGcode = ";IMEX_MODE:{imex_mode}\n"
                                       ";IMEX_MODE_INDEX:{imex_mode_index}\n"
                                       ";IMEX_MODE_GCODE:{imex_mode_gcode}\n";

static void imex_7x4_printer(DynamicPrintConfig &config)
{
    config.set_deserialize_strict({
        { "nozzle_diameter",           "0.4,0.4,0.4,0.4,0.4,0.4,0.4" },
        { "printer_extruder_id",       "1,2,3,4,5,6,7" },
        { "printer_extruder_variant",  "Direct Drive Standard,Direct Drive Standard,Direct Drive Standard,"
                                       "Direct Drive Standard,Direct Drive Standard,Direct Drive Standard,"
                                       "Direct Drive Standard" },
        { "extruder_printable_height", "0,0,0,0,0,0,0" },
        { "physical_extruder_map",     "0,0,0,0,1,2,3" },
        { "filament_self_index",       "1,2,3,4,5,6,7" },
        { "is_imex",                   "1" },
        { "imex_mode_names",           "primary;copy;iq-copy" },
        { "imex_mode_active_tools",    "0:P;0:P,1:C;0:P,1:C,2:C,3:C" },
        { "skirt_loops",               "0" },
        { "brim_type",                 "no_brim" },
        { "gcode_flavor",              "klipper" },
    });
    size_flush_to_nozzles(config);
}

static void set_mode_gcodes(DynamicPrintConfig &config, const std::vector<std::string> &gcodes)
{
    config.option<ConfigOptionStrings>("imex_mode_gcodes", true)->values = gcodes;
}

static void all_regions_on_filament(DynamicPrintConfig &config, int filament_1based)
{
    for (const char *key : { "outer_wall_filament_id", "inner_wall_filament_id",
                             "sparse_infill_filament_id", "internal_solid_filament_id",
                             "top_surface_filament_id", "bottom_surface_filament_id" })
        config.set_deserialize_strict({ { key, std::to_string(filament_1based) } });
}


TEST_CASE("IMEX mode placeholders resolve to the active mode's name, index and script",
          "[ImexModeGcode][IMEX]")
{
    auto [mode, mode_index, mode_script] = GENERATE(table<std::string, int, std::string>({
        { "copy",    1, kCopyScript },
        { "iq-copy", 2, kQuadScript },
    }));
    INFO("active mode: " << mode);

    DynamicPrintConfig config = multifilament_config(7);
    imex_7x4_printer(config);
    all_regions_on_filament(config, 1);
    set_mode_gcodes(config, { "", kCopyScript, kQuadScript });
    config.set_deserialize_strict({
        { "imex_parallel_mode",  mode },
        { "machine_start_gcode", kReportStartGcode },
    });

    const std::string gcode = slice({ cube(20) }, config);

    CHECK(has_line(gcode, ";IMEX_MODE:" + mode));
    CHECK(has_line(gcode, ";IMEX_MODE_INDEX:" + std::to_string(mode_index)));
    CHECK(has_line(gcode, ";IMEX_MODE_GCODE:" + mode_script));
    CHECK(has_line(gcode, mode_script));
    CHECK_FALSE(has_line(gcode, mode == "copy" ? kQuadScript : kCopyScript));
}

TEST_CASE("A placeholder inside an IMEX mode's G-code script is expanded",
          "[ImexModeGcode][IMEX]")
{
    const std::string templ = "SET_DUAL_CARRIAGE MODE={imex_mode} INDEX={imex_mode_index}"
                              " TEMP={nozzle_temperature_initial_layer[0]}";

    DynamicPrintConfig config = multifilament_config(7);
    imex_7x4_printer(config);
    all_regions_on_filament(config, 1);
    set_mode_gcodes(config, { "", templ, kQuadScript });
    config.set_deserialize_strict({
        { "imex_parallel_mode",               "copy" },
        { "nozzle_temperature_initial_layer", "200" },
        { "machine_start_gcode",              ";START\n" },
    });

    const std::string gcode = slice({ cube(20) }, config);

    CHECK(has_line(gcode, "SET_DUAL_CARRIAGE MODE=copy INDEX=1 TEMP=200"));
    CHECK_FALSE(has_line(gcode, templ));
}

TEST_CASE("A global declared in the IMEX mode G-code is visible to machine_start_gcode",
          "[ImexModeGcode][IMEX]")
{
    DynamicPrintConfig config = multifilament_config(7);
    imex_7x4_printer(config);
    all_regions_on_filament(config, 1);
    set_mode_gcodes(config, { "", std::string("{global imex_carriage_count = 2}") + kCopyScript, kQuadScript });
    config.set_deserialize_strict({
        { "imex_parallel_mode",  "copy" },
        { "machine_start_gcode", ";IMEX_CARRIAGES:{imex_carriage_count}\n" },
    });

    const std::string gcode = slice({ cube(20) }, config);

    const std::size_t start_gcode_at = find_exact_line(gcode, ";IMEX_CARRIAGES:2");
    REQUIRE(start_gcode_at != std::string::npos);

    const std::size_t mode_script_at = find_exact_line(gcode, kCopyScript);
    REQUIRE(mode_script_at != std::string::npos);
    CHECK(mode_script_at < start_gcode_at);
}

TEST_CASE("Primary-mode IMEX prints emit no mode G-code", "[ImexModeGcode][IMEX]")
{
    DynamicPrintConfig config = multifilament_config(7);
    imex_7x4_printer(config);
    all_regions_on_filament(config, 1);
    set_mode_gcodes(config, { "", kCopyScript, kQuadScript });
    config.set_deserialize_strict({
        { "imex_parallel_mode",  "primary" },
        { "machine_start_gcode", kReportStartGcode },
    });

    const std::string gcode = slice({ cube(20) }, config);

    CHECK(has_line(gcode, ";IMEX_MODE:primary"));
    CHECK(has_line(gcode, ";IMEX_MODE_INDEX:0"));
    CHECK(has_line(gcode, ";IMEX_MODE_GCODE:"));
    CHECK_FALSE(has_line(gcode, kCopyScript));
    CHECK_FALSE(has_line(gcode, kQuadScript));
}

TEST_CASE("An IMEX mode the printer no longer defines falls back to Primary",
          "[ImexModeGcode][IMEX]")
{
    static const char *kPrimaryScript = "SET_DUAL_CARRIAGE MODE=PRIMARY";

    DynamicPrintConfig config = multifilament_config(7);
    imex_7x4_printer(config);
    all_regions_on_filament(config, 1);
    set_mode_gcodes(config, { kPrimaryScript, kCopyScript, kQuadScript });
    config.set_deserialize_strict({
        { "imex_parallel_mode",  "copy-renamed" },
        { "machine_start_gcode", kReportStartGcode },
    });

    const std::string gcode = slice({ cube(20) }, config);

    CHECK(has_line(gcode, ";IMEX_MODE:primary"));
    CHECK(has_line(gcode, ";IMEX_MODE_INDEX:0"));
    CHECK(has_line(gcode, ";IMEX_MODE_GCODE:" + std::string(kPrimaryScript)));

    CHECK(has_line(gcode, kPrimaryScript));
    CHECK_FALSE(has_line(gcode, kCopyScript));
    CHECK_FALSE(has_line(gcode, kQuadScript));
}

TEST_CASE("A printer with IMEX disabled resolves the mode placeholders to nothing",
          "[ImexModeGcode][IMEX]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    set_mode_gcodes(config, { "", kCopyScript, kQuadScript });
    config.set_deserialize_strict({
        { "imex_mode_names",        "primary;copy;iq-copy" },
        { "imex_mode_active_tools", "0:P;0:P,1:C;0:P,1:C,2:C,3:C" },
        { "is_imex",                "0" },
        { "imex_parallel_mode",     "copy" },
        { "skirt_loops",            "0" },
        { "brim_type",              "no_brim" },
        { "gcode_flavor",           "klipper" },
        { "machine_start_gcode",    kReportStartGcode },
    });

    const std::string gcode = slice({ cube(20) }, config);

    CHECK(has_line(gcode, ";IMEX_MODE:"));
    CHECK(has_line(gcode, ";IMEX_MODE_INDEX:0"));
    CHECK(has_line(gcode, ";IMEX_MODE_GCODE:"));
    CHECK_FALSE(has_line(gcode, kCopyScript));
    CHECK_FALSE(has_line(gcode, kQuadScript));
}

TEST_CASE("IMEX mode placeholders are defined on an ordinary single-extruder printer",
          "[ImexModeGcode][IMEX]")
{
    const std::string gcode = slice({ cube(20) }, {
        { "skirt_loops",         "0" },
        { "brim_type",           "no_brim" },
        { "gcode_flavor",        "klipper" },
        { "machine_start_gcode", kReportStartGcode },
    });

    CHECK(has_line(gcode, ";IMEX_MODE:"));
    CHECK(has_line(gcode, ";IMEX_MODE_INDEX:0"));
    CHECK(has_line(gcode, ";IMEX_MODE_GCODE:"));
}

TEST_CASE("Every carriage a parallel mode declares active is addressed, and no other",
          "[ImexModeGcode][IMEX]")
{
    DynamicPrintConfig config = multifilament_config(7);
    imex_7x4_printer(config);
    all_regions_on_filament(config, 1);
    set_mode_gcodes(config, { "", kCopyScript, kQuadScript });
    config.set_deserialize_strict({
        { "imex_parallel_mode",               "copy" },
        { "enable_pressure_advance",          "1,1,1,1,1,1,1" },
        { "pressure_advance",                 "0.010,0.020,0.030,0.040,0.050,0.060,0.070" },
        { "nozzle_temperature_initial_layer", "235,235,235,235,235,235,235" },
        { "nozzle_temperature",               "240,241,242,243,244,245,246" },
    });

    const std::string gcode = slice({ cube(20) }, config);

    CHECK(has_command_containing(gcode, "M104 S240 T0 ; set IMEX tool temperature"));
    CHECK(has_command_containing(gcode, "M104 S244 T1 ; set IMEX tool temperature"));
    CHECK_FALSE(has_command_containing(gcode, "T2 ; set IMEX tool temperature"));
    CHECK_FALSE(has_command_containing(gcode, "T3 ; set IMEX tool temperature"));

    CHECK(has_command_containing(gcode, "SET_PRESSURE_ADVANCE ADVANCE=0.05 EXTRUDER=extruder1;"));
    CHECK(has_command_containing(gcode, "SET_PRESSURE_ADVANCE ADVANCE=0.01 EXTRUDER=extruder;"));
    CHECK_FALSE(has_command_containing(gcode, "EXTRUDER=extruder2;"));
}
