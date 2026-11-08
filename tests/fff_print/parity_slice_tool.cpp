#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"
#include <boost/filesystem/directory.hpp>
#include <cmath>
#include "libslic3r/PrintBase.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/FlushVolCalc.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
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
    std::string vendor, printer, process, output, model, plate, config_out, project_out, defaults_out;
    std::vector<std::string> filaments, colours;
    std::vector<std::pair<std::string, std::string>> overrides;
};

[[noreturn]] void usage(const std::string &error)
{
    std::cerr << "error: " << error << "\n"
              << "usage: parity_slice_tool --vendor V --printer NAME --process NAME --filament NAME... [--profiles DIR]\n"
                 "       [--set key=value]... [--colours #RRGGBB,...] [--config-out FILE] [--project-out FILE] [--defaults-out FILE]\n"
                 "       -o OUT.gcode (MODEL | --plate PLATE.json)\n";
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
        else if (arg == "--vendor") a.vendor = next();
        else if (arg == "--profiles") a.profiles = next();
        else if (arg == "--config-out") a.config_out = next();
        else if (arg == "--project-out") a.project_out = next();
        else if (arg == "--defaults-out") a.defaults_out = next();
        else if (arg == "--plate") a.plate = next();
        else if (arg == "-o") a.output = next();
        else if (arg == "--colours") {
            const std::string list = next();
            for (size_t pos = 0; pos <= list.size();) {
                const size_t end = std::min(list.find(',', pos), list.size());
                a.colours.push_back(list.substr(pos, end - pos));
                pos = end + 1;
            }
        } else if (arg == "--set") {
            const std::string kv  = next();
            const size_t      pos = kv.find('=');
            if (pos == std::string::npos)
                usage("--set expects key=value, got " + kv);
            a.overrides.emplace_back(kv.substr(0, pos), kv.substr(pos + 1));
        } else if (!arg.empty() && arg[0] == '-')
            usage("unknown option " + arg);
        else
            a.model = arg;
    }
    if (a.vendor.empty() || a.printer.empty() || a.process.empty() || a.filaments.empty() || a.output.empty() ||
        a.model.empty() == a.plate.empty())
        usage("--vendor, --printer, --process, --filament, -o and a model or --plate are required");
    return a;
}

std::string find_preset_file(const Args &a, const char *kind, const std::string &name)
{
    std::vector<fs::path> dirs = { fs::path(a.profiles) / a.vendor / kind };
    if (std::string(kind) == "filament")
        dirs.push_back(fs::path(a.profiles) / "OrcaFilamentLibrary" / kind);
    for (const fs::path &dir : dirs) {
        if (!fs::is_directory(dir))
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
    const std::string  file = find_preset_file(a, kind, name);
    if (!bundle.resolve_preset_config(config, type, file, ForwardCompatibilitySubstitutionRule::EnableSilent, error))
        throw std::runtime_error("cannot resolve " + file + ": " + error);
    return config;
}

void apply_filaments(PresetBundle &bundle, const Args &a, DynamicPrintConfig &config)
{
    const size_t                    n = a.filaments.size();
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
    config.option<ConfigOptionInts>("filament_map", true)->values.assign(n, 1);
    auto rgb = [](const std::string &hex, unsigned char out[3]) {
        for (int c = 0; c < 3; ++c)
            out[c] = (unsigned char) std::strtol(hex.substr(1 + 2 * c, 2).c_str(), nullptr, 16);
    };
    const auto        *volume  = config.option<ConfigOptionFloats>("nozzle_volume");
    const auto        *dataset = config.option<ConfigOptionInts>("nozzle_flush_dataset");
    FlushVolCalculator calculator(volume != nullptr && !volume->values.empty() ? int(volume->values.front()) : 0, g_max_flush_volume,
                                  dataset != nullptr && !dataset->values.empty() ? dataset->values.front() : 0);
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

Model load_plate(const std::string &path)
{
    boost::nowide::ifstream      in(path);
    const nlohmann::json         j   = nlohmann::json::parse(in);
    const fs::path               dir = fs::path(path).parent_path();
    Model                        model;
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
        object->name        = entry.value("name", file.stem().string());
        object->clear_instances();
        if (const int extruder = entry.value("extruder", 0); extruder > 0)
            object->config.set("extruder", extruder);
        const nlohmann::json places = entry.contains("instances") ? entry.at("instances") : nlohmann::json::array({ entry });
        for (const nlohmann::json &place : places)
            object->add_instance()->set_rotation(Vec3d(0., 0., Geometry::deg2rad(place.value("rot", 0.))));
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

void place_wipe_tower(DynamicPrintConfig &config)
{
    double x = 165., y = 250.;
    if (const auto *structure = config.option<ConfigOptionEnum<PrinterStructure>>("printer_structure");
        structure != nullptr && structure->value == PrinterStructure::psI3)
        x = 0.;
    const double   width = config.opt_float("prime_tower_width"), depth = 60., margin = 15.;
    const Polygons bed{ Polygon::new_scale(config.opt<ConfigOptionPoints>("printable_area")->values) };
    const Vec2f    move = WipeTower::move_box_inside_polygon(BoundingBox(Point::new_scale(x, y), Point::new_scale(x + width, y + depth)),
                                                             bed, scaled<coord_t>(margin));
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
        const fs::path datadir = fs::temp_directory_path() / fs::unique_path("parity_%%%%%%%%");
        fs::create_directories(datadir);
        set_data_dir(datadir.string());
        set_temporary_dir(datadir.string());

        PresetBundle       bundle;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.apply(resolve(bundle, args, "machine", Preset::TYPE_PRINTER, args.printer));
        config.apply(resolve(bundle, args, "process", Preset::TYPE_PRINT, args.process));
        apply_filaments(bundle, args, config);
        for (const auto &[key, value] : args.overrides)
            config.set_deserialize_strict(key, value);

        Model model = args.plate.empty() ?
            Model::read_from_file(args.model, nullptr, nullptr, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances) :
            load_plate(args.plate);
        if (model.objects.empty())
            throw std::runtime_error("no objects in " + args.model);
        if (args.plate.empty()) {
            for (ModelObject *object : model.objects)
                object->ensure_on_bed();
            model.center_instances_around_point(BoundingBoxf(config.opt<ConfigOptionPoints>("printable_area")->values).center());
        }
        if (std::none_of(args.overrides.begin(), args.overrides.end(), [](const auto &kv) { return kv.first.rfind("wipe_tower_", 0) == 0; }))
            place_wipe_tower(config);
        if (!args.config_out.empty())
            config.save(args.config_out);
        if (!args.defaults_out.empty())
            DynamicPrintConfig::full_print_config().save(args.defaults_out);

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
            params.path            = args.project_out;
            params.model           = &model;
            params.config          = &config;
            params.strategy        = SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::ShareMesh;
            if (!store_bbs_3mf(params))
                throw std::runtime_error("cannot write " + args.project_out);
        }

        Print print;
        print.is_BBL_printer() = args.vendor == "BBL";
        for (ModelObject *object : model.objects)
            print.auto_assign_extruders(object);
        print.apply(model, config);
        std::vector<StringObjectException> warnings;
        if (const StringObjectException err = print.validate(&warnings); !err.string.empty())
            throw std::runtime_error("validate: " + err.string);
        try {
            print.process();
        } catch (const SlicingErrors &errors) {
            throw std::runtime_error(print.slicing_errors_message(errors));
        }
        GCodeProcessorResult result;
        print.export_gcode(args.output, &result, nullptr);
        std::cout << "estimated_time_s " << result.print_statistics.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)].time << "\n";
        fs::remove_all(datadir);
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
