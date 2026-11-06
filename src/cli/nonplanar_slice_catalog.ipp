#pragma once

#include "libslic3r/Config.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <initializer_list>
#include <ios>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace {
void write_catalog(const std::string &path, const nlohmann::json &catalog)
{
    boost::nowide::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << catalog.dump(2) << '\n';
    out.close();
    if (!out) throw std::runtime_error("cannot write catalog " + path);
}

std::string option_type_name(ConfigOptionType type)
{
    switch (type) {
    case coNone: return "none";
    case coFloat: return "float"; case coFloats: return "floats";
    case coInt: return "int"; case coInts: return "ints";
    case coString: return "string"; case coStrings: return "strings";
    case coPercent: return "percent"; case coPercents: return "percents";
    case coFloatOrPercent: return "float_or_percent"; case coFloatsOrPercents: return "floats_or_percents";
    case coPoint: return "point"; case coPoints: return "points"; case coPoint3: return "point3";
    case coBool: return "bool"; case coBools: return "bools";
    case coEnum: return "enum"; case coEnums: return "enums";
    case coPointsGroups: return "points_groups"; case coIntsGroups: return "ints_groups";
    default: return "unknown";
    }
}
void dump_options(const std::string &path)
{
    nlohmann::json options = nlohmann::json::array();
    for (const auto &[key, def] : print_config_def.options)
        options.push_back({{"key", key}, {"type", option_type_name(def.type)}, {"type_id", int(def.type)},
            {"label", def.label}, {"full_label", def.full_label}, {"tooltip", def.tooltip},
            {"category", def.category}, {"sidetext", def.sidetext}, {"enum_values", def.enum_values},
            {"enum_labels", def.enum_labels}, {"min", def.min}, {"max", def.max}, {"mode", int(def.mode)},
            {"default", def.default_value ? def.default_value->serialize() : ""},
            {"nullable", def.nullable}, {"readonly", def.readonly}, {"cli", def.cli}});
    write_catalog(path, {{"schema", "orca.options.v1"}, {"options", options}});
}

nlohmann::json read_catalog_json(const fs::path &path)
{
    boost::nowide::ifstream in(path.string());
    if (!in) throw std::runtime_error("cannot read " + path.string());
    return nlohmann::json::parse(in);
}

void dump_presets(const Args &args)
{

    PresetBundle library, bundle;
    const auto rule = ForwardCompatibilitySubstitutionRule::EnableSilent;
    if (fs::is_regular_file(fs::path(args.profiles) / "OrcaFilamentLibrary.json"))
        library.load_vendor_configs_from_json(args.profiles, "OrcaFilamentLibrary", PresetBundle::LoadSystem, rule, nullptr, false);
    bundle.load_vendor_configs_from_json(args.profiles, args.vendor, PresetBundle::LoadSystem, rule,
        args.vendor == "OrcaFilamentLibrary" ? nullptr : &library, false);
    if (library.error_count() || bundle.error_count()) throw std::runtime_error("invalid vendor preset bundle");
    const auto manifest = read_catalog_json(fs::path(args.profiles) / (args.vendor + ".json"));

    std::map<std::string, nlohmann::json> sources;
    auto collect = [&](const std::string &vendor) {
        const auto index = read_catalog_json(fs::path(args.profiles) / (vendor + ".json"));
        for (const char *list : {"machine_list", "process_list", "filament_list"})
            if (index.contains(list)) for (const auto &entry : index.at(list)) {
                auto source = read_catalog_json(fs::path(args.profiles) / vendor / entry.at("sub_path").get<std::string>());

                std::string name = source.at("name").get<std::string>();
                sources[std::move(name)] = std::move(source);
            }
    };
    if (fs::is_regular_file(fs::path(args.profiles) / "OrcaFilamentLibrary.json")) collect("OrcaFilamentLibrary");
    collect(args.vendor);
    nlohmann::json catalog = {{"schema", "orca.presets.v1"}, {"vendor", args.vendor},
        {"name", manifest.value("name", args.vendor)}, {"version", manifest.value("version", "")},
        {"machine", nlohmann::json::array()}, {"process", nlohmann::json::array()},
        {"filament", nlohmann::json::array()}, {"printer_models", nlohmann::json::array()}};
    for (const auto &[kind, collection] : std::initializer_list<std::pair<const char *, const PresetCollection *>>{
            {"machine", &bundle.printers}, {"process", &bundle.prints}, {"filament", &bundle.filaments}}) {
        for (const Preset &preset : collection->get_presets()) {
            if (preset.is_default) continue;
            nlohmann::json values = nlohmann::json::object(), chain = nlohmann::json::array();
            for (const auto &key : preset.config.keys()) values[key] = preset.config.opt_serialize(key);
            auto source = sources.find(preset.name);
            std::string parent = source == sources.end() ? preset.inherits() : source->second.value("inherits", "");
            std::vector<std::string> seen;
            while (!parent.empty()) {
                if (std::find(seen.begin(), seen.end(), parent) != seen.end()) throw std::runtime_error("cyclic preset chain " + parent);
                seen.push_back(parent); chain.push_back(parent);
                const auto it = sources.find(parent);
                if (it == sources.end()) throw std::runtime_error("missing preset parent " + parent);
                parent = it->second.value("inherits", "");
            }
            nlohmann::json entry = {{"name", preset.name}, {"type", kind}, {"inherits", chain},
                {"visible", preset.is_visible}, {"instantiation", true}, {"values", values},
                {"compatible_printers", values.value("compatible_printers", "")},
                {"compatible_printers_condition", values.value("compatible_printers_condition", "")}};

            if (collection == &bundle.printers) {
                PrintConfig printer;
                printer.apply(preset.config, true);
                entry["native_support_capable"] = Nonplanar::native_support_capable(printer);
            }
            if (collection == &bundle.filaments) {
                entry["filament_id"] = preset.filament_id;
                entry["setting_id"] = preset.setting_id;
            }


            if (collection != &bundle.printers) {
                const PresetWithVendorProfile self = collection->get_preset_with_vendor_profile(preset);
                nlohmann::json printers = nlohmann::json::array(), prints = nlohmann::json::array();
                for (const Preset &printer : bundle.printers.get_presets())
                    if (!printer.is_default && is_compatible_with_printer(self, bundle.printers.get_preset_with_vendor_profile(printer)))
                        printers.push_back(printer.name);
                entry["compatible_printers_evaluated"] = std::move(printers);
                if (collection == &bundle.filaments) {
                    for (const Preset &print : bundle.prints.get_presets())
                        if (!print.is_default && is_compatible_with_print(self, bundle.prints.get_preset_with_vendor_profile(print),
                                                                          bundle.printers.get_edited_preset_with_vendor_profile()))
                            prints.push_back(print.name);
                    entry["compatible_prints_evaluated"] = std::move(prints);
                }
            }
            catalog[kind].push_back(std::move(entry));
        }
    }
    if (manifest.contains("machine_model_list")) for (const auto &entry : manifest.at("machine_model_list"))
        catalog["printer_models"].push_back(read_catalog_json(fs::path(args.profiles) / args.vendor / entry.at("sub_path").get<std::string>()));
    write_catalog(args.dump_presets, catalog);
}
}
