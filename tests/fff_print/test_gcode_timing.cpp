#include <catch2/catch_all.hpp>

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/catch_message.hpp>
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/MultiNozzleUtils.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "test_utils.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <ios>
#include <map>
#include <math.h>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "libslic3r/Config.hpp"

namespace Slic3r { enum ExtrusionRole : uint8_t; }

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

namespace {

constexpr size_t NORMAL = static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal);

FullPrintConfig make_config(double load_time, double unload_time, double tool_change_time)
{
    FullPrintConfig config;
    config.gcode_flavor.value = gcfMarlinFirmware;
    config.filament_diameter.values = {1.75, 1.75};
    config.filament_map.values = {1, 1};
    config.machine_load_filament_time.value = load_time;
    config.machine_unload_filament_time.value = unload_time;
    config.machine_tool_change_time.value = tool_change_time;
    return config;
}

void run_processor(GCodeProcessor& proc, const FullPrintConfig& config, const char* gcode)
{
    GCodeProcessor::s_IsBBLPrinter = true;
    ScopedTemporaryFile temp(".gcode");
    {
        std::ofstream os(temp.string());
        os << gcode;
    }
    proc.apply_config(config);
    proc.process_file(temp.string());
}

std::map<ExtrusionRole, double> role_times(const GCodeProcessorResult& r)
{
    std::map<ExtrusionRole, double> m;
    for (const auto& mv : r.moves)
        if (mv.type == EMoveType::Extrude)
            m[mv.extrusion_role] += mv.time[NORMAL];
    return m;
}

double sum_tool_change_time(const GCodeProcessorResult& r)
{
    double t = 0.0;
    for (const auto& mv : r.moves)
        if (mv.type == EMoveType::Tool_change)
            t += mv.time[NORMAL];
    return t;
}

double filament_change_delay(const GCodeProcessorResult& r)
{
    const auto& s = r.print_statistics;
    return s.total_filament_load_time + s.total_filament_unload_time + s.total_tool_change_time;
}

}

TEST_CASE("Filament-change time is attributed to tool-change moves, not extrusion roles", "[GCodeTiming]")
{
    const char* gcode =
        "M83\n"
        "; FEATURE: Outer wall\n"
        "G1 X10 Y10 Z0.2 F600\n"
        "G1 X0 Y0 F6000\n"
        "T0\n"
        "; FEATURE: Outer wall\n"
        "G1 X50 Y0 E5 F1800\n"
        "G1 X50 Y50 E5\n"
        "; FEATURE: Sparse infill\n"
        "G1 X0 Y50 E5\n"
        "G1 X0 Y0 E5\n"
        "T1\n"
        "; FEATURE: Inner wall\n"
        "G1 X50 Y0 E5\n"
        "G1 X50 Y50 E5\n";

    GCodeProcessor proc_zero;
    run_processor(proc_zero, make_config(0.0, 0.0, 0.0), gcode);
    const GCodeProcessorResult& r_zero = proc_zero.get_result();

    const double load = 10.0;
    const double unload = 5.0;
    GCodeProcessor proc_delay;
    run_processor(proc_delay, make_config(load, unload, 0.0), gcode);
    const GCodeProcessorResult& r_delay = proc_delay.get_result();

    const double delay = filament_change_delay(r_delay);

    REQUIRE(delay > 0.0);
    REQUIRE_THAT(filament_change_delay(r_zero), WithinAbs(0.0, 1e-9));

    const auto roles_zero  = role_times(r_zero);
    const auto roles_delay = role_times(r_delay);
    REQUIRE(roles_zero.size() >= 3);
    REQUIRE(roles_zero.size() == roles_delay.size());
    for (const auto& [role, zero_time] : roles_zero) {
        INFO("extrusion role index = " << static_cast<int>(role));
        REQUIRE(roles_delay.count(role) == 1);
        REQUIRE_THAT(roles_delay.at(role), WithinAbs(zero_time, 1e-2));
    }

    REQUIRE_THAT(sum_tool_change_time(r_delay), WithinAbs(delay, 1e-2));

    const double first_layer_delta = proc_delay.get_first_layer_time(PrintEstimatedStatistics::ETimeMode::Normal)
                                   - proc_zero.get_first_layer_time(PrintEstimatedStatistics::ETimeMode::Normal);
    REQUIRE_THAT(first_layer_delta, WithinAbs(delay, 1e-2));
}

TEST_CASE("Filament-change time is not dropped when few motion blocks precede the change", "[GCodeTiming]")
{
    const char* gcode =
        "; FEATURE: Outer wall\n"
        "G1 X10 Y10 Z0.2 F600\n"
        "T0\n"
        "G1 X50 Y0 E5 F1800\n"
        "G1 X50 Y50 E5\n"
        "T1\n"
        "G1 X0 Y50 E5\n"
        "G1 X0 Y0 E5\n";

    GCodeProcessor proc_zero;
    run_processor(proc_zero, make_config(0.0, 0.0, 0.0), gcode);

    const double load = 10.0;
    const double unload = 5.0;
    GCodeProcessor proc_delay;
    run_processor(proc_delay, make_config(load, unload, 0.0), gcode);
    const GCodeProcessorResult& r_delay = proc_delay.get_result();

    const double delay = filament_change_delay(r_delay);
    REQUIRE(delay > 0.0);

    const double total_delta = proc_delay.get_time(PrintEstimatedStatistics::ETimeMode::Normal)
                             - proc_zero.get_time(PrintEstimatedStatistics::ETimeMode::Normal);
    REQUIRE_THAT(total_delta, WithinAbs(delay, 1e-2));
}

TEST_CASE("Back-to-back tool changes buffer then merge into one tool-change block", "[GCodeTiming]")
{
    const char* gcode =
        "T0\n"
        "T1\n"
        "G1 X10 Y0 Z0.2 F6000\n"
        "G1 X10 Y10\n"
        "G1 X0 Y10\n";

    GCodeProcessor proc_zero;
    run_processor(proc_zero, make_config(0.0, 0.0, 0.0), gcode);
    const GCodeProcessorResult& r_zero = proc_zero.get_result();

    GCodeProcessor proc_delay;
    run_processor(proc_delay, make_config(10.0, 5.0, 0.0), gcode);
    const GCodeProcessorResult& r_delay = proc_delay.get_result();

    const double delay = filament_change_delay(r_delay);
    REQUIRE(delay > 0.0);
    REQUIRE_THAT(delay, WithinAbs(25.0, 1e-6));
    REQUIRE_THAT(filament_change_delay(r_zero), WithinAbs(0.0, 1e-9));

    const double total_delta = proc_delay.get_time(PrintEstimatedStatistics::ETimeMode::Normal)
                             - proc_zero.get_time(PrintEstimatedStatistics::ETimeMode::Normal);
    REQUIRE_THAT(total_delta, WithinAbs(delay, 1e-2));

    REQUIRE_THAT(sum_tool_change_time(r_delay), WithinAbs(25.0, 1e-2));
    REQUIRE_THAT(sum_tool_change_time(r_zero), WithinAbs(0.0, 1e-9));

    std::vector<double> tc;
    for (const auto& mv : r_delay.moves)
        if (mv.type == EMoveType::Tool_change)
            tc.push_back(mv.time[NORMAL]);
    REQUIRE(tc.size() >= 2);
    REQUIRE_THAT(tc[0], WithinAbs(25.0, 1e-2));
    REQUIRE_THAT(tc[1], WithinAbs(0.0, 1e-9));
}

TEST_CASE("Trailing tool change at end of file is drained, not dropped", "[GCodeTiming]")
{
    const char* gcode =
        "G1 X10 Y0 Z0.2 F6000\n"
        "G1 X10 Y10\n"
        "G1 X0 Y10\n"
        "G4 S0\n"
        "T0\n";

    GCodeProcessor proc_zero;
    run_processor(proc_zero, make_config(0.0, 0.0, 0.0), gcode);
    const GCodeProcessorResult& r_zero = proc_zero.get_result();

    GCodeProcessor proc_delay;
    run_processor(proc_delay, make_config(10.0, 5.0, 0.0), gcode);
    const GCodeProcessorResult& r_delay = proc_delay.get_result();

    const double delay = filament_change_delay(r_delay);
    REQUIRE(delay > 0.0);
    REQUIRE_THAT(delay, WithinAbs(10.0, 1e-6));

    const double total_delta = proc_delay.get_time(PrintEstimatedStatistics::ETimeMode::Normal)
                             - proc_zero.get_time(PrintEstimatedStatistics::ETimeMode::Normal);
    REQUIRE_THAT(total_delta, WithinAbs(delay, 1e-2));

    REQUIRE_THAT(sum_tool_change_time(r_delay), WithinAbs(10.0, 1e-2));
    REQUIRE_THAT(sum_tool_change_time(r_zero), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Carried-forward tool-change delay reaches the total without polluting roles", "[GCodeTiming]")
{
    const char* gcode =
        "M83\n"
        "G4 S3\n"
        "T0\n"
        "; FEATURE: Inner wall\n"
        "G1 X20 Y0 Z0.2 E5 F1800\n"
        "G4 S0\n"
        "G1 X20 Y20 E5\n"
        "G1 X0 Y20 E5\n";

    GCodeProcessor proc_zero;
    run_processor(proc_zero, make_config(0.0, 0.0, 0.0), gcode);
    const GCodeProcessorResult& r_zero = proc_zero.get_result();

    GCodeProcessor proc_delay;
    run_processor(proc_delay, make_config(10.0, 5.0, 0.0), gcode);
    const GCodeProcessorResult& r_delay = proc_delay.get_result();

    const double delay = filament_change_delay(r_delay);
    REQUIRE(delay > 0.0);
    REQUIRE_THAT(delay, WithinAbs(10.0, 1e-6));

    const double total_delta = proc_delay.get_time(PrintEstimatedStatistics::ETimeMode::Normal)
                             - proc_zero.get_time(PrintEstimatedStatistics::ETimeMode::Normal);
    REQUIRE_THAT(total_delta, WithinAbs(delay, 1e-2));

    const auto rz = role_times(r_zero);
    const auto rd = role_times(r_delay);
    REQUIRE(rz.size() >= 1);
    REQUIRE(rz.size() == rd.size());
    for (const auto& [role, zero_time] : rz) {
        INFO("extrusion role index = " << static_cast<int>(role));
        REQUIRE(rd.count(role) == 1);
        REQUIRE_THAT(rd.at(role), WithinAbs(zero_time, 1e-2));
    }
}

TEST_CASE("Per-slot machine limits follow the active nozzle", "[GCodeTiming][MultiNozzle]")
{
    FullPrintConfig config = make_config(0.0, 0.0, 0.0);
    config.extruder_type.values            = {static_cast<int>(etDirectDrive)};
    config.printer_extruder_id.values      = {1, 1};
    config.printer_extruder_variant.values = {"Direct Drive Standard", "Direct Drive High Flow"};
    config.machine_max_speed_x.values = {200., 200., 50., 50.};
    config.machine_max_speed_y.values = {200., 200., 50., 50.};
    config.machine_max_speed_z.values = {200., 200., 50., 50.};
    config.machine_max_speed_e.values = {200., 200., 50., 50.};
    for (auto *accel : {&config.machine_max_acceleration_x, &config.machine_max_acceleration_y,
                        &config.machine_max_acceleration_z, &config.machine_max_acceleration_e})
        accel->values = {100000., 100000., 100000., 100000.};
    config.machine_max_acceleration_travel.values    = {100000., 100000.};
    config.machine_max_acceleration_extruding.values = {100000., 100000.};
    config.machine_max_jerk_x.values = {10000., 10000.};
    config.machine_max_jerk_y.values = {10000., 10000.};
    config.machine_max_jerk_z.values = {10000., 10000.};
    config.machine_max_jerk_e.values = {10000., 10000.};

    std::vector<MultiNozzleUtils::NozzleInfo> nozzles;
    {
        MultiNozzleUtils::NozzleInfo n;
        n.diameter = "0.4";
        n.volume_type = nvtStandard; n.extruder_id = 0; n.group_id = 0; nozzles.push_back(n);
        n.volume_type = nvtHighFlow; n.extruder_id = 0; n.group_id = 1; nozzles.push_back(n);
    }
    std::vector<int>          filament_nozzle_map = {0, 1};
    std::vector<unsigned int> used_filaments      = {0, 1};
    auto group = MultiNozzleUtils::LayeredNozzleGroupResult::create(filament_nozzle_map, nozzles, used_filaments);
    REQUIRE(group.has_value());
    auto context = std::make_shared<MultiNozzleUtils::LayeredNozzleGroupResult>(*group);

    const char* gcode =
        "M83\n"
        "T0 H0\n"
        "G1 X100 F30000\n"
        "T1 H1\n"
        "G1 X0 F30000\n"
        "G1 X1 F30000\n";

    auto travel_times_by_tool = [](const GCodeProcessorResult& r) {
        std::vector<double> out(1, 0.0);
        for (const auto& mv : r.moves) {
            if (mv.type == EMoveType::Tool_change)
                out.push_back(0.0);
            else if (mv.type == EMoveType::Travel)
                out.back() += mv.time[NORMAL];
        }
        return out;
    };

    SECTION("the move on the High Flow nozzle is clamped by its own slot") {
        GCodeProcessor proc;
        proc.initialize_from_context(context);
        run_processor(proc, config, gcode);
        auto times = travel_times_by_tool(proc.get_result());
        REQUIRE(times.size() == 3);
        REQUIRE_THAT(times[1], Catch::Matchers::WithinRel(100.0 / 200.0, 0.10));
        REQUIRE_THAT(times[2], Catch::Matchers::WithinRel(101.0 / 50.0, 0.10));
    }
    SECTION("an emitted envelope line reaches every slot") {
        const std::string enveloped = std::string("M201 X20000\nM203 X80\n") + gcode;
        GCodeProcessor proc;
        proc.initialize_from_context(context);
        run_processor(proc, config, enveloped.c_str());
        auto times = travel_times_by_tool(proc.get_result());
        REQUIRE(times.size() == 3);
        REQUIRE_THAT(times[1], Catch::Matchers::WithinRel(100.0 / 80.0, 0.10));
        REQUIRE_THAT(times[2], Catch::Matchers::WithinRel(101.0 / 80.0, 0.10));
    }
    SECTION("no grouping context degrades to slot 0") {
        GCodeProcessor proc;
        run_processor(proc, config, gcode);
        auto times = travel_times_by_tool(proc.get_result());
        REQUIRE(times.size() == 3);
        REQUIRE_THAT(times[1], Catch::Matchers::WithinRel(100.0 / 200.0, 0.10));
        REQUIRE_THAT(times[2], Catch::Matchers::WithinRel(101.0 / 200.0, 0.10));
    }
}

namespace {

FullPrintConfig make_junction_config(GCodeFlavor flavor, double corner_velocity, double junction_deviation)
{
    FullPrintConfig config;
    config.gcode_flavor.value = flavor;
    config.filament_diameter.values = {1.75};
    config.filament_map.values = {1};

    const std::vector<double> accel   = {1000.0, 1000.0};
    const std::vector<double> axis    = {20000.0, 20000.0};
    const std::vector<double> speed   = {500.0, 500.0};
    config.machine_max_acceleration_extruding.values = accel;
    config.machine_max_acceleration_travel.values    = accel;
    config.machine_max_acceleration_retracting.values = accel;
    config.machine_max_acceleration_x.values = axis;
    config.machine_max_acceleration_y.values = axis;
    config.machine_max_acceleration_z.values = axis;
    config.machine_max_acceleration_e.values = axis;
    config.machine_max_speed_x.values = speed;
    config.machine_max_speed_y.values = speed;
    config.machine_max_speed_z.values = speed;
    config.machine_max_speed_e.values = speed;
    config.machine_max_jerk_x.values = {corner_velocity, corner_velocity};
    config.machine_max_jerk_y.values = {corner_velocity, corner_velocity};
    config.machine_max_jerk_z.values = {corner_velocity, corner_velocity};
    config.machine_max_jerk_e.values = {100.0, 100.0};
    config.machine_max_junction_deviation.values = {junction_deviation, junction_deviation};
    config.machine_min_extruding_rate.values = {0.0, 0.0};
    config.machine_min_travel_rate.values    = {0.0, 0.0};
    return config;
}

constexpr double junction_x = 60.0;
constexpr double junction_y = 60.0;

std::string corner_gcode(double turn_deg, double orientation_deg, double e_per_mm = 0.0)
{
    const double len   = 40.0;
    const double a_in  = orientation_deg * M_PI / 180.0;
    const double a_out = (orientation_deg + turn_deg) * M_PI / 180.0;
    std::ostringstream extrude;
    if (e_per_mm > 0.0)
        extrude << std::fixed << std::setprecision(4) << " E" << len * e_per_mm;

    std::ostringstream os;
    os << std::fixed << std::setprecision(4)
       << "M83\n"
       << "G1 Z0.2 F1200\n"
       << "G1 X" << junction_x - len * std::cos(a_in)  << " Y" << junction_y - len * std::sin(a_in)  << " F6000\n"
       << "G1 X" << junction_x                         << " Y" << junction_y   << extrude.str()      << " F9000\n"
       << "G1 X" << junction_x + len * std::cos(a_out) << " Y" << junction_y + len * std::sin(a_out)
       << extrude.str() << " F9000\n";
    return os.str();
}

double corner_speed(const GCodeProcessorResult& r)
{
    for (const auto& mv : r.moves)
        if ((mv.type == EMoveType::Travel || mv.type == EMoveType::Extrude) &&
            std::abs(mv.position.x() - junction_x) < 1e-3 &&
            std::abs(mv.position.y() - junction_y) < 1e-3)
            return mv.actual_feedrate;
    return -1.0;
}

double planned_corner_speed(GCodeFlavor flavor, double corner_velocity, double junction_deviation,
                            double turn_deg, double orientation_deg = 0.0, double e_per_mm = 0.0)
{
    GCodeProcessor proc;
    run_processor(proc, make_junction_config(flavor, corner_velocity, junction_deviation),
                  corner_gcode(turn_deg, orientation_deg, e_per_mm).c_str());
    return corner_speed(proc.get_result());
}

}

TEST_CASE("Klipper corners are planned with junction deviation derived from the square corner velocity",
          "[GCodeTiming][JunctionDeviation]")
{
    const double scv = 5.0;

    SECTION("a right angle is taken at exactly the square corner velocity") {
        REQUIRE_THAT(planned_corner_speed(gcfKlipper, scv, 0.0, 90.0), Catch::Matchers::WithinRel(scv, 0.02));
    }

    SECTION("a shallow corner is taken far faster than the per-axis jerk model allows") {
        const double jd_speed     = planned_corner_speed(gcfKlipper, scv, 0.0, 6.0);
        const double jerk_speed   = planned_corner_speed(gcfMarlinLegacy, scv, 0.0, 6.0);
        REQUIRE_THAT(jd_speed, Catch::Matchers::WithinRel(86.87, 0.02));
        REQUIRE_THAT(jerk_speed, Catch::Matchers::WithinRel(47.75, 0.02));
    }
}

TEST_CASE("Junction deviation limits a corner by its angle alone, not by its orientation",
          "[GCodeTiming][JunctionDeviation]")
{
    const double scv = 5.0;
    const double turn = 6.0;

    SECTION("Klipper plans both orientations identically") {
        const double on_axis  = planned_corner_speed(gcfKlipper, scv, 0.0, turn, 0.0);
        const double diagonal = planned_corner_speed(gcfKlipper, scv, 0.0, turn, 45.0);
        REQUIRE(on_axis > 0.0);
        REQUIRE_THAT(diagonal, Catch::Matchers::WithinRel(on_axis, 0.02));
    }

    SECTION("the classic jerk model keeps its orientation dependence") {
        const double on_axis  = planned_corner_speed(gcfMarlinLegacy, scv, 0.0, turn, 0.0);
        const double diagonal = planned_corner_speed(gcfMarlinLegacy, scv, 0.0, turn, 45.0);
        REQUIRE(on_axis > 0.0);
        REQUIRE(diagonal / on_axis > 1.2);
    }
}

TEST_CASE("Junction deviation is only used where the firmware actually plans with it",
          "[GCodeTiming][JunctionDeviation]")
{
    const double jerk = 5.0;

    SECTION("Marlin 2 with M205 J disabled keeps the classic jerk planning") {
        const double classic = planned_corner_speed(gcfMarlinLegacy, jerk, 0.0, 90.0);
        REQUIRE(classic > 0.0);
        REQUIRE_THAT(planned_corner_speed(gcfMarlinFirmware, jerk, 0.0, 90.0),
                     Catch::Matchers::WithinRel(classic, 1e-4));
    }

    SECTION("Marlin 2 with M205 J enabled switches to junction deviation") {
        REQUIRE_THAT(planned_corner_speed(gcfMarlinFirmware, jerk, 0.05, 90.0),
                     Catch::Matchers::WithinRel(10.99, 0.02));
    }

    SECTION("machines without junction deviation are untouched by the jerk values it would ignore") {
        const double without = planned_corner_speed(gcfMarlinLegacy, jerk, 0.0, 90.0);
        REQUIRE_THAT(planned_corner_speed(gcfMarlinLegacy, jerk, 0.05, 90.0),
                     Catch::Matchers::WithinRel(without, 1e-4));
    }
}

TEST_CASE("How fast a corner is taken does not depend on how much is extruded through it",
          "[GCodeTiming][JunctionDeviation]")
{
    const double scv  = 5.0;
    const double turn = 6.0;
    const double geometric = planned_corner_speed(gcfKlipper, scv, 0.0, turn);
    REQUIRE(geometric > 0.0);

    for (double e_per_mm : {0.029, 0.1})
        REQUIRE_THAT(planned_corner_speed(gcfKlipper, scv, 0.0, turn, 0.0, e_per_mm),
                     Catch::Matchers::WithinRel(geometric, 0.02));

    SECTION("and the same holds on Marlin 2") {
        const double marlin = planned_corner_speed(gcfMarlinFirmware, scv, 0.05, turn);
        REQUIRE(marlin > 0.0);
        REQUIRE_THAT(planned_corner_speed(gcfMarlinFirmware, scv, 0.05, turn, 0.0, 0.029),
                     Catch::Matchers::WithinRel(marlin, 0.02));
    }
}
