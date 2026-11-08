#include <catch2/catch_all.hpp>
#include "libslic3r/GCode/ClearanceVerifier.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "test_helpers.hpp"
#include "test_utils.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>

using namespace Slic3r;
using namespace Slic3r::Nonplanar;
namespace {
struct CompatibleTags
{
    bool previous = GCodeProcessor::s_IsBBLPrinter;
    CompatibleTags() { GCodeProcessor::s_IsBBLPrinter = false; }
    ~CompatibleTags() { GCodeProcessor::s_IsBBLPrinter = previous; }
};
std::string read(const std::string &path)
{
    std::ifstream in(path);
    REQUIRE(in.good());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
ClearanceParams fixture_head(const std::string &filename)
{
    ClearanceParams params;
    params.head = load_head_envelope(read(std::string(TEST_DATA_DIR) + "/nonplanar/" + filename));
    return params;
}
}

TEST_CASE("The chronological verifier recalls every seeded trap with clear controls", "[Nonplanar][Clearance]")
{
    const CompatibleTags tags;
    const std::string name = GENERATE("dome_pillar", "travel_through_pillar", "gantry", "plane_40",
                                     "order_control", "plane_20_control", "planar_control");
    const std::string root = std::string(TEST_DATA_DIR) + "/nonplanar/";
    const auto fixture = nlohmann::json::parse(read(root + "trap_" + name + ".json"));
    GCodeProcessor processor;
    processor.apply_config(FullPrintConfig());
    processor.process_file(root + fixture.at("gcode").get<std::string>());
    const auto &result = processor.get_result();
    const auto params = fixture_head(fixture.at("true_head").get<std::string>());
    const auto report = verify_clearance(result, params, ThreeAxisKinematics());
    std::set<unsigned int> hit_lines;
    for (size_t i = 0; i < result.moves.size(); ++i)
        if (report.per_move[i] < -1e-5f) hit_lines.insert(result.moves[i].gcode_id);
    for (unsigned int line : fixture.at("expected").at("hit_lines")) {
        INFO("trap " << name << ", expected line " << line);
        REQUIRE(hit_lines.count(line) == 1);
    }
    if (fixture.at("control").get<bool>()) {
        INFO("trap " << name << ", first hit " << (report.worst.empty() ? 0 : report.worst.front().gcode_line));
        REQUIRE(report.passed());
    }
    REQUIRE(report.worst.size() <= 256);
}

TEST_CASE("A real planar cube has no clearance hits at the default margin", "[Nonplanar][Clearance]")
{
    Print print; Model model;
    const auto config = DynamicPrintConfig::full_print_config();
    Test::init_print({Test::cube(20.)}, print, model, config);
    print.process();
    GCodeProcessorResult result;
    Test::gcode(print, &result);
    REQUIRE_FALSE(result.nonplanar);
    REQUIRE(result.clearance.empty());
    const auto params = fixture_head("trap_head_true.json");
    const auto report = verify_clearance(result, params, ThreeAxisKinematics());
    INFO("first hit " << (report.worst.empty() ? 0 : report.worst.front().gcode_line));
    REQUIRE(report.passed());
}

TEST_CASE("Export clearance reports an end G-code collision without blocking and skips disabled checking", "[Nonplanar][Clearance]")
{
    const std::string mode = GENERATE("off", "warn", "block");
    auto config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict("nonplanar_collision_check", mode);
    config.set_deserialize_strict("nonplanar_head", "generic-conservative");
    config.set_deserialize_strict("machine_end_gcode", ";_NONPLANAR_BEGIN patch=0 shell=0\nG90\nG1 X{(first_layer_print_min[0]+first_layer_print_max[0])/2} Y{(first_layer_print_min[1]+first_layer_print_max[1])/2} Z0.2 F1200\nG1 X{first_layer_print_max[0]}\n;_NONPLANAR_END\n");
    Print print; Model model;
    Test::init_print({Test::cube(20.)}, print, model, config);
    print.process();
    GCodeProcessorResult result;
    Test::gcode(print, &result);
    REQUIRE(result.nonplanar);
    if (mode == "off") { REQUIRE(result.clearance.empty()); REQUIRE_FALSE(result.clearance_report.ran); }
    else {
        REQUIRE(result.clearance_report.ran);
        REQUIRE(result.clearance_report.machine_hit_moves > 0);
        REQUIRE(result.clearance_report.hit_moves == 0);
        REQUIRE(result.clearance.size() == result.moves.size());
    }
}

TEST_CASE("The processor retains stationary tip rotary moves for clearance checking", "[Nonplanar][Clearance]")
{
    const CompatibleTags tags;
    FullPrintConfig config;
    config.nonplanar_kinematics.value = RotaryLayout::TableBC;
    config.rotary_firmware_rtcp.value = true;
    GCodeProcessor processor;
    processor.apply_config(config);
    ScopedTemporaryFile file(".gcode");
    {
        std::ofstream out(file.string());
        out << "G90\nM83\n;HEIGHT:0.2\n;WIDTH:0.45\n;TYPE:Internal solid infill\n"
               "G1 X-4 Y-0.5 Z0 F1200\nG1 X-4 Y0.5 E0.1\nG1 X0 Y0 Z0\nG1 B90\n";
    }
    processor.process_file(file.string());
    const auto &result = processor.get_result();
    REQUIRE(result.nonplanar);
    REQUIRE_FALSE(result.moves.empty());
    REQUIRE(result.joints.size() == result.moves.size());
    REQUIRE_THAT(result.joints.back().rot[0], Catch::Matchers::WithinAbs(90., 1e-9));
    ClearanceParams params;
    params.head.margin = 0.05; params.head.lateral_margin = 0.;
    params.head.boxes.push_back({ "heater_block", Vec3d(-2., -2., 2.), Vec3d(2., 2., 5.) });
    const auto report = verify_clearance(result, params, *make_tool_kinematics(config));
    REQUIRE(report.per_move.back() < 0.);
    REQUIRE(report.worst.front().component == "heater_block");
}

TEST_CASE("Benchy clearance overhead is recorded when a local mesh is supplied", "[Nonplanar][Clearance][Benchmark]")
{
    const char *filename = std::getenv("NONPLANAR_BENCHY");
    if (filename == nullptr || *filename == '\0') SKIP("Set NONPLANAR_BENCHY to an unmodified local Benchy STL to measure export overhead.");
    const Model benchy = Model::read_from_file(filename);
    REQUIRE_FALSE(benchy.objects.empty());
    Print print; Model model;
    Test::init_print({benchy.objects.front()->mesh()}, print, model, DynamicPrintConfig::full_print_config());
    print.process();
    GCodeProcessorResult result;
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    Test::gcode(print, &result);
    const double export_s = std::chrono::duration<double>(Clock::now() - start).count();
    ClearanceParams params;
    params.head = HeadLibrary::generic_conservative();
    const auto check_start = Clock::now();
    const auto report = verify_clearance(result, params, ThreeAxisKinematics());
    const double check_s = std::chrono::duration<double>(Clock::now() - check_start).count();
    std::cout << "Benchy export=" << export_s << " s clearance=" << check_s << " s overhead=" << 100. * check_s / export_s << "% hits=" << report.hit_moves << '\n';
    REQUIRE(report.ran);
    REQUIRE(report.per_move.size() == result.moves.size());
}
