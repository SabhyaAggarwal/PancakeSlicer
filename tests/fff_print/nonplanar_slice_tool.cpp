#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

#include "admesh/stl.h"
#include <Eigen/Geometry>
#include <boost/filesystem/directory.hpp>
#include "libslic3r/Nonplanar/Report.hpp"
#include "libslic3r/Nonplanar/ShellBase.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintBase.hpp"
#include <math.h>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/CutUtils.hpp"
#include "libslic3r/enum_bitmask.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/FlushVolCalc.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/GCode/ClearanceVerifier.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/GCode/WipeTowerEstimate.hpp"
#include "libslic3r/GCodeWriter.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/ModelArrange.hpp"
#include "libslic3r/Arrange.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Nonplanar/AutoOrient.hpp"
#include "libslic3r/Nonplanar/Calibration.hpp"
#include "libslic3r/Nonplanar/ConformalShells.hpp"
#include "libslic3r/Nonplanar/AutoMode.hpp"
#include "libslic3r/Nonplanar/FlowMod.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Nonplanar/Multiplanar.hpp"
#include "libslic3r/Nonplanar/NpField.hpp"
#include "libslic3r/Nonplanar/OrientationField.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/calib.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/miniz_extension.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/system/error_code.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <miniz.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <ios>
#include <limits>
#include <cctype>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <ostream>
#include <stdexcept>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace Slic3r;
namespace fs = boost::filesystem;

void RGB2HSV(float r, float g, float b, float *h, float *s, float *v)
{
    const float cmax = std::max({ r, g, b }), cmin = std::min({ r, g, b }), delta = cmax - cmin;
    if (std::abs(delta) < 0.001f) *h = 0.f;
    else if (cmax == r) *h = 60.f * std::fmod((g - b) / delta, 6.f);
    else if (cmax == g) *h = 60.f * ((b - r) / delta + 2);
    else *h = 60.f * ((r - g) / delta + 4);
    *s = std::abs(cmax) < 0.001f ? 0.f : delta / cmax;
    *v = cmax;
}

namespace {

struct Args
{
    std::string profiles = std::string(PROFILES_DIR);
    std::string vendor   = "Prusa";
    std::string printer, process, datadir, output, model, config_out, paint, gcode_3mf, json, plate, project_out, head_out, layer_profile, project, save_3mf,
                tip_out, planned_out, verify_gcode, field_file, dump_field, move_times, calib, calib_head, calib_picks, calib_params, moves_out, dump_options, dump_presets;
    bool        progress = false;
    int         calib_fit = -1;
    int         plate_index = 0;
    std::vector<std::string> filaments, colours;
    std::vector<std::pair<std::string, int>> parts;
    std::vector<std::pair<std::string, std::string>> overrides;
    std::vector<CustomGCode::Item> events;
    bool        center_set = false;
    bool        verify     = false;
    bool        explain    = false;
    bool        auto_orient = false;
    bool        orient_only = false;
    std::string edit;
    std::string cut, cut_keep = "both";
    bool        cut_place_lower = false;
    bool        force_planar    = false;
    std::string arrange_only;
    double      arrange_spacing  = -1.;
    bool        arrange_rotation = true;
    bool        surface_grid    = false;
    size_t      surface_grid_max_cells = 40000;
    std::string patch_preview;
    double      preview_budget        = 2.;
    size_t      preview_max_triangles = 300000;
    std::string confirm_up;
    std::vector<std::string> argv;
    Vec2d       center     = Vec2d::Zero();
};

struct StepMeshing
{
    double linear_deflection = 0.003;
    double angle_deflection  = 0.5;
    bool   split_compound    = false;
} step_meshing;

void clear_object_modes(Model &model)
{
    for (ModelObject *object : model.objects) {
        object->config.erase("nonplanar_mode");
        for (ModelVolume *volume : object->volumes)
            volume->config.erase("nonplanar_mode");
        for (auto &[range, range_config] : object->layer_config_ranges)
            range_config.erase("nonplanar_mode");
    }
}

Model read_input(const std::string &file)
{
    if (boost::algorithm::iends_with(file, ".step") || boost::algorithm::iends_with(file, ".stp"))
        return Model::read_from_step(file, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances, nullptr, nullptr, nullptr,
                                     step_meshing.linear_deflection, step_meshing.angle_deflection, step_meshing.split_compound);
    return Model::read_from_file(file, nullptr, nullptr, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances);
}

[[noreturn]] void usage(const std::string &error)
{
    std::cerr << "error: " << error << "\n"
              << "usage: nonplanar_slice_tool --printer NAME --process NAME --filament NAME [--vendor Prusa]\n"
                 "       [--profiles DIR] [--datadir DIR] [--bed-type NAME] [--set key=value]... [--center X,Y]\n"
                 "       [--config-out FILE] [--project-out FILE] [--head-out FILE] [--gcode-3mf FILE] [--json FILE]\n"
                 "       [--field-file FIELD.npz] [--dump-field FIELD.npz]\n"
                 "       [--atom TOOLPATH] [--verify] [--explain] [--auto-orient | --orient-only] [--save-3mf PROJECT.3mf]\n"
                 "       [--plate-index N] [--layer-profile adaptive:QUALITY | ramp:H_BOTTOM:H_TOP | range:DEPTH:H | file:PATH] [--calib-head ID]\n"
                 "       -o OUT.gcode (MODEL | PROJECT.3mf | --plate PLATE.json | --project SPEC.json | --calib TEST)\n"
                 "       [--moves-out FILE] [--progress] [--calib-params key=value,...]\n"
                 "       (plate JSON objects take \"plate\": N; a plate JSON of several plates slices with --plate-index N)\n"
                 "       (plate JSON objects and volumes take \"paint\"; plate JSON objects take \"layer_height_profile\")\n"
                 "       [--surface-grid [--surface-grid-max-cells N]]   (with --json: per-cell top surface error)\n"
                 "       [--force-planar]   (nonplanar_mode off on the plate, objects, volumes and height ranges)\n"
                 "       [--edit EDITS.json]   (a loaded PROJECT.3mf: add/delete objects and instances, set filaments; --edit takes \"add_volume\"; --edit takes \"set_paint\")\n"
                 "       [--cut-only Z | X,Y,Z,NX,NY,NZ [--cut-keep upper|lower|both] [--cut-place-lower]]\n"
                 "       [--arrange-only OUT.json [--arrange-spacing MM] [--arrange-no-rotation]]   (the desktop's arrange, no slice)\n"
                 "       [--patch-preview FILE.json [--patch-preview-budget S] [--patch-preview-max-triangles N]]\n"
                 "       [--step-linear-deflection MM] [--step-angle-deflection A] [--step-split-compound]\n"
                 "       nonplanar_slice_tool --dump-options FILE | --dump-presets VENDOR FILE [--profiles DIR]\n"
                 "       nonplanar_slice_tool --load-gcode FILE.gcode   (times the preview's G-code load)\n";
    std::exit(2);
}

std::optional<CalibMode> classic_calib_mode(const std::string &kind);
std::vector<coordf_t> read_layer_profile(const std::string &path)
{
    std::ifstream in(path);
    if (!in)
        usage("--layer-profile file:" + path + ": cannot open the file");
    std::vector<coordf_t> profile;
    std::string           line;
    for (int number = 1; std::getline(in, line); ++number) {
        if (line.find_first_not_of(" \t\r") == std::string::npos)
            continue;
        std::istringstream fields(line);
        double             z, h;
        std::string        rest;
        if (!(fields >> z >> h) || (fields >> rest) || !std::isfinite(z) || !std::isfinite(h))
            usage("--layer-profile file:" + path + ": line " + std::to_string(number) + " is not a \"z h\" pair: " + line);
        if (h <= 0.)
            usage("--layer-profile file:" + path + ": line " + std::to_string(number) + " has a layer height <= 0");
        if (!profile.empty() && z <= profile[profile.size() - 2])
            usage("--layer-profile file:" + path + ": line " + std::to_string(number) + " does not increase z");
        profile.insert(profile.end(), { z, h });
    }
    if (profile.size() < 4)
        usage("--layer-profile file:" + path + " needs at least two \"z h\" lines");
    return profile;
}

Args parse_args(int argc, char **argv)
{
    Args a;
    a.argv.assign(argv, argv + argc);
    for (int i = 1; i < argc; ++i) {
        const std::string arg  = argv[i];
        auto              next = [&]() -> std::string {
            if (i + 1 >= argc)
                usage(arg + " needs a value");
            return argv[++i];
        };
        if (arg == "--printer") a.printer = next();
        else if (arg == "--process") a.process = next();
        else if (arg == "--filament") a.filaments.push_back(next());
        else if (arg == "--bed-type") {
            const std::string name = next();
            BedType bed_type;
            if (!ConfigOptionEnum<BedType>::from_string(name, bed_type) || bed_type <= btDefault || bed_type >= btCount)
                usage("--bed-type expects a plate name, e.g. Textured PEI Plate or Cool Plate");
            a.overrides.emplace_back("curr_bed_type", name);
        }
        else if (arg == "--colours") {
            std::string list = next();
            for (size_t pos = 0; pos <= list.size();) {
                const size_t end = std::min(list.find(',', pos), list.size());
                a.colours.push_back(list.substr(pos, end - pos));
                pos = end + 1;
            }
        } else if (arg == "--part") {
            const std::string spec = next();
            const size_t      pos  = spec.rfind(':');
            if (pos == std::string::npos)
                usage("--part expects FILE:EXTRUDER");
            a.parts.emplace_back(spec.substr(0, pos), std::atoi(spec.c_str() + pos + 1));
        } else if (arg == "--paint") a.paint = next();
        else if (arg == "--layer-profile") {
            a.layer_profile = next();
            if (a.layer_profile.rfind("file:", 0) == 0)
                read_layer_profile(a.layer_profile.substr(5));
        } else if (arg == "--vendor") a.vendor = next();
        else if (arg == "--profiles") a.profiles = next();
        else if (arg == "--datadir") a.datadir = next();
        else if (arg == "--config-out") a.config_out = next();
        else if (arg == "--project-out") a.project_out = next();
        else if (arg == "--head-out") a.head_out = next();
        else if (arg == "--gcode-3mf") a.gcode_3mf = next();
        else if (arg == "--explain") a.explain = true;
        else if (arg == "--atom") {
            const std::string path = next();
            if (path.empty())
                usage("--atom needs a nonempty toolpath filename");
            a.overrides.emplace_back("nonplanar_atom_toolpath", path);
        }
        else if (arg == "--field-file") {
            a.field_file = fs::absolute(next()).string();
            a.overrides.emplace_back("nonplanar_field_file", a.field_file);
        } else if (arg == "--dump-field") a.dump_field = fs::absolute(next()).string();
        else if (arg == "--json") a.json = next();
        else if (arg == "--moves-out") a.moves_out = next();
        else if (arg == "--dump-options") a.dump_options = next();
        else if (arg == "--dump-presets") { a.vendor = next(); a.dump_presets = next(); }
        else if (arg == "--progress") a.progress = true;
        else if (arg == "--move-times") a.move_times = next();
        else if (arg == "--plate-index") {
            const std::string value = next();
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                usage("--plate-index expects a positive 1-based integer");
            try { a.plate_index = std::stoi(value); }
            catch (const std::exception &) { usage("--plate-index is out of range"); }
            if (a.plate_index < 1) usage("--plate-index expects a positive 1-based integer");
        }
        else if (arg == "--plate") a.plate = next();
        else if (arg == "--project") a.project = next();
        else if (arg == "--save-3mf") a.save_3mf = next();
        else if (arg == "--tip-out") a.tip_out = next();
        else if (arg == "--planned-out") a.planned_out = next();
        else if (arg == "--verify-gcode") a.verify_gcode = next();
        else if (arg == "--calib") a.calib = next();
        else if (arg == "--calib-head") a.calib_head = next();
        else if (arg == "--calib-fit") a.calib_fit = std::atoi(next().c_str());
        else if (arg == "--calib-picks") a.calib_picks = next();
        else if (arg == "--calib-params") a.calib_params = next();
        else if (arg == "--verify") a.verify = true;
        else if (arg == "--auto-orient") a.auto_orient = true;
        else if (arg == "--orient-only") a.auto_orient = a.orient_only = true;
        else if (arg == "--edit") a.edit = next();
        else if (arg == "--force-planar") a.force_planar = true;
        else if (arg == "--surface-grid") a.surface_grid = true;
        else if (arg == "--surface-grid-max-cells") {
            const long n = std::atol(next().c_str());
            if (n < 1) usage("--surface-grid-max-cells expects a positive number");
            a.surface_grid_max_cells = size_t(n);
        }
        else if (arg == "--cut-only") a.cut = next();
        else if (arg == "--arrange-only") a.arrange_only = next();
        else if (arg == "--arrange-spacing") {
            const std::string value = next();
            char *end = nullptr;
            a.arrange_spacing = std::strtod(value.c_str(), &end);
            if (value.empty() || *end != '\0' || !(a.arrange_spacing >= 0.))
                usage("--arrange-spacing expects a distance in mm >= 0");
        } else if (arg == "--arrange-no-rotation") a.arrange_rotation = false;
        else if (arg == "--cut-keep") {
            a.cut_keep = next();
            if (a.cut_keep != "upper" && a.cut_keep != "lower" && a.cut_keep != "both")
                usage("--cut-keep expects upper, lower or both");
        } else if (arg == "--cut-place-lower") a.cut_place_lower = true;
        else if (arg == "--patch-preview") a.patch_preview = next();
        else if (arg == "--patch-preview-budget") {
            a.preview_budget = std::atof(next().c_str());
            if (!(a.preview_budget > 0.)) usage("--patch-preview-budget expects seconds > 0");
        } else if (arg == "--patch-preview-max-triangles") {
            const long n = std::atol(next().c_str());
            if (n < 1000) usage("--patch-preview-max-triangles expects at least 1000");
            a.preview_max_triangles = size_t(n);
        } else if (arg == "--step-linear-deflection" || arg == "--step-angle-deflection") {
            const std::string value = next();
            char *end = nullptr;
            const double v = std::strtod(value.c_str(), &end);
            if (value.empty() || *end != '\0' || !(v > 0.))
                usage(arg + " expects a positive number");
            (arg == "--step-linear-deflection" ? step_meshing.linear_deflection : step_meshing.angle_deflection) = v;
        } else if (arg == "--step-split-compound") step_meshing.split_compound = true;
        else if (arg == "--confirm-up") a.confirm_up = next();
        else if (arg == "-o" || arg == "--output") a.output = fs::absolute(next()).string();
        else if (arg == "--set") {
            const std::string kv  = next();
            const size_t      pos = kv.find('=');
            if (pos == std::string::npos)
                usage("--set expects key=value, got " + kv);
            a.overrides.emplace_back(kv.substr(0, pos), kv.substr(pos + 1));
        } else if (arg == "--event") {
            const std::string ev    = next();
            const size_t      colon = ev.find(':');
            CustomGCode::Item item{ 0., CustomGCode::Custom, 1, "", "" };
            const std::string type  = colon == std::string::npos ? "" : ev.substr(colon + 1, ev.find(':', colon + 1) - colon - 1);
            const std::string text  = ev.find(':', colon + 1) == std::string::npos ? "" : ev.substr(ev.find(':', colon + 1) + 1);
            item.print_z            = std::atof(ev.substr(0, colon).c_str());
            if (type == "pause") item.type = CustomGCode::PausePrint;
            else if (type == "template") item.type = CustomGCode::Template;
            else if (type == "colour") { item.type = CustomGCode::ColorChange; item.color = "#FF0000"; }
            else if (type == "change") { item.type = CustomGCode::ToolChange; item.extruder = std::max(1, std::atoi(text.c_str())); }
            else if (type == "custom") {
                item.extra = text;
                for (size_t pos; (pos = item.extra.find("\\n")) != std::string::npos;)
                    item.extra.replace(pos, 2, "\n");
            } else
                usage("--event expects Z:pause, Z:custom:GCODE, Z:template, Z:colour or Z:change:N");
            a.events.emplace_back(item);
        } else if (arg == "--center") {
            const std::string xy = next();
            if (std::sscanf(xy.c_str(), "%lf,%lf", &a.center.x(), &a.center.y()) != 2)
                usage("--center expects X,Y");
            a.center_set = true;
        } else if (! arg.empty() && arg[0] == '-')
            usage("unknown option " + arg);
        else
            a.model = arg;
    }
    if (!a.field_file.empty() && std::none_of(a.overrides.begin(), a.overrides.end(), [](const auto &o) { return o.first == "nonplanar_mode"; }))
        a.overrides.insert(a.overrides.begin(), { "nonplanar_mode", "field" });
    if (!a.dump_options.empty() || !a.dump_presets.empty() || !a.verify_gcode.empty()) return a;
    if (a.printer.empty() || a.process.empty() || a.filaments.empty() ||
        (a.output.empty() && ! a.orient_only && a.confirm_up.empty() && a.save_3mf.empty() && a.patch_preview.empty() && a.arrange_only.empty()) ||
        int(! a.model.empty()) + int(! a.plate.empty()) + int(! a.project.empty()) + int(! a.calib.empty()) != 1)
        usage("--printer, --process, --filament, -o and one of a model, --plate, --project or --calib are required");
    if (a.surface_grid && a.json.empty())
        usage("--surface-grid needs --json");
    if (! a.cut.empty() && a.output.empty())
        usage("--cut-only needs -o PIECES.3mf or -o PIECES.stl");
    if (! a.calib.empty() && ! Nonplanar::Calibration::parse_test(a.calib) && ! classic_calib_mode(a.calib))
        usage("unknown --calib test " + a.calib);
    if (! a.calib_params.empty() && ! classic_calib_mode(a.calib))
        usage("--calib-params needs a classic --calib KIND");
    return a;
}

std::string find_preset_file(const Args &a, const char *kind, const std::string &name)
{
    if (fs::is_regular_file(name))
        return name;
    std::vector<fs::path> dirs = { fs::path(a.profiles) / a.vendor / kind };
    if (std::string(kind) == "filament")
        dirs.push_back(fs::path(a.profiles) / "OrcaFilamentLibrary" / kind);
    for (const fs::path &dir : dirs) {
        if (! fs::is_directory(dir))
            continue;
        for (const fs::directory_entry &entry : fs::recursive_directory_iterator(dir)) {
            if (entry.path().extension() != ".json")
                continue;
            boost::nowide::ifstream in(entry.path().string());
            const nlohmann::json    j = nlohmann::json::parse(in, nullptr, false);
            if (j.is_object() && j.value("name", std::string()) == name)
                return entry.path().string();
        }
    }
    throw std::runtime_error(std::string(kind) + " preset \"" + name + "\" not found under " + dirs.front().string());
}

DynamicPrintConfig resolve(PresetBundle &bundle, const Args &a, const char *kind, Preset::Type type, const std::string &name)
{
    DynamicPrintConfig config;
    std::string        error, filament_id, preset_name = name;
    if (fs::path(name).extension() == ".json" && fs::is_regular_file(name)) {
        std::map<std::string, std::string> key_values;
        config.load_from_json(name, ForwardCompatibilitySubstitutionRule::EnableSilent, key_values, error);
        if (! error.empty())
            throw std::runtime_error("cannot load " + name + ": " + error);
        preset_name = key_values[BBL_JSON_KEY_NAME];
        if (preset_name.empty())
            preset_name = fs::path(name).stem().string();
        filament_id = key_values[BBL_JSON_KEY_FILAMENT_ID];
    } else {
        const std::string file = find_preset_file(a, kind, name);
        if (!bundle.resolve_preset_config(config, type, file, ForwardCompatibilitySubstitutionRule::EnableSilent, error, true, &filament_id))
            throw std::runtime_error("cannot resolve " + file + ": " + error);
    }
    if (type == Preset::TYPE_PRINTER)
        config.set_key_value("printer_settings_id", new ConfigOptionString(preset_name));
    else if (type == Preset::TYPE_PRINT)
        config.set_key_value("print_settings_id", new ConfigOptionString(preset_name));
    else if (type == Preset::TYPE_FILAMENT) {
        config.set_key_value("filament_settings_id", new ConfigOptionStrings{preset_name});
        if (!filament_id.empty())
            config.set_key_value("filament_ids", new ConfigOptionStrings{filament_id});
    }
    if (type != Preset::TYPE_PRINTER)
        config.erase("printer_settings_id");
    if (type != Preset::TYPE_PRINT)
        config.erase("print_settings_id");
    if (type != Preset::TYPE_FILAMENT)
        config.erase("filament_settings_id");
    return config;
}

Vec3d emitted_tip(const GCodeProcessorResult &result, size_t i, bool rotary)
{
    return !rotary && result.has_joints() ? result.joints[i].xyz : Vec3d(result.moves[i].position.cast<double>());
}

struct WallFragmentation
{
    size_t runs = 0;
    double wall_mm = 0., near_end_mm = 0.;
};
WallFragmentation wall_fragmentation(const GCodeProcessorResult &result, bool rotary)
{
    WallFragmentation out;
    double run_mm = 0.;
    bool   in_run = false;
    Vec3d  last   = Vec3d::Zero();
    auto   close  = [&]() {
        if (in_run)
            out.near_end_mm += std::min(run_mm, 2.), ++out.runs;
        in_run = false, run_mm = 0.;
    };
    size_t prev = 0;
    for (size_t i = 1; i < result.moves.size(); ++i) {
        const auto &m = result.moves[i];
        if (m.internal_only)
            continue;
        const Vec3d a = emitted_tip(result, prev, rotary), b = emitted_tip(result, i, rotary);
        prev          = i;
        const bool  wall = m.type == EMoveType::Extrude && m.delta_extruder > 0.f && is_perimeter(m.extrusion_role);
        if (!wall || (in_run && (a - last).norm() > 1e-3)) {
            close();
            if (!wall)
                continue;
        }
        const double l = (b - a).norm();
        out.wall_mm += l, run_mm += l, in_run = true, last = b;
    }
    close();
    return out;
}

std::pair<double, double> max_vz(const GCodeProcessorResult &result, size_t *actual_line = nullptr, bool rotary = false)
{
    double commanded = 0., actual = 0.;
    size_t previous = 0;
    for (size_t i = 1; i < result.moves.size(); ++i) {
        const auto &m = result.moves[i];
        if (m.internal_only)
            continue;
        const Vec3d d = emitted_tip(result, i, rotary) - emitted_tip(result, previous, rotary);
        const size_t begin = previous + 1;
        previous = i;
        const double l = d.norm();
        if (l < 1e-6 || (m.type != EMoveType::Extrude && m.type != EMoveType::Travel))
            continue;
        const double z_fraction = std::abs(d.z()) / l;
        commanded = std::max(commanded, z_fraction * m.feedrate);
        for (size_t j = begin; j <= i; ++j) {
            const auto &sample = result.moves[j];
            if (sample.gcode_id != m.gcode_id)
                continue;
            if (const double v = z_fraction * sample.actual_feedrate; v > actual) {
                actual = v;
                if (actual_line)
                    *actual_line = m.gcode_id;
            }
        }
    }
    return { commanded, actual };
}

indexed_triangle_set plate_mesh(const Model &model)
{
    indexed_triangle_set out;
    for (const ModelObject *object : model.objects)
        for (const ModelInstance *instance : object->instances) {
            indexed_triangle_set its = object->raw_indexed_triangle_set();
            its_transform(its, instance->get_matrix());
            its_merge(out, its);
        }
    return out;
}

std::string vendor_json_value(const std::vector<fs::path> &dirs, std::string name, const std::string &key)
{
    for (int depth = 0; depth < 8 && ! name.empty(); ++depth) {
        std::string inherits;
        for (const fs::path &dir : dirs) {
            if (! fs::is_directory(dir))
                continue;
            for (const fs::directory_entry &entry : fs::recursive_directory_iterator(dir)) {
                if (entry.path().extension() != ".json")
                    continue;
                boost::nowide::ifstream in(entry.path().string());
                const nlohmann::json    j = nlohmann::json::parse(in, nullptr, false);
                if (! j.is_object() || j.value("name", std::string()) != name)
                    continue;
                if (j.contains(key) && j[key].is_string())
                    return j[key].get<std::string>();
                inherits = j.value("inherits", std::string());
                break;
            }
            if (! inherits.empty())
                break;
        }
        name = inherits;
    }
    return {};
}

void write_gcode_3mf(const Args &args, Model &model, Print &print, DynamicPrintConfig &config, GCodeProcessorResult &result,
                     const PlateData *source_plate)
{
    auto plate                     = source_plate ? std::make_unique<PlateData>(*source_plate) : std::make_unique<PlateData>();
    plate->objects_and_instances.clear();
    if (!source_plate)
        plate->config.apply_only(config, {"filament_map_mode", "filament_map", "filament_volume_map"});
    else if (!plate->config.has("filament_map_mode"))
        plate->config.apply_only(config, {"filament_map_mode"});
    if (const auto *mode = plate->config.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode"); mode && is_auto_filament_map_mode(mode->value)) {
        plate->config.option<ConfigOptionInts>("filament_map", true)->values        = print.get_filament_maps();
        plate->config.option<ConfigOptionInts>("filament_volume_map", true)->values = print.get_filament_volume_maps();
    }
    plate->plate_index             = 0;
    for (size_t i = 0; i < model.objects.size(); ++i)
        for (size_t j = 0; j < model.objects[i]->instances.size(); ++j)
            plate->objects_and_instances.emplace_back(int(i), int(j));
    plate->gcode_file              = args.output;
    plate->is_sliced_valid         = true;
    plate->toolpath_outside        = result.toolpath_outside;
    plate->timelapse_warning_code  = result.timelapse_warning_code;
    plate->limit_filament_maps     = result.limit_filament_maps;
    plate->layer_filaments         = result.layer_filaments;
    plate->filament_change_sequence = result.filament_change_sequence;
    plate->nozzle_change_sequence  = result.nozzle_change_sequence;
    plate->optimal_assignment     = result.optimal_assignment;
    plate->is_label_object_enabled = result.label_object_enabled;
    plate->is_support_used         = print.is_support_used();
    plate->printer_model_id = vendor_json_value({ fs::path(args.profiles) / args.vendor / "machine" }, config.opt_string("printer_model"), "model_id");
    plate->nozzle_diameters = config.option("nozzle_diameter")->serialize();
    plate->gcode_prediction = std::to_string(int(result.print_statistics.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)].time));
    plate->first_layer_time = std::to_string(result.initial_layer_time);
    plate->nonplanar_report.clear();
    if (!result.nonplanar_report.empty())
        plate->nonplanar_report = result.nonplanar_report.to_json();
    plate->slice_filaments_info.clear();
    plate->mixed_filaments_info.clear();
    plate->nozzle_group_result.reset();
    plate->parse_filament_info(&result);
    nlohmann::json bambu_ids;
    {
        boost::nowide::ifstream in((fs::path(resources_dir()) / "printers" / "bambu_filament_ids.json").string());
        bambu_ids = nlohmann::json::parse(in, nullptr, false);
    }
    for (FilamentInfo &info : plate->slice_filaments_info) {
        std::string display_type;
        info.type        = config.get_filament_type(display_type, int(info.id));
        info.color       = config.option<ConfigOptionStrings>("filament_colour")->get_at(info.id);
        const auto *ids = config.option<ConfigOptionStrings>("filament_ids");
        info.filament_id = ids && info.id < ids->values.size() ? ids->values[info.id] : std::string();
        if (info.filament_id.empty())
            info.filament_id = vendor_json_value({ fs::path(args.profiles) / args.vendor / "filament",
                                               fs::path(args.profiles) / "OrcaFilamentLibrary" / "filament" },
                                             args.filaments[std::min(size_t(info.id), args.filaments.size() - 1)], "filament_id");
        if (bambu_ids.is_object() && bambu_ids["filaments"].contains(info.filament_id))
            info.filament_id = bambu_ids["filaments"][info.filament_id].value("bambu_id", info.filament_id);
    }
    char weight[32];
    std::snprintf(weight, sizeof(weight), "%.2f", print.print_statistics().total_weight);
    plate->gcode_weight = weight;

    const indexed_triangle_set mesh     = plate_mesh(model);
    const std::string &colour = config.option<ConfigOptionStrings>("filament_colour")->get_at(0);
    ThumbnailData plate_png = render_thumbnail(mesh, 512, 512, false, colour), top_png = render_thumbnail(mesh, 512, 512, true, colour);
    ThumbnailData no_light_png = plate_png, pick_png = top_png;
    if (!plate->thumbnail_file.empty() && fs::exists(plate->thumbnail_file))
        plate_png.reset();
    else
        plate->plate_thumbnail.load_from(plate_png);
    if (!plate->no_light_thumbnail_file.empty() && fs::exists(plate->no_light_thumbnail_file))
        no_light_png.reset();
    if (!plate->top_file.empty() && fs::exists(plate->top_file))
        top_png.reset();
    if (!plate->pick_file.empty() && fs::exists(plate->pick_file))
        pick_png.reset();

    PlateBBoxData bboxes;
    bboxes.is_seq_print     = config.opt_enum<PrintSequence>("print_sequence") == PrintSequence::ByObject;
    bboxes.first_extruder   = print.get_tool_ordering().first_extruder();
    bboxes.nozzle_diameter  = float(config.option<ConfigOptionFloats>("nozzle_diameter")->get_at(0));
    bboxes.bed_type         = bed_type_to_gcode_string(config.opt_enum<BedType>("curr_bed_type"));
    bboxes.first_layer_time = result.initial_layer_time;
    BoundingBoxf all;
    for (PrintObject *object : print.objects()) {
        BBoxData   data;
        const auto b = unscaled(object->get_first_layer_bbox(data.area, data.layer_height, data.name));
        all.merge(BoundingBoxf(b.min, b.max));
        data.area *= float(SCALING_FACTOR * SCALING_FACTOR);
        data.id   = int(object->id().id);
        data.bbox = { b.min.x(), b.min.y(), b.max.x(), b.max.y() };
        bboxes.bbox_objs.push_back(data);
    }
    bboxes.bbox_all = { all.min.x(), all.min.y(), all.max.x(), all.max.y() };
    for (const FilamentInfo &info : plate->slice_filaments_info) {
        bboxes.filament_ids.push_back(info.id);
        bboxes.filament_colors.push_back(info.color);
    }

    model.get_backup_path();
    StoreParams params;
    params.path                    = args.gcode_3mf;
    params.model                   = &model;
    params.plate_data_list         = { plate.get() };
    params.export_plate_idx        = 0;
    params.config                  = &config;
    params.thumbnail_data          = { &plate_png };
    params.no_light_thumbnail_data = { &no_light_png };
    params.top_thumbnail_data      = { &top_png };
    params.pick_thumbnail_data     = { &pick_png };
    params.id_bboxes               = { &bboxes };
    params.strategy = SaveStrategy::Silence | SaveStrategy::WithGcode | SaveStrategy::SplitModel | SaveStrategy::ShareMesh |
                      SaveStrategy::SkipModel;
    if (source_plate)
        params.strategy = params.strategy | SaveStrategy::UseLoadedId;
    if (! store_bbs_3mf(params))
        throw std::runtime_error("cannot write " + args.gcode_3mf);
}

void apply_filaments(PresetBundle &bundle, const Args &a, DynamicPrintConfig &config)
{
    const size_t n = a.filaments.size();
    std::vector<DynamicPrintConfig> slots;
    for (const std::string &name : a.filaments)
        slots.emplace_back(resolve(bundle, a, "filament", Preset::TYPE_FILAMENT, name));
    config.apply(slots.front());
    if (n == 1 && a.colours.empty())
        return;
    for (const std::string &key : slots.front().keys())
        if (auto *dst = dynamic_cast<ConfigOptionVectorBase *>(config.optptr(key))) {
            const auto *first = dynamic_cast<const ConfigOptionVectorBase *>(slots.front().optptr(key));
            if (first == nullptr || first->empty())
                continue;
            dst->resize(n, first);
            for (size_t i = 0; i < n; ++i)
                if (const ConfigOption *src = slots[i].optptr(key))
                    dst->set_at(src, i, 0);
        }
    static const char *defaults[] = { "#F2F2F2", "#D02020", "#2050D0", "#20A040", "#E0C020", "#202020", "#E07020", "#9040C0" };
    std::vector<std::string> colours;
    for (size_t i = 0; i < n; ++i)
        colours.push_back(i < a.colours.size() ? a.colours[i] : defaults[i % 8]);
    config.option<ConfigOptionStrings>("filament_colour", true)->values = colours;
    if (auto *map = config.option<ConfigOptionInts>("filament_map", true))
        map->values.assign(n, 1);
    auto rgb = [](const std::string &hex, unsigned char out[3]) {
        for (int c = 0; c < 3; ++c)
            out[c] = (unsigned char) std::strtol(hex.substr(1 + 2 * c, 2).c_str(), nullptr, 16);
    };
    const auto *volume  = config.option<ConfigOptionFloats>("nozzle_volume");
    const auto *dataset = config.option<ConfigOptionInts>("nozzle_flush_dataset");
    const int   min_vol = volume != nullptr && !volume->values.empty() ? int(volume->values.front()) : 0;
    FlushVolCalculator calculator(min_vol, g_max_flush_volume, dataset != nullptr && !dataset->values.empty() ? dataset->values.front() : 0);
    std::vector<double> &matrix = config.option<ConfigOptionFloats>("flush_volumes_matrix", true)->values;
    matrix.assign(n * n, 0.);
    for (size_t from = 0; from < n; ++from)
        for (size_t to = 0; to < n; ++to)
            if (from != to) {
                unsigned char f[3], t[3];
                rgb(colours[from], f);
                rgb(colours[to], t);
                matrix[from * n + to] = calculator.calc_flush_vol(255, f[0], f[1], f[2], 255, t[0], t[1], t[2]);
            }
    config.option<ConfigOptionFloats>("flush_multiplier", true)->values.resize(1, 1.);
    config.option<ConfigOptionFloats>("flush_volumes_vector", true)->values.assign(2 * n, 140.);
}

void apply_multi_material(const Args &a, Model &model)
{
    ModelObject *object = model.objects.front();
    if (!a.paint.empty()) {
        ModelVolume            *volume = object->volumes.front();
        boost::nowide::ifstream in(a.paint);
        std::vector<int>        ids;
        for (int id; in >> id;)
            ids.push_back(id);
        if (ids.size() != volume->mesh().its.indices.size())
            throw std::runtime_error("--paint has " + std::to_string(ids.size()) + " entries, the mesh " +
                                     std::to_string(volume->mesh().its.indices.size()) + " triangles");
        TriangleSelector selector(volume->mesh());
        for (size_t i = 0; i < ids.size(); ++i)
            if (ids[i] > 0)
                selector.set_facet(int(i), EnforcerBlockerType(ids[i]));
        volume->mmu_segmentation_facets.set(selector);
    }
    for (const auto &[file, extruder] : a.parts) {
        Model part = read_input(file);
        if (part.objects.empty())
            throw std::runtime_error("no objects in " + file);
        const ModelVolume *src    = part.objects.front()->volumes.front();
        ModelVolume       *volume = object->add_volume(src->mesh());
        volume->set_transformation(src->get_transformation());
        volume->config.set("extruder", extruder);
    }
    if (!a.parts.empty())
        object->volumes.front()->config.set("extruder", 1);
}

void place_wipe_tower(DynamicPrintConfig &config, const Model &model, size_t filaments)
{
    double x = 165., y = 250.;
    if (const auto *structure = config.option<ConfigOptionEnum<PrinterStructure>>("printer_structure");
        structure != nullptr && structure->value == PrinterStructure::psI3)
        x = 0.;
    std::vector<unsigned int> ids(filaments);
    for (size_t i = 0; i < filaments; ++i)
        ids[i] = unsigned(i);
    double height = 0.;
    for (const ModelObject *object : model.objects)
        height = std::max(height, object->bounding_box_exact().size().z());
    const WipeTowerFootprint fp = estimate_wipe_tower_footprint(config, resolve_wipe_tower_type(config), ids,
                                                                config.opt_float("layer_height"), height);
    const BoundingBoxf bed(config.opt<ConfigOptionPoints>("printable_area")->values);
    const double       margin = WIPE_TOWER_AUTO_MARGIN + fp.brim_width;
    x = std::clamp(x, bed.min.x() + margin, std::max(bed.min.x() + margin, bed.max.x() - fp.width - margin));
    y = std::clamp(y, bed.min.y() + margin, std::max(bed.min.y() + margin, bed.max.y() - fp.depth - margin));
    Polygons outline{ Polygon::new_scale(config.opt<ConfigOptionPoints>("printable_area")->values) };
    const Vec2f move = WipeTower::move_box_inside_polygon(BoundingBox(Point::new_scale(x, y), Point::new_scale(x + fp.width, y + fp.depth)),
                                                          outline, scaled<coord_t>(margin));
    config.option<ConfigOptionFloats>("wipe_tower_x", true)->values = { x + move.x() };
    config.option<ConfigOptionFloats>("wipe_tower_y", true)->values = { y + move.y() };
}

std::string layer_profile_mismatch(const std::vector<coordf_t> &profile, const SlicingParameters &sp)
{
    const double height = sp.object_print_z_uncompensated_max - sp.object_print_z_min;
    std::ostringstream why;
    why.precision(6);
    if (std::abs(profile[profile.size() - 2] - height) > 1e-3)
        why << "its last z " << profile[profile.size() - 2] << " is not the object's top " << height << " (within 0.001 mm)";
    else if (profile[1] != sp.first_object_layer_height)
        why << "its first layer height " << profile[1] << " is not the first layer height " << sp.first_object_layer_height;
    return why.str();
}

std::vector<ObjectID> plate_layer_profiles;

void check_plate_layer_profiles(const Print &print)
{
    for (const PrintObject *po : print.objects()) {
        if (std::find(plate_layer_profiles.begin(), plate_layer_profiles.end(), po->model_object()->id()) == plate_layer_profiles.end())
            continue;
        if (const std::string why = layer_profile_mismatch(po->model_object()->layer_height_profile.get(), po->slicing_parameters()); !why.empty())
            throw std::runtime_error("plate object " + po->model_object()->name + ": layer_height_profile: " + why);
    }
}

void apply_layer_profile(const Args &args, Model &model, Print &print, const DynamicPrintConfig &config)
{
    if (args.layer_profile.empty())
        return;
    const size_t colon = args.layer_profile.find(':');
    const std::string kind = args.layer_profile.substr(0, colon);
    double a = 0., b = 0.;
    const int values = colon == std::string::npos ? 0 : std::sscanf(args.layer_profile.c_str() + colon + 1, "%lf:%lf", &a, &b);
    std::vector<coordf_t> given;
    if (kind == "file")
        given = read_layer_profile(args.layer_profile.substr(colon + 1));
    else if (!((kind == "adaptive" && values >= 1) || ((kind == "ramp" || kind == "range") && values == 2)))
        usage("--layer-profile expects adaptive:Q, ramp:H0:H1, range:D:H or file:PATH");
    for (ModelObject *object : model.objects) {
        const PrintObject *print_object = nullptr;
        for (const PrintObject *po : print.objects())
            if (po->model_object()->id() == object->id())
                print_object = po;
        if (print_object == nullptr)
            continue;
        const SlicingParameters &sp  = print_object->slicing_parameters();
        const double             top = sp.object_print_z_uncompensated_height();
        if (kind == "file") {
            if (const std::string why = layer_profile_mismatch(given, sp); !why.empty())
                usage("--layer-profile " + args.layer_profile + ": " + why);
            object->layer_height_profile.set(given);
        } else if (kind == "adaptive")
            object->layer_height_profile.set(layer_height_profile_adaptive(sp, *object, float(a)));
        else if (kind == "ramp")
            object->layer_height_profile.set(std::vector<coordf_t>{ 0., sp.first_object_layer_height, sp.first_object_layer_height, a, top, b });
        else {
            ModelConfig range;
            range.set("layer_height", b);
            object->layer_config_ranges[{ std::max(0., top - a), top }] = range;
        }
    }
    print.apply(model, config);
}

void place(const Args &args, Model &model, DynamicPrintConfig &config, bool saved = false)
{
    if (args.plate.empty() && args.calib.empty() && (!saved || args.center_set)) {
        Vec2d center = args.center;
        if (! args.center_set) {
            const BoundingBoxf bed(config.opt<ConfigOptionPoints>("printable_area")->values);
            center = bed.center();
        }
        if (!saved)
            for (ModelObject *object : model.objects)
                object->ensure_on_bed();
        model.center_instances_around_point(center);
    }
    if (!saved && std::none_of(args.overrides.begin(), args.overrides.end(), [](const auto &kv) { return kv.first.rfind("wipe_tower_", 0) == 0; }))
        place_wipe_tower(config, model, args.filaments.size());
}

void rotate_model(Model &model, const Matrix3d &rotation)
{
    const Eigen::AngleAxisd aa(rotation);
    if (std::abs(aa.angle()) < 1e-9)
        return;
    for (ModelObject *object : model.objects)
        object->rotate(aa.angle(), aa.axis());
}

struct OrientSlice
{
    bool        ok = false;
    double      time_s = 0., footprint = 0., support = 0.;
    size_t      hits = 0;
    std::string error;
};

OrientSlice slice_orientation(const Args &args, const Model &source, const Matrix3d &rotation, const DynamicPrintConfig &config_in,
                              const fs::path &dir)
{
    OrientSlice out;
    try {
        Model              model(source);
        DynamicPrintConfig config = config_in;
        rotate_model(model, rotation);
        place(args, model, config);
        Print print;
        print.is_BBL_printer() = config.opt_string("printer_model").rfind("Bambu Lab", 0) == 0;
        for (ModelObject *object : model.objects)
            print.auto_assign_extruders(object);
        print.apply(model, config);
        Model::setBedPolygon(config, print.config());
        if (const StringObjectException err = print.validate(); ! err.string.empty())
            throw std::runtime_error("validate: " + err.string);
        print.process();
        GCodeProcessorResult result;
        const fs::path       gcode = dir / fs::unique_path("orient_%%%%%%%%.gcode");
        print.export_gcode(gcode.string(), &result, nullptr);
        fs::remove(gcode);
        out.time_s = result.print_statistics.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)].time;
        out.hits   = result.clearance_report.ran ? result.clearance_report.hit_moves : 0;
        for (const auto &[extruder, volume] : result.print_statistics.support_volumes_per_extruder)
            out.support += volume;
        for (const PrintObject *object : print.objects())
            for (const Nonplanar::ConformalPatch &patch : object->conformal().patches)
                if (patch.kind == Nonplanar::PatchKind::Top)
                    out.footprint += area(patch.footprint) * SCALING_FACTOR * SCALING_FACTOR;
        out.ok = true;
    } catch (const std::exception &e) {
        out.error = e.what();
    }
    return out;
}

OrientSlice slice_orientation_child(const Args &args, const Vec3d &up, const fs::path &dir)
{
    const auto quote = [](const std::string &v) {
#ifdef _WIN32
        return "\"" + v + "\"";
#else
        std::string q = "'";
        for (char c : v)
            q += c == '\'' ? std::string("'\\''") : std::string(1, c);
        return q + "'";
#endif
    };
    const fs::path out = dir / fs::unique_path("orient_%%%%%%%%.txt");
    std::string    cmd;
    for (size_t i = 0; i < args.argv.size(); ++i)
        if (args.argv[i] == "--json" || args.argv[i] == "--project-out" || args.argv[i] == "--head-out")
            ++i;
        else
            cmd += quote(args.argv[i]) + " ";
    std::ostringstream u;
    u.precision(17);
    u << up.x() << "," << up.y() << "," << up.z();
    cmd += "--confirm-up " + quote(u.str()) + " > " + quote(out.string());
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";
#endif
    const int   rc = std::system(cmd.c_str());
    OrientSlice sl;
    boost::nowide::ifstream in(out.string());
    for (std::string line; std::getline(in, line);) {
        std::istringstream ls(line);
        std::string        key;
        ls >> key;
        if (key == "orient_slice")
            ls >> sl.ok >> sl.time_s >> sl.footprint >> sl.support >> sl.hits;
        else if (key == "orient_slice_error")
            std::getline(ls, sl.error);
    }
    in.close();
    fs::remove(out);
    if (! sl.ok && sl.error.empty())
        sl.error = "slice exited with status " + std::to_string(rc);
    return sl;
}

void print_orient_score(const char *tag, const Nonplanar::OrientScore &s)
{
    std::cout << tag << " up " << s.up.x() << "," << s.up.y() << "," << s.up.z() << " cost " << s.cost << " time_s " << s.time_s
              << " defect " << s.defect << " curved_share " << s.curved_share << " flat " << s.flat_top << " curved " << s.curved_top << " steep " << s.steep_top
              << " support_area " << s.support_area << " support_volume " << s.support_volume << " bed " << s.bed
              << " height " << s.height << " stable " << s.stable << "\n";
}

Matrix3d choose_orientation(const Args &args, const Model &model, const DynamicPrintConfig &config, const fs::path &dir)
{
    if (model.objects.size() != 1) {
        std::cout << "auto_orient skipped: " << model.objects.size() << " objects\n";
        return Matrix3d::Identity();
    }
    const Nonplanar::OrientParams params = Nonplanar::orient_params(config);
    const auto                    t0     = std::chrono::steady_clock::now();
    const TriangleMesh            mesh   = model.objects.front()->raw_mesh();
    const Nonplanar::OrientResult res    = Nonplanar::auto_orient(mesh.its, params);
    std::cout << "auto_orient_mesh_s " << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << "\n";
    print_orient_score("auto_orient_original", res.original);
    for (const Nonplanar::OrientScore &s : res.ranked)
        print_orient_score("auto_orient_candidate", s);
    Nonplanar::OrientScore chosen = res.original;
    std::vector<Nonplanar::OrientScore> trial{ res.original };
    std::vector<Nonplanar::OrientScore> pool;
    if (! res.ranked.empty())
        pool.push_back(res.ranked.front());
    std::vector<Nonplanar::OrientScore> axes;
    for (const Vec3d &a : std::initializer_list<Vec3d>{ Vec3d::UnitX(), -Vec3d::UnitX(), Vec3d::UnitY(), -Vec3d::UnitY(), -Vec3d::UnitZ() })
        axes.push_back(Nonplanar::score_orientation(mesh.its, a, params));
    std::sort(axes.begin(), axes.end(), [](const auto &l, const auto &r) { return l.cost < r.cost; });
    pool.insert(pool.end(), axes.begin(), axes.end());
    pool.insert(pool.end(), res.ranked.begin(), res.ranked.end());
    for (const Nonplanar::OrientScore &s : pool)
        if (trial.size() < 6 && s.stable && (! res.original.stable || s.height <= 1.25 * res.original.height) &&
            std::none_of(trial.begin(), trial.end(), [&](const auto &t) { return t.up.dot(s.up) > std::cos(2. * M_PI / 180.); }))
            trial.push_back(s);
    if (trial.size() > 1) {
        for (Nonplanar::OrientScore &s : trial) {
            const OrientSlice sl = slice_orientation_child(args, s.up, dir);
            std::cout << "auto_orient_slice up " << s.up.x() << "," << s.up.y() << "," << s.up.z() << " ok " << sl.ok << " time_s "
                      << sl.time_s << " footprint " << sl.footprint << " support " << sl.support << " hits " << sl.hits
                      << (sl.error.empty() ? "" : " error " + sl.error) << "\n";
            if (! sl.ok || sl.hits > 0) {
                s.stable = false;
                s.cost   = std::numeric_limits<double>::max();
                continue;
            }
            s.measured        = true;
            s.time_s          = sl.time_s;
            s.printed_support = sl.support;
            s.curved_share    = s.curved_top > 0. ? std::min(1., sl.footprint / s.curved_top) : 1.;
            Nonplanar::rescore(s);
        }
        const Nonplanar::OrientScore &original = trial.front();
        for (size_t i = 1; i < trial.size(); ++i)
            if (trial[i].cost < std::numeric_limits<double>::max() && Nonplanar::replaces(trial[i], original) &&
                (chosen.up == res.original.up || trial[i].cost < chosen.cost))
                chosen = trial[i];
    }
    const Matrix3d r = Nonplanar::rotation_to_z(chosen.up);
    print_orient_score("auto_orient_chosen", chosen);
    std::cout << "auto_orient_rotated " << (chosen.up == res.original.up ? 0 : 1) << "\n"
              << "auto_orient_rotation";
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            std::cout << " " << r(i, j);
    std::cout << "\n";
    return r;
}

void add_placed_instances(ModelObject *object, const nlohmann::json &entry, const Vec3d &origin = Vec3d::Zero())
{
    const nlohmann::json places = entry.contains("instances") ? entry.at("instances") : nlohmann::json::array({ entry });
    for (const nlohmann::json &place : places) {
        ModelInstance *instance = object->add_instance();
        instance->set_rotation(Vec3d(0., 0., Geometry::deg2rad(place.value("rot", 0.))));
        instance->printable = place.value("printable", true);
    }
    object->ensure_on_bed();
    const size_t first = object->instances.size() - places.size();
    for (size_t i = 0; i < places.size(); ++i) {
        const BoundingBoxf3 box = object->instance_bounding_box(first + i, true);
        object->instances[first + i]->set_offset(object->instances[first + i]->get_offset() +
                                                 Vec3d(origin.x() + places[i].at("x").get<double>() - box.center().x(),
                                                       origin.y() + places[i].at("y").get<double>() - box.center().y(),
                                                       places[i].contains("z") ? places[i].at("z").get<double>() - box.min.z() : 0.));
    }
}

void apply_plate_paint(ModelVolume &volume, const nlohmann::json &paint, const std::string &where)
{
    const int triangles = int(volume.mesh().its.indices.size());
    for (const auto &[kind, map] : paint.items()) {
        FacetsAnnotation &facets = kind == "support" ? volume.supported_facets :
                                   kind == "seam"    ? volume.seam_facets :
                                   kind == "fuzzy"   ? volume.fuzzy_skin_facets :
                                   kind == "color"   ? volume.mmu_segmentation_facets :
                                                       throw std::runtime_error(where + ": unknown paint " + kind);
        if (!map.is_object())
            throw std::runtime_error(where + ": paint " + kind + " is a map of triangle index to paint string");
        std::map<int, std::string> painted;
        for (const auto &[key, value] : map.items()) {
            size_t end = 0;
            const int triangle = key.empty() || !std::isdigit((unsigned char) key.front()) ? -1 : std::stoi(key, &end);
            if (end != key.size() || triangle < 0 || triangle >= triangles)
                throw std::runtime_error(where + ": paint " + kind + " triangle " + key + " is not one of the volume's " +
                                         std::to_string(triangles) + " triangles");
            const std::string code = value.is_string() ? value.get<std::string>() : std::string();
            if (code.empty() || code.find_first_not_of("0123456789ABCDEF") != std::string::npos)
                throw std::runtime_error(where + ": paint " + kind + " triangle " + key + " is not a paint string (uppercase hex)");
            painted.emplace(triangle, code);
        }
        facets.reset();
        facets.reserve(int(painted.size()));
        for (const auto &[triangle, code] : painted) {
            facets.set_triangle_from_string(triangle, code);
            if (facets.get_triangle_as_string(triangle) != code)
                throw std::runtime_error(where + ": paint " + kind + " triangle " + std::to_string(triangle) + " has a malformed paint string");
        }
        facets.shrink_to_fit();
    }
}

struct PlateLayout
{
    int                                                     count = 0;
    std::map<int, std::pair<std::string, DynamicPrintConfig>> plates;
    std::map<ObjectID, int>                                 instance_plate;
};

struct PlateFiles
{
    fs::path                                      dir;
    bool                                          cache = true;
    std::map<std::string, std::unique_ptr<Model>> loaded;

    const Model &operator()(const std::string &name)
    {
        fs::path file = name;
        if (file.is_relative())
            file = dir / file;
        auto it = cache ? loaded.find(file.string()) : loaded.end();
        if (it == loaded.end())
            it = loaded.insert_or_assign(file.string(), cache ? std::make_unique<Model>(read_input(file.string())) :
                                                                std::unique_ptr<Model>(new Model(read_input(file.string())))).first;
        if (it->second->objects.empty())
            throw std::runtime_error("no objects in " + file.string());
        return *it->second;
    }
};

void apply_plate_settings(ModelConfig &config, const nlohmann::json &settings)
{
    ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Disable);
    for (const auto &[key, value] : settings.items())
        config.set_deserialize(key, value.is_string() ? value.get<std::string>() : value.dump(), ctx);
}

ModelVolume *add_plate_volume(ModelObject &object, const nlohmann::json &v, PlateFiles &files)
{
    static const std::map<std::string, ModelVolumeType> types = {
        { "part", ModelVolumeType::MODEL_PART },           { "modifier", ModelVolumeType::PARAMETER_MODIFIER },
        { "negative", ModelVolumeType::NEGATIVE_VOLUME },  { "support_enforcer", ModelVolumeType::SUPPORT_ENFORCER },
        { "support_blocker", ModelVolumeType::SUPPORT_BLOCKER } };
    const auto type = types.find(v.value("type", "part"));
    if (type == types.end())
        throw std::runtime_error("unknown volume type " + v.value("type", ""));
    Transform3d transform = Transform3d::Identity();
    if (const nlohmann::json m = v.value("transform", nlohmann::json()); !m.is_null()) {
        const bool flat = m.is_array() && m.size() == 16, rows = m.is_array() && m.size() == 4;
        if (!flat && !rows)
            throw std::runtime_error("a volume transform is a row-major 4x4 matrix: 16 numbers or 4 rows of 4");
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                transform.matrix()(r, c) = (flat ? m.at(r * 4 + c) : m.at(r).at(c)).get<double>();
    }
    ModelVolume *volume = object.add_volume(files(v.at("file").get<std::string>()).objects.front()->raw_mesh(), type->second, false);
    volume->set_transformation(Geometry::Transformation(transform));
    volume->center_geometry_after_creation();
    volume->name = v.value("name", fs::path(v.at("file").get<std::string>()).stem().string());
    if (const int extruder = v.value("extruder", 0); extruder > 0)
        volume->config.set("extruder", extruder);
    if (v.contains("settings"))
        apply_plate_settings(volume->config, v.at("settings"));
    if (v.contains("paint"))
        apply_plate_paint(*volume, v.at("paint"), "plate object " + object.name + " volume " + volume->name);
    return volume;
}

ModelObject *add_plate_object(Model &model, const nlohmann::json &entry, PlateFiles &files)
{
    if (!entry.contains("file") && !entry.contains("volumes"))
        throw std::runtime_error("plate object " + std::to_string(model.objects.size() + 1) + " needs a file or volumes");
    ModelObject *object = entry.contains("file") ? model.add_object(*files(entry.at("file").get<std::string>()).objects.front()) : model.add_object();
    object->name = entry.value("name", fs::path(entry.contains("file") ? entry.at("file").get<std::string>() : "object").stem().string());
    object->clear_instances();
    if (const int extruder = entry.value("extruder", 0); extruder > 0)
        object->config.set("extruder", extruder);
    if (entry.contains("settings"))
        apply_plate_settings(object->config, entry.at("settings"));
    if (const nlohmann::json paint = entry.value("paint", nlohmann::json()); !paint.is_null()) {
        const std::string where = "plate object " + object->name;
        const size_t parts = object->volumes.size();
        if (!entry.contains("file") || (paint.is_object() && parts != 1) || (paint.is_array() && paint.size() != parts) ||
            !(paint.is_object() || paint.is_array()))
            throw std::runtime_error(where + ": paint needs a file and is a map for a file of one part, else a list of one map "
                                     "per part (this file has " + std::to_string(parts) + ")");
        for (size_t i = 0; i < parts; ++i)
            if (const nlohmann::json &p = paint.is_array() ? paint.at(i) : paint; !p.is_null())
                apply_plate_paint(*object->volumes[i], p, where + " part " + std::to_string(i + 1));
    }
    for (const nlohmann::json &v : entry.value("volumes", nlohmann::json::array()))
        add_plate_volume(*object, v, files);
    if (entry.contains("volumes")) {
        if (std::none_of(object->volumes.begin(), object->volumes.end(), [](const ModelVolume *v) { return v->is_model_part(); }))
            throw std::runtime_error("plate object " + object->name + " has no part volume");
        object->sort_volumes(true);
        object->invalidate_bounding_box();
    }
    if (const nlohmann::json lhp = entry.value("layer_height_profile", nlohmann::json()); !lhp.is_null()) {
        const std::string where = "plate object " + object->name + ": layer_height_profile";
        std::vector<coordf_t> profile;
        for (const nlohmann::json &v : lhp.is_array() ? lhp : throw std::runtime_error(where + " is a list of z, h pairs")) {
            if (v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number())
                profile.insert(profile.end(), { v[0].get<double>(), v[1].get<double>() });
            else if (v.is_number())
                profile.push_back(v.get<double>());
            else
                throw std::runtime_error(where + " is a list of z, h pairs");
        }
        if (profile.size() < 4 || profile.size() % 2 != 0)
            throw std::runtime_error(where + " needs at least two z, h pairs");
        for (size_t i = 0; i < profile.size(); i += 2) {
            if (!std::isfinite(profile[i]) || !std::isfinite(profile[i + 1]) || profile[i + 1] <= 0.)
                throw std::runtime_error(where + " has a layer height <= 0 at z " + std::to_string(profile[i]));
            if (i > 0 && profile[i] <= profile[i - 2])
                throw std::runtime_error(where + " does not increase z at " + std::to_string(profile[i]));
        }
        object->layer_height_profile.set(profile);
        plate_layer_profiles.push_back(object->id());
    }
    return object;
}

Model load_plate(const std::string &path, PlateLayout &layout)
{
    boost::nowide::ifstream in(path);
    const nlohmann::json    j = nlohmann::json::parse(in);
    Model                   model;
    PlateFiles              files{ fs::path(path).parent_path(), true, {} };
    ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Disable);
    for (const nlohmann::json &entry : j.at("objects")) {
        ModelObject *object = add_plate_object(model, entry, files);
        const size_t first = object->instances.size();
        add_placed_instances(object, entry);
        const nlohmann::json places = entry.contains("instances") ? entry.at("instances") : nlohmann::json::array({ entry });
        for (size_t i = 0; i < places.size(); ++i) {
            const nlohmann::json plate = places[i].contains("plate") ? places[i].at("plate") : entry.value("plate", nlohmann::json());
            if (plate.is_null())
                continue;
            if (!plate.is_number_integer() || plate.get<int>() < 1)
                throw std::runtime_error("plate object " + object->name + ": plate is a 1-based plate number");
            layout.instance_plate[object->instances[first + i]->id()] = plate.get<int>();
            layout.count = std::max(layout.count, plate.get<int>());
        }
    }
    if (model.objects.empty())
        throw std::runtime_error("no objects in " + path);
    for (const nlohmann::json &plate : j.value("plates", nlohmann::json::array())) {
        const nlohmann::json index = plate.value("index", nlohmann::json());
        if (!index.is_number_integer() || index.get<int>() < 1)
            throw std::runtime_error("plates: each plate needs a 1-based index");
        auto &[name, config] = layout.plates[index.get<int>()];
        name = plate.value("name", std::string());
        const nlohmann::json settings = plate.value("settings", nlohmann::json::object());
        for (const auto &[key, value] : settings.items())
            config.set_deserialize(key, value.is_string() ? value.get<std::string>() : value.dump(), ctx);
        layout.count = std::max(layout.count, index.get<int>());
    }
    if (layout.count > 0)
        for (const ModelObject *object : model.objects)
            for (const ModelInstance *instance : object->instances)
                layout.instance_plate.emplace(instance->id(), 1);
    return model;
}

Model load_project(const std::string &path)
{
    boost::nowide::ifstream in(path);
    const nlohmann::json    j   = nlohmann::json::parse(in);
    const fs::path          dir = fs::path(path).parent_path();
    auto resolve_path = [&dir](const std::string &file) { return fs::path(file).is_relative() ? (dir / file).string() : file; };
    auto read_model   = [](const std::string &file) {
        Model m = read_input(file);
        if (m.objects.empty() || m.objects.front()->volumes.empty())
            throw std::runtime_error("no objects in " + file);
        return m;
    };
    ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Disable);
    auto apply_settings = [&ctx](ModelConfig &config, const nlohmann::json &settings) {
        for (const auto &[key, value] : settings.items())
            config.set_deserialize(key, value.is_string() ? value.get<std::string>() : value.dump(), ctx);
    };

    Model        model  = read_model(resolve_path(j.at("object").get<std::string>()));
    ModelObject *object = model.objects.front();
    object->name        = j.value("name", fs::path(j.at("object").get<std::string>()).stem().string());
    ModelVolume *main   = object->volumes.front();
    if (j.contains("settings"))
        apply_settings(object->config, j.at("settings"));
    for (const nlohmann::json &v : j.value("volumes", nlohmann::json::array())) {
        static const std::map<std::string, ModelVolumeType> types = {
            { "part", ModelVolumeType::MODEL_PART },           { "modifier", ModelVolumeType::PARAMETER_MODIFIER },
            { "negative", ModelVolumeType::NEGATIVE_VOLUME },  { "support_enforcer", ModelVolumeType::SUPPORT_ENFORCER },
            { "support_blocker", ModelVolumeType::SUPPORT_BLOCKER } };
        const auto type = types.find(v.value("type", "modifier"));
        if (type == types.end())
            throw std::runtime_error("unknown volume type " + v.value("type", ""));
        Model        src    = read_model(resolve_path(v.at("file").get<std::string>()));
        TriangleMesh mesh   = src.objects.front()->volumes.front()->mesh();
        mesh.transform(src.objects.front()->volumes.front()->get_matrix());
        if (v.contains("offset"))
            mesh.translate(v["offset"][0].get<float>(), v["offset"][1].get<float>(), v["offset"][2].get<float>());
        ModelVolume *volume = object->add_volume(std::move(mesh), type->second, false);
        volume->set_transformation(Geometry::Transformation(main->get_matrix() * Geometry::translation_transform(-main->mesh().get_init_shift())));
        volume->center_geometry_after_creation();
        volume->name = fs::path(v.at("file").get<std::string>()).stem().string();
        if (v.contains("settings"))
            apply_settings(volume->config, v.at("settings"));
    }
    for (const nlohmann::json &r : j.value("ranges", nlohmann::json::array())) {
        ModelConfig &config = object->layer_config_ranges[{ r.at("z")[0].get<double>(), r.at("z")[1].get<double>() }];
        config.set("layer_height", r.value("layer_height", 0.2));
        apply_settings(config, r.value("settings", nlohmann::json::object()));
    }
    const nlohmann::json paint = j.value("paint", nlohmann::json::object());
    for (const auto &[kind, file] : paint.items()) {
        boost::nowide::ifstream pin(resolve_path(file.get<std::string>()));
        std::vector<int>        ids;
        for (int id; pin >> id;)
            ids.push_back(id);
        if (ids.size() != main->mesh().its.indices.size())
            throw std::runtime_error("paint " + kind + " has " + std::to_string(ids.size()) + " entries, the mesh " +
                                     std::to_string(main->mesh().its.indices.size()) + " triangles");
        TriangleSelector selector(main->mesh());
        for (size_t i = 0; i < ids.size(); ++i)
            if (ids[i] > 0)
                selector.set_facet(int(i), EnforcerBlockerType(ids[i]));
        if (kind == "seam") main->seam_facets.set(selector);
        else if (kind == "support") main->supported_facets.set(selector);
        else if (kind == "fuzzy") main->fuzzy_skin_facets.set(selector);
        else if (kind == "color") main->mmu_segmentation_facets.set(selector);
        else throw std::runtime_error("unknown paint " + kind);
    }
    return model;
}

DynamicPrintConfig project_config(PresetBundle &bundle, const Args &a, const DynamicPrintConfig &config)
{
    DynamicPrintConfig out = config;
    const size_t n = config.option<ConfigOptionStrings>("filament_settings_id")->size();
    std::vector<std::string> lists;
    if (const auto *saved = config.option<ConfigOptionStrings>("different_settings_to_system"))
        lists = saved->values;
    lists.resize(n + 2);
    auto diff_slot = [&](size_t list, const char *kind, Preset::Type type, const std::string &name, const std::string &saved_name,
                   const std::vector<std::string> &keys, size_t slot) {
        const DynamicPrintConfig system = resolve(bundle, a, kind, type, name);
        const std::string system_name = type == Preset::TYPE_PRINTER ? system.opt_string("printer_settings_id") :
                                        type == Preset::TYPE_PRINT   ? system.opt_string("print_settings_id") :
                                                                       system.option<ConfigOptionStrings>("filament_settings_id")->values.front();
        if (system_name != saved_name)
            return;
        std::vector<std::string> changed;
        for (const std::string &key : keys) {
            const ConfigOption *base = system.option(key), *value = config.option(key);
            if (base == nullptr || value == nullptr || key == "inherits" || boost::algorithm::ends_with(key, "_settings_id") ||
                boost::algorithm::starts_with(key, "compatible_"))
                continue;
            const auto *base_vector = dynamic_cast<const ConfigOptionVectorBase *>(base);
            const auto *value_vector = dynamic_cast<const ConfigOptionVectorBase *>(value);
            bool differs = base->serialize() != value->serialize();
            if (type == Preset::TYPE_FILAMENT && n > 1 && base_vector && value_vector && value_vector->size() == n * base_vector->size()) {
                const std::vector<std::string> b = base_vector->vserialize(), v = value_vector->vserialize();
                differs = !std::equal(b.begin(), b.end(), v.begin() + slot * b.size());
            }
            if (differs)
                changed.push_back(key);
        }
        lists[list] = escape_strings_cstyle(changed);
    };
    diff_slot(0, "process", Preset::TYPE_PRINT, a.process, config.opt_string("print_settings_id"), Preset::print_options(), 0);
    for (size_t i = 0; i < std::min(n, a.filaments.size()); ++i)
        diff_slot(i + 1, "filament", Preset::TYPE_FILAMENT, a.filaments[i],
            config.option<ConfigOptionStrings>("filament_settings_id")->values[i], Preset::filament_options(), i);
    diff_slot(n + 1, "machine", Preset::TYPE_PRINTER, a.printer, config.opt_string("printer_settings_id"), Preset::printer_options(), 0);
    out.set_key_value("different_settings_to_system", new ConfigOptionStrings(lists));
    return out;
}

struct SavedInstance
{
    int         plate = 0;
    std::string plate_name;
    int         object_id = 0, instance_id = 0, identify_id = 0;
};

std::vector<SavedInstance> read_saved_instances(const std::string &path)
{
    std::vector<SavedInstance> out;
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (! open_zip_reader(&zip, path))
        return out;
    size_t size = 0;
    void  *data = mz_zip_reader_extract_file_to_heap(&zip, "Metadata/model_settings.config", &size, 0);
    close_zip_reader(&zip);
    if (data == nullptr)
        return out;
    std::istringstream in(std::string(static_cast<const char *>(data), size));
    mz_free(data);
    namespace pt = boost::property_tree;
    pt::ptree tree;
    pt::read_xml(in, tree);
    const auto root = tree.get_child_optional("config");
    if (!root) return out;
    for (const auto &[tag, plate] : *root) {
        if (tag != "plate")
            continue;
        SavedInstance on_plate;
        for (const auto &[key, meta] : plate)
            if (key == "metadata") {
                const std::string name = meta.get("<xmlattr>.key", std::string());
                if (name == "plater_id") on_plate.plate = meta.get("<xmlattr>.value", 0);
                else if (name == "plater_name") on_plate.plate_name = meta.get("<xmlattr>.value", std::string());
            }
        for (const auto &[item, instance] : plate) {
            if (item != "model_instance")
                continue;
            SavedInstance saved = on_plate;
            for (const auto &[key, meta] : instance)
                if (key == "metadata") {
                    const std::string name  = meta.get("<xmlattr>.key", std::string());
                    const int         value = meta.get("<xmlattr>.value", -1);
                    if (name == "object_id") saved.object_id = value;
                    else if (name == "instance_id") saved.instance_id = value;
                    else if (name == "identify_id") saved.identify_id = value;
                }
            out.push_back(saved);
        }
    }
    return out;
}

void restore_loaded_ids(const std::vector<SavedInstance> &instances, Model &model)
{
    std::map<int, std::vector<std::pair<int, int>>> saved;
    for (const SavedInstance &instance : instances)
        if (instance.instance_id >= 0 && instance.identify_id > 0)
            saved[instance.object_id].emplace_back(instance.instance_id, instance.identify_id);
    for (const auto &[id, instances] : saved)
        for (ModelObject *object : model.objects) {
            auto kept = [object](const std::pair<int, int> &i) {
                return size_t(i.first) < object->instances.size() && object->instances[i.first]->loaded_id == size_t(i.second);
            };
            if (std::none_of(instances.begin(), instances.end(), kept))
                continue;
            for (const auto &[index, identify_id] : instances)
                if (size_t(index) < object->instances.size())
                    object->instances[index]->loaded_id = identify_id;
            break;
        }
}

#include "nonplanar_slice_tool_save.ipp"

void save_project(PresetBundle &bundle, const Args &args, Model &model, const DynamicPrintConfig &config, bool loaded_ids,
                  const PlateDataPtrs &plates, const std::string &cut_information = {})
{
    model.get_backup_path();
    StoreParams params;
    params.path            = args.save_3mf;
    params.model           = &model;
    params.plate_data_list = plates;
    DynamicPrintConfig saved = project_config(bundle, args, config);
    params.config          = &saved;
    params.strategy        = SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::ShareMesh |
                             (loaded_ids ? SaveStrategy::UseLoadedId : SaveStrategy::Default);
    if (! store_bbs_3mf(params))
        throw std::runtime_error("cannot write " + args.save_3mf);
    Args written = args;
    written.model = args.save_3mf;
    save_source_project(written, config, true, cut_information);
}

void arrange_only(const Args &args, Model &model, const DynamicPrintConfig &config)
{
    arrangement::ArrangePolygons items;
    std::vector<ModelInstance *> instances;
    for (ModelObject *object : model.objects)
        for (ModelInstance *instance : object->instances) {
            arrangement::ArrangePolygon item = get_instance_arrange_poly(instance, config);
            item.itemid = int(items.size());
            items.push_back(std::move(item));
            instances.push_back(instance);
        }
    arrangement::ArrangeParams params;
    params.allow_rotations         = args.arrange_rotation;
    params.clearance_height_to_rod = float(config.opt_float("extruder_clearance_height_to_rod"));
    params.clearance_height_to_lid = float(config.opt_float("extruder_clearance_height_to_lid"));
    params.clearance_radius        = float(config.opt_float("extruder_clearance_radius"));
    params.nozzle_height           = float(config.opt_float("nozzle_height"));
    params.printable_height        = float(config.opt_float("printable_height"));
    params.align_center            = config.option<ConfigOptionPoint>("best_object_pos")->value;
    params.is_seq_print            = config.opt_enum<PrintSequence>("print_sequence") == PrintSequence::ByObject;
    params.align_to_y_axis         = config.opt_enum<PrinterStructure>("printer_structure") == PrinterStructure::psI3;
    params.min_obj_distance        = args.arrange_spacing >= 0. ? scaled(args.arrange_spacing) : 0;
    if (params.is_seq_print) {
        params.bed_shrink_x = BED_SHRINK_SEQ_PRINT;
        params.bed_shrink_y = BED_SHRINK_SEQ_PRINT;
    }
    params.progressind = [](unsigned, std::string) {};
    arrangement::update_arrange_params(params, &config, items);
    arrangement::update_selected_items_inflation(items, &config, params);
    arrangement::update_selected_items_axis_align(items, &config, params);
    const Points bed = arrangement::get_shrink_bedpts(&config, params);
    const auto &exclude = config.option<ConfigOptionPoints>("bed_exclude_area")->values;
    for (size_t i = 0; i + 3 < exclude.size(); i += 4) {
        BoundingBoxf box;
        for (size_t k = i; k < i + 4; ++k)
            box.merge(exclude[k]);
        arrangement::ArrangePolygon region;
        region.poly.contour = Polygon({ { scaled(box.min.x()), scaled(box.min.y()) }, { scaled(box.max.x()), scaled(box.min.y()) },
                                        { scaled(box.max.x()), scaled(box.max.y()) }, { scaled(box.min.x()), scaled(box.max.y()) } });
        region.is_virt_object = true;
        region.bed_idx        = 0;
        region.height         = 1;
        region.inflation      = scaled(1.);
        params.excluded_regions.push_back(std::move(region));
    }
    arrangement::arrange(items, {}, bed, params);

    nlohmann::json out = { { "schema", "orca.arrange.v1" }, { "objects", nlohmann::json::array() } };
    int plates = 0, unplaced = 0;
    for (size_t o = 0; o < model.objects.size(); ++o)
        out["objects"].push_back({ { "index", o }, { "name", model.objects[o]->name }, { "instances", nlohmann::json::array() } });
    for (const arrangement::ArrangePolygon &item : items) {
        ModelInstance *instance = instances[size_t(item.itemid)];
        ModelObject   *object   = instance->get_object();
        const size_t   o = std::find(model.objects.begin(), model.objects.end(), object) - model.objects.begin();
        const size_t   i = std::find(object->instances.begin(), object->instances.end(), instance) - object->instances.begin();
        nlohmann::json place = { { "index", i } };
        if (item.bed_idx < 0) {
            place["plate"] = nullptr;
            ++unplaced;
        } else {
            instance->apply_arrange_result(item.translation.cast<double>(), item.rotation);
            const BoundingBoxf3 box = object->instance_bounding_box(i);
            place["plate"] = item.bed_idx + 1;
            place["x"]     = box.center().x();
            place["y"]     = box.center().y();
            place["rot"]   = Geometry::rad2deg(item.rotation);
            plates         = std::max(plates, item.bed_idx + 1);
        }
        out["objects"][o]["instances"].push_back(std::move(place));
    }
    for (nlohmann::json &object : out["objects"])
        std::sort(object["instances"].begin(), object["instances"].end(),
                  [](const nlohmann::json &a, const nlohmann::json &b) { return a.at("index") < b.at("index"); });
    out["plates"]   = plates;
    out["unplaced"] = unplaced;
    boost::nowide::ofstream file(args.arrange_only);
    file << out.dump(1) << "\n";
    if (!file)
        throw std::runtime_error("cannot write " + args.arrange_only);
    std::cout << "arranged " << items.size() - unplaced << " plates " << plates << " unplaced " << unplaced << "\n";
}

void cut_only(PresetBundle &bundle, const Args &args, Model &model, DynamicPrintConfig &config)
{
    if (model.objects.size() != 1 || model.objects.front()->instances.size() != 1)
        throw std::runtime_error("--cut-only needs a model of one object with one instance, this has " +
                                 std::to_string(model.objects.size()) + " objects");
    double v[6];
    const int n = std::sscanf(args.cut.c_str(), "%lf,%lf,%lf,%lf,%lf,%lf", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
    Vec3d point = Vec3d::Zero(), normal = Vec3d::UnitZ();
    if (n == 1)
        point.z() = v[0];
    else if (n == 6) {
        point  = Vec3d(v[0], v[1], v[2]);
        normal = Vec3d(v[3], v[4], v[5]);
    }
    if ((n != 1 && n != 6) || !(normal.norm() > 1e-9))
        usage("--cut-only expects Z or X,Y,Z,NX,NY,NZ with a nonzero normal");
    ModelObject *object = model.objects.front();
    const Transform3d rotation(Eigen::Quaterniond::FromTwoVectors(Vec3d::UnitZ(), normal.normalized()));
    const Transform3d cut_matrix = Geometry::translation_transform(point - object->instances.front()->get_offset()) * rotation;
    const bool upper = args.cut_keep != "lower", lower = args.cut_keep != "upper";
    const ModelObjectCutAttributes attributes = only_if(upper, ModelObjectCutAttribute::KeepUpper) |
                                                only_if(lower, ModelObjectCutAttribute::KeepLower) |
                                                ModelObjectCutAttribute::PlaceOnCutUpper |
                                                only_if(args.cut_place_lower, ModelObjectCutAttribute::PlaceOnCutLower) |
                                                ModelObjectCutAttribute::InvalidateCutInfo;
    Cut cut(object, 0, cut_matrix, attributes);
    const ModelObjectPtrs &pieces = cut.perform_with_plane();
    if (pieces.size() != size_t(upper) + size_t(lower))
        throw std::runtime_error("the cut plane does not cross the object");
    const std::vector<std::string> names = upper && lower ? std::vector<std::string>{ "upper", "lower" } :
                                                            std::vector<std::string>{ upper ? "upper" : "lower" };
    Model result;
    for (const ModelObject *piece : pieces) {
        ModelObject *added = result.add_object(*piece);
        added->ensure_on_bed();
    }
    for (size_t i = 0; i < result.objects.size(); ++i) {
        TriangleMesh mesh = result.objects[i]->mesh();
        std::cout << "cut_piece " << names[i] << " triangles " << mesh.facets_count() << " open_edges "
                  << its_num_open_edges(mesh.its) << " volume_mm3 " << mesh.volume() << "\n";
    }
    if (boost::algorithm::iends_with(args.output, ".3mf")) {
        double x = 0.;
        for (ModelObject *piece : result.objects) {
            const BoundingBoxf3 box = piece->instance_bounding_box(0);
            piece->instances.front()->set_offset(piece->instances.front()->get_offset() + Vec3d(x - box.min.x(), 0., 0.));
            piece->invalidate_bounding_box();
            x += box.size().x() + 10.;
        }
        place(args, result, config);
        PlateDataPtrs plates;
        auto plate = std::make_unique<PlateData>();
        plate->plate_index = 0;
        for (size_t i = 0; i < result.objects.size(); ++i)
            plate->objects_and_instances.emplace_back(int(i), 0);
        plates.push_back(plate.get());
        Args written = args;
        written.save_3mf = args.output;
        save_project(bundle, written, result, config, false, plates);
        std::cout << "cut_project " << args.output << "\n";
        return;
    }
    std::string stem = args.output;
    if (boost::algorithm::iends_with(stem, ".stl"))
        stem.resize(stem.size() - 4);
    for (size_t i = 0; i < result.objects.size(); ++i) {
        TriangleMesh mesh = result.objects[i]->mesh();
        const std::string file = stem + "." + names[i] + ".stl";
        if (!store_stl(file.c_str(), &mesh, true))
            throw std::runtime_error("cannot write " + file);
        std::cout << "cut_file " << names[i] << " " << file << "\n";
    }
}

}

#include "nonplanar_slice_json.ipp"
#include "nonplanar_slice_moves.ipp"
#include "nonplanar_slice_catalog.ipp"
#include "nonplanar_model_context.ipp"
#include "nonplanar_patch_preview.ipp"
#include "nonplanar_project_edit.ipp"

void select_layout_plate(const Args &args, Model &model, DynamicPrintConfig &config, const PlateLayout &layout,
                         std::vector<SourcePlate> &source_plates, ContextInstances &membership,
                         std::optional<Model> &full_model, std::optional<DynamicPrintConfig> &full_config)
{
    PlateDataPtrs plates;
    ScopeGuard release([&plates] { release_PlateData_list(plates); });
    for (int index = 1; index <= layout.count; ++index) {
        SourcePlate &source = source_plates.emplace_back();
        source.plate_index  = index - 1;
        if (const auto it = layout.plates.find(index); it != layout.plates.end())
            std::tie(source.name, source.config) = it->second;
        auto *plate        = new PlateData;
        plate->plate_index = source.plate_index;
        plate->plate_name  = source.name;
        plate->config      = source.config;
        plates.push_back(plate);
    }
    for (ModelObject *object : model.objects) {
        for (ModelInstance *instance : object->instances) {
            const int index = layout.instance_plate.at(instance->id());
            instance->set_offset(instance->get_offset() + context_plate_origin(config, plates, index));
            membership[instance->id()] = {{"saved_object_id", nullptr}, {"saved_instance_id", nullptr}, {"source_plate_index", index},
                                          {"source_plate_name", source_plates[size_t(index - 1)].name}, {"saved_transform", nullptr}};
        }
        object->invalidate_bounding_box();
    }
    const int selected = args.plate_index ? args.plate_index : layout.count == 1 ? 1 : 0;
    if (selected == 0 && !(args.output.empty() && (!args.save_3mf.empty() || !args.arrange_only.empty())))
        throw std::runtime_error("--plate-index is required for a plate JSON of several plates");
    if (selected == 0)
        return;
    if (!args.save_3mf.empty()) {
        full_model  = model;
        full_config = config;
    }
    select_context_plate(model, config, plates, membership, selected);
}
#include "nonplanar_calib_classic.ipp"

int main(int argc, char **argv)
{
    if (argc == 3 && std::string(argv[1]) == "--load-gcode") {
        const auto     t0 = std::chrono::steady_clock::now();
        const double   c0 = process_cpu_seconds();
        GCodeProcessor processor;
        processor.process_file(argv[2]);
        const auto &moves = processor.get_result().moves;
        std::cout << "preview_load_s " << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << "\n"
                  << "preview_load_cpu_s " << process_cpu_seconds() - c0 << "\n"
                  << "preview_moves " << moves.size() << "\n"
                  << "preview_moves_mb " << double(moves.size() * sizeof(moves.front())) / 1e6 << "\n";
        return 0;
    }
    const Args args = parse_args(argc, argv);
    nlohmann::json record;
    if (const char *level = std::getenv("NP_LOG_LEVEL"))
        set_logging_level(unsigned(std::atoi(level)));
    try {
        if (!args.dump_options.empty()) dump_options(args.dump_options);
        if (!args.dump_options.empty() && args.dump_presets.empty()) return 0;
        if (! args.json.empty()) slice_json_record(record, args);
        auto progress = [&args](const char *stage, int percent) {
            if (args.progress) std::cout << "progress " << stage << " " << percent << std::endl;
        };
        progress("load", 0);
        set_resources_dir((fs::path(args.profiles) / "..").lexically_normal().string());
        const fs::path datadir = args.datadir.empty() ? fs::temp_directory_path() / fs::unique_path("np_slice_%%%%%%%%") :
                                                        fs::path(args.datadir);
        fs::create_directories(datadir);
        set_data_dir(datadir.string());
        set_temporary_dir(datadir.string());
        Nonplanar::HeadLibrary::instance().reload();
        if (!args.verify_gcode.empty()) {
            DynamicPrintConfig file_config;
            file_config.load_from_gcode_file(args.verify_gcode, ForwardCompatibilitySubstitutionRule::Enable);
            FullPrintConfig full;
            full.apply(file_config, true);
            GCodeProcessor processor;
            processor.apply_config(full);
            processor.process_file(args.verify_gcode);
            ClearanceParams params;
            params.head        = Nonplanar::resolve_head_envelope(full);
            params.contact_bar = true;
            const ClearanceReport report = verify_clearance(processor.get_result(), params, *make_tool_kinematics(full));
            std::cout << "clearance_hit_moves " << report.hit_moves << "\nclearance_land_strict_moves " << report.land_strict_moves
                      << "\nclearance_min_mm " << report.min_clearance << "\n";
            for (const ClearanceHit &hit : report.worst)
                std::cout << "clearance_hit line " << hit.gcode_line << " move " << hit.move_id << " " << hit.component << " "
                          << hit.penetration << " mm\n";
            std::cout << "clearance_hit_classes land " << report.hit_land_moves << " cone " << report.hit_cone_moves << " body "
                      << report.hit_body_moves << " travel " << report.hit_travel_moves << " extrusion " << report.hit_extrusion_moves << "\n";
            if (const char *lines = std::getenv("NP_VERIFY_LINES")) {
                std::map<unsigned, float> least;
                std::stringstream         list(lines);
                for (std::string item; std::getline(list, item, ',');)
                    if (!item.empty())
                        least[unsigned(std::stoul(item))] = std::numeric_limits<float>::infinity();
                const GCodeProcessorResult &result = processor.get_result();
                for (size_t i = 0; i < result.moves.size() && i < report.per_move.size(); ++i)
                    if (auto it = least.find(result.moves[i].gcode_id); it != least.end())
                        it->second = std::min(it->second, report.per_move[i]);
                for (const auto &[line, clearance] : least)
                    std::cout << "clearance_line " << line << " " << clearance << " mm\n";
            }
            return 0;
        }

        if (!args.dump_presets.empty()) { dump_presets(args); return 0; }
        PresetBundle       bundle;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.apply(resolve(bundle, args, "machine", Preset::TYPE_PRINTER, args.printer));
        const std::string default_bed = !config.opt_string("default_bed_type").empty() ? config.opt_string("default_bed_type") :
            vendor_json_value({fs::path(args.profiles) / args.vendor / "machine"}, config.opt_string("printer_model"), "default_bed_type");
        if (args.vendor == "BBL" || config.opt_bool("support_multi_bed_types") || !default_bed.empty()) {
            Preset printer(Preset::TYPE_PRINTER, args.printer);
            printer.config = config;
            printer.config.set_key_value("default_bed_type", new ConfigOptionString(default_bed));
            config.set_key_value("curr_bed_type", new ConfigOptionEnum<BedType>(printer.get_default_bed_type(&bundle)));
        }
        config.apply(resolve(bundle, args, "process", Preset::TYPE_PRINT, args.process));
        apply_filaments(bundle, args, config);
        Model model;
        const bool project_input = boost::algorithm::iequals(fs::path(args.model).extension().string(), ".3mf");
        if (args.plate_index && !project_input && args.plate.empty())
            throw std::runtime_error("--plate-index requires a saved .3mf project or a plate JSON");
        if (!args.edit.empty() && !project_input)
            throw std::runtime_error("--edit requires a saved .3mf project");
        bool project_settings = false;
        ContextInstances saved_instances;
        std::unique_ptr<PlateData> source_plate;
        std::vector<SourcePlate> source_plates;
        std::optional<Model> full_model;
        std::optional<DynamicPrintConfig> full_config;
        std::vector<ObjectID> source_objects;
        if (project_input) {
            ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::EnableSilent);
            En3mfType type;
            PlateDataPtrs plates;
            std::vector<Preset *> presets;
            Semver version;
            DynamicPrintConfig project_config;
            model = Model::read_from_archive(args.model, &project_config, &substitutions, type,
                LoadStrategy::LoadModel | LoadStrategy::LoadConfig | LoadStrategy::AddDefaultInstances,
                &plates, &presets, &version);
            if (!project_config.empty()) {
                config = DynamicPrintConfig::full_print_config();
                config.apply(project_config);
                project_settings = true;
            }
            std::unique_ptr<ConfigOption> source_area(config.option("printable_area")->clone());
            load_slice_config(args.model, config, project_settings);
            const std::vector<SavedInstance> saved = read_saved_instances(args.model);
            restore_loaded_ids(saved, model);
            for (const ModelObject *object : model.objects)
                source_objects.push_back(object->id());
            if (!args.json.empty() || args.plate_index || !args.save_3mf.empty() || !args.edit.empty())
                saved_instances = context_instances(args.model, model, saved);
            if (!args.edit.empty()) {
                DynamicPrintConfig source_layout = config;
                source_layout.set_key_value("printable_area", source_area->clone());
                edit_project(args.edit, model, source_layout, plates, saved_instances);
            }
            if (!args.json.empty()) {
                auto &sources = record["model_context"]["source_plates"];
                sources = Json::array();
                for (const PlateData *plate : plates)
                    if (plate) sources.push_back({{"index", plate->plate_index + 1}, {"name", plate->plate_name},
                                                   {"settings", context_settings(plate->config)}});
            }
            if (!args.save_3mf.empty()) {
                for (const PlateData *plate : plates)
                    if (plate) source_plates.push_back({plate->plate_index, plate->plate_name, plate->locked, plate->config,
                                                        plate->thumbnail_file, plate->no_light_thumbnail_file, plate->top_file, plate->pick_file});
                if (args.plate_index) {
                    full_model  = model;
                    full_config = config;
                }
            }
            if (args.plate_index) {
                std::unique_ptr<ConfigOption> slice_area(config.option("printable_area")->clone());
                config.set_key_value("printable_area", source_area.release());
                select_context_plate(model, config, plates, saved_instances, args.plate_index);
                config.set_key_value("printable_area", slice_area.release());
                if (!args.json.empty())
                    record["model_context"]["source_plate"] = {{"index", args.plate_index},
                        {"name", plates.front()->plate_name}, {"settings", context_settings(plates.front()->config)}};
            }
            if (plates.size() == 1 && plates.front() != nullptr) {
                const auto *bed_type = plates.front()->config.option<ConfigOptionEnum<BedType>>("curr_bed_type");
                if (bed_type != nullptr && bed_type->value > btDefault && bed_type->value < btCount)
                    config.apply_only(plates.front()->config, {"curr_bed_type"});
            }
            for (ModelObject *object : model.objects)
                for (ModelInstance *instance : object->instances)
                    instance->use_loaded_id_for_label = true;
            if (plates.size() == 1) {
                source_plate.reset(plates.front());
                plates.clear();
                const bool changed_colours = !args.colours.empty() || std::any_of(args.overrides.begin(), args.overrides.end(),
                    [](const auto &setting) { return setting.first == "filament_colour"; });
                if (args.auto_orient || args.center_set || changed_colours || !args.parts.empty() || !args.paint.empty() || !args.edit.empty()) {
                    source_plate->plate_thumbnail.reset();
                    source_plate->thumbnail_file.clear();
                    source_plate->no_light_thumbnail_file.clear();
                    source_plate->top_file.clear();
                    source_plate->pick_file.clear();
                }
            }
            release_PlateData_list(plates);
            for (Preset *preset : presets)
                delete preset;
        } else if (args.calib.empty()) {
            PlateLayout layout;
            model = ! args.plate.empty()   ? load_plate(args.plate, layout) :
                    ! args.project.empty() ? load_project(args.project) :
                read_input(args.model);
            if (layout.count > 0)
                select_layout_plate(args, model, config, layout, source_plates, saved_instances, full_model, full_config);
            else if (args.plate_index > 1)
                throw std::runtime_error("--plate-index does not name a plate in this plate JSON");
        }
        if (!args.json.empty())
            record["model_context"]["presets"] = context_presets(config, project_settings);
        for (const auto &[key, value] : args.overrides) {
            config.set_deserialize_strict(key, value);
            if (full_config)
                full_config->set_deserialize_strict(key, value);
        }
        if (project_input) {
            repair_project_flush(config);
            complete_project_ranges(model, config);
            if (full_config) repair_project_flush(*full_config);
            if (full_model) complete_project_ranges(*full_model, full_config ? *full_config : config);
        }
        if (! args.config_out.empty())
            config.save(args.config_out);
        if (! args.head_out.empty()) {
            PrintConfig printer_config;
            printer_config.apply(config, true);
            boost::nowide::ofstream out(args.head_out, std::ios::binary);
            out << Nonplanar::save_head_envelope(Nonplanar::resolve_head_envelope(printer_config));
            if (!out)
                throw std::runtime_error("cannot write " + args.head_out);
        }

        std::optional<Nonplanar::Calibration::Plate> calib_plate;
        std::optional<Calib_Params> classic_params;
        Nonplanar::HeadEnvelope calib_head;
        if (classic_calib_mode(args.calib)) {
            classic_params = classic_calib(args, model, config);
            for (const auto &[key, value] : args.overrides)
                config.set_deserialize_strict(key, value);
            finish_classic_calib(model, config);
        } else if (! args.calib.empty()) {
            PrintConfig printer_config;
            printer_config.apply(config, true);
            for (const auto &[key, value] : args.overrides)
                if (key == "nonplanar_head")
                    printer_config.nonplanar_head.value = value;
            if (! args.calib_head.empty())
                printer_config.nonplanar_head.value = args.calib_head;
            calib_head  = Nonplanar::resolve_head_envelope(printer_config);
            calib_plate = Nonplanar::Calibration::make_plate(*Nonplanar::Calibration::parse_test(args.calib), config, calib_head);
            config.apply(calib_plate->config);
            for (const auto &[key, value] : args.overrides)
                if (key != "nonplanar_head" || ! calib_plate->config.has(key))
                    config.set_deserialize_strict(key, value);
            Nonplanar::Calibration::add_to_model(*calib_plate, model);
        }
        if (model.objects.empty())
            throw std::runtime_error("no objects in " + args.model);
        if (! args.cut.empty()) {
            cut_only(bundle, args, model, config);
            return 0;
        }
        apply_multi_material(args, model);
        if (! args.arrange_only.empty()) {
            arrange_only(args, model, config);
            return 0;
        }
        if (! args.events.empty()) {
            CustomGCode::Info &info = model.plates_custom_gcodes[model.curr_plate_index];
            info.gcodes             = args.events;
            std::sort(info.gcodes.begin(), info.gcodes.end());
            const bool change = std::any_of(info.gcodes.begin(), info.gcodes.end(),
                                            [](const CustomGCode::Item &item) { return item.type == CustomGCode::ToolChange; });
            info.mode = config.option<ConfigOptionFloats>("filament_diameter")->size() == 1 ? CustomGCode::SingleExtruder :
                        change ? CustomGCode::MultiAsSingle : CustomGCode::MultiExtruder;
        }
        if (! args.save_3mf.empty()) {
            if (preserves_project_geometry(args)) {
                save_source_project(args, full_config ? *full_config : config, project_settings);
            } else {
                place(args, model, config, project_settings && !args.auto_orient);
                Model &saved_model = full_model ? *full_model : model;
                const std::vector<std::unique_ptr<PlateData>> plates = saved_plates(saved_model, source_plates, saved_instances);
                PlateDataPtrs plate_list;
                for (const auto &plate : plates)
                    plate_list.push_back(plate.get());
                save_project(bundle, args, saved_model, full_config ? *full_config : config, project_input, plate_list,
                             project_input ? carried_cut_information(args.model, source_objects, saved_model) : std::string());
            }
            std::cout << "project " << args.save_3mf << "\n";
            if (args.output.empty())
                return 0;
        }
        if (args.force_planar) {
            config.set_key_value("nonplanar_mode", new ConfigOptionEnum<NonplanarMode>(NonplanarMode::Off));
            clear_object_modes(model);
        }
        if (! args.confirm_up.empty()) {
            Vec3d up;
            if (std::sscanf(args.confirm_up.c_str(), "%lf,%lf,%lf", &up.x(), &up.y(), &up.z()) != 3)
                usage("--confirm-up expects X,Y,Z");
            const OrientSlice sl = slice_orientation(args, model, Nonplanar::rotation_to_z(up), config, datadir);
            std::cout << "orient_slice " << sl.ok << " " << sl.time_s << " " << sl.footprint << " " << sl.support << " " << sl.hits << "\n";
            if (! sl.error.empty())
                std::cout << "orient_slice_error " << sl.error << "\n";
            return 0;
        }
        if (args.auto_orient)
            rotate_model(model, choose_orientation(args, model, config, datadir));
        if (args.orient_only)
            return 0;
        place(args, model, config, project_settings && !args.auto_orient);
        if (!args.patch_preview.empty()) {
            write_patch_preview(args, model, config);
            if (args.output.empty())
                return 0;
        }

        if (!args.project_out.empty()) {
            fs::create_directories(model.get_backup_path());
            PlateData plate;
            plate.plate_index = 0;
            for (size_t i = 0; i < model.objects.size(); ++i)
                for (size_t j = 0; j < model.objects[i]->instances.size(); ++j)
                    plate.objects_and_instances.emplace_back(int(i), int(j));
            StoreParams params;
            params.plate_data_list = { &plate };
            params.path = args.project_out;
            params.model = &model;
            DynamicPrintConfig saved = project_config(bundle, args, config);
            params.config = &saved;
            params.strategy = SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::ShareMesh |
                              (project_input ? SaveStrategy::UseLoadedId : SaveStrategy::Default);
            if (!store_bbs_3mf(params))
                throw std::runtime_error("cannot write " + args.project_out);
        }

        std::mutex progress_mutex;
        int last_percent = 0;
        Print print;
        if (classic_params)
            print.set_calib_params(*classic_params);
        print.is_BBL_printer() = config.opt_string("printer_model").rfind("Bambu Lab", 0) == 0;
        for (ModelObject *object : model.objects)
            print.auto_assign_extruders(object);
        print.apply(model, config);
        Model::setBedPolygon(config, print.config());
        check_plate_layer_profiles(print);
        apply_layer_profile(args, model, print, config);
        if (! args.json.empty()) {
            const auto head = Nonplanar::resolve_head_envelope(print.config());
            const std::string file = Nonplanar::HeadLibrary::instance().source_path(head.id);
            record["inputs"]["mode"] = config.opt_serialize("nonplanar_mode");
            record["inputs"]["zaa_enabled"] = config.opt_bool("zaa_enabled");
            record["inputs"]["deform"] = config.opt_serialize("nonplanar_deform");
            const std::string atom = config.opt_string("nonplanar_atom_toolpath");
            record["inputs"]["atom_toolpath"] = atom.empty() ? nlohmann::json(nullptr) :
                nlohmann::json{{"file", atom}, {"sha1", file_sha1(atom)}};
            record["inputs"]["head"] = {{"id", head.id}, {"file", file}, {"sha1", file.empty() ? nlohmann::json(nullptr) : nlohmann::json(file_sha1(file))}};
        }

        std::vector<StringObjectException> warnings;
        if (const StringObjectException err = print.validate(&warnings); ! err.string.empty())
            throw std::runtime_error("validate: " + err.string);
        for (const StringObjectException &w : warnings)
            std::cerr << "warning: " << w.string << "\n";
        const auto   t_process = std::chrono::steady_clock::now();
        const double c_process = process_cpu_seconds();
        progress("load", 100);
        progress("slice", 0);
        if (args.progress) print.set_status_callback([&](const PrintBase::SlicingStatus &status) {
            std::lock_guard<std::mutex> lock(progress_mutex);
            const int percent = std::clamp(status.percent, 0, 100);
            if (percent > last_percent) { last_percent = percent; progress("slice", percent); }
        });
        try {
            print.process();
        } catch (const SlicingErrors &errors) {
            throw std::runtime_error(print.slicing_errors_message(errors));
        }
        if (args.progress) print.set_status_default();
        if (last_percent < 100) progress("slice", 100);
        progress("export", 0);
        const auto   t_export = std::chrono::steady_clock::now();
        const double c_export = process_cpu_seconds();
        GCodeProcessorResult result;
        const indexed_triangle_set mesh = plate_mesh(model);
        std::vector<std::array<double, 7>> planned;
        if (!args.planned_out.empty())
            GCodeWriter::planned_moves = &planned;
        print.export_gcode(args.output, &result, [&mesh, &config](const ThumbnailsParams &params) {
            ThumbnailsList list;
            for (const Vec2d &size : params.sizes)
                list.push_back(render_thumbnail(mesh, unsigned(size.x()), unsigned(size.y()), false,
                    config.option<ConfigOptionStrings>("filament_colour")->get_at(0)));
            return list;
        }, args.verify || !args.json.empty());
        GCodeWriter::planned_moves = nullptr;
        if (!args.planned_out.empty()) {
            boost::nowide::ofstream out(args.planned_out);
            out << "# x y z b c planned_tip_speed duration\n" << std::setprecision(10);
            for (const std::array<double, 7> &m : planned)
                out << m[0] << ' ' << m[1] << ' ' << m[2] << ' ' << m[3] << ' ' << m[4] << ' ' << m[5] << ' ' << m[6] << '\n';
        }
        const auto   t_done = std::chrono::steady_clock::now();
        const double c_done = process_cpu_seconds();

        progress("export", 100);
        if (!args.json.empty() || !args.moves_out.empty()) {
            const auto statistics = print_statistics_json(print, result);
            if (!args.json.empty()) record["statistics"] = statistics;
            if (!args.moves_out.empty()) write_moves(args.moves_out, result, args.filaments, statistics);
        }

        if (args.explain)
            for (size_t i = 0; i < print.objects().size(); ++i) {
                std::cout << "explain object " << print.objects()[i]->model_object()->name << "\n";
                for (const Nonplanar::ReportPatch &patch : result.nonplanar_report.patches)
                    if (patch.object == i && patch.top)
                        std::cout << patch.explain << "\n";
            }

        {
            const WallFragmentation frag = wall_fragmentation(result, print.config().nonplanar_kinematics.value != RotaryLayout::None);
            const double            per_mm = frag.wall_mm > 0. ? 2. * double(frag.runs) / frag.wall_mm : 0.;
            const double            share  = frag.wall_mm > 0. ? frag.near_end_mm / frag.wall_mm : 0.;
            std::cout << "wall_fragmentation runs " << frag.runs << " wall_mm " << frag.wall_mm << " ends_per_mm " << per_mm
                      << " share_within_1mm_of_end " << share << "\n";
            if (!args.json.empty())
                record["wall_fragmentation"] = { { "runs", frag.runs }, { "wall_mm", frag.wall_mm }, { "ends_per_mm", per_mm },
                                                 { "share_within_1mm_of_end", share } };
        }
        size_t     vz_line         = 0;
        const auto [vz, vz_actual] = max_vz(result, &vz_line, print.config().nonplanar_kinematics.value != RotaryLayout::None);
        const auto &normal         = result.print_statistics.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)];
        std::cout << "time_process_s " << std::chrono::duration<double>(t_export - t_process).count() << "\n"
                  << "time_export_s " << std::chrono::duration<double>(t_done - t_export).count() << "\n"
                  << "gcode " << args.output << "\n"
                  << "estimated_time_s " << normal.time << "\n"
                  << "max_vz_commanded " << vz << "\n"
                  << "max_vz_planned " << vz_actual << "\n"
                  << "max_vz_planned_line " << vz_line << "\n"
                  << "machine_max_speed_z " << config.opt<ConfigOptionFloats>("machine_max_speed_z")->get_at(0) << "\n"
                  << "cpu_process_s " << c_export - c_process << "\n"
                  << "cpu_export_s " << c_done - c_export << "\n"
                  << "preview_moves " << result.moves.size() << "\n"
                  << "preview_moves_mb " << double(result.moves.size() * sizeof(result.moves.front())) / 1e6 << "\n";
        if (! args.json.empty()) {
            auto &text = record["text_summary"];
            text["time_process_s"] = text_value(std::chrono::duration<double>(t_export - t_process).count());
            text["time_export_s"] = text_value(std::chrono::duration<double>(t_done - t_export).count());
            text["gcode"] = args.output;
            text["estimated_time_s"] = text_value(normal.time);
            text["max_vz_commanded"] = text_value(vz);
            text["max_vz_planned"] = text_value(vz_actual);
            text["max_vz_planned_line"] = text_value(vz_line);
            text["machine_max_speed_z"] = text_value(config.opt<ConfigOptionFloats>("machine_max_speed_z")->get_at(0));
        }
        for (const auto &warning : print.step_state_with_warnings(psSkirtBrim).warnings) {
            std::cout << "print_warning " << warning.message << "\n";
            if (!args.json.empty()) {
                if (!record["text_summary"].contains("print_warning"))
                    record["text_summary"]["print_warning"] = warning.message;
                record["print_warnings"].push_back(warning.message);
            }
        }
        for (const auto &warning : print.step_state_with_warnings(psGCodeExport).warnings)
            std::cout << "export_warning " << warning.message << "\n";
        if (! args.json.empty())
            for (const auto &warning : print.step_state_with_warnings(psGCodeExport).warnings) {
                record["text_summary"]["export_warning"] = warning.message;
                break;
            }
        for (const PrintObject *object : print.objects())
            for (int step = 0; step < int(posCount); ++step)
                for (const auto &warning : object->step_state_with_warnings(PrintObjectStep(step)).warnings) {
                    std::cout << "object_warning " << warning.message << "\n";
                    if (! args.json.empty()) {
                        if (! record["text_summary"].contains("object_warning"))
                            record["text_summary"]["object_warning"] = warning.message;
                        record["object_warnings"].push_back(warning.message);
                    }
                }
        Nonplanar::FlowModStats flow;
        for (const PrintObject *object : print.objects())
            flow.merge(object->conformal().flow_mod);
        if (flow.points > 0)
            std::cout << "flow_points " << flow.points << "\n"
                      << "flow_normal_gap_mm " << flow.min_gap << " " << flow.max_gap << "\n"
                      << "flow_laid_mm " << flow.min_laid << " " << flow.max_laid << "\n"
                      << "flow_clamped thick " << flow.max_clamped << " thin " << flow.min_clamped << " dragged " << flow.dragged
                      << " contact " << flow.contact << "\n";
        if (! args.json.empty() && flow.points > 0) {
            auto &text = record["text_summary"];
            text["flow_points"] = text_value(flow.points);
            text["flow_normal_gap_mm"] = text_value(flow.min_gap) + " " + text_value(flow.max_gap);
            text["flow_laid_mm"] = text_value(flow.min_laid) + " " + text_value(flow.max_laid);
            text["flow_clamped"] = "thick " + text_value(flow.max_clamped) + " thin " + text_value(flow.min_clamped) +
                " dragged " + text_value(flow.dragged) + " contact " + text_value(flow.contact);
        }
        double np_extrude = 0., np_other = 0.;
        std::map<std::string, double> np_kind;
        for (size_t i = 0; i < result.moves.size(); ++i) {
            const auto &m = result.moves[i];
            if (result.state(m).nonplanar_patch_shell == 0xFF)
                continue;
            const double t = m.time[size_t(PrintEstimatedStatistics::ETimeMode::Normal)];
            if (m.type == EMoveType::Extrude) {
                np_extrude += t;
                continue;
            }
            np_other += t;
            const Vec3f d = i > 0 ? Vec3f(m.position - result.moves[i - 1].position) : Vec3f::Zero();
            const bool  xy = std::abs(d.x()) + std::abs(d.y()) > 1e-4f, z = std::abs(d.z()) > 1e-4f;
            np_kind[m.type == EMoveType::Retract || m.type == EMoveType::Unretract ? "retract" : xy && z ? "xyz" : xy ? "xy" : z ? "z" : "other"] += t;
        }
        for (const auto &[kind, t] : np_kind)
            std::cout << "nonplanar_other_kind_s " << t << " " << kind << "\n";
        if (! args.json.empty() && !np_kind.empty())
            record["text_summary"]["nonplanar_other_kind_s"] = text_value(np_kind.begin()->second) + " " + np_kind.begin()->first;
        std::map<std::string, double> role_time;
        for (const auto &m : result.moves)
            role_time[m.type == EMoveType::Extrude ? ExtrusionEntity::role_to_string(m.extrusion_role) : std::string("Travel and other")] +=
                m.time[size_t(PrintEstimatedStatistics::ETimeMode::Normal)];
        for (const auto &[role, t] : role_time)
            std::cout << "role_time_s " << t << " " << role << "\n";
        if (! args.json.empty() && !role_time.empty())
            record["text_summary"]["role_time_s"] = text_value(role_time.begin()->second) + " " + role_time.begin()->first;
        if (!args.move_times.empty()) {
            std::ofstream mt(args.move_times);
            for (const auto &m : result.moves)
                mt << m.gcode_id << ' ' << m.position.x() << ' ' << m.position.y() << ' ' << m.position.z() << ' '
                   << m.time[size_t(PrintEstimatedStatistics::ETimeMode::Normal)] << ' ' << int(result.state(m).nonplanar_patch_shell != 0xFF) << '\n';
        }
        std::map<std::string, double> feature_time;
        for (size_t i = 0; i < result.moves.size(); ++i) {
            const auto &m  = result.moves[i];
            const bool  np = result.state(m).nonplanar_patch_shell != 0xFF;
            std::string kind;
            if (m.type == EMoveType::Extrude)
                kind = ExtrusionEntity::role_to_string(m.extrusion_role);
            else {
                const Vec3f d  = i > 0 ? Vec3f(m.position - result.moves[i - 1].position) : Vec3f::Zero();
                const bool  xy = std::abs(d.x()) + std::abs(d.y()) > 1e-4f, z = std::abs(d.z()) > 1e-4f;
                kind = m.type == EMoveType::Retract || m.type == EMoveType::Unretract ? "retract" :
                       xy && z                                                       ? "move xyz" :
                       xy                                                            ? "move xy" :
                       z                                                             ? "move z" :
                                                                                       "other";
            }
            feature_time[(np ? "np " : "planar ") + kind] += m.time[size_t(PrintEstimatedStatistics::ETimeMode::Normal)];
        }
        for (const auto &[kind, t] : feature_time)
            std::cout << "feature_time_s " << t << " " << kind << "\n";
        std::cout << "nonplanar_extrude_time_s " << np_extrude << "\n"
                  << "nonplanar_other_time_s " << np_other << "\n";
        if (! args.json.empty()) {
            record["text_summary"]["nonplanar_extrude_time_s"] = text_value(np_extrude);
            record["text_summary"]["nonplanar_other_time_s"] = text_value(np_other);
            if (!feature_time.empty())
                record["text_summary"]["feature_time_s"] = text_value(feature_time.begin()->second) + " " + feature_time.begin()->first;
        }
        if (calib_plate) {
            std::cout << "calib_test " << args.calib << "\n" << "calib_objects " << calib_plate->objects.size() << "\n";
            for (const auto &object : calib_plate->objects)
                std::cout << "calib_object " << object.center.x() << " " << object.center.y() << " " << object.name << "\n";
            const auto test = *Nonplanar::Calibration::parse_test(args.calib);
            if (test == Nonplanar::Calibration::Test::Clearance || test == Nonplanar::Calibration::Test::Verifier) {
                const double gap = config.opt_float("layer_height");
                std::cout << "calib_head " << calib_head.id << "\n"
                          << "calib_model_first_touch " << Nonplanar::Calibration::predicted_first_touch(calib_head, gap) << "\n";
                for (const auto &step : Nonplanar::Calibration::clearance_steps()) {
                    std::string component;
                    const double c = Nonplanar::Calibration::step_clearance(calib_head, step, gap, &component);
                    std::cout << "calib_step " << step.slope_deg << " " << step.relief << " clearance " << c << " " << component << "\n";
                }
            }
            if (args.calib_fit >= 0) {
                const double gap = config.opt_float("layer_height");
                const std::string id = calib_head.id + "-calibrated";
                const auto fit = Nonplanar::Calibration::fit_clearance(calib_head, args.calib_fit, gap, id, calib_head.name + " (calibrated)");
                Nonplanar::HeadLibrary::instance().save_user_head(fit.head);
                Nonplanar::HeadLibrary::instance().reload();
                const Nonplanar::HeadEnvelope *loaded = Nonplanar::HeadLibrary::instance().find(id);
                PrintConfig selected;
                selected.apply(config, true);
                selected.nonplanar_head.value = id;
                selected.nonplanar_head_snapshot.value.clear();
                std::cout << "calib_fit_shift_mm " << fit.shift << "\n"
                          << "calib_fit_slope_cap_deg " << fit.slope_cap_deg << "\n"
                          << "calib_fit_band_height_mm " << fit.band_height << "\n"
                          << "calib_fit_gantry_floor_mm " << fit.gantry_floor << "\n"
                          << "calib_fit_model_first_touch " << Nonplanar::Calibration::predicted_first_touch(fit.head, gap) << "\n"
                          << "calib_roundtrip_head " << (loaded != nullptr && *loaded == fit.head && Nonplanar::resolve_head_envelope(selected) == fit.head ? "ok" : "MISMATCH") << "\n"
                          << "calib_roundtrip_head_file " << Nonplanar::HeadLibrary::instance().source_path(id) << "\n";
                if (loaded != nullptr)
                    std::cout << "calib_fit_verifier_first_touch "
                              << Nonplanar::Calibration::verifier_first_touch(*calib_plate, result, *loaded, *make_tool_kinematics(print.config())) << "\n";
            }
            if (! args.calib_picks.empty()) {
                std::vector<int> picks;
                for (size_t pos = 0; pos <= args.calib_picks.size();) {
                    const size_t end = std::min(args.calib_picks.find(',', pos), args.calib_picks.size());
                    picks.push_back(std::atoi(args.calib_picks.substr(pos, end - pos).c_str()));
                    pos = end + 1;
                }
                DynamicPrintConfig written;
                if (test == Nonplanar::Calibration::Test::TiltFlow)
                    written.set_key_value("nonplanar_tilt_flow", new ConfigOptionFloats(Nonplanar::Calibration::tilt_flow_result(
                        config.option<ConfigOptionFloats>("nonplanar_tilt_flow")->values, picks)));
                else if (test == Nonplanar::Calibration::Test::CurvedTop && picks.size() == 2)
                    written = Nonplanar::Calibration::curved_top_result(picks[0], picks[1]);
                DynamicPrintConfig saved = config;
                saved.apply(written);
                const std::string path = (datadir / "calib_roundtrip.json").string();
                saved.save_to_json(path, "calibrated", "User", "2.0.0.0");
                DynamicPrintConfig                 loaded;
                std::map<std::string, std::string> key_values;
                std::string                        reason;
                loaded.load_from_json(path, ForwardCompatibilitySubstitutionRule::EnableSilent, key_values, reason);
                bool same = ! written.empty();
                for (const std::string &key : written.keys()) {
                    std::cout << "calib_written " << key << " = " << written.opt_serialize(key) << "\n";
                    same = same && loaded.has(key) && *loaded.option(key) == *written.option(key);
                }
                std::cout << "calib_roundtrip_config " << (same ? "ok" : "MISMATCH") << "\n";
            }
            if (test == Nonplanar::Calibration::Test::Verifier) {
                ClearanceReport report;
                const int first = Nonplanar::Calibration::verifier_first_touch(*calib_plate, result, calib_head,
                                                                               *make_tool_kinematics(print.config()), &report);
                std::cout << "calib_verifier_first_touch " << first << "\n"
                          << "calib_verifier_hit_moves " << report.hit_moves << "\n";
                std::vector<size_t> hits(calib_plate->objects.size(), 0);
                std::vector<float>  worst(calib_plate->objects.size(), 0.f);
                for (size_t i = 0; i < report.per_move.size() && i < result.moves.size(); ++i) {
                    if (!(report.per_move[i] < 0.f) || result.moves[i].type != EMoveType::Extrude)
                        continue;
                    const Vec2d p(result.moves[i].position.x(), result.moves[i].position.y());
                    size_t best = 0;
                    for (size_t k = 1; k < calib_plate->objects.size(); ++k)
                        if ((p - calib_plate->objects[k].center).norm() < (p - calib_plate->objects[best].center).norm())
                            best = k;
                    ++hits[best];
                    worst[best] = std::min(worst[best], report.per_move[i]);
                }
                for (size_t k = 0; k < hits.size(); ++k)
                    std::cout << "calib_verifier_step " << k + 1 << " hits " << hits[k] << " worst " << worst[k] << "\n";
                for (const ClearanceHit &hit : report.worst)
                    std::cout << "calib_verifier_hit " << hit.component << " " << hit.penetration << " at " << hit.tip.x() << "," << hit.tip.y() << "," << hit.tip.z() << "\n";
            }
        }
        progress("verify", 0);
        if (args.verify) {
            ClearanceParams params;
            params.head               = Nonplanar::resolve_head_envelope(print.config());
            params.contact_bar        = true;
            result.clearance_report   = verify_clearance(result, params, *make_tool_kinematics(print.config()));
        }
        if (!args.tip_out.empty()) {
            const auto       kinematics = make_tool_kinematics(print.config());
            const bool       rotary     = kinematics->rotary_count() > 0;
            boost::nowide::ofstream tips(args.tip_out);
            tips << "# ax ay az bx by bz nx ny nz width height part\n";
            for (size_t i = 1; i < result.moves.size(); ++i) {
                const auto &m = result.moves[i];
                if (m.type != EMoveType::Extrude)
                    continue;
                const Vec3f &a    = result.moves[i - 1].position, &b = m.position;
                const Vec3d  axis = rotary && result.has_joints() ? kinematics->forward(result.joints[i]).axis : Vec3d::UnitZ();
                const ExtrusionRole r = m.extrusion_role;
                const bool part = !(r == erSkirt || r == erBrim || r == erWipeTower || r == erCustom || r == erNone ||
                                    r == erSupportMaterial || r == erSupportMaterialInterface || r == erSupportTransition);
                tips << a.x() << ' ' << a.y() << ' ' << a.z() << ' ' << b.x() << ' ' << b.y() << ' ' << b.z() << ' '
                     << axis.x() << ' ' << axis.y() << ' ' << axis.z() << ' ' << m.width << ' ' << m.height << ' ' << int(part) << '\n';
            }
        }
        if (!args.dump_field.empty()) {
            for (size_t i = 0; i < print.objects().size(); ++i) {
                const PrintObject &object = *print.objects()[i];
                Nonplanar::NpField file;
                bool               found = false;
                for (const Nonplanar::ConformalPatch &patch : object.conformal().patches)
                    if (patch.kind == Nonplanar::PatchKind::Field && patch.field) {
                        file  = patch.field->to_npfield();
                        found = true;
                        break;
                    }
                if (!found) {
                    const indexed_triangle_set mesh = Nonplanar::slicing_frame_mesh(object);
                    BoundingBoxf3              box;
                    for (const stl_vertex &v : mesh.vertices)
                        box.merge(v.cast<double>());
                    file = Nonplanar::planar_npfield(box, 1., object.config().layer_height.value, print.config().initial_layer_print_height.value);
                }
                Nonplanar::to_plate_frame(file, Nonplanar::plate_offset(object));
                file.machine = args.printer;
                const std::string path = print.objects().size() == 1 ? args.dump_field :
                                                                       args.dump_field + "." + std::to_string(i) + ".npz";
                Nonplanar::save_npfield(path, file);
                std::cout << "field_dumped " << path << " " << file.solver << " " << file.dims[0] << "x" << file.dims[1] << "x" << file.dims[2] << "\n";
            }
        }
        progress("verify", 100);
        const ClearanceReport &clearance = result.clearance_report;
        std::cout << "clearance_checked " << clearance.ran << "\n";
        if (clearance.ran) {
            std::cout << "clearance_hit_moves " << clearance.hit_moves << "\n"
                      << "clearance_machine_hit_moves " << clearance.machine_hit_moves << "\n"
                      << "clearance_tolerated_moves " << clearance.tolerated_moves << "\n"
                      << "clearance_land_strict_moves " << clearance.land_strict_moves << "\n"
                      << "clearance_min_mm " << clearance.min_clearance << "\n";
            for (const ClearanceHit &hit : clearance.worst)
                std::cout << "clearance_hit line " << hit.gcode_line << " " << hit.component << " " << hit.penetration << " mm\n";
            for (const std::string &omission : clearance.omissions)
                std::cout << "clearance_omission " << omission << "\n";
            std::cout << "clearance_hit_classes land " << clearance.hit_land_moves << " cone " << clearance.hit_cone_moves << " body "
                      << clearance.hit_body_moves << " travel " << clearance.hit_travel_moves << " extrusion " << clearance.hit_extrusion_moves << "\n";
        }
        if (! args.json.empty()) {
            auto &text = record["text_summary"];
            text["clearance_checked"] = text_value(clearance.ran);
            if (clearance.ran) {
                text["clearance_hit_moves"] = text_value(clearance.hit_moves);
                text["clearance_machine_hit_moves"] = text_value(clearance.machine_hit_moves);
                text["clearance_tolerated_moves"]   = text_value(clearance.tolerated_moves);
                text["clearance_min_mm"] = text_value(clearance.min_clearance);
                if (!clearance.worst.empty()) {
                    const auto &hit = clearance.worst.front();
                    text["clearance_hit"] = "line " + text_value(hit.gcode_line) + " " + hit.component + " " + text_value(hit.penetration) + " mm";
                }
                if (!clearance.omissions.empty()) text["clearance_omission"] = clearance.omissions.front();
            }
            if (!args.gcode_3mf.empty()) text["gcode_3mf"] = args.gcode_3mf;
        }
        if (! args.gcode_3mf.empty()) {
            write_gcode_3mf(args, model, print, config, result, source_plate.get());
            std::cout << "gcode_3mf " << args.gcode_3mf << "\n";
        }
        const Nonplanar::NonplanarReport report = result.nonplanar_report.objects.empty() ? Nonplanar::make_report(print, result, true) :
                                                                                           result.nonplanar_report;
        for (size_t i = 0; i < report.objects.size(); ++i) {
            const Nonplanar::ReportObject &object = report.objects[i];
            if (print.objects()[i]->auto_z_contouring()) {
                std::cout << "nonplanar_auto_z_contouring " << object.name << "\n";
                if (!args.json.empty())
                    record["auto_z_contouring_objects"].push_back(object.name);
            }
            if (!object.oneshot.empty()) {
                std::cout << "nonplanar_oneshot " << object.oneshot << "\n"
                          << "nonplanar_oneshot_student " << object.oneshot_student << " polish_k " << object.oneshot_polish_k << "\n";
                if (object.oneshot_max_time_ratio_override > 0.)
                    std::cout << "nonplanar_oneshot_max_time_ratio_override " << object.oneshot_max_time_ratio_override << "\n";
                if (!args.json.empty()) {
                    record["oneshot"] = { { "decision", object.oneshot }, { "student", object.oneshot_student },
                                          { "polish_k", object.oneshot_polish_k } };
                    if (object.oneshot_max_time_ratio_override > 0.)
                        record["oneshot"]["max_time_ratio_override"] = object.oneshot_max_time_ratio_override;
                }
            }
            std::cout << "nonplanar_object " << object.name << "\n"
                      << "nonplanar_top_patches " << object.top_patches << "\n"
                      << "nonplanar_bands " << object.bands << "\n"
                      << "nonplanar_top_footprint_mm2 " << object.curved_top << "\n";
            if (! args.json.empty()) {
                auto &text = record["text_summary"];
                if (!text.contains("nonplanar_top_patches")) {
                    text["nonplanar_top_patches"] = text_value(object.top_patches);
                    text["nonplanar_bands"] = text_value(object.bands);
                    text["nonplanar_top_footprint_mm2"] = text_value(object.curved_top);
                }
                for (const Nonplanar::ReportFallback &f : report.fallbacks)
                    if (f.object == i && !text.contains("nonplanar_fallback"))
                        text["nonplanar_fallback"] = f.reason + " " + text_value(f.area) + " patch " + text_value(f.patch);
            }
            for (size_t r = 0; r < object.regions.size(); ++r)
                std::cout << "nonplanar_region " << r << " " << object.regions[r] << "\n";
            for (const Nonplanar::ReportPatch &patch : report.patches)
                if (patch.object == i)
                    std::cout << "nonplanar_patch " << patch.id << " kind " << patch.kind << " region " << patch.region << " band "
                              << patch.band << " shells " << patch.shells << " xy " << patch.center.x() << "," << patch.center.y() << "\n";
            for (const Nonplanar::PatchBase &b : print.objects()[i]->conformal().base) {
                std::cout << "nonplanar_base patches ";
                for (size_t k = 0; k < b.patches.size(); ++k)
                    std::cout << (k ? "," : "") << b.patches[k];
                std::cout << " area " << b.area << " on_csparse " << b.share[0] << " on_sparse " << b.share[1] << " on_solid " << b.share[2]
                          << " on_air " << b.share[3] << " on_underside " << b.share[4] << " span " << b.span << " reach_p50 " << b.reach_p50
                          << " solid_min " << b.solid_min << " solid_p50 " << b.solid_p50 << " slope_p90 " << b.slope_p90 << " sag_rms " << b.sag_rms
                          << " sag_max " << b.sag_max
                          << " solid_layers " << b.solid_layers << " bridged " << b.bridged << "\n";
            }
            for (const Nonplanar::ReportFallback &f : report.fallbacks)
                if (f.object == i)
                    std::cout << "nonplanar_fallback " << f.reason << " " << f.area << " patch " << f.patch << "\n";
        }
        if (! args.json.empty()) {
            progress("json", 0);
            slice_json_results(record, args, print, result, model, config);
            if (!args.plate.empty()) {
                boost::nowide::ifstream in(args.plate);
                const auto layout = nlohmann::json::parse(in);
                for (size_t i = 0; i < print.objects().size(); ++i) {
                    const ModelObject *object = print.objects()[i]->model_object();
                    const auto it = std::find_if(model.objects.begin(), model.objects.end(),
                        [object](const ModelObject *source) { return source->id() == object->id(); });
                    const auto &entry = layout.at("objects").at(size_t(it - model.objects.begin()));
                    if (entry.contains("settings")) {
                        auto &settings = record["objects"][i]["settings"] = nlohmann::json::object();
                        for (const auto &[key, value] : entry.at("settings").items())
                            settings[key] = object->config.has(key) ? nlohmann::json(object->config.opt_serialize(key)) : nlohmann::json(nullptr);
                    }
                }
            }
            record["totals"]["move_count"] = result.moves.size();
            slice_model_context(record, print, config, saved_instances);
            record["status"] = "sliced";
            write_slice_json(args.json, record);
            progress("json", 100);
        }
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        if (! args.json.empty()) {
            record["status"] = "error";
            record["totals"]["aborts"].push_back(e.what());
            record["totals"]["exceptions"].push_back(e.what());
            try { write_slice_json(args.json, record); }
            catch (const std::exception &json_error) { std::cerr << "error: " << json_error.what() << "\n"; }
        }
        return 1;
    }
    return 0;
}
