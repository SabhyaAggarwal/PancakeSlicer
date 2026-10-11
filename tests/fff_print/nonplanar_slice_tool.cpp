#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/GCode/ClearanceVerifier.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Point.hpp"
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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
namespace fs = boost::filesystem;

namespace {

struct Args
{
    std::string profiles = std::string(PROFILES_DIR);
    std::string vendor   = "Prusa";
    std::string printer, process, filament, datadir, output, model, config_out;
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
        else if (arg == "--filament") a.filament = next();
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
    if (a.printer.empty() || a.process.empty() || a.filament.empty() || a.output.empty() || a.model.empty())
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
    const std::string  file = find_preset_file(a, kind, name);
    DynamicPrintConfig config;
    std::string        error;
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
        config.apply(resolve(bundle, args, "filament", Preset::TYPE_FILAMENT, args.filament));
        for (const auto &[key, value] : args.overrides)
            config.set_deserialize_strict(key, value);
        if (! args.config_out.empty())
            config.save(args.config_out);

        Model model = Model::read_from_file(args.model, nullptr, nullptr, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances);
        if (model.objects.empty())
            throw std::runtime_error("no objects in " + args.model);
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
