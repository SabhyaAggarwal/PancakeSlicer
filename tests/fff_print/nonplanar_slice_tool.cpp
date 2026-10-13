#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

#include "admesh/stl.h"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/FlushVolCalc.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/GCode/ClearanceVerifier.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/GCode/WipeTowerEstimate.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Nonplanar/ConformalShells.hpp"
#include "libslic3r/Nonplanar/FlowMod.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"

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
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
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
    std::string printer, process, datadir, output, model, config_out, paint, gcode_3mf;
    std::vector<std::string> filaments, colours;
    std::vector<std::pair<std::string, int>> parts;
    std::vector<std::pair<std::string, std::string>> overrides;
    bool        center_set = false;
    bool        verify     = false;
    Vec2d       center     = Vec2d::Zero();
};

[[noreturn]] void usage(const std::string &error)
{
    std::cerr << "error: " << error << "\n"
              << "usage: nonplanar_slice_tool --printer NAME --process NAME --filament NAME [--vendor Prusa]\n"
                 "       [--profiles DIR] [--datadir DIR] [--set key=value]... [--center X,Y]\n"
                 "       [--config-out FILE] [--gcode-3mf FILE] [--verify] -o OUT.gcode MODEL\n";
    std::exit(2);
}

Args parse_args(int argc, char **argv)
{
    Args a;
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
        else if (arg == "--vendor") a.vendor = next();
        else if (arg == "--profiles") a.profiles = next();
        else if (arg == "--datadir") a.datadir = next();
        else if (arg == "--config-out") a.config_out = next();
        else if (arg == "--gcode-3mf") a.gcode_3mf = next();
        else if (arg == "--verify") a.verify = true;
        else if (arg == "-o" || arg == "--output") a.output = next();
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
    if (a.printer.empty() || a.process.empty() || a.filaments.empty() || a.output.empty() || a.model.empty())
        usage("--printer, --process, --filament, -o and a model are required");
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
    for (size_t i = 1; i < result.moves.size(); ++i) {
        const auto  &m = result.moves[i];
        const Vec3d  d = (m.position - result.moves[i - 1].position).cast<double>();
        const double l = d.norm();
        if (l < 1e-6 || (m.type != EMoveType::Extrude && m.type != EMoveType::Travel))
            continue;
        commanded = std::max(commanded, std::abs(d.z()) / l * m.feedrate);
        if (const double v = std::abs(d.z()) / l * m.actual_feedrate; v > actual) {
            actual = v;
            if (actual_line)
                *actual_line = m.gcode_id;
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

}

int main(int argc, char **argv)
{
    const Args args = parse_args(argc, argv);
    if (const char *level = std::getenv("NP_LOG_LEVEL"))
        set_logging_level(unsigned(std::atoi(level)));
    try {
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
        for (const auto &[key, value] : args.overrides)
            config.set_deserialize_strict(key, value);
        if (! args.config_out.empty())
            config.save(args.config_out);

        Model model = Model::read_from_file(args.model, nullptr, nullptr, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances);
        if (model.objects.empty())
            throw std::runtime_error("no objects in " + args.model);
        apply_multi_material(args, model);
        Vec2d center = args.center;
        if (! args.center_set) {
            const BoundingBoxf bed(config.opt<ConfigOptionPoints>("printable_area")->values);
            center = bed.center();
        }
        for (ModelObject *object : model.objects)
            object->ensure_on_bed();
        model.center_instances_around_point(center);
        if (std::none_of(args.overrides.begin(), args.overrides.end(), [](const auto &kv) { return kv.first.rfind("wipe_tower_", 0) == 0; }))
            place_wipe_tower(config, model, args.filaments.size());

        Print print;
        print.is_BBL_printer() = args.printer.rfind("Bambu Lab", 0) == 0;
        for (ModelObject *object : model.objects)
            print.auto_assign_extruders(object);
        print.apply(model, config);
        std::vector<StringObjectException> warnings;
        if (const StringObjectException err = print.validate(&warnings); ! err.string.empty())
            throw std::runtime_error("validate: " + err.string);
        for (const StringObjectException &w : warnings)
            std::cerr << "warning: " << w.string << "\n";
        const auto t_process = std::chrono::steady_clock::now();
        print.process();
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
        for (const auto &warning : print.step_state_with_warnings(psGCodeExport).warnings)
            std::cout << "export_warning " << warning.message << "\n";
        Nonplanar::FlowModStats flow;
        for (const PrintObject *object : print.objects())
            flow.merge(object->conformal().flow_mod);
        if (flow.points > 0)
            std::cout << "flow_points " << flow.points << "\n"
                      << "flow_normal_gap_mm " << flow.min_gap << " " << flow.max_gap << "\n"
                      << "flow_laid_mm " << flow.min_laid << " " << flow.max_laid << "\n"
                      << "flow_clamped thick " << flow.max_clamped << " thin " << flow.min_clamped << " dragged " << flow.dragged
                      << " contact " << flow.contact << "\n";
        double np_extrude = 0., np_other = 0.;
        std::map<std::string, double> np_kind;
        for (size_t i = 0; i < result.moves.size(); ++i) {
            const auto &m = result.moves[i];
            if (m.nonplanar_patch_shell == 0xFF)
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
        std::map<std::string, double> role_time;
        for (const auto &m : result.moves)
            role_time[m.type == EMoveType::Extrude ? ExtrusionEntity::role_to_string(m.extrusion_role) : std::string("Travel and other")] +=
                m.time[size_t(PrintEstimatedStatistics::ETimeMode::Normal)];
        for (const auto &[role, t] : role_time)
            std::cout << "role_time_s " << t << " " << role << "\n";
        std::map<std::string, double> feature_time;
        for (size_t i = 0; i < result.moves.size(); ++i) {
            const auto &m  = result.moves[i];
            const bool  np = m.nonplanar_patch_shell != 0xFF;
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
        if (args.verify && !result.clearance_report.ran) {
            ClearanceParams params;
            params.head               = Nonplanar::resolve_head_envelope(print.config());
            result.clearance_report   = verify_clearance(result, params, *make_tool_kinematics(print.config()));
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
        if (! args.gcode_3mf.empty()) {
            write_gcode_3mf(args, model, print, config, result, datadir);
            std::cout << "gcode_3mf " << args.gcode_3mf << "\n";
        }
        for (const PrintObject *object : print.objects()) {
            const Nonplanar::ConformalResult &conformal = object->conformal();
            size_t top = 0, bands = 0;
            double footprint = 0.;
            for (const Nonplanar::ConformalPatch &patch : conformal.patches)
                if (patch.kind == Nonplanar::PatchKind::Top) {
                    ++top;
                    bands     = std::max(bands, size_t(patch.band + 1));
                    footprint += area(patch.footprint) * SCALING_FACTOR * SCALING_FACTOR;
                }
            std::cout << "nonplanar_top_patches " << top << "\n"
                      << "nonplanar_bands " << bands << "\n"
                      << "nonplanar_top_footprint_mm2 " << footprint << "\n";
            for (const Nonplanar::PatchDiagnostics &diag : conformal.diagnostics)
                if (diag.fallback_area > 0.)
                    std::cout << "nonplanar_fallback " << (diag.fallback_reason.empty() ? "untagged" : diag.fallback_reason) << " "
                              << diag.fallback_area << " patch " << diag.patch_id << "\n";
        }
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
