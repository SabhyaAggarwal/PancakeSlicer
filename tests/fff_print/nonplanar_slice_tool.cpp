#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

#include "admesh/stl.h"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/FlushVolCalc.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/GCode/ClearanceVerifier.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/GCode/WipeTowerEstimate.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Nonplanar/AutoOrient.hpp"
#include "libslic3r/Nonplanar/Calibration.hpp"
#include "libslic3r/Nonplanar/ConformalShells.hpp"
#include "libslic3r/Nonplanar/AutoMode.hpp"
#include "libslic3r/Nonplanar/FlowMod.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
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
#include "libslic3r/libslic3r.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <ios>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
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
                tip_out, calib, calib_head, calib_picks;
    int         calib_fit = -1;
    std::vector<std::string> filaments, colours;
    std::vector<std::pair<std::string, int>> parts;
    std::vector<std::pair<std::string, std::string>> overrides;
    bool        center_set = false;
    bool        verify     = false;
    bool        explain    = false;
    bool        auto_orient = false;
    bool        orient_only = false;
    std::string confirm_up;
    std::vector<std::string> argv;
    Vec2d       center     = Vec2d::Zero();
};

[[noreturn]] void usage(const std::string &error)
{
    std::cerr << "error: " << error << "\n"
              << "usage: nonplanar_slice_tool --printer NAME --process NAME --filament NAME [--vendor Prusa]\n"
                 "       [--profiles DIR] [--datadir DIR] [--set key=value]... [--center X,Y]\n"
                 "       [--config-out FILE] [--project-out FILE] [--head-out FILE] [--gcode-3mf FILE] [--json FILE]\n"
                 "       [--atom TOOLPATH] [--verify] [--explain] [--auto-orient | --orient-only] [--save-3mf PROJECT.3mf]\n"
                 "       [--layer-profile adaptive:QUALITY | ramp:H_BOTTOM:H_TOP | range:DEPTH:H] [--calib-head ID]\n"
                 "       -o OUT.gcode (MODEL | PROJECT.3mf | --plate PLATE.json | --project SPEC.json | --calib TEST)\n";
    std::exit(2);
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
        else if (arg == "--layer-profile") a.layer_profile = next();
        else if (arg == "--vendor") a.vendor = next();
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
        else if (arg == "--json") a.json = next();
        else if (arg == "--plate") a.plate = next();
        else if (arg == "--project") a.project = next();
        else if (arg == "--save-3mf") a.save_3mf = next();
        else if (arg == "--tip-out") a.tip_out = next();
        else if (arg == "--calib") a.calib = next();
        else if (arg == "--calib-head") a.calib_head = next();
        else if (arg == "--calib-fit") a.calib_fit = std::atoi(next().c_str());
        else if (arg == "--calib-picks") a.calib_picks = next();
        else if (arg == "--verify") a.verify = true;
        else if (arg == "--auto-orient") a.auto_orient = true;
        else if (arg == "--orient-only") a.auto_orient = a.orient_only = true;
        else if (arg == "--confirm-up") a.confirm_up = next();
        else if (arg == "-o" || arg == "--output") a.output = fs::absolute(next()).string();
        else if (arg == "--set") {
            const std::string kv  = next();
            const size_t      pos = kv.find('=');
            if (pos == std::string::npos)
                usage("--set expects key=value, got " + kv);
            a.overrides.emplace_back(kv.substr(0, pos), kv.substr(pos + 1));
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
    if (a.printer.empty() || a.process.empty() || a.filaments.empty() ||
        (a.output.empty() && ! a.orient_only && a.confirm_up.empty() && a.save_3mf.empty()) ||
        int(! a.model.empty()) + int(! a.plate.empty()) + int(! a.project.empty()) + int(! a.calib.empty()) != 1)
        usage("--printer, --process, --filament, -o and one of a model, --plate, --project or --calib are required");
    if (! a.calib.empty() && ! Nonplanar::Calibration::parse_test(a.calib))
        usage("unknown --calib test " + a.calib);
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
    std::string        error;
    if (fs::path(name).extension() == ".json" && fs::is_regular_file(name)) {
        std::map<std::string, std::string> key_values;
        config.load_from_json(name, ForwardCompatibilitySubstitutionRule::EnableSilent, key_values, error);
        if (! error.empty())
            throw std::runtime_error("cannot load " + name + ": " + error);
        return config;
    }
    const std::string  file = find_preset_file(a, kind, name);
    if (! bundle.resolve_preset_config(config, type, file, ForwardCompatibilitySubstitutionRule::EnableSilent, error))
        throw std::runtime_error("cannot resolve " + file + ": " + error);
    return config;
}

std::pair<double, double> max_vz(const GCodeProcessorResult &result, size_t *actual_line = nullptr)
{
    double commanded = 0., actual = 0.;
    size_t previous = 0;
    for (size_t i = 1; i < result.moves.size(); ++i) {
        const auto &m = result.moves[i];
        if (m.internal_only)
            continue;
        const Vec3d d = m.position.cast<double>() - result.moves[previous].position.cast<double>();
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

ThumbnailData render_thumbnail(const indexed_triangle_set &mesh, unsigned width, unsigned height, bool top)
{
    ThumbnailData thumb;
    thumb.set(width, height);
    std::fill(thumb.pixels.begin(), thumb.pixels.end(), 0);
    const double c = top ? 0. : std::sqrt(0.5);
    const Vec3d  right(1., 0., 0.), up = top ? Vec3d(0., 1., 0.) : Vec3d(0., c, c), toward = right.cross(up);
    const Vec3d  light = Vec3d(-0.3, -0.5, 1.).normalized();
    std::vector<Vec3d> p(mesh.vertices.size());
    BoundingBoxf       box;
    for (size_t i = 0; i < p.size(); ++i) {
        const Vec3d v = mesh.vertices[i].cast<double>();
        p[i]          = Vec3d(v.dot(right), v.dot(up), v.dot(toward));
        box.merge(Vec2d(p[i].x(), p[i].y()));
    }
    if (! box.defined)
        return thumb;
    const double scale = 0.9 * std::min(width / std::max(box.size().x(), 1e-3), height / std::max(box.size().y(), 1e-3));
    const Vec2d  mid   = box.center();
    for (Vec3d &q : p)
        q = Vec3d((q.x() - mid.x()) * scale + 0.5 * width, (q.y() - mid.y()) * scale + 0.5 * height, q.z());
    std::vector<double> depth(size_t(width) * height, -std::numeric_limits<double>::max());
    for (const stl_triangle_vertex_indices &f : mesh.indices) {
        const Vec3d &a = p[f[0]], &b = p[f[1]], &d = p[f[2]];
        const double area = (b.x() - a.x()) * (d.y() - a.y()) - (d.x() - a.x()) * (b.y() - a.y());
        if (std::abs(area) < 1e-12)
            continue;
        const Vec3d  n     = (mesh.vertices[f[1]] - mesh.vertices[f[0]]).cast<double>().cross((mesh.vertices[f[2]] - mesh.vertices[f[0]]).cast<double>()).normalized();
        const double shade = 0.35 + 0.65 * std::abs(n.dot(light));
        const int    x0 = std::max(0, int(std::floor(std::min({ a.x(), b.x(), d.x() })))), x1 = std::min(int(width) - 1, int(std::ceil(std::max({ a.x(), b.x(), d.x() }))));
        const int    y0 = std::max(0, int(std::floor(std::min({ a.y(), b.y(), d.y() })))), y1 = std::min(int(height) - 1, int(std::ceil(std::max({ a.y(), b.y(), d.y() }))));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const double px = x + 0.5, py = y + 0.5;
                const double w0 = ((b.x() - px) * (d.y() - py) - (d.x() - px) * (b.y() - py)) / area;
                const double w1 = ((d.x() - px) * (a.y() - py) - (a.x() - px) * (d.y() - py)) / area;
                const double w2 = 1. - w0 - w1;
                if (w0 < 0. || w1 < 0. || w2 < 0.)
                    continue;
                const double z = w0 * a.z() + w1 * b.z() + w2 * d.z();
                double      &zb = depth[size_t(y) * width + x];
                if (z <= zb)
                    continue;
                zb                = z;
                unsigned char *px4 = &thumb.pixels[4 * (size_t(y) * width + x)];
                px4[0] = (unsigned char) (0x00 * shade);
                px4[1] = (unsigned char) (0xAE * shade);
                px4[2] = (unsigned char) (0x42 * shade);
                px4[3] = 255;
            }
    }
    return thumb;
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
                     const fs::path &datadir)
{
    auto plate                     = std::make_unique<PlateData>();
    plate->plate_index             = 0;
    for (size_t i = 0; i < model.objects.size(); ++i)
        for (size_t j = 0; j < model.objects[i]->instances.size(); ++j)
            plate->objects_and_instances.emplace_back(int(i), int(j));
    plate->gcode_file              = args.output;
    plate->is_sliced_valid         = true;
    plate->toolpath_outside        = result.toolpath_outside;
    plate->timelapse_warning_code  = result.timelapse_warning_code;
    plate->is_label_object_enabled = result.label_object_enabled;
    plate->is_support_used         = print.is_support_used();
    plate->printer_model_id = vendor_json_value({ fs::path(args.profiles) / args.vendor / "machine" }, config.opt_string("printer_model"), "model_id");
    plate->nozzle_diameters = config.option("nozzle_diameter")->serialize();
    plate->gcode_prediction = std::to_string(int(result.print_statistics.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)].time));
    plate->first_layer_time = std::to_string(result.initial_layer_time);
    if (!result.nonplanar_report.empty())
        plate->nonplanar_report = result.nonplanar_report.to_json();
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
    ThumbnailData              plate_png = render_thumbnail(mesh, 512, 512, false), top_png = render_thumbnail(mesh, 512, 512, true);
    plate->plate_thumbnail.load_from(plate_png);

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

    model.set_backup_path((datadir / "backup").string());
    StoreParams params;
    params.path                    = args.gcode_3mf;
    params.model                   = &model;
    params.plate_data_list         = { plate.get() };
    params.export_plate_idx        = 0;
    params.config                  = &config;
    params.thumbnail_data          = { &plate_png };
    params.no_light_thumbnail_data = { &plate_png };
    params.top_thumbnail_data      = { &top_png };
    params.pick_thumbnail_data     = { &top_png };
    params.id_bboxes               = { &bboxes };
    params.strategy = SaveStrategy::Silence | SaveStrategy::WithGcode | SaveStrategy::SplitModel | SaveStrategy::ShareMesh |
                      SaveStrategy::SkipModel;
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
        Model part = Model::read_from_file(file, nullptr, nullptr, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances);
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

void apply_layer_profile(const Args &args, Model &model, Print &print, const DynamicPrintConfig &config)
{
    if (args.layer_profile.empty())
        return;
    const size_t colon = args.layer_profile.find(':');
    const std::string kind = args.layer_profile.substr(0, colon);
    double a = 0., b = 0.;
    const int values = colon == std::string::npos ? 0 : std::sscanf(args.layer_profile.c_str() + colon + 1, "%lf:%lf", &a, &b);
    if (!((kind == "adaptive" && values >= 1) || ((kind == "ramp" || kind == "range") && values == 2)))
        usage("--layer-profile expects adaptive:Q, ramp:H0:H1 or range:D:H");
    for (ModelObject *object : model.objects) {
        const PrintObject *print_object = nullptr;
        for (const PrintObject *po : print.objects())
            if (po->model_object()->id() == object->id())
                print_object = po;
        if (print_object == nullptr)
            continue;
        const SlicingParameters &sp  = print_object->slicing_parameters();
        const double             top = sp.object_print_z_uncompensated_height();
        if (kind == "adaptive")
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
        print.is_BBL_printer() = args.printer.rfind("Bambu Lab", 0) == 0;
        for (ModelObject *object : model.objects)
            print.auto_assign_extruders(object);
        print.apply(model, config);
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

Model load_plate(const std::string &path)
{
    boost::nowide::ifstream in(path);
    const nlohmann::json    j = nlohmann::json::parse(in);
    const fs::path          dir = fs::path(path).parent_path();
    Model                   model;
    std::map<std::string, Model> loaded;
    for (const nlohmann::json &entry : j.at("objects")) {
        fs::path file = entry.at("file").get<std::string>();
        if (file.is_relative())
            file = dir / file;
        auto it = loaded.find(file.string());
        if (it == loaded.end())
            it = loaded.emplace(file.string(), Model::read_from_file(file.string(), nullptr, nullptr,
                                                                      LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances)).first;
        if (it->second.objects.empty())
            throw std::runtime_error("no objects in " + file.string());
        ModelObject *object = model.add_object(*it->second.objects.front());
        object->name = entry.value("name", file.stem().string());
        object->clear_instances();
        if (const int extruder = entry.value("extruder", 0); extruder > 0)
            object->config.set("extruder", extruder);
        const nlohmann::json places = entry.contains("instances") ? entry.at("instances") : nlohmann::json::array({ entry });
        for (const nlohmann::json &place : places) {
            ModelInstance *instance = object->add_instance();
            instance->set_rotation(Vec3d(0., 0., Geometry::deg2rad(place.value("rot", 0.))));
        }
        object->ensure_on_bed();
        for (size_t i = 0; i < places.size(); ++i) {
            const BoundingBoxf3 box = object->instance_bounding_box(i, true);
            object->instances[i]->set_offset(object->instances[i]->get_offset() +
                                             Vec3d(places[i].at("x").get<double>() - box.center().x(),
                                                   places[i].at("y").get<double>() - box.center().y(), 0.));
        }
    }
    if (model.objects.empty())
        throw std::runtime_error("no objects in " + path);
    return model;
}

Model load_project(const std::string &path)
{
    boost::nowide::ifstream in(path);
    const nlohmann::json    j   = nlohmann::json::parse(in);
    const fs::path          dir = fs::path(path).parent_path();
    auto resolve_path = [&dir](const std::string &file) { return fs::path(file).is_relative() ? (dir / file).string() : file; };
    auto read_model   = [](const std::string &file) {
        Model m = Model::read_from_file(file, nullptr, nullptr, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances);
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

void save_project(const Args &args, Model &model, DynamicPrintConfig &config, const fs::path &datadir)
{
    auto plate         = std::make_unique<PlateData>();
    plate->plate_index = 0;
    for (size_t i = 0; i < model.objects.size(); ++i)
        for (size_t j = 0; j < model.objects[i]->instances.size(); ++j)
            plate->objects_and_instances.emplace_back(int(i), int(j));
    model.set_backup_path((datadir / "backup").string());
    StoreParams params;
    params.path            = args.save_3mf;
    params.model           = &model;
    params.plate_data_list = { plate.get() };
    params.config          = &config;
    params.strategy        = SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::ShareMesh;
    if (! store_bbs_3mf(params))
        throw std::runtime_error("cannot write " + args.save_3mf);
}

}

#include "nonplanar_slice_json.ipp"

int main(int argc, char **argv)
{
    const Args args = parse_args(argc, argv);
    nlohmann::json record;
    if (const char *level = std::getenv("NP_LOG_LEVEL"))
        set_logging_level(unsigned(std::atoi(level)));
    try {
        if (! args.json.empty()) slice_json_record(record, args);
        set_resources_dir((fs::path(args.profiles) / "..").lexically_normal().string());
        const fs::path datadir = args.datadir.empty() ? fs::temp_directory_path() / fs::unique_path("np_slice_%%%%%%%%") :
                                                        fs::path(args.datadir);
        fs::create_directories(datadir);
        set_data_dir(datadir.string());
        set_temporary_dir(datadir.string());
        Nonplanar::HeadLibrary::instance().reload();

        PresetBundle       bundle;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.apply(resolve(bundle, args, "machine", Preset::TYPE_PRINTER, args.printer));
        config.apply(resolve(bundle, args, "process", Preset::TYPE_PRINT, args.process));
        apply_filaments(bundle, args, config);
        Model model;
        const bool project_input = boost::algorithm::iequals(fs::path(args.model).extension().string(), ".3mf");
        bool project_settings = false;
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
            for (ModelObject *object : model.objects)
                for (ModelInstance *instance : object->instances)
                    instance->use_loaded_id_for_label = true;
            release_PlateData_list(plates);
            for (Preset *preset : presets)
                delete preset;
        } else if (args.calib.empty()) {
            model = ! args.plate.empty()   ? load_plate(args.plate) :
                    ! args.project.empty() ? load_project(args.project) :
                Model::read_from_file(args.model, nullptr, nullptr, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances);
        }
        for (const auto &[key, value] : args.overrides)
            config.set_deserialize_strict(key, value);
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
        Nonplanar::HeadEnvelope calib_head;
        if (! args.calib.empty()) {
            PrintConfig printer_config;
            printer_config.apply(config, true);
            if (! args.calib_head.empty())
                printer_config.nonplanar_head.value = args.calib_head;
            calib_head  = Nonplanar::resolve_head_envelope(printer_config);
            calib_plate = Nonplanar::Calibration::make_plate(*Nonplanar::Calibration::parse_test(args.calib), config, calib_head);
            config.apply(calib_plate->config);
            for (const auto &[key, value] : args.overrides)
                config.set_deserialize_strict(key, value);
            Nonplanar::Calibration::add_to_model(*calib_plate, model);
        }
        if (model.objects.empty())
            throw std::runtime_error("no objects in " + args.model);
        apply_multi_material(args, model);
        if (! args.save_3mf.empty()) {
            place(args, model, config);
            save_project(args, model, config, datadir);
            std::cout << "project " << args.save_3mf << "\n";
            if (args.output.empty())
                return 0;
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

        if (!args.project_out.empty()) {
            model.set_backup_path((datadir / "project-backup").string());
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
            params.config = &config;
            params.strategy = SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::ShareMesh |
                              (project_input ? SaveStrategy::UseLoadedId : SaveStrategy::Default);
            if (!store_bbs_3mf(params))
                throw std::runtime_error("cannot write " + args.project_out);
        }

        Print print;
        print.is_BBL_printer() = (project_settings ? config.opt_string("printer_model") : args.printer).rfind("Bambu Lab", 0) == 0;
        for (ModelObject *object : model.objects)
            print.auto_assign_extruders(object);
        print.apply(model, config);
        apply_layer_profile(args, model, print, config);
        if (! args.json.empty()) {
            const auto head = Nonplanar::resolve_head_envelope(print.config());
            const std::string file = Nonplanar::HeadLibrary::instance().source_path(head.id);
            record["inputs"]["mode"] = config.opt_serialize("nonplanar_mode");
            record["inputs"]["zaa_enabled"] = config.opt_bool("zaa_enabled");
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
        const auto t_process = std::chrono::steady_clock::now();
        try {
            print.process();
        } catch (const SlicingErrors &errors) {
            throw std::runtime_error(print.slicing_errors_message(errors));
        }
        const auto t_export = std::chrono::steady_clock::now();
        GCodeProcessorResult result;
        const indexed_triangle_set mesh = plate_mesh(model);
        print.export_gcode(args.output, &result, [&mesh](const ThumbnailsParams &params) {
            ThumbnailsList list;
            for (const Vec2d &size : params.sizes)
                list.push_back(render_thumbnail(mesh, unsigned(size.x()), unsigned(size.y()), false));
            return list;
        });
        const auto t_done = std::chrono::steady_clock::now();

        if (args.explain)
            for (size_t i = 0; i < print.objects().size(); ++i) {
                std::cout << "explain object " << print.objects()[i]->model_object()->name << "\n";
                for (const Nonplanar::ReportPatch &patch : result.nonplanar_report.patches)
                    if (patch.object == i && patch.top)
                        std::cout << patch.explain << "\n";
            }

        size_t     vz_line         = 0;
        const auto [vz, vz_actual] = max_vz(result, &vz_line);
        const auto &normal         = result.print_statistics.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)];
        std::cout << "time_process_s " << std::chrono::duration<double>(t_export - t_process).count() << "\n"
                  << "time_export_s " << std::chrono::duration<double>(t_done - t_export).count() << "\n"
                  << "gcode " << args.output << "\n"
                  << "estimated_time_s " << normal.time << "\n"
                  << "max_vz_commanded " << vz << "\n"
                  << "max_vz_planned " << vz_actual << "\n"
                  << "max_vz_planned_line " << vz_line << "\n"
                  << "machine_max_speed_z " << config.opt<ConfigOptionFloats>("machine_max_speed_z")->get_at(0) << "\n";
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
        if (args.verify && !result.clearance_report.ran) {
            ClearanceParams params;
            params.head               = Nonplanar::resolve_head_envelope(print.config());
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
        const ClearanceReport &clearance = result.clearance_report;
        std::cout << "clearance_checked " << clearance.ran << "\n";
        if (clearance.ran) {
            std::cout << "clearance_hit_moves " << clearance.hit_moves << "\n"
                      << "clearance_machine_hit_moves " << clearance.machine_hit_moves << "\n"
                      << "clearance_min_mm " << clearance.min_clearance << "\n";
            for (const ClearanceHit &hit : clearance.worst)
                std::cout << "clearance_hit line " << hit.gcode_line << " " << hit.component << " " << hit.penetration << " mm\n";
            for (const std::string &omission : clearance.omissions)
                std::cout << "clearance_omission " << omission << "\n";
        }
        if (! args.json.empty()) {
            auto &text = record["text_summary"];
            text["clearance_checked"] = text_value(clearance.ran);
            if (clearance.ran) {
                text["clearance_hit_moves"] = text_value(clearance.hit_moves);
                text["clearance_machine_hit_moves"] = text_value(clearance.machine_hit_moves);
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
            write_gcode_3mf(args, model, print, config, result, datadir);
            std::cout << "gcode_3mf " << args.gcode_3mf << "\n";
        }
        const Nonplanar::NonplanarReport report = result.nonplanar_report.objects.empty() ? Nonplanar::make_report(print, result, true) :
                                                                                           result.nonplanar_report;
        for (size_t i = 0; i < report.objects.size(); ++i) {
            const Nonplanar::ReportObject &object = report.objects[i];
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
                          << " on_air " << b.share[3] << " span " << b.span << " reach_p50 " << b.reach_p50 << " solid_min " << b.solid_min
                          << " solid_p50 " << b.solid_p50 << " slope_p90 " << b.slope_p90 << " sag_rms " << b.sag_rms << " sag_max " << b.sag_max
                          << " solid_layers " << b.solid_layers << " bridged " << b.bridged << "\n";
            }
            for (const Nonplanar::ReportFallback &f : report.fallbacks)
                if (f.object == i)
                    std::cout << "nonplanar_fallback " << f.reason << " " << f.area << " patch " << f.patch << "\n";
        }
        if (! args.json.empty()) {
            slice_json_results(record, args, print, result, model, config);
            record["status"] = "sliced";
            write_slice_json(args.json, record);
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
