#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "libslic3r/GCode/ConformalEmit.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/GCodeWriter.hpp"
#include "libslic3r/Nonplanar/BeadModel.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "test_utils.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Nonplanar;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double FilamentDiameter = 1.75;
constexpr double Width            = 0.45;

double filament_area() { return 0.25 * M_PI * FilamentDiameter * FilamentDiameter; }

void set_limits(PrintConfig &config)
{
    config.machine_max_speed_x.values        = { 200., 200. };
    config.machine_max_speed_y.values        = { 200., 200. };
    config.machine_max_speed_z.values        = { 8., 8. };
    config.machine_max_acceleration_z.values = { 200., 200. };
    config.filament_diameter.values          = { FilamentDiameter };
    config.use_relative_e_distances.value    = true;
}

struct Emitted
{
    std::string gcode;
    double      volume = 0.;
};

Emitted emit(GCodeWriter &writer, const PrintConfig &config, const std::vector<ConformalPoint> &points, double speed)
{
    Emitted out;
    out.gcode = "G90\nG21\nM83\n;TYPE:Top surface\n";
    out.gcode += writer.travel_to_pose(points.front().pose);
    writer.set_speed(out.gcode, speed * 60.);
    ConformalEmitParams params;
    params.nominal_speed  = speed;
    params.e_per_mm3      = 1. / filament_area();
    params.limits.max_v_xy = config.machine_max_speed_x.values.front();
    params.limits.max_v_z  = config.machine_max_speed_z.values.front();
    params.limits.max_a_z  = config.machine_max_acceleration_z.values.front();
    float last_height = 0.f;
    out.volume = emit_conformal_extrusion(writer, points, Width, params, last_height, out.gcode);
    out.gcode += writer.end_inverse_time();
    return out;
}

const GCodeProcessorResult &process(GCodeProcessor &processor, const PrintConfig &config, const std::string &gcode)
{
    ScopedTemporaryFile temp(".gcode");
    {
        std::ofstream os(temp.string());
        os << gcode;
    }
    FullPrintConfig full;
    full.apply(config, true);
    processor.apply_config(full);
    processor.process_file(temp.string());
    return processor.get_result();
}

double sum_e(const std::string &gcode)
{
    double             e = 0.;
    std::istringstream in(gcode);
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("G1 ", 0) != 0)
            continue;
        if (const size_t pos = line.find(" E"); pos != std::string::npos)
            e += std::stod(line.substr(pos + 2));
    }
    return e;
}

std::vector<ConformalPoint> dome_raster(double half_width)
{
    std::vector<ConformalPoint> pts;
    const double R = 60.;
    bool         forward = true;
    for (double y = -half_width; y <= half_width + 1e-9; y += Width) {
        std::vector<ConformalPoint> row;
        for (double x = -half_width; x <= half_width + 1e-9; x += 0.25) {
            ConformalPoint p;
            p.pose.tip    = Vec3d(100. + x, 100. + y, 20. + std::sqrt(R * R - x * x - y * y) - R);
            p.bead_height = 0.2f;
            row.push_back(p);
        }
        if (! forward)
            std::reverse(row.begin(), row.end());
        pts.insert(pts.end(), row.begin(), row.end());
        forward = ! forward;
    }
    return pts;
}

}

TEST_CASE("Conformal extrusion charges the bead area over the projected length", "[Nonplanar][NonplanarEmit]")
{
    PrintConfig config;
    set_limits(config);
    GCodeWriter writer;
    writer.apply_print_config(config);
    writer.set_extruders({ 0 });
    writer.set_extruder(0);

    const std::vector<ConformalPoint> dome = dome_raster(10.);
    const Emitted                     out  = emit(writer, config, dome, 60.);

    double projected = 0.;
    for (size_t i = 1; i < dome.size(); ++i)
        projected += (dome[i].pose.tip - dome[i - 1].pose.tip).head<2>().norm();
    const double expected = bead_area(Width, double(0.2f)) * projected;
    CHECK_THAT(out.volume, WithinRel(expected, 1e-9));
    CHECK_THAT(sum_e(out.gcode) * filament_area(), WithinRel(expected, 0.01));
    double length3d = 0.;
    for (size_t i = 1; i < dome.size(); ++i)
        length3d += (dome[i].pose.tip - dome[i - 1].pose.tip).norm();
    CHECK(bead_area(Width, double(0.2f)) * length3d > expected * 1.003);
}

TEST_CASE("Processed conformal moves stay under the machine Z speed", "[Nonplanar][NonplanarEmit]")
{
    PrintConfig config;
    set_limits(config);
    GCodeWriter writer;
    writer.apply_print_config(config);
    writer.set_extruders({ 0 });
    writer.set_extruder(0);

    std::vector<ConformalPoint> wave;
    for (double x = 0.; x <= 36. + 1e-9; x += 0.1) {
        ConformalPoint p;
        p.pose.tip    = Vec3d(50. + x, 50., 5. + 0.25 * std::sin(2. * M_PI * x / 6.));
        p.bead_height = 0.2f;
        wave.push_back(p);
    }
    const Emitted              out    = emit(writer, config, wave, 60.);
    GCodeProcessor              processor;
    const GCodeProcessorResult &result = process(processor, config, out.gcode);

    double max_vz = 0.;
    size_t extrusions = 0;
    for (size_t i = 1; i < result.moves.size(); ++i) {
        const auto &m = result.moves[i];
        if (m.type != EMoveType::Extrude)
            continue;
        ++extrusions;
        const Vec3f d = m.position - result.moves[i - 1].position;
        if (d.norm() > 0.f)
            max_vz = std::max(max_vz, double(m.feedrate) * std::abs(d.z()) / d.norm());
    }
    CHECK(extrusions > 100);
    CHECK(max_vz <= config.machine_max_speed_z.values.front() * 1.001);
    CHECK(max_vz > 0.5 * config.machine_max_speed_z.values.front());
}

TEST_CASE("Three-axis pose moves are ordinary XYZ moves", "[Nonplanar][NonplanarEmit]")
{
    PrintConfig config;
    set_limits(config);
    GCodeWriter a, b;
    for (GCodeWriter *w : { &a, &b }) {
        w->apply_print_config(config);
        w->set_extruders({ 0 });
        w->set_extruder(0);
    }
    std::string ga, gb;
    for (const Vec3d &p : { Vec3d(10., 10., 1.), Vec3d(12., 10., 1.2), Vec3d(14., 11., 1.2) }) {
        a.extrude_to_pose(ga, { p, Vec3d::UnitZ() }, 0.05);
        b.extrude_to_xyz(gb, p, 0.05);
    }
    CHECK(ga == gb);
    CHECK(a.end_inverse_time().empty());
}

TEST_CASE("Rotary table G-code parses back to the programmed tips", "[Nonplanar][NonplanarEmit]")
{
    PrintConfig config;
    set_limits(config);
    config.nonplanar_kinematics.value        = RotaryLayout::TableBC;
    config.rotary_axis_limits.values         = { -60., 60., -180., 180. };
    config.rotary_max_speed.values           = { 28.6, 57.3 };
    config.rotary_pivot.values               = { 91., -62.65, 102. };
    config.rotary_tool_mount.values          = { -3., 35., 82.05 };
    config.rotary_tool_length.value          = 60.;
    config.rotary_table_height.value         = 6.5;
    config.rotary_linear_moves_table.values  = { false, true, false };
    config.rotary_inverse_time_feed.value    = true;
    config.nonplanar_max_tilt.value          = 60.;
    config.machine_max_speed_x.values        = { 100., 100. };
    config.machine_max_speed_y.values        = { 80., 80. };
    config.machine_max_speed_z.values        = { 20., 20. };

    GCodeWriter writer;
    writer.apply_print_config(config);
    writer.set_tool_kinematics(make_tool_kinematics(config));
    writer.set_extruders({ 0 });
    writer.set_extruder(0);

    std::vector<ConformalPoint> path;
    for (int i = 0; i <= 40; ++i) {
        const double   t = i / 40.;
        ConformalPoint p;
        p.pose.tip    = Vec3d(-10. + 20. * t, 3. * std::sin(M_PI * t), 8. + 2. * t);
        const double tilt = 30. * M_PI / 180. * t, az = M_PI * t;
        p.pose.axis   = Vec3d(std::sin(tilt) * std::cos(az), std::sin(tilt) * std::sin(az), std::cos(tilt));
        p.bead_height = 0.2f;
        path.push_back(p);
    }
    const Emitted out = emit(writer, config, path, 20.);
    INFO(out.gcode.substr(0, 600));
    CHECK_THAT(out.gcode, Catch::Matchers::ContainsSubstring("G93\n"));
    CHECK_THAT(out.gcode, Catch::Matchers::ContainsSubstring("G94\nG1 F"));
    CHECK_THAT(out.gcode, Catch::Matchers::ContainsSubstring(" B"));
    CHECK_THAT(out.gcode, Catch::Matchers::ContainsSubstring(" C"));

    GCodeProcessor              processor;
    const GCodeProcessorResult &result = process(processor, config, out.gcode);
    auto distance_to_path = [&path](const Vec3d &q) {
        double best = 1e9;
        for (size_t i = 1; i < path.size(); ++i) {
            const Vec3d  a = path[i - 1].pose.tip, b = path[i].pose.tip;
            const double t = std::clamp((q - a).dot(b - a) / (b - a).squaredNorm(), 0., 1.);
            best           = std::min(best, (a + t * (b - a) - q).norm());
        }
        return best;
    };
    size_t extrusions = 0;
    double worst      = 0.;
    for (const auto &m : result.moves)
        if (m.type == EMoveType::Extrude) {
            ++extrusions;
            worst = std::max(worst, distance_to_path(m.position.cast<double>()));
        }
    CHECK(extrusions >= path.size() - 1);
    CHECK(worst < 0.005);
    const Vec3d end = result.moves.back().position.cast<double>();
    CHECK_THAT(end.x(), WithinAbs(path.back().pose.tip.x(), 2e-3));
    CHECK_THAT(end.y(), WithinAbs(path.back().pose.tip.y(), 2e-3));
    CHECK_THAT(end.z(), WithinAbs(path.back().pose.tip.z(), 2e-3));
    CHECK_THAT(sum_e(out.gcode) * filament_area(), WithinRel(out.volume, 0.01));
}
