#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

#include <vector>
#include <boost/filesystem/path.hpp>
#include <map>
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <utility>
#include "libslic3r/CustomGCode.hpp"
#include <iterator>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/log/core/record_view.hpp>
#include <boost/log/expressions/message.hpp>
#include <cstddef>
#include <exception>
#include "libslic3r/PlaceholderParser.hpp"
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/errors.hpp>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/GCode.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/GCode/WipeTowerEstimate.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Utils.hpp"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/core/null_deleter.hpp>
#include <boost/make_shared.hpp>
#include <boost/program_options.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Format/STEP.hpp"

namespace fs = boost::filesystem;

using namespace Slic3r;
namespace po = boost::program_options;

void generate_custom_presets(PresetBundle* preset_bundle, AppConfig& app_config)
{
    struct cus_preset
    {
        std::string name;
        std::string parent_name;
    };
    auto createCustomPrinters = [&](Preset::Type type) {
        std::vector<cus_preset> custom_preset;
        PresetCollection*                            collection = nullptr;
        if (type == Preset::TYPE_PRINT)
            collection = &preset_bundle->prints;
        else if (type == Preset::TYPE_FILAMENT)
            collection = &preset_bundle->filaments;
        else if (type == Preset::TYPE_PRINTER)
            collection = &preset_bundle->printers;
        else
            return;
        custom_preset.reserve(collection->size());
        for (auto& parent : collection->get_presets()) {
            if (!parent.is_system)
                continue;
            auto new_name = parent.name + "_orca_test";
            if (parent.vendor)
                new_name = parent.vendor->name + "_" + new_name;
            custom_preset.push_back({new_name, parent.name});
        }
        for (auto p : custom_preset) {
            auto parent = collection->find_preset(p.parent_name);
            auto vendor = collection->get_preset_with_vendor_profile(*parent);
            if (type == Preset::TYPE_FILAMENT) {
                parent->config.set_key_value("filament_start_gcode",
                                             new ConfigOptionStrings({"this_is_orca_test_filament_start_gcode_mock"}));
                parent->config.set_key_value("filament_notes", new ConfigOptionString(vendor.vendor->name));
            } else if (type == Preset::TYPE_PRINT) {
                parent->config.set_key_value("filename_format", new ConfigOptionString("this_is_orca_test_filename_format_mock"));
                parent->config.set_key_value("notes", new ConfigOptionString(vendor.vendor->name));
            } else if (type == Preset::TYPE_PRINTER) {
                parent->config.set_key_value("machine_start_gcode", new ConfigOptionString("this_is_orca_test_machine_start_gcode_mock"));
                parent->config.set_key_value("printer_notes", new ConfigOptionString(vendor.vendor->name));
            }

            collection->save_current_preset(p.name, false, false, parent);

        }
    };
    createCustomPrinters(Preset::TYPE_PRINTER);
    createCustomPrinters(Preset::TYPE_FILAMENT);
    createCustomPrinters(Preset::TYPE_PRINT);

    std::string       user_sub_folder  = DEFAULT_USER_FOLDER_NAME;
    const std::string dir_user_presets = data_dir() + "/" + PRESET_USER_DIR + "/" + user_sub_folder;

    fs::path user_folder(data_dir() + "/" + PRESET_USER_DIR);
    if (!fs::exists(user_folder))
        fs::create_directory(user_folder);

    fs::path folder(dir_user_presets);
    if (!fs::exists(folder))
        fs::create_directory(folder);
    std::map<std::string, std::string> need_to_delete_list;

    preset_bundle->prints.save_user_presets(dir_user_presets, PRESET_PRINT_NAME, need_to_delete_list);
    preset_bundle->filaments.save_user_presets(dir_user_presets, PRESET_FILAMENT_NAME, need_to_delete_list);
    preset_bundle->printers.save_user_presets(dir_user_presets, PRESET_PRINTER_NAME, need_to_delete_list);

    std::cout << "Custom presets generated successfully" << std::endl;
}

namespace {

Vec2d printable_area_center(const DynamicPrintConfig &cfg)
{
    const auto *opt = cfg.option<ConfigOptionPoints>("printable_area");
    if (opt == nullptr || opt->values.empty())
        return Vec2d(100., 100.);
    Vec2d lo = opt->values.front(), hi = opt->values.front();
    for (const Vec2d &p : opt->values) { lo = lo.cwiseMin(p); hi = hi.cwiseMax(p); }
    return 0.5 * (lo + hi);
}

Vec2d place_wipe_tower(DynamicPrintConfig &cfg, const Vec2d &center)
{
    const auto *area = cfg.option<ConfigOptionPoints>("printable_area");
    if (area == nullptr || area->values.size() < 3)
        return Vec2d::Zero();
    const WipeTowerFootprint footprint = estimate_wipe_tower_footprint(cfg, resolve_wipe_tower_type(cfg), {0, 1}, cfg.opt_float("layer_height"), 10.);
    if (footprint.depth < EPSILON)
        return Vec2d::Zero();
    const double margin = WIPE_TOWER_MARGIN + footprint.brim_width;
    Slic3r::Polygon box({Point::new_scale(0., 0.), Point::new_scale(footprint.width, 0.), Point::new_scale(footprint.width, footprint.depth), Point::new_scale(0., footprint.depth)});
    box.rotate(Geometry::deg2rad(cfg.opt_float("wipe_tower_rotation_angle")));
    const BoundingBox local = get_extents(box);
    const Vec2d       lo    = unscale(local.min);
    const Vec2d       size  = unscale(local.max) - lo;
    Vec2d             pos(center.x() + 5. + margin + 5. - lo.x(), center.y() - size.y() / 2. - lo.y());
    box.translate(Point::new_scale(pos.x(), pos.y()));
    const Polygons    bed{Polygon::new_scale(area->values)};
    const BoundingBox tower = get_extents(box);
    BoundingBox       pair  = tower;
    pair.merge(Point::new_scale(center.x() - 5., center.y() - 5.));
    pair.merge(Point::new_scale(center.x() + 5., center.y() + 5.));
    const Point       room  = get_extents(bed).size() - Point::new_scale(2. * margin, 2. * margin);
    const bool        rigid = pair.size().x() < room.x() && pair.size().y() < room.y();
    const Vec2d       move  = WipeTower::move_box_inside_polygon(rigid ? pair : tower, bed, scaled<coord_t>(margin)).cast<double>();
    pos += move;
    cfg.option<ConfigOptionFloats>("wipe_tower_x", true)->values = {pos.x()};
    cfg.option<ConfigOptionFloats>("wipe_tower_y", true)->values = {pos.y()};
    return rigid ? move : Vec2d::Zero();
}

std::string slice_two_color_cube_and_export(DynamicPrintConfig cfg, bool is_bbl, bool by_object)
{
    const Vec2d        center = printable_area_center(cfg);
    const bool         belt   = cfg.opt_bool("belt_printer");
    std::vector<Vec2d> cube_mins;
    if (by_object) {
        cfg.set_key_value("print_sequence", new ConfigOptionEnum<PrintSequence>(PrintSequence::ByObject));
        cfg.set_key_value("enable_prime_tower", new ConfigOptionBool(false));
        cfg.set_key_value("enable_wrapping_detection", new ConfigOptionBool(false));
        cfg.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(tlTraditional));
        cfg.set_key_value("skirt_loops", new ConfigOptionInt(0));
        cube_mins = {center + Vec2d(-20., -5.), center + Vec2d(10., -5.)};
    } else if (belt) {
        cube_mins = {center - Vec2d(5., 15.), center + Vec2d(-5., 5.)};
    } else {
        if (!cfg.opt_string("wrapping_detection_gcode").empty())
            cfg.set_key_value("enable_wrapping_detection", new ConfigOptionBool(true));
        cube_mins = {center - Vec2d(5., 5.) + place_wipe_tower(cfg, center)};
    }

    Model  model;
    Print  print;
    print.is_BBL_printer() = is_bbl;
    for (const Vec2d &cube_min : cube_mins) {
        TriangleMesh m = make_cube(10, 10, 10);
        m.translate(static_cast<float>(cube_min.x()), static_cast<float>(cube_min.y()), 0.f);
        ModelObject *obj = model.add_object();
        obj->name = "cube";
        obj->add_volume(m);
        obj->add_instance();
        if (belt && !by_object) {
            if (&cube_min == &cube_mins.back())
                obj->config.set_key_value("extruder", new ConfigOptionInt(2));
        } else {
            DynamicPrintConfig range_config;
            range_config.set_key_value("extruder",     new ConfigOptionInt(2));
            range_config.set_key_value("layer_height", new ConfigOptionFloat(cfg.opt_float("layer_height")));
            obj->layer_config_ranges[{4.0, 10.0}].assign_config(std::move(range_config));
        }
        obj->ensure_on_bed();
        print.auto_assign_extruders(obj);
    }
    if (!by_object)
        model.plates_custom_gcodes[model.curr_plate_index].gcodes = {{2., CustomGCode::PausePrint, 1, "", ""},
                                                                     {7., CustomGCode::Template, 1, "", ""}};
    print.apply(model, cfg);
    print.validate();

    print.set_status_silent();
    print.process();
    const fs::path tmp = fs::temp_directory_path() / fs::unique_path("orca-validate-%%%%-%%%%.gcode");
    print.export_gcode(tmp.string(), nullptr, nullptr);
    std::ifstream in(tmp.string());
    std::string   out((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    boost::system::error_code ec;
    fs::remove(tmp, ec);
    print.output_filename();
    return out;
}

void select_printer_default_presets(PresetBundle &bundle)
{
    const Preset &printer_preset = bundle.printers.get_selected_preset();
    const std::string def_print = printer_preset.config.opt_string("default_print_profile");
    if (!def_print.empty())
        bundle.prints.select_preset_by_name(def_print, true);
    if (const auto *def_fil = printer_preset.config.option<ConfigOptionStrings>("default_filament_profile");
        def_fil != nullptr && !def_fil->values.empty())
        bundle.filaments.select_preset_by_name(def_fil->values.front(), true);
    bundle.filament_presets.assign(1, bundle.filaments.get_selected_preset_name());
}

static std::string g_slice_context;

void install_slice_context_log_sink()
{
    namespace logging = boost::log;
    namespace sinks   = boost::log::sinks;
    namespace expr    = boost::log::expressions;

    auto backend = boost::make_shared<sinks::text_ostream_backend>();
    backend->add_stream(boost::shared_ptr<std::ostream>(&std::clog, boost::null_deleter()));
    backend->auto_flush(true);

    auto sink = boost::make_shared<sinks::synchronous_sink<sinks::text_ostream_backend>>(backend);
    sink->set_formatter([](const logging::record_view &rec, logging::formatting_ostream &strm) {
        strm << "[" << rec[logging::trivial::severity] << "]";
        if (!g_slice_context.empty())
            strm << " [" << g_slice_context << "]";
        strm << " " << rec[expr::smessage];
    });

    logging::core::get()->remove_all_sinks();
    logging::core::get()->add_sink(sink);
}

DynamicPrintConfig slice_config(PresetBundle &bundle)
{
    const size_t nozzles = bundle.printers.get_selected_preset().config.option<ConfigOptionFloats>("nozzle_diameter")->size();
    bundle.set_num_filaments((unsigned int) std::max<size_t>(2, nozzles));

    const bool pin_filament_map = bundle.is_bbl_vendor() && nozzles > 1;
    auto &fmap = bundle.project_config.option<ConfigOptionInts>("filament_map", true)->values;
    if (pin_filament_map) {
        for (size_t i = 0; i < fmap.size(); ++i)
            fmap[i] = int(i % nozzles) + 1;
    }

    bundle.reset_default_nozzle_volume_type();
    bundle.project_config.option<ConfigOptionInts>("filament_volume_map", true)->values =
        bundle.get_default_nozzle_volume_types_for_filaments(fmap);

    DynamicPrintConfig cfg = bundle.full_config();
    cfg.set_key_value("enable_prime_tower", new ConfigOptionBool(true));
    if (pin_filament_map)
        cfg.set_key_value("filament_map_mode", new ConfigOptionEnum<FilamentMapMode>(fmmManual));

    if (auto *variants = cfg.option<ConfigOptionStrings>("filament_extruder_variant")) {
        auto &self_index = cfg.option<ConfigOptionInts>("filament_self_index", true)->values;
        if (self_index.size() != variants->size()) {
            self_index.resize(variants->size());
            for (size_t i = 0; i < self_index.size(); ++i)
                self_index[i] = int(i) + 1;
        }
    }
    return cfg;
}

std::string slice_selection(PresetBundle &bundle, const std::string &what, bool by_object, const std::string &outdir, const std::string &file_base)
{
    try {
        const std::string out = slice_two_color_cube_and_export(slice_config(bundle), bundle.is_bbl_vendor(), by_object);
        if (!outdir.empty() && !out.empty())
            save_string_file(fs::path(outdir) / (file_base + ".gcode"), out);
        if (out.empty() || out.find("G1") == std::string::npos) {
            BOOST_LOG_TRIVIAL(error) << what << " produced no g-code";
            return {};
        }
        return out;
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << what << " failed to slice: " << ex.what();
        return {};
    }
}

using Templates = std::set<std::pair<std::string, std::string>>;
Templates preset_templates(const DynamicPrintConfig &cfg)
{
    Templates out;
    for (const std::string &key : cfg.keys()) {
        if (key != "filename_format" && !boost::algorithm::ends_with(key, "_gcode"))
            continue;
        const ConfigOption *opt = cfg.option(key);
        if (opt->type() == coString) {
            if (const std::string &text = static_cast<const ConfigOptionString *>(opt)->value; !text.empty())
                out.emplace(key, text);
        } else if (opt->type() == coStrings) {
            for (const std::string &text : static_cast<const ConfigOptionStrings *>(opt)->values)
                if (!text.empty())
                    out.emplace(key, text);
        }
    }
    return out;
}

int slice_all_printers(const std::string &vendor, const std::string &outdir)
{
    install_slice_context_log_sink();
    PlaceholderParser::check_inactive_branches = true;

    if (!outdir.empty()) {
        boost::system::error_code ec;
        fs::create_directories(outdir, ec);
        if (ec) {
            BOOST_LOG_TRIVIAL(error) << "Could not create output directory \"" << outdir << "\": " << ec.message();
            std::cout << "Validation failed" << std::endl;
            return 1;
        }
        std::cout << "Saving sliced g-code to " << outdir << std::endl;
    }

    PresetBundle bundle;
    bundle.set_is_validation_mode(true);
    bundle.set_vendor_to_validate(vendor);
    AppConfig app_config;
    app_config.set("preset_folder", "default");
    try {
        bundle.load_presets(app_config, ForwardCompatibilitySubstitutionRule::Disable);
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << ex.what();
        std::cout << "Validation failed" << std::endl;
        return 1;
    }

    std::vector<std::pair<std::string, std::string>> printers;
    for (const Preset &p : bundle.printers.get_presets()) {
        if (p.vendor == nullptr) continue;
        const std::string model   = p.config.opt_string("printer_model");
        const std::string variant = p.config.opt_string("printer_variant");
        if (model.empty() || variant.empty()) continue;
        app_config.set_variant(p.vendor->id, model, variant, true);
        printers.push_back({p.vendor->name, p.name});
    }
    bundle.load_installed_printers(app_config);

    if (printers.empty()) {
        BOOST_LOG_TRIVIAL(error) << "No instantiable printer presets found"
            << (vendor.empty() ? "" : " for vendor " + vendor);
        std::cout << "Validation failed" << std::endl;
        return 1;
    }
    std::cout << "Slicing " << printers.size() << " printer preset(s)"
              << (vendor.empty() ? "" : " for vendor " + vendor) << "..." << std::endl;

    Templates covered;
    auto cover = [&covered](const Preset &preset) {
        const Templates templates = preset_templates(preset.config);
        covered.insert(templates.begin(), templates.end());
    };
    auto is_covered = [&covered](const Preset &preset) {
        const Templates templates = preset_templates(preset.config);
        return std::includes(covered.begin(), covered.end(), templates.begin(), templates.end());
    };
    int failures = 0, extra_processes = 0, extra_filaments = 0, by_object_slices = 0;
    for (const auto &[vendor_name, printer] : printers) {
        g_slice_context = vendor_name + " / " + printer;
        const bool selected = bundle.printers.select_preset_by_name(printer, true);
        if (!selected || bundle.printers.get_selected_preset_name() != printer) {
            BOOST_LOG_TRIVIAL(error) << "Printer preset \"" << printer << "\" could not be selected";
            ++failures;
            continue;
        }

        select_printer_default_presets(bundle);
        bundle.update_multi_material_filament_presets();
        bundle.update_compatible(PresetSelectCompatibleType::Always);

        if (bundle.prints.get_selected_preset().is_default || bundle.filaments.get_selected_preset().is_default) {
            BOOST_LOG_TRIVIAL(error) << "Printer \"" << printer << "\" fell back to a default preset (process=\""
                << bundle.prints.get_selected_preset_name() << "\", filament=\""
                << bundle.filaments.get_selected_preset_name() << "\")";
            ++failures;
            continue;
        }

        const std::string print_name    = bundle.prints.get_selected_preset_name();
        const std::string filament_name = bundle.filaments.get_selected_preset_name();
        const std::string what          = "Printer \"" + printer + "\"";
        const std::string file_base     = sanitize_filename(vendor_name) + "__" + sanitize_filename(printer);
        const bool        belt   = bundle.printers.get_selected_preset().config.opt_bool("belt_printer");
        const std::string marker = belt ? "\nT1\n" : "CP TOOLCHANGE START";
        if (const std::string out = slice_selection(bundle, what, false, outdir, file_base); out.empty())
            ++failures;
        else if (out.find(marker) == std::string::npos) {
            BOOST_LOG_TRIVIAL(error) << what << " sliced but the filament change never fired (no "
                                     << (belt ? "T1" : "CP TOOLCHANGE START") << ")";
            ++failures;
        }
        cover(bundle.prints.get_selected_preset());
        cover(bundle.filaments.get_selected_preset());

        const std::string &by_object_gcode = bundle.printers.get_selected_preset().config.opt_string("printing_by_object_gcode");
        if (!by_object_gcode.empty() && covered.emplace("printing_by_object_gcode", by_object_gcode).second) {
            ++by_object_slices;
            g_slice_context = vendor_name + " / " + printer + " / by object";
            if (slice_selection(bundle, what + " printing by object", true, outdir, file_base + "__by_object").empty())
                ++failures;
        }

        const PresetWithVendorProfile printer_with_vendor = bundle.printers.get_edited_preset_with_vendor_profile();
        auto limiting_process = [&bundle, &printer_with_vendor](const Preset &filament) -> std::string {
            const auto *processes = filament.config.option<ConfigOptionStrings>("compatible_prints");
            if (processes == nullptr || processes->values.empty())
                return {};
            const PresetWithVendorProfile filament_with_vendor = bundle.filaments.get_preset_with_vendor_profile(filament);
            if (!is_compatible_with_printer(filament_with_vendor, printer_with_vendor))
                return {};
            for (const std::string &name : processes->values)
                if (const Preset *process = bundle.prints.find_preset(name);
                    process != nullptr && process->is_visible && process->is_compatible &&
                    is_compatible_with_print(filament_with_vendor, bundle.prints.get_preset_with_vendor_profile(*process), printer_with_vendor))
                    return name;
            return {};
        };
        std::vector<std::pair<const Preset *, std::string>> extras;
        size_t                                              hidden = 0;
        for (const PresetCollection *presets : {&bundle.prints, &bundle.filaments})
            for (const Preset &preset : presets->get_presets()) {
                if (!preset.is_system || preset.is_default)
                    continue;
                std::string process;
                if (preset.is_compatible)
                    process = preset.type == Preset::TYPE_PRINT ? preset.name : print_name;
                else if (preset.type == Preset::TYPE_FILAMENT)
                    process = limiting_process(preset);
                if (process.empty() || is_covered(preset))
                    continue;
                if (preset.is_visible)
                    extras.emplace_back(&preset, process);
                else
                    ++hidden;
            }
        if (hidden > 0)
            BOOST_LOG_TRIVIAL(warning) << "Printer \"" << printer << "\": " << hidden
                << " compatible system preset(s) with unexpanded templates are hidden and were not sliced";

        for (const auto &[preset, process] : extras) {
            if (is_covered(*preset))
                continue;
            const bool         is_print = preset->type == Preset::TYPE_PRINT;
            const std::string &filament = is_print ? filament_name : preset->name;
            std::string        extra    = std::string(is_print ? "process" : "filament") + " \"" + preset->name + "\"";
            if (!is_print && process != print_name)
                extra += " and process \"" + process + "\"";
            g_slice_context = vendor_name + " / " + printer + " / " + extra;
            if (is_print)
                ++extra_processes;
            else
                ++extra_filaments;
            cover(*preset);
            bundle.prints.select_preset_by_name(process, true);
            bundle.filaments.select_preset_by_name(filament, true);
            bundle.filament_presets.assign(1, bundle.filaments.get_selected_preset_name());
            bundle.update_multi_material_filament_presets();
            if (bundle.prints.get_selected_preset_name() != process || bundle.filaments.get_selected_preset_name() != filament) {
                BOOST_LOG_TRIVIAL(error) << what << " could not select " << extra;
                ++failures;
                continue;
            }
            if (slice_selection(bundle, what + " with " + extra, false, outdir, file_base + "__" + sanitize_filename(preset->name)).empty())
                ++failures;
        }
        bundle.prints.select_preset_by_name(print_name, true);
        bundle.filaments.select_preset_by_name(filament_name, true);
    }
    g_slice_context.clear();

    const int slices = int(printers.size()) + extra_processes + extra_filaments + by_object_slices;
    std::cout << "Sliced " << printers.size() << " printer preset(s), " << extra_processes << " more process preset(s), "
              << extra_filaments << " more filament preset(s) and " << by_object_slices << " printer(s) by object" << std::endl;
    if (failures > 0) {
        std::cout << failures << " of " << slices << " slice(s) failed" << std::endl;
        std::cout << "Validation failed" << std::endl;
        return 1;
    }
    std::cout << "All " << slices << " slice(s) succeeded" << std::endl;
    std::cout << "Validation completed successfully" << std::endl;
    return 0;
}

}

int main(int argc, char* argv[])
{
    po::options_description desc("Orca Profile Validator\nUsage");
    // clang-format off
    desc.add_options()("help,h", "help")
#ifdef __APPLE__
    ("path,p", po::value<std::string>()->default_value("../../../../../../../resources/profiles"), "profile folder")
#else
    ("path,p", po::value<std::string>()->default_value("../../../resources/profiles"), "profile folder")
#endif
    ("vendor,v", po::value<std::string>()->default_value(""), "Vendor name. Optional, all profiles present in the folder will be validated if not specified")
    ("generate_presets,g", po::value<bool>()->default_value(false), "Generate user presets for mock test")
    ("slice,s", po::bool_switch()->default_value(false), "Slice a two-colour cube through every printer, and through every other system process and filament preset whose templates no printer's own slice expands, so every custom g-code and filename_format shipped is expanded, names in {if} branches not taken included (catches placeholder/flow errors that static checks miss). Off unless this flag is present.")
    ("outdir,o", po::value<std::string>()->default_value(""), "With -s, also save each slice's g-code to this folder (as <vendor>__<printer>[__<preset>].gcode) for manual inspection. Optional.")
    ("check_filament_subtypes,f", po::bool_switch()->default_value(true), "Also flag printers with duplicate (ambiguous) filament subtypes. Off unless this flag is present.")
    ("log_level,l", po::value<int>()->default_value(2), "Log level. Optional, default is 2 (warning). Higher values produce more detailed logs.");
    // clang-format on

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            std::cout << desc << "\n";
            return 1;
        }

        po::notify(vm);
    } catch (const po::error& e) {
        std::cerr << "Error: " << e.what() << "\n";
        std::cerr << desc << "\n";
        return 1;
    }

    std::string path                 = vm["path"].as<std::string>();
    std::string vendor               = vm["vendor"].as<std::string>();
    int         log_level            = vm["log_level"].as<int>();
    bool        generate_user_preset = vm["generate_presets"].as<bool>();
    bool        slice_mode           = vm["slice"].as<bool>();
    std::string slice_outdir         = vm["outdir"].as<std::string>();
    bool        check_filament_subtypes = vm["check_filament_subtypes"].as<bool>();

    if (!fs::exists(path) || !fs::is_directory(path)) {
        std::cerr << "Error: " << path << " is not a valid directory\n";
        return 1;
    }

    set_data_dir(path);
    if (fs::exists(fs::path(path).parent_path() / "info"))
        set_resources_dir(fs::path(path).parent_path().string());

    auto user_dir = fs::path(Slic3r::data_dir()) / PRESET_USER_DIR;
    user_dir.make_preferred();
    if (!fs::exists(user_dir))
        fs::create_directory(user_dir);

    set_logging_level(log_level);

    if (slice_mode)
        return slice_all_printers(vendor, slice_outdir);

    auto preset_bundle = new PresetBundle();
    preset_bundle->set_is_validation_mode(true);
    preset_bundle->set_vendor_to_validate(vendor);

    preset_bundle->set_default_suppressed(true);
    AppConfig app_config;
    app_config.set("preset_folder", "default");

    if(generate_user_preset)
        preset_bundle->remove_user_presets_directory("default");

    try {
        auto preset_substitutions = preset_bundle->load_presets(app_config, ForwardCompatibilitySubstitutionRule::Disable);
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << ex.what();
        std::cout << "Validation failed" << std::endl;
        return 1;
    }
    std::cout << "Total loaded vendors: " << preset_bundle->vendors.size() << std::endl;

    if (generate_user_preset) {
        generate_custom_presets(preset_bundle, app_config);
        return 0;
    }

    if (preset_bundle->has_errors(check_filament_subtypes)) {
        std::cout << "Validation failed" << std::endl;
        return 1;
    }

    std::cout << "Validation completed successfully" << std::endl;
    return 0;
}
