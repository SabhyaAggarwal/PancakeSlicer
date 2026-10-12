#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/FlushVolCalc.hpp"
#include "libslic3r/GCode/ClearanceVerifier.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Model.hpp"
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
#include <cmath>
#include <cstdio>
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
    std::string vendor   = "Prusa";
    std::string printer, process, datadir, output, model, config_out, paint;
    std::vector<std::string> filaments, colours;
    std::vector<std::pair<std::string, int>> parts;
    std::vector<std::pair<std::string, std::string>> overrides;
    bool        center_set = false;
    Vec2d       center     = Vec2d::Zero();
};

[[noreturn]] void usage(const std::string &error)
{
    std::cerr << "error: " << error << "\n"
              << "usage: nonplanar_slice_tool --printer NAME --process NAME --filament NAME [--vendor Prusa]\n"
                 "       [--profiles DIR] [--datadir DIR] [--set key=value]... [--center X,Y]\n"
                 "       [--config-out FILE] -o OUT.gcode MODEL\n";
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

std::pair<double, double> max_vz(const GCodeProcessorResult &result)
{
    double commanded = 0., actual = 0.;
    for (size_t i = 1; i < result.moves.size(); ++i) {
        const auto  &m = result.moves[i];
        const Vec3d  d = (m.position - result.moves[i - 1].position).cast<double>();
        const double l = d.norm();
        if (l < 1e-6 || (m.type != EMoveType::Extrude && m.type != EMoveType::Travel))
            continue;
        commanded = std::max(commanded, std::abs(d.z()) / l * m.feedrate);
        actual    = std::max(actual, std::abs(d.z()) / l * m.actual_feedrate);
    }
    return { commanded, actual };
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

}

int main(int argc, char **argv)
{
    const Args args = parse_args(argc, argv);
    try {
        set_resources_dir((fs::path(args.profiles) / "..").lexically_normal().string());
        const fs::path datadir = args.datadir.empty() ? fs::temp_directory_path() / fs::unique_path("np_slice_%%%%%%%%") :
                                                        fs::path(args.datadir);
        fs::create_directories(datadir);
        set_data_dir(datadir.string());
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
        print.process();
        GCodeProcessorResult result;
        print.export_gcode(args.output, &result, nullptr);

        const auto [vz, vz_actual] = max_vz(result);
        const auto &normal         = result.print_statistics.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)];
        std::cout << "gcode " << args.output << "\n"
                  << "estimated_time_s " << normal.time << "\n"
                  << "max_vz_commanded " << vz << "\n"
                  << "max_vz_planned " << vz_actual << "\n"
                  << "machine_max_speed_z " << config.opt<ConfigOptionFloats>("machine_max_speed_z")->get_at(0) << "\n";
        for (const auto &warning : print.step_state_with_warnings(psGCodeExport).warnings)
            std::cout << "export_warning " << warning.message << "\n";
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
        const ClearanceReport &clearance = result.clearance_report;
        std::cout << "clearance_checked " << clearance.ran << "\n";
        if (clearance.ran) {
            std::cout << "clearance_hit_moves " << clearance.hit_moves << "\n"
                      << "clearance_min_mm " << clearance.min_clearance << "\n";
            for (const ClearanceHit &hit : clearance.worst)
                std::cout << "clearance_hit line " << hit.gcode_line << " " << hit.component << " " << hit.penetration << " mm\n";
            for (const std::string &omission : clearance.omissions)
                std::cout << "clearance_omission " << omission << "\n";
        }
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
