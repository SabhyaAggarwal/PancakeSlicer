#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/directory.hpp>
#include <boost/filesystem/file_status.hpp>
#include <catch2/catch_all.hpp>

#include <algorithm>
#include <boost/filesystem.hpp>
#include <cstddef>
#include <fstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/IMEXHelpers.hpp"
#include "libslic3r/ParallelResolve.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/miniz_extension.hpp"

#include "test_utils.hpp"

#include <iostream>
#include <initializer_list>
#include <vector>
#include <string>
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Preset.hpp"
#include <utility>
#include "libslic3r/Config.hpp"
#include <map>
#include "libslic3r/libslic3r.h"
#include "libslic3r/PublishSettings.hpp"
#include <set>
#include "libslic3r/TriangleSelector.hpp"
#include <iterator>
#include <miniz.h>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace Slic3r;

namespace {

namespace fs = boost::filesystem;

bool contains_key(const std::vector<std::string> &keys, const std::string &key)
{
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

void check_double_vector(const std::vector<double> &actual, std::initializer_list<double> expected)
{
    REQUIRE(actual.size() == expected.size());
    size_t index = 0;
    for (double value : expected)
        REQUIRE_THAT(actual[index++], Catch::Matchers::WithinAbs(value, 1e-6));
}

void write_print_preset(const DynamicPrintConfig &default_config, const fs::path &file, const std::string &name, const std::string &inherits = {})
{
    DynamicPrintConfig config(default_config);
    config.option<ConfigOptionString>("print_settings_id", true)->value = name;
    config.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = inherits;

    fs::create_directories(file.parent_path());
    config.save_to_json(file.string(), name, "User", "1.0.0");
}

void write_preset_with_inherits(const DynamicPrintConfig &default_config, const fs::path &file,
                                const std::string &name, const std::string &inherits)
{
    DynamicPrintConfig config(default_config);
    config.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = inherits;

    fs::create_directories(file.parent_path());
    config.save_to_json(file.string(), name, "User", "1.0.0");
}

void write_minimal_child(const fs::path &file, const std::string &name, const std::string &inherits)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file.string())
        << R"({"type":"process","name":")" << name << R"(","from":"User","version":"1.0.0","inherits":")"
        << inherits << R"("})";
}

Preset &add_inmemory_preset(PresetCollection &coll, const std::string &name, const std::string &inherits = {})
{
    DynamicPrintConfig config(coll.default_preset().config);
    config.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = inherits;
    return coll.load_preset(std::string(), name, config, false);
}

void set_renamed_from(PresetCollection &coll, const std::string &preset_name, std::vector<std::string> old_names)
{
    for (auto it = coll.begin(); it != coll.end(); ++it)
        if (it->name == preset_name)
            it->renamed_from = std::move(old_names);
}

DynamicPrintConfig published_pla_file_config()
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75 };
    config.opt<ConfigOptionInts>("filament_self_index")->values = { 1 };
    config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard" };
    config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000" };
    config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA" };
    config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic" };
    config.opt<ConfigOptionStrings>("filament_ids")->values = { "GFL99" };
    config.option<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.9 };
    return config;
}

struct RenameTestCollection : public PresetCollection
{
    RenameTestCollection()
        : PresetCollection(Preset::TYPE_PRINT, Preset::print_options(),
                           static_cast<const PrintRegionConfig &>(FullPrintConfig::defaults()))
    {}
    using PresetCollection::update_map_system_profile_renamed;
};

}

TEST_CASE("Preset identity is canonicalized from load path", "[Preset][Identity]")
{
    ScopedTemporaryDir         temp_dir;
    PresetBundle               bundle;
    PresetsConfigSubstitutions substitutions;

    write_print_preset(bundle.prints.default_preset().config, temp_dir.path() / PRESET_PRINT_NAME / "User.json", "User");
    write_print_preset(bundle.prints.default_preset().config, temp_dir.path() / PRESET_LOCAL_DIR / "bundle-1" / PRESET_PRINT_NAME / "LocalBundle.json", "LocalBundle");
    write_print_preset(bundle.prints.default_preset().config, temp_dir.path() / PRESET_SUBSCRIBED_DIR / "remote-1" / PRESET_PRINT_NAME / "Subscribed.json", "Subscribed");

    bundle.prints.load_presets(temp_dir.path().string(), PRESET_PRINT_NAME, substitutions, ForwardCompatibilitySubstitutionRule::Disable);
    bundle.prints.load_presets((temp_dir.path() / PRESET_LOCAL_DIR / "bundle-1").string(), PRESET_PRINT_NAME, substitutions, ForwardCompatibilitySubstitutionRule::Disable);
    bundle.prints.load_presets((temp_dir.path() / PRESET_SUBSCRIBED_DIR / "remote-1").string(), PRESET_PRINT_NAME, substitutions, ForwardCompatibilitySubstitutionRule::Disable);

    const Preset *root_user = bundle.prints.find_preset("User");
    REQUIRE(root_user != nullptr);
    CHECK(root_user->name == "User");
    CHECK_FALSE(root_user->is_from_bundle());

    const Preset *local_bundle = bundle.prints.find_preset("_local/bundle-1/LocalBundle");
    REQUIRE(local_bundle != nullptr);
    CHECK(local_bundle->name == "_local/bundle-1/LocalBundle");
    CHECK(local_bundle->is_from_bundle());

    const Preset *subscribed = bundle.prints.find_preset("_subscribed/remote-1/Subscribed");
    REQUIRE(subscribed != nullptr);
    CHECK(subscribed->name == "_subscribed/remote-1/Subscribed");
    CHECK(subscribed->is_from_bundle());
}

TEST_CASE("Legacy bundle import without bundle metadata stays in the user preset directory", "[Preset][Identity]")
{
    ScopedTemporaryDir temp_dir;
    PresetBundle  bundle;

    PresetsConfigSubstitutions substitutions;
    std::vector<std::string>   result;
    int                        overwrite = 0;
    std::string                file      = (temp_dir.path() / "legacy-bundle" / "Imported.json").string();
    const fs::path             user_root = temp_dir.path() / "user";

    write_print_preset(bundle.prints.default_preset().config, file, "Imported");
    fs::create_directories(user_root);
    bundle.prints.update_user_presets_directory(user_root.string(), PRESET_PRINT_NAME);

    REQUIRE(bundle.import_json_presets(
        substitutions,
        file,
        [](std::string const &) { return 1; },
        ForwardCompatibilitySubstitutionRule::Disable,
        overwrite,
        result));

    const Preset *imported = bundle.prints.find_preset("Imported");
    REQUIRE(imported != nullptr);
    CHECK(imported->name == "Imported");
    CHECK(imported->bundle_id.empty());
    CHECK_FALSE(imported->is_from_bundle());
    CHECK(fs::equivalent(fs::path(imported->file).parent_path().parent_path(), user_root / PRESET_PRINT_NAME));
}

TEST_CASE("Current vendor type tolerates missing printer model", "[Preset][Bundle]")
{
    PresetBundle bundle;

    VendorProfile orca_vendor; orca_vendor.id = "ORCA";
    VendorProfile::PrinterModel model;
    model.name = "Orca Test";
    orca_vendor.models.emplace_back(model);
    bundle.vendors.emplace("ORCA", std::move(orca_vendor));

    bundle.printers.get_edited_preset().config.erase("printer_model");

    CHECK(bundle.get_current_vendor_type() == VendorType::Unknown);
}

TEST_CASE("A malformed entry in a vendor's preset list is counted, not thrown", "[Preset][Bundle]")
{
    ScopedTemporaryDir dir;

    std::ofstream((dir.path() / "Acme.json").string())
        << R"({"version":"1.0.0","name":"Acme","process_list":[123,)"
        << R"({"name":"0.20mm Standard @Acme","sub_path":"process/standard.json"}]})";
    fs::create_directories(dir.path() / "Acme" / "process");
    std::ofstream((dir.path() / "Acme" / "process" / "standard.json").string())
        << R"({"type":"process","name":"0.20mm Standard @Acme","from":"system",)"
        << R"("instantiation":"true","layer_height":"0.2"})";

    PresetBundle bundle;
    size_t       loaded = 0;
    REQUIRE_NOTHROW(loaded = bundle.load_vendor_configs_from_json(
                        dir.path().string(), "Acme", PresetBundle::LoadSystem,
                        ForwardCompatibilitySubstitutionRule::EnableSilent).second);

    CHECK(bundle.error_count() > 0);
    CHECK(loaded == 1);
}

TEST_CASE("Printer extruder count tolerates missing nozzle diameter", "[Preset][Bundle]")
{
    PresetBundle bundle;
    DynamicPrintConfig& config = bundle.printers.get_edited_preset().config;

    config.erase("nozzle_diameter");
    CHECK(bundle.get_printer_extruder_count() == 1);

    config.set_key_value("nozzle_diameter", new ConfigOptionFloats());
    CHECK(bundle.get_printer_extruder_count() == 1);

    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({ 0.4, 0.6 }));
    CHECK(bundle.get_printer_extruder_count() == 2);
}

TEST_CASE("Selected printer uses its default or saved bed type", "[Preset][Bundle]")
{
    PresetBundle bundle;
    Preset& printer = add_inmemory_preset(bundle.printers, "Test Printer");
    printer.is_system = true;
    printer.config.option<ConfigOptionString>("printer_model")->value = "TEST-MODEL";
    printer.config.option<ConfigOptionString>("printer_variant")->value = "0.4";
    printer.config.option<ConfigOptionString>("default_bed_type")->value = "Engineering Plate";

    AppConfig app_config;
    app_config.set("curr_bed_type", std::to_string(static_cast<int>(btPTE)));
    PresetBundle::PresetPreferences preferred_selection;
    BedType expected_bed_type;

    SECTION("New printer uses its symbolic default") {
        expected_bed_type = btEP;
        preferred_selection = {"TEST-MODEL", "0.4"};
    }
    SECTION("Re-enabled printer uses its saved selection") {
        expected_bed_type = btPC;
        preferred_selection = {"TEST-MODEL", "0.4"};
        app_config.set_printer_setting("Test Printer", "curr_bed_type",
                                       std::to_string(static_cast<int>(expected_bed_type)));
    }
    SECTION("Existing printer keeps its saved selection after presets reload") {
        expected_bed_type = btPCT;
        app_config.set("presets", PRESET_PRINTER_NAME, "Test Printer");
        app_config.set_printer_setting("Test Printer", "curr_bed_type",
                                       std::to_string(static_cast<int>(expected_bed_type)));
    }

    bundle.load_selections(app_config, preferred_selection);
    bundle.export_selections(app_config);

    CHECK(bundle.project_config.opt_enum<BedType>("curr_bed_type") == expected_bed_type);
    CHECK(app_config.get_printer_setting("Test Printer", "curr_bed_type") == std::to_string(static_cast<int>(expected_bed_type)));
}

TEST_CASE("A directory of user presets loads with each one resolved against its parent", "[Preset][Bundle]")
{
    ScopedTemporaryDir   temp_dir;
    RenameTestCollection coll;

    Preset &parent = add_inmemory_preset(coll, "Parent Process");
    parent.config.option<ConfigOptionFloat>("layer_height", true)->value = 0.24;
    parent.is_system = true;

    constexpr int children = 400;
    for (int i = 0; i < children; ++ i)
        write_minimal_child(temp_dir.path() / PRESET_PRINT_NAME / ("Child " + std::to_string(i) + ".json"),
                            "Child " + std::to_string(i), "Parent Process");

    PresetsConfigSubstitutions substitutions;
    coll.load_presets(temp_dir.path().string(), PRESET_PRINT_NAME, substitutions,
                      ForwardCompatibilitySubstitutionRule::Disable);

    CHECK(coll.size() == size_t(children) + 2);
    CHECK(coll.error_count() == 0);
    for (int i = 0; i < children; ++ i) {
        const Preset *child = coll.find_preset("Child " + std::to_string(i));
        REQUIRE(child != nullptr);
        CHECK(child->inherits() == "Parent Process");
        CHECK(child->alias == "Child " + std::to_string(i));
        CHECK(child->loaded);
        REQUIRE(child->config.option<ConfigOptionFloat>("layer_height") != nullptr);
        CHECK_THAT(child->config.opt_float("layer_height"), Catch::Matchers::WithinAbs(0.24, 1e-9));
    }
}

TEST_CASE("Repeated loads of a user preset directory produce the same presets", "[Preset][Bundle]")
{
    ScopedTemporaryDir temp_dir;

    auto seed_directory = [&]() {
        for (int i = 0; i < 200; ++ i)
            write_minimal_child(temp_dir.path() / PRESET_PRINT_NAME / ("Child " + std::to_string(i) + ".json"),
                                "Child " + std::to_string(i), "Parent Process");
    };

    std::vector<std::vector<std::string>> names_per_run;
    for (int run = 0; run < 3; ++ run) {
        RenameTestCollection coll;
        Preset &parent = add_inmemory_preset(coll, "Parent Process");
        parent.is_system = true;
        if (run == 0)
            seed_directory();

        PresetsConfigSubstitutions substitutions;
        coll.load_presets(temp_dir.path().string(), PRESET_PRINT_NAME, substitutions,
                          ForwardCompatibilitySubstitutionRule::Disable);

        std::vector<std::string> names;
        for (auto it = coll.begin(); it != coll.end(); ++ it)
            names.push_back(it->name + "|" + it->alias + "|" + it->inherits());
        names_per_run.push_back(std::move(names));
    }

    REQUIRE(names_per_run[0].size() > 200);
    CHECK(names_per_run[1] == names_per_run[0]);
    CHECK(names_per_run[2] == names_per_run[0]);
}

TEST_CASE("An unreadable user preset is counted and removed while the rest still load", "[Preset][Bundle]")
{
    ScopedTemporaryDir   temp_dir;
    RenameTestCollection coll;
    const fs::path       dir = temp_dir.path() / PRESET_PRINT_NAME;

    for (int i = 0; i < 20; ++ i)
        write_preset_with_inherits(coll.default_preset().config, dir / ("Good " + std::to_string(i) + ".json"),
                                   "Good " + std::to_string(i), std::string());
    fs::create_directories(dir);
    std::ofstream((dir / "Broken.json").string()) << "{not-json";

    PresetsConfigSubstitutions substitutions;
    coll.load_presets(temp_dir.path().string(), PRESET_PRINT_NAME, substitutions,
                      ForwardCompatibilitySubstitutionRule::EnableSilent);

    CHECK(coll.error_count() == 1);
    CHECK(coll.find_preset("Broken") == nullptr);
    CHECK_FALSE(fs::exists(dir / "Broken.json"));
    for (int i = 0; i < 20; ++ i)
        CHECK(coll.find_preset("Good " + std::to_string(i)) != nullptr);
}

TEST_CASE("A user filament naming no compatible printer gets the one after its @, in memory and on disk", "[Preset][Bundle]")
{
    ScopedTemporaryDir temp_dir;
    PresetBundle       bundle;
    const fs::path     file = temp_dir.path() / PRESET_FILAMENT_NAME / "My PLA @Test Printer.json";
    REQUIRE(bundle.filaments.default_preset().config.option<ConfigOptionStrings>("compatible_printers")->values.empty());
    write_preset_with_inherits(bundle.filaments.default_preset().config, file, "My PLA @Test Printer", std::string());

    PresetsConfigSubstitutions substitutions;
    bundle.filaments.load_presets(temp_dir.path().string(), PRESET_FILAMENT_NAME, substitutions,
                                  ForwardCompatibilitySubstitutionRule::EnableSilent);

    const std::vector<std::string> expected { "Test Printer" };
    const Preset *preset = bundle.filaments.find_preset("My PLA @Test Printer");
    REQUIRE(preset != nullptr);
    CHECK(preset->config.option<ConfigOptionStrings>("compatible_printers")->values == expected);

    DynamicPrintConfig                 saved;
    std::map<std::string, std::string> key_values;
    std::string                        reason;
    saved.load_from_json(file.string(), ForwardCompatibilitySubstitutionRule::EnableSilent, key_values, reason);
    REQUIRE(reason.empty());
    REQUIRE(saved.option<ConfigOptionStrings>("compatible_printers") != nullptr);
    CHECK(saved.option<ConfigOptionStrings>("compatible_printers")->values == expected);
}

TEST_CASE("A user preset's setting id equal to its base id is dropped in memory, not in the .info written back", "[Preset][Bundle]")
{
    ScopedTemporaryDir temp_dir;
    PresetBundle       bundle;
    const fs::path     file = temp_dir.path() / PRESET_FILAMENT_NAME / "My PLA @Test Printer.json";
    write_preset_with_inherits(bundle.filaments.default_preset().config, file, "My PLA @Test Printer", std::string());
    fs::path info = file;
    info.replace_extension(".info");
    std::ofstream(info.string()) << "sync_info = \nuser_id = \nsetting_id = PFUS1\nbase_id = PFUS1\nupdated_time = 0\n";

    PresetsConfigSubstitutions substitutions;
    bundle.filaments.load_presets(temp_dir.path().string(), PRESET_FILAMENT_NAME, substitutions,
                                  ForwardCompatibilitySubstitutionRule::EnableSilent);

    const Preset *preset = bundle.filaments.find_preset("My PLA @Test Printer");
    REQUIRE(preset != nullptr);
    CHECK(preset->setting_id.empty());
    CHECK(preset->base_id == "PFUS1");
    Preset reloaded(Preset::TYPE_FILAMENT, "My PLA @Test Printer");
    reloaded.load_info(info.string());
    CHECK(reloaded.setting_id == "PFUS1");
}

TEST_CASE("A user preset that is not loaded still reports its substituted values", "[Preset][Bundle]")
{
    ScopedTemporaryDir   temp_dir;
    RenameTestCollection coll;
    const fs::path       dir = temp_dir.path() / PRESET_PRINT_NAME;
    fs::create_directories(dir);
    std::ofstream((dir / "Orphan.json").string())
        << R"({"type":"process","name":"Orphan","from":"User","version":"1.0.0","inherits":"No Such Parent",)"
        << R"("wall_generator":"no_such_generator"})";

    PresetsConfigSubstitutions substitutions;
    coll.load_presets(temp_dir.path().string(), PRESET_PRINT_NAME, substitutions,
                      ForwardCompatibilitySubstitutionRule::Enable);

    CHECK(coll.find_preset("Orphan") == nullptr);
    CHECK(coll.error_count() == 1);
    REQUIRE(substitutions.size() == 1);
    CHECK(substitutions.front().preset_name == "Orphan");
}

TEST_CASE("find_preset resolves a system preset's renamed_from", "[Preset][Rename]")
{
    RenameTestCollection coll;

    add_inmemory_preset(coll, "New Process");
    set_renamed_from(coll, "New Process", { "Old Process" });
    coll.update_map_system_profile_renamed();

    const std::string *renamed = coll.get_preset_name_renamed("Old Process");
    REQUIRE(renamed != nullptr);
    CHECK(*renamed == "New Process");

    const Preset *resolved = coll.find_preset("Old Process");
    REQUIRE(resolved != nullptr);
    CHECK(resolved->name == "New Process");

    CHECK(coll.find_preset("Totally Unknown") == nullptr);

    Preset       &child  = add_inmemory_preset(coll, "Child Process", "Old Process");
    const Preset *parent = coll.get_preset_parent(child);
    REQUIRE(parent != nullptr);
    CHECK(parent->name == "New Process");
}

TEST_CASE("find_preset resolves a preset renamed more than once", "[Preset][Rename]")
{
    RenameTestCollection coll;

    add_inmemory_preset(coll, "New Process");
    set_renamed_from(coll, "New Process", { "Original Process", "Old Process" });
    coll.update_map_system_profile_renamed();

    for (const char *old_name : { "Original Process", "Old Process" }) {
        INFO("resolving old name: " << old_name);
        const std::string *renamed = coll.get_preset_name_renamed(old_name);
        REQUIRE(renamed != nullptr);
        CHECK(*renamed == "New Process");

        const Preset *resolved = coll.find_preset(old_name);
        REQUIRE(resolved != nullptr);
        CHECK(resolved->name == "New Process");
    }

    Preset &child = add_inmemory_preset(coll, "Child Process", "Original Process");
    REQUIRE(coll.get_preset_parent(child) != nullptr);
    CHECK(coll.get_preset_parent(child)->name == "New Process");
}

TEST_CASE("find_preset2 auto-matches removed Generic vendor profiles to the library", "[Preset][Rename]")
{
    PresetBundle bundle;

    add_inmemory_preset(bundle.filaments, "Generic PLA @System");

    CHECK(bundle.filaments.find_preset("Voron Generic PLA") == nullptr);
    CHECK(bundle.filaments.find_preset2("Voron Generic PLA", false) == nullptr);

    const Preset *matched = bundle.filaments.find_preset2("Voron Generic PLA", true);
    REQUIRE(matched != nullptr);
    CHECK(matched->name == "Generic PLA @System");

    CHECK(bundle.filaments.find_preset2("BrandX Generic PETG", true) == nullptr);
}

TEST_CASE("Renamed parent is normalized into a loaded preset's inherits", "[Preset][Rename]")
{
    ScopedTemporaryDir   temp_dir;
    RenameTestCollection coll;

    add_inmemory_preset(coll, "New Process");
    set_renamed_from(coll, "New Process", { "Old Process" });
    coll.update_map_system_profile_renamed();

    write_preset_with_inherits(coll.default_preset().config,
                               temp_dir.path() / PRESET_PRINT_NAME / "Child.json", "Child", "Old Process");

    PresetsConfigSubstitutions substitutions;
    coll.load_presets(temp_dir.path().string(), PRESET_PRINT_NAME, substitutions,
                      ForwardCompatibilitySubstitutionRule::Disable);

    const Preset *child = coll.find_preset("Child");
    REQUIRE(child != nullptr);
    CHECK(child->inherits() == "New Process");
    REQUIRE(coll.get_preset_parent(*child) != nullptr);
    CHECK(coll.get_preset_parent(*child)->name == "New Process");
}

TEST_CASE("Removed Generic parent is normalized into a loaded filament's inherits", "[Preset][Rename]")
{
    ScopedTemporaryDir temp_dir;
    PresetBundle  bundle;

    add_inmemory_preset(bundle.filaments, "Generic PLA @System");

    write_preset_with_inherits(bundle.filaments.default_preset().config,
                               temp_dir.path() / PRESET_FILAMENT_NAME / "MyPLA.json", "MyPLA", "Voron Generic PLA");

    PresetsConfigSubstitutions substitutions;
    bundle.filaments.load_presets(temp_dir.path().string(), PRESET_FILAMENT_NAME, substitutions,
                                  ForwardCompatibilitySubstitutionRule::Disable);

    const Preset *child = bundle.filaments.find_preset("MyPLA");
    REQUIRE(child != nullptr);
    CHECK(child->inherits() == "Generic PLA @System");
    REQUIRE(bundle.filaments.get_preset_parent(*child) != nullptr);
    CHECK(bundle.filaments.get_preset_parent(*child)->name == "Generic PLA @System");
}

namespace {

std::vector<std::string> &compatible_list(PresetCollection &coll, const std::string &preset_name, const char *field_key)
{
    Preset *preset = coll.find_preset(preset_name, false, true);
    REQUIRE(preset != nullptr);
    return preset->config.option<ConfigOptionStrings>(field_key, true)->values;
}

}

TEST_CASE("Renamed printer/process names are normalized into compatible lists on load", "[Preset][Rename]")
{
    PresetBundle bundle;

    add_inmemory_preset(bundle.printers, "New Printer");
    set_renamed_from(bundle.printers, "New Printer", { "Old Printer" });
    add_inmemory_preset(bundle.prints, "New Process");
    set_renamed_from(bundle.prints, "New Process", { "Old Process" });

    add_inmemory_preset(bundle.prints, "My Process");
    compatible_list(bundle.prints, "My Process", "compatible_printers") = { "Old Printer" };

    add_inmemory_preset(bundle.filaments, "My Filament");
    compatible_list(bundle.filaments, "My Filament", "compatible_printers") = { "Old Printer", "Unknown Printer" };
    compatible_list(bundle.filaments, "My Filament", "compatible_prints")   = { "Old Process" };

    AppConfig app_config;
    bundle.load_installed_printers(app_config);
    bundle.normalize_compatible_presets();

    CHECK(compatible_list(bundle.prints, "My Process", "compatible_printers") == std::vector<std::string>{ "New Printer" });

    CHECK(compatible_list(bundle.filaments, "My Filament", "compatible_prints") == std::vector<std::string>{ "New Process" });

    CHECK(compatible_list(bundle.filaments, "My Filament", "compatible_printers") ==
          (std::vector<std::string>{ "New Printer", "Unknown Printer" }));

    CHECK_FALSE(bundle.prints.find_preset("My Process", false, true)->is_dirty);

    bundle.normalize_compatible_presets();
    CHECK(compatible_list(bundle.prints, "My Process", "compatible_printers") == std::vector<std::string>{ "New Printer" });
}

TEST_CASE("Renamed names are normalized into a SYSTEM preset's compatible lists", "[Preset][Rename]")
{
    PresetBundle bundle;

    add_inmemory_preset(bundle.printers, "New Printer");
    set_renamed_from(bundle.printers, "New Printer", { "Old Printer" });
    add_inmemory_preset(bundle.prints, "New Process");
    set_renamed_from(bundle.prints, "New Process", { "Old Process" });

    add_inmemory_preset(bundle.filaments, "System Filament").is_system = true;
    compatible_list(bundle.filaments, "System Filament", "compatible_printers") = { "Old Printer" };
    compatible_list(bundle.filaments, "System Filament", "compatible_prints")   = { "Old Process" };

    AppConfig app_config;
    bundle.load_installed_printers(app_config);
    bundle.normalize_compatible_presets();

    CHECK(compatible_list(bundle.filaments, "System Filament", "compatible_printers") ==
          std::vector<std::string>{ "New Printer" });
    CHECK(compatible_list(bundle.filaments, "System Filament", "compatible_prints") ==
          std::vector<std::string>{ "New Process" });

    CHECK_FALSE(bundle.filaments.find_preset("System Filament", false, true)->is_dirty);
    bundle.normalize_compatible_presets();
    CHECK(compatible_list(bundle.filaments, "System Filament", "compatible_printers") ==
          std::vector<std::string>{ "New Printer" });
}

TEST_CASE("compatible_prints on SLA materials resolves against sla_prints, not prints", "[Preset][Rename]")
{
    PresetBundle bundle;

    add_inmemory_preset(bundle.sla_prints, "New SLA Process");
    set_renamed_from(bundle.sla_prints, "New SLA Process", { "Old SLA Process" });
    add_inmemory_preset(bundle.prints, "Wrong FFF Process");
    set_renamed_from(bundle.prints, "Wrong FFF Process", { "Old SLA Process" });

    add_inmemory_preset(bundle.sla_materials, "My SLA Material");
    compatible_list(bundle.sla_materials, "My SLA Material", "compatible_prints") = { "Old SLA Process" };

    AppConfig app_config;
    bundle.load_installed_printers(app_config);
    bundle.normalize_compatible_presets();

    CHECK(compatible_list(bundle.sla_materials, "My SLA Material", "compatible_prints") ==
          std::vector<std::string>{ "New SLA Process" });
}

TEST_CASE("Profile validator flags dangling and renamed preset references", "[Preset][Validate]")
{
    PresetBundle bundle;

    add_inmemory_preset(bundle.printers, "Real Printer");
    add_inmemory_preset(bundle.printers, "New Printer");
    set_renamed_from(bundle.printers, "New Printer", { "Old Printer" });

    add_inmemory_preset(bundle.prints, "Real Process").is_system = true;

    add_inmemory_preset(bundle.filaments, "Good Filament").is_system = true;
    compatible_list(bundle.filaments, "Good Filament", "compatible_printers") = { "Real Printer" };
    compatible_list(bundle.filaments, "Good Filament", "compatible_prints")   = { "Real Process" };

    AppConfig app_config;
    bundle.load_installed_printers(app_config);
    CHECK_FALSE(bundle.check_preset_references());

    SECTION("deleted compatible_printers is flagged") {
        add_inmemory_preset(bundle.filaments, "Ghost Ref Filament").is_system = true;
        compatible_list(bundle.filaments, "Ghost Ref Filament", "compatible_printers") = { "Ghost Printer" };
        CHECK(bundle.check_preset_references());
    }

    SECTION("renamed compatible_printers (old name) is flagged") {
        add_inmemory_preset(bundle.filaments, "Old Ref Filament").is_system = true;
        compatible_list(bundle.filaments, "Old Ref Filament", "compatible_printers") = { "Old Printer" };
        CHECK(bundle.check_preset_references());
    }

    SECTION("deleted compatible_prints is flagged") {
        add_inmemory_preset(bundle.filaments, "Bad Process Ref").is_system = true;
        compatible_list(bundle.filaments, "Bad Process Ref", "compatible_prints") = { "Ghost Process" };
        CHECK(bundle.check_preset_references());
    }

    SECTION("deleted inherits parent is flagged") {
        add_inmemory_preset(bundle.filaments, "Orphan Filament", "Ghost Parent").is_system = true;
        CHECK(bundle.check_preset_references());
    }

    SECTION("non-system preset with a dangling reference is ignored") {
        add_inmemory_preset(bundle.filaments, "User Filament");
        compatible_list(bundle.filaments, "User Filament", "compatible_printers") = { "Ghost Printer" };
        CHECK_FALSE(bundle.check_preset_references());
    }
}

TEST_CASE("Every printer variant has a compatible default material", "[Preset][Validate][DefaultMaterials]")
{
    PresetBundle bundle;
    auto &vendor = bundle.vendors["Acme"];
    vendor.id = vendor.name = "Acme";
    vendor.models.emplace_back();
    auto &model = vendor.models.back();
    model.id = model.name = "Acme Printer";
    model.default_materials = {"Acme PLA @0.4", "Acme PLA @0.6"};

    for (const std::string variant : {"0.4", "0.6"}) {
        model.variants.emplace_back(variant);
        const std::string printer_name = "Acme Printer " + variant;
        Preset &printer = add_inmemory_preset(bundle.printers, printer_name);
        printer.is_system = true;
        printer.is_visible = false;
        printer.vendor = &vendor;
        printer.config.option<ConfigOptionString>("printer_model")->value = model.id;
        printer.config.option<ConfigOptionString>("printer_variant")->value = variant;
        printer.config.option<ConfigOptionFloats>("nozzle_diameter")->values = {std::stod(variant)};

        Preset &filament = add_inmemory_preset(bundle.filaments, "Acme PLA @" + variant);
        filament.is_system = true;
        filament.vendor = &vendor;
        filament.alias = "Acme PLA";
        filament.config.option<ConfigOptionStrings>("compatible_printers")->values = {printer_name};
    }

    Preset &other_printer = add_inmemory_preset(bundle.printers, "Acme Printer 0.2");
    other_printer.vendor = &vendor;
    Preset &other_filament = add_inmemory_preset(bundle.filaments, "Acme PLA @0.2");
    other_filament.is_system = true;
    other_filament.vendor = &vendor;
    other_filament.alias = "Acme PLA";
    other_filament.config.option<ConfigOptionStrings>("compatible_printers")->values = {"Acme Printer 0.2"};

    CHECK_FALSE(bundle.has_errors());
    bool expected_errors = true;

    SECTION("A model default for one nozzle does not cover another nozzle") {
        model.default_materials = {"Acme PLA @0.4"};
    }
    SECTION("An empty default list leaves every variant uncovered") {
        model.default_materials.clear();
    }
    SECTION("An unknown filament cannot be a default") {
        model.default_materials = {"Missing PLA"};
    }
    SECTION("An unknown name is an error even when a compatible default covers the variant") {
        model.default_materials.insert(model.default_materials.begin(), "Missing PLA");
    }
    SECTION("An unknown default_filament_profile name is an error") {
        bundle.printers.find_preset("Acme Printer 0.6", false, true)
            ->config.option<ConfigOptionStrings>("default_filament_profile", true)->values = {"Missing PLA"};
    }
    SECTION("A known default_filament_profile name is not an error") {
        bundle.printers.find_preset("Acme Printer 0.6", false, true)
            ->config.option<ConfigOptionStrings>("default_filament_profile", true)->values = {"Acme PLA @0.6"};
        expected_errors = false;
    }
    SECTION("A short alias does not resolve as an installed default") {
        model.default_materials = {"Acme PLA"};
    }
    SECTION("A user filament cannot satisfy a shipped default") {
        bundle.filaments.find_preset("Acme PLA @0.6", false, true)->is_system = false;
    }
    SECTION("One compatible default per variant is sufficient") {
        model.default_materials.insert(model.default_materials.begin(), "Acme PLA @0.2");
        expected_errors = false;
    }
    SECTION("Compatibility conditions apply to each nozzle") {
        model.default_materials = {"Acme PLA @0.4"};
        Preset *filament = bundle.filaments.find_preset("Acme PLA @0.4", false, true);
        auto &library = bundle.vendors[PresetBundle::ORCA_FILAMENT_LIBRARY];
        library.id = library.name = PresetBundle::ORCA_FILAMENT_LIBRARY;
        filament->vendor = &library;
        filament->config.option<ConfigOptionStrings>("compatible_printers")->values.clear();
        filament->config.option<ConfigOptionString>("compatible_printers_condition")->value = "nozzle_diameter[0] == 0.4";
    }
    SECTION("Library defaults respect printer exclusions") {
        model.default_materials = {"Acme PLA @0.4"};
        Preset *filament = bundle.filaments.find_preset("Acme PLA @0.4", false, true);
        auto &library = bundle.vendors[PresetBundle::ORCA_FILAMENT_LIBRARY];
        library.id = library.name = PresetBundle::ORCA_FILAMENT_LIBRARY;
        filament->vendor = &library;
        filament->config.option<ConfigOptionStrings>("compatible_printers")->values.clear();
        CHECK_FALSE(bundle.check_printer_default_materials());
        filament->m_excluded_from.insert("Acme Printer 0.6");
    }
    SECTION("User printers do not need model defaults") {
        model.default_materials = {"Acme PLA @0.4"};
        bundle.printers.find_preset("Acme Printer 0.6", false, true)->is_system = false;
        expected_errors = false;
    }

    CHECK(bundle.check_printer_default_materials() == expected_errors);
    CHECK(bundle.has_errors() == expected_errors);
}

TEST_CASE("Plugin capability override keys are scoped per preset type", "[Preset][Plugin]")
{
    CHECK(Preset::plugin_overrides_key(Preset::TYPE_PRINT)    == std::string("print_plugin_config_overrides"));
    CHECK(Preset::plugin_overrides_key(Preset::TYPE_PRINTER)  == std::string("printer_plugin_config_overrides"));
    CHECK(Preset::plugin_overrides_key(Preset::TYPE_FILAMENT) == std::string("filament_plugin_config_overrides"));

    const std::pair<Preset::Type, const std::vector<std::string>*> scopes[] = {
        {Preset::TYPE_PRINT,    &Preset::print_options()},
        {Preset::TYPE_PRINTER,  &Preset::printer_options()},
        {Preset::TYPE_FILAMENT, &Preset::filament_options()},
    };
    for (const auto &owner : scopes)
        for (const auto &scoped : scopes) {
            const std::string key = Preset::plugin_overrides_key(scoped.first);
            CAPTURE(owner.first, key);
            CHECK(contains(*owner.second, key) == (owner.first == scoped.first));
        }
}

namespace {

struct LibraryFilamentTestCollection : public PresetCollection
{
    LibraryFilamentTestCollection()
        : PresetCollection(Preset::TYPE_FILAMENT, Preset::filament_options(),
                           static_cast<const PrintRegionConfig &>(FullPrintConfig::defaults()))
    {}
    using PresetCollection::update_library_profile_excluded_from;
};

}

TEST_CASE("Missing app config is accepted as default CLI state", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    AppConfig app_config;
    app_config.set_loading_path((dir.path() / "missing.conf").string());
    CHECK(app_config.load_if_exists().empty());
}

TEST_CASE("Read-only user preset loading does not create or delete files", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    PresetBundle       bundle;
    PresetsConfigSubstitutions substitutions;

    const fs::path missing_root = dir.path() / "missing-user";
    bundle.prints.load_presets(missing_root.string(), PRESET_PRINT_NAME, substitutions,
                               ForwardCompatibilitySubstitutionRule::EnableSilent, nullptr,
                               PresetOrigin(), true);
    CHECK_FALSE(fs::exists(missing_root / PRESET_PRINT_NAME));

    const fs::path malformed = dir.path() / "existing-user" / PRESET_PRINT_NAME / "malformed.json";
    fs::create_directories(malformed.parent_path());
    std::ofstream(malformed.string()) << "{not-json";
    bundle.prints.load_presets((dir.path() / "existing-user").string(), PRESET_PRINT_NAME, substitutions,
                               ForwardCompatibilitySubstitutionRule::EnableSilent, nullptr,
                               PresetOrigin(), true);
    CHECK(fs::exists(malformed));
}

TEST_CASE("Typeless preset resolution probes loaded FFF collections", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      source_file = dir.path() / "typeless-process.json";
    std::ofstream(source_file.string()) << R"({"name":"Typeless Process","from":"User"})";

    PresetBundle bundle;
    Preset      &process = add_inmemory_preset(bundle.prints, "Typeless Process");
    process.file = source_file.string();
    process.config.option<ConfigOptionFloats>("travel_speed", true)->values = {321.0};

    DynamicPrintConfig raw;
    Preset::Type       resolved_type = Preset::TYPE_INVALID;
    std::string        error;
    REQUIRE(bundle.resolve_preset_config_type(raw, resolved_type, source_file.string(),
                                              ForwardCompatibilitySubstitutionRule::EnableSilent, error, false));
    CHECK(error.empty());
    CHECK(resolved_type == Preset::TYPE_PRINT);
    REQUIRE(raw.option<ConfigOptionFloats>("travel_speed")->values.size() == 1);
    CHECK_THAT(raw.option<ConfigOptionFloats>("travel_speed")->values.front(), Catch::Matchers::WithinAbs(321.0, 1e-6));
}

TEST_CASE("Typeless preset resolution preserves duplicate identity ambiguity", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      source_file = dir.path() / "duplicate-process.json";
    std::ofstream(source_file.string()) << "{}";

    PresetBundle bundle;
    add_inmemory_preset(bundle.prints, "First Process Identity").file  = source_file.string();
    add_inmemory_preset(bundle.prints, "Second Process Identity").file = source_file.string();

    DynamicPrintConfig raw;
    Preset::Type       resolved_type = Preset::TYPE_INVALID;
    std::string        error;
    CHECK_FALSE(bundle.resolve_preset_config_type(raw, resolved_type, source_file.string(),
                                                  ForwardCompatibilitySubstitutionRule::EnableSilent, error, false));
    CHECK(error == "Preset identity is ambiguous");
    CHECK(resolved_type == Preset::TYPE_INVALID);
}

TEST_CASE("Typeless preset resolution rejects cross-type ambiguity", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      source_file = dir.path() / "ambiguous.json";
    std::ofstream(source_file.string()) << "{}";

    PresetBundle bundle;
    add_inmemory_preset(bundle.prints, "Process Identity").file       = source_file.string();
    add_inmemory_preset(bundle.filaments, "Filament Identity").file = source_file.string();

    DynamicPrintConfig raw;
    Preset::Type       resolved_type = Preset::TYPE_INVALID;
    std::string        error;
    CHECK_FALSE(bundle.resolve_preset_config_type(raw, resolved_type, source_file.string(),
                                                  ForwardCompatibilitySubstitutionRule::EnableSilent, error, false));
    CHECK(error == "Preset type is ambiguous");
    CHECK(resolved_type == Preset::TYPE_INVALID);
}

TEST_CASE("Typeless preset resolution rejects a missing type candidate", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      source_file = dir.path() / "unknown.json";
    std::ofstream(source_file.string()) << "{}";

    PresetBundle       bundle;
    DynamicPrintConfig raw;
    Preset::Type       resolved_type = Preset::TYPE_INVALID;
    std::string        error;
    CHECK_FALSE(bundle.resolve_preset_config_type(raw, resolved_type, source_file.string(),
                                                  ForwardCompatibilitySubstitutionRule::EnableSilent, error, false));
    CHECK(error == "Preset type could not be resolved");
    CHECK(resolved_type == Preset::TYPE_INVALID);
}

TEST_CASE("Exact file resolution rejects multiple preset identities", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      source_file = dir.path() / "duplicate.json";
    std::ofstream(source_file.string()) << "{}";

    PresetBundle bundle;
    Preset &first = add_inmemory_preset(bundle.prints, "First Identity");
    first.file = source_file.string();
    Preset &second = add_inmemory_preset(bundle.prints, "Second Identity");
    second.file = source_file.string();

    DynamicPrintConfig raw;
    raw.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "Parent";

    std::string error;
    CHECK_FALSE(bundle.resolve_preset_config(raw, Preset::TYPE_PRINT, source_file.string(),
                                             ForwardCompatibilitySubstitutionRule::EnableSilent, error, false));
    CHECK(error == "Preset identity is ambiguous");
}

TEST_CASE("System preset resolution returns the canonical vendor configuration", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir source_dir;
    PresetBundle bundle;

    VendorProfile vendor("VendorB");
    vendor.name = "Vendor B";
    auto [vendor_it, inserted] = bundle.vendors.emplace(vendor.id, std::move(vendor));
    REQUIRE(inserted);

    Preset &resolved = add_inmemory_preset(bundle.prints, "Vendor B Process", "fdm_process_common");
    resolved.is_system = true;
    resolved.vendor    = &vendor_it->second;
    resolved.file      = (source_dir.path() / "vendor-b-process.json").string();
    std::ofstream(resolved.file) << "{}";
    resolved.config.option<ConfigOptionFloats>("travel_speed", true)->values = {321.0};
    resolved.config.option<ConfigOptionInt>("wall_loops", true)->value       = 2;

    DynamicPrintConfig raw;
    raw.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "fdm_process_common";
    raw.option<ConfigOptionInt>("wall_loops", true)->value             = 5;

    std::string error;
    REQUIRE(bundle.resolve_preset_config(raw, Preset::TYPE_PRINT, resolved.file,
                                         ForwardCompatibilitySubstitutionRule::EnableSilent, error));
    CHECK(error.empty());
    REQUIRE(raw.option<ConfigOptionFloats>("travel_speed")->values.size() == 1);
    CHECK_THAT(raw.option<ConfigOptionFloats>("travel_speed")->values.front(), Catch::Matchers::WithinAbs(321.0, 1e-6));
    CHECK(raw.option<ConfigOptionInt>("wall_loops")->value == 2);
}

TEST_CASE("Manifest-backed preset resolution loads the source vendor tree", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      vendor_dir = dir.path() / "Acme";
    const fs::path      child_file = vendor_dir / "process" / "nested" / "child.json";

    std::ofstream((dir.path() / "Acme.json").string())
        << R"({"version":"1.0.0","name":"Acme","process_list":[)"
        << R"({"name":"fdm_process_common","sub_path":"process/base.json"},)"
        << R"({"name":"Acme Process","sub_path":"process/nested/child.json"}]})";
    fs::create_directories(child_file.parent_path());
    std::ofstream((vendor_dir / "process" / "base.json").string())
        << R"({"type":"process","name":"fdm_process_common","from":"system",)"
        << R"("instantiation":"false","travel_speed":["321"]})";
    std::ofstream(child_file.string())
        << R"({"type":"process","name":"Acme Process","from":"system",)"
        << R"("instantiation":"true","inherits":"fdm_process_common","wall_loops":"5"})";

    DynamicPrintConfig raw;
    raw.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "fdm_process_common";
    raw.option<ConfigOptionInt>("wall_loops", true)->value             = 5;

    PresetBundle bundle;
    std::string  error;
    REQUIRE(bundle.resolve_preset_config(raw, Preset::TYPE_PRINT, child_file.string(),
                                         ForwardCompatibilitySubstitutionRule::EnableSilent, error));
    CHECK(error.empty());
    REQUIRE(raw.option<ConfigOptionFloats>("travel_speed")->values.size() == 1);
    CHECK_THAT(raw.option<ConfigOptionFloats>("travel_speed")->values.front(), Catch::Matchers::WithinAbs(321.0, 1e-6));
    CHECK(raw.option<ConfigOptionInt>("wall_loops")->value == 5);
}

TEST_CASE("Manifest-backed resolution is scoped to the explicit source root", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    auto write_vendor = [&](const std::string &root_name, double travel_speed) {
        const fs::path root       = dir.path() / root_name;
        const fs::path child_file = root / "Acme" / "process" / "child.json";
        fs::create_directories(child_file.parent_path());
        std::ofstream((root / "Acme.json").string())
            << R"({"version":"1.0.0","name":"Acme","process_list":[)"
            << R"({"name":"fdm_process_common","sub_path":"process/base.json"},)"
            << R"({"name":"Acme Process","sub_path":"process/child.json"}]})";
        std::ofstream((root / "Acme" / "process" / "base.json").string())
            << R"({"type":"process","name":"fdm_process_common","from":"system",)"
            << R"("instantiation":"false","travel_speed":[")" << travel_speed << R"("]})";
        std::ofstream(child_file.string())
            << R"({"type":"process","name":"Acme Process","from":"system",)"
            << R"("instantiation":"true","inherits":"fdm_process_common"})";
        return child_file;
    };

    const fs::path source_a = write_vendor("root-a", 111.0);
    const fs::path source_b = write_vendor("root-b", 222.0);
    REQUIRE(fs::exists(source_a));

    DynamicPrintConfig raw;
    raw.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "synthetic-parent-marker";

    PresetBundle bundle;
    std::string  error;
    REQUIRE(bundle.resolve_preset_config(raw, Preset::TYPE_PRINT, source_b.string(),
                                         ForwardCompatibilitySubstitutionRule::EnableSilent, error));
    REQUIRE(raw.option<ConfigOptionFloats>("travel_speed")->values.size() == 1);
    CHECK_THAT(raw.option<ConfigOptionFloats>("travel_speed")->values.front(), Catch::Matchers::WithinAbs(222.0, 1e-6));
}

TEST_CASE("Exact-only resolution rejects an unconfigured manifest-backed file", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      source_file = dir.path() / "Acme" / "process" / "child.json";
    fs::create_directories(source_file.parent_path());
    std::ofstream((dir.path() / "Acme.json").string())
        << R"({"version":"1.0.0","name":"Acme","process_list":[)"
        << R"({"name":"Acme Process","sub_path":"process/child.json"}]})";
    std::ofstream(source_file.string())
        << R"({"type":"process","name":"Acme Process","from":"system",)"
        << R"("instantiation":"true","layer_height":"0.2"})";

    DynamicPrintConfig raw;
    raw.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "Some Parent";

    PresetBundle bundle;
    std::string  error;
    CHECK_FALSE(bundle.resolve_preset_config(raw, Preset::TYPE_PRINT, source_file.string(),
                                             ForwardCompatibilitySubstitutionRule::EnableSilent, error, false));
    CHECK(error == "Preset was not found in the loaded bundle");
}

TEST_CASE("Vendor filament resolution uses the shared Orca library base", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      library_dir = dir.path() / PresetBundle::ORCA_FILAMENT_LIBRARY;
    const fs::path      vendor_dir  = dir.path() / "Acme";
    const fs::path      child_file  = vendor_dir / "filament" / "nested" / "petg.json";

    std::ofstream((dir.path() / (std::string(PresetBundle::ORCA_FILAMENT_LIBRARY) + ".json")).string())
        << R"({"version":"1.0.0","name":"OrcaFilamentLibrary","filament_list":[)"
        << R"({"name":"fdm_filament_pet","sub_path":"filament/pet.json","filament_id":"GFL99"}]})";
    fs::create_directories(library_dir / "filament");
    std::ofstream((library_dir / "filament" / "pet.json").string())
        << R"({"type":"filament","name":"fdm_filament_pet","from":"system",)"
        << R"("filament_id":"GFL99","instantiation":"false",)"
        << R"("filament_type":["PETG"],"filament_density":["1.27"]})";

    std::ofstream((dir.path() / "Acme.json").string())
        << R"({"version":"1.0.0","name":"Acme","filament_list":[)"
        << R"({"name":"Acme PETG","sub_path":"filament/nested/petg.json","filament_id":"GFA00"}]})";
    fs::create_directories(child_file.parent_path());
    std::ofstream(child_file.string())
        << R"({"type":"filament","name":"Acme PETG","from":"system",)"
        << R"("filament_id":"GFA00","instantiation":"true","inherits":"fdm_filament_pet"})";

    DynamicPrintConfig raw;
    raw.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "fdm_filament_pet";

    PresetBundle bundle;
    std::string  error;
    REQUIRE(bundle.resolve_preset_config(raw, Preset::TYPE_FILAMENT, child_file.string(),
                                         ForwardCompatibilitySubstitutionRule::EnableSilent, error));
    CHECK(error.empty());
    CHECK(raw.opt_string("filament_type", 0u) == "PETG");
    REQUIRE(raw.option<ConfigOptionFloats>("filament_density")->values.size() == 1);
    CHECK_THAT(raw.option<ConfigOptionFloats>("filament_density")->values.front(), Catch::Matchers::WithinAbs(1.27, 1e-6));
}

TEST_CASE("Manifest-backed resolution rejects a missing parent", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      child_file = dir.path() / "Acme" / "process" / "child.json";

    std::ofstream((dir.path() / "Acme.json").string())
        << R"({"version":"1.0.0","name":"Acme","process_list":[)"
        << R"({"name":"Acme Process","sub_path":"process/child.json"}]})";
    fs::create_directories(child_file.parent_path());
    std::ofstream(child_file.string())
        << R"({"type":"process","name":"Acme Process","from":"system",)"
        << R"("instantiation":"true","inherits":"Missing Parent","layer_height":"0.2"})";

    DynamicPrintConfig raw;
    raw.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "Missing Parent";

    PresetBundle bundle;
    std::string  error;
    CHECK_FALSE(bundle.resolve_preset_config(raw, Preset::TYPE_PRINT, child_file.string(),
                                             ForwardCompatibilitySubstitutionRule::EnableSilent, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("Manifest-backed resolution rejects a vendor load with malformed entries", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      child_file = dir.path() / "Acme" / "process" / "child.json";

    std::ofstream((dir.path() / "Acme.json").string())
        << R"({"version":"1.0.0","name":"Acme","process_list":[123,)"
        << R"({"name":"Acme Process","sub_path":"process/child.json"}]})";
    fs::create_directories(child_file.parent_path());
    std::ofstream(child_file.string())
        << R"({"type":"process","name":"Acme Process","from":"system",)"
        << R"("instantiation":"true","layer_height":"0.2"})";

    DynamicPrintConfig raw;
    raw.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "fdm_process_common";

    PresetBundle bundle;
    std::string  error;
    CHECK_FALSE(bundle.resolve_preset_config(raw, Preset::TYPE_PRINT, child_file.string(),
                                             ForwardCompatibilitySubstitutionRule::EnableSilent, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("Manifest-backed resolution rejects files absent from the vendor manifest", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      listed_file   = dir.path() / "Acme" / "process" / "listed.json";
    const fs::path      unlisted_file = dir.path() / "Acme" / "process" / "unlisted.json";

    std::ofstream((dir.path() / "Acme.json").string())
        << R"({"version":"1.0.0","name":"Acme","process_list":[)"
        << R"({"name":"Listed Process","sub_path":"process/listed.json"}]})";
    fs::create_directories(listed_file.parent_path());
    std::ofstream(listed_file.string())
        << R"({"type":"process","name":"Listed Process","from":"system",)"
        << R"("instantiation":"true","layer_height":"0.2"})";
    std::ofstream(unlisted_file.string())
        << R"({"type":"process","name":"Unlisted Process","from":"system",)"
        << R"("instantiation":"true","inherits":"fdm_process_common"})";

    DynamicPrintConfig raw;
    raw.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "fdm_process_common";

    PresetBundle bundle;
    std::string  error;
    CHECK_FALSE(bundle.resolve_preset_config(raw, Preset::TYPE_PRINT, unlisted_file.string(),
                                             ForwardCompatibilitySubstitutionRule::EnableSilent, error));
    CHECK(error == "Source file is not an instantiated preset in its vendor manifest");
}

TEST_CASE("Manifest-backed resolution rejects a mismatched preset type", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      process_file = dir.path() / "Acme" / "process" / "child.json";

    std::ofstream((dir.path() / "Acme.json").string())
        << R"({"version":"1.0.0","name":"Acme","process_list":[)"
        << R"({"name":"Acme Process","sub_path":"process/child.json"}]})";
    fs::create_directories(process_file.parent_path());
    std::ofstream(process_file.string())
        << R"({"type":"process","name":"Acme Process","from":"system",)"
        << R"("instantiation":"true","layer_height":"0.2"})";

    DynamicPrintConfig raw;
    raw.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "fdm_filament_common";

    PresetBundle bundle;
    std::string  error;
    CHECK_FALSE(bundle.resolve_preset_config(raw, Preset::TYPE_FILAMENT, process_file.string(),
                                             ForwardCompatibilitySubstitutionRule::EnableSilent, error));
    CHECK(error == "Source file is not an instantiated preset in its vendor manifest");
}

TEST_CASE("Resolution terminates when no vendor manifest exists", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      detached_file = dir.path() / "detached.json";
    std::ofstream(detached_file.string()) << "{}";

    DynamicPrintConfig raw;
    raw.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "Missing Parent";

    PresetBundle bundle;
    std::string  error;
    CHECK_FALSE(bundle.resolve_preset_config(raw, Preset::TYPE_PRINT, detached_file.string(),
                                             ForwardCompatibilitySubstitutionRule::EnableSilent, error));
    CHECK(error == "Preset was not found in the loaded bundle");
}

TEST_CASE("Manifest-backed resolution reuses the vendor tree it already loaded", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      process_dir = dir.path() / "Acme" / "process";
    fs::create_directories(process_dir);
    std::ofstream((dir.path() / "Acme.json").string())
        << R"({"version":"1.0.0","name":"Acme","process_list":[)"
        << R"({"name":"fdm_process_common","sub_path":"process/base.json"},)"
        << R"({"name":"Acme First","sub_path":"process/first.json"},)"
        << R"({"name":"Acme Second","sub_path":"process/second.json"}]})";
    auto write_base = [&](double travel_speed) {
        std::ofstream((process_dir / "base.json").string())
            << R"({"type":"process","name":"fdm_process_common","from":"system",)"
            << R"("instantiation":"false","travel_speed":[")" << travel_speed << R"("]})";
    };
    auto write_child = [&](const std::string &file, const std::string &name) {
        std::ofstream((process_dir / file).string())
            << R"({"type":"process","name":")" << name << R"(","from":"system",)"
            << R"("instantiation":"true","inherits":"fdm_process_common"})";
    };
    write_base(111.0);
    write_child("first.json", "Acme First");
    write_child("second.json", "Acme Second");

    auto travel_speed = [&](PresetBundle &bundle, const std::string &file) {
        DynamicPrintConfig raw;
        raw.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "fdm_process_common";
        std::string error;
        REQUIRE(bundle.resolve_preset_config(raw, Preset::TYPE_PRINT, (process_dir / file).string(),
                                             ForwardCompatibilitySubstitutionRule::EnableSilent, error));
        return raw.option<ConfigOptionFloats>("travel_speed")->values.front();
    };

    PresetBundle bundle;
    CHECK_THAT(travel_speed(bundle, "first.json"), Catch::Matchers::WithinAbs(111.0, 1e-6));

    write_base(222.0);
    CHECK_THAT(travel_speed(bundle, "second.json"), Catch::Matchers::WithinAbs(111.0, 1e-6));

    PresetBundle fresh;
    CHECK_THAT(travel_speed(fresh, "second.json"), Catch::Matchers::WithinAbs(222.0, 1e-6));
}

TEST_CASE("Manifest-backed resolution does not keep a vendor tree that failed to load", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      child_file = dir.path() / "Acme" / "process" / "child.json";
    auto write_manifest = [&](const std::string &leading_entry) {
        std::ofstream((dir.path() / "Acme.json").string())
            << R"({"version":"1.0.0","name":"Acme","process_list":[)" << leading_entry
            << R"({"name":"Acme Process","sub_path":"process/child.json"}]})";
    };
    write_manifest("123,");
    fs::create_directories(child_file.parent_path());
    std::ofstream(child_file.string())
        << R"({"type":"process","name":"Acme Process","from":"system",)"
        << R"("instantiation":"true","layer_height":"0.2"})";

    PresetBundle bundle;
    auto resolve = [&](std::string &error) {
        DynamicPrintConfig raw;
        raw.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "fdm_process_common";
        return bundle.resolve_preset_config(raw, Preset::TYPE_PRINT, child_file.string(),
                                            ForwardCompatibilitySubstitutionRule::EnableSilent, error);
    };

    std::string error;
    CHECK_FALSE(resolve(error));
    CHECK_FALSE(error.empty());

    write_manifest("");
    error.clear();
    CHECK(resolve(error));
    CHECK(error.empty());
}

TEST_CASE("Manifest-backed resolution reuses the library base for type-probed files", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      library_pet  = dir.path() / PresetBundle::ORCA_FILAMENT_LIBRARY / "filament" / "pet.json";
    const fs::path      filament_dir = dir.path() / "Acme" / "filament";

    std::ofstream((dir.path() / (std::string(PresetBundle::ORCA_FILAMENT_LIBRARY) + ".json")).string())
        << R"({"version":"1.0.0","name":"OrcaFilamentLibrary","filament_list":[)"
        << R"({"name":"fdm_filament_pet","sub_path":"filament/pet.json","filament_id":"GFL99"}]})";
    fs::create_directories(library_pet.parent_path());
    auto write_library_pet = [&](double density) {
        std::ofstream(library_pet.string())
            << R"({"type":"filament","name":"fdm_filament_pet","from":"system",)"
            << R"("filament_id":"GFL99","instantiation":"false",)"
            << R"("filament_type":["PETG"],"filament_density":[")" << density << R"("]})";
    };
    write_library_pet(1.27);

    std::ofstream((dir.path() / "Acme.json").string())
        << R"({"version":"1.0.0","name":"Acme","filament_list":[)"
        << R"({"name":"Acme PETG","sub_path":"filament/petg.json","filament_id":"GFA00"},)"
        << R"({"name":"Acme PETG Matte","sub_path":"filament/petg_matte.json","filament_id":"GFA01"}]})";
    fs::create_directories(filament_dir);
    auto write_child = [&](const std::string &file, const std::string &name, const std::string &filament_id) {
        std::ofstream((filament_dir / file).string())
            << R"({"type":"filament","name":")" << name << R"(","from":"system",)"
            << R"("filament_id":")" << filament_id << R"(","instantiation":"true","inherits":"fdm_filament_pet"})";
    };
    write_child("petg.json", "Acme PETG", "GFA00");
    write_child("petg_matte.json", "Acme PETG Matte", "GFA01");

    auto density = [](const DynamicPrintConfig &config) {
        return config.option<ConfigOptionFloats>("filament_density")->values.front();
    };

    PresetBundle       bundle;
    DynamicPrintConfig first;
    first.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "fdm_filament_pet";
    std::string error;
    REQUIRE(bundle.resolve_preset_config(first, Preset::TYPE_FILAMENT, (filament_dir / "petg.json").string(),
                                         ForwardCompatibilitySubstitutionRule::EnableSilent, error));
    CHECK_THAT(density(first), Catch::Matchers::WithinAbs(1.27, 1e-6));

    write_library_pet(1.5);

    DynamicPrintConfig second;
    Preset::Type       type = Preset::TYPE_INVALID;
    REQUIRE(bundle.resolve_preset_config_type(second, type, (filament_dir / "petg_matte.json").string(),
                                              ForwardCompatibilitySubstitutionRule::EnableSilent, error));
    CHECK(type == Preset::TYPE_FILAMENT);
    CHECK_THAT(density(second), Catch::Matchers::WithinAbs(1.27, 1e-6));
}

TEST_CASE("Manifest-backed resolution shares the library between vendors under one root", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir dir;
    const fs::path      library_dir = dir.path() / PresetBundle::ORCA_FILAMENT_LIBRARY / "filament";

    std::ofstream((dir.path() / (std::string(PresetBundle::ORCA_FILAMENT_LIBRARY) + ".json")).string())
        << R"({"version":"1.0.0","name":"OrcaFilamentLibrary","filament_list":[)"
        << R"({"name":"fdm_filament_pet","sub_path":"filament/pet.json","filament_id":"GFL99"},)"
        << R"({"name":"Generic PETG","sub_path":"filament/generic_petg.json","filament_id":"GFL98"}]})";
    fs::create_directories(library_dir);
    auto write_library_pet = [&](double density) {
        std::ofstream((library_dir / "pet.json").string())
            << R"({"type":"filament","name":"fdm_filament_pet","from":"system",)"
            << R"("filament_id":"GFL99","instantiation":"false",)"
            << R"("filament_type":["PETG"],"filament_density":[")" << density << R"("]})";
    };
    write_library_pet(1.27);
    std::ofstream((library_dir / "generic_petg.json").string())
        << R"({"type":"filament","name":"Generic PETG","from":"system",)"
        << R"("filament_id":"GFL98","instantiation":"true","inherits":"fdm_filament_pet"})";

    auto write_vendor = [&](const std::string &vendor, const std::string &filament_id) {
        const fs::path filament_dir = dir.path() / vendor / "filament";
        fs::create_directories(filament_dir);
        std::ofstream((dir.path() / (vendor + ".json")).string())
            << R"({"version":"1.0.0","name":")" << vendor << R"(","filament_list":[)"
            << R"({"name":")" << vendor << R"( PETG","sub_path":"filament/petg.json","filament_id":")" << filament_id << R"("}]})";
        std::ofstream((filament_dir / "petg.json").string())
            << R"({"type":"filament","name":")" << vendor << R"( PETG","from":"system",)"
            << R"("filament_id":")" << filament_id << R"(","instantiation":"true","inherits":"fdm_filament_pet"})";
        return filament_dir / "petg.json";
    };
    const fs::path acme_petg = write_vendor("Acme", "GFA00");
    const fs::path beta_petg = write_vendor("Beta", "GFB00");

    auto density = [&](PresetBundle &bundle, const fs::path &file) {
        DynamicPrintConfig raw;
        raw.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "fdm_filament_pet";
        std::string error;
        REQUIRE(bundle.resolve_preset_config(raw, Preset::TYPE_FILAMENT, file.string(),
                                             ForwardCompatibilitySubstitutionRule::EnableSilent, error));
        return raw.option<ConfigOptionFloats>("filament_density")->values.front();
    };

    PresetBundle bundle;
    CHECK_THAT(density(bundle, acme_petg), Catch::Matchers::WithinAbs(1.27, 1e-6));

    write_library_pet(1.5);
    CHECK_THAT(density(bundle, beta_petg), Catch::Matchers::WithinAbs(1.27, 1e-6));
    CHECK_THAT(density(bundle, library_dir / "generic_petg.json"), Catch::Matchers::WithinAbs(1.27, 1e-6));

    PresetBundle fresh;
    CHECK_THAT(density(fresh, beta_petg), Catch::Matchers::WithinAbs(1.5, 1e-6));
}

TEST_CASE("A printer specific filament supersedes the generic library filament with the same alias", "[Preset][Bundle]")
{
    LibraryFilamentTestCollection filaments;
    PresetCollection              printers(Preset::TYPE_PRINTER, Preset::printer_options(),
                                           static_cast<const PrintRegionConfig &>(FullPrintConfig::defaults()));
    VendorProfile                 library(PresetBundle::ORCA_FILAMENT_LIBRARY);
    VendorProfile                 vendor("Vendor");
    library.name = PresetBundle::ORCA_FILAMENT_LIBRARY;
    vendor.name  = "Vendor";

    auto add_filament = [&filaments](const VendorProfile &owner, const std::string &name, std::vector<std::string> compatible_printers) {
        Preset &preset = add_inmemory_preset(filaments, name);
        preset.alias   = "Generic ABS";
        preset.vendor  = &owner;
        preset.config.option<ConfigOptionStrings>("compatible_printers", true)->values = std::move(compatible_printers);
    };

    add_filament(library, "Generic ABS @System", {});
    add_filament(library, "Generic ABS @Printer A", { "Printer A" });
    add_filament(vendor,  "Generic ABS @Printer B", { "Printer B" });

    filaments.update_library_profile_excluded_from();

    const Preset *generic = filaments.find_preset("Generic ABS @System");
    REQUIRE(generic != nullptr);
    CHECK(generic->m_excluded_from.count("Printer A") == 1);
    CHECK(generic->m_excluded_from.count("Printer B") == 1);
    CHECK(generic->m_excluded_from.size() == 2);

    const Preset *specific = filaments.find_preset("Generic ABS @Printer A");
    REQUIRE(specific != nullptr);
    CHECK(specific->m_excluded_from.empty());

    add_inmemory_preset(printers, "Printer A");
    add_inmemory_preset(printers, "Printer C");
    const Preset *printer_a = printers.find_preset("Printer A");
    const Preset *printer_c = printers.find_preset("Printer C");
    REQUIRE(printer_a != nullptr);
    REQUIRE(printer_c != nullptr);

    const PresetWithVendorProfile generic_lib(*generic, &library);
    CHECK_FALSE(is_compatible_with_printer(generic_lib, PresetWithVendorProfile(*printer_a, nullptr)));
    CHECK(is_compatible_with_printer(generic_lib, PresetWithVendorProfile(*printer_c, nullptr)));
}

namespace {

struct MachineFilaments
{
    PresetBundle  bundle;
    VendorProfile library{PresetBundle::ORCA_FILAMENT_LIBRARY};
    VendorProfile vendor{"Vendor"};

    MachineFilaments()
    {
        library.name = PresetBundle::ORCA_FILAMENT_LIBRARY;
        vendor.name  = "Vendor";

        Preset &printer = add_inmemory_preset(bundle.printers, "Printer A 0.4 nozzle");
        printer.is_system = true;
        printer.vendor    = &vendor;
        printer.config.option<ConfigOptionString>("printer_model", true)->value = "Printer A";

        add_filament(library, "Generic ABS @System",    "Generic ABS", {});
        add_filament(vendor,  "Generic ABS @Printer A", "Generic ABS", { "Printer A 0.4 nozzle" });
        add_filament(library, "FilAr ABS @System",      "FilAr ABS",   {});
        add_filament(vendor,  "Vendor PLA @Printer B",  "Vendor PLA",  { "Printer B 0.4 nozzle" });

        Preset *shadowed = bundle.filaments.find_preset("Generic ABS @System");
        REQUIRE(shadowed != nullptr);
        shadowed->m_excluded_from.insert("Printer A 0.4 nozzle");
    }

    void add_filament(const VendorProfile &owner, const std::string &name, const std::string &alias,
                      std::vector<std::string> compatible_printers)
    {
        Preset &preset = add_inmemory_preset(bundle.filaments, name);
        preset.is_system = true;
        preset.alias     = alias;
        preset.vendor    = &owner;
        compatible_list(bundle.filaments, name, "compatible_printers") = std::move(compatible_printers);
    }

    bool offers(const std::string &preset_name, bool include_user_presets = false)
    {
        const std::vector<Preset *> offered =
            bundle.get_filament_presets_for_machine("Printer A", "0.4", include_user_presets);
        return std::any_of(offered.begin(), offered.end(),
                           [&preset_name](const Preset *p) { return p->name == preset_name; });
    }
};

}

TEST_CASE("Filaments offered for a machine follow the app's compatibility rule", "[Preset][Bundle]")
{
    MachineFilaments f;

    SECTION("a library filament with no compatible_printers is offered") {
        CHECK(f.offers("FilAr ABS @System"));
    }

    SECTION("a same alias vendor filament shadows the library generic") {
        CHECK(f.offers("Generic ABS @Printer A"));
        CHECK_FALSE(f.offers("Generic ABS @System"));
    }

    SECTION("a filament naming a different printer is not offered") {
        CHECK_FALSE(f.offers("Vendor PLA @Printer B"));
    }

    SECTION("a user filament is offered only when the printer supports user presets") {
        add_inmemory_preset(f.bundle.filaments, "My PLA");

        CHECK_FALSE(f.offers("My PLA", false));
        CHECK(f.offers("My PLA", true));
    }
}


namespace {

const char *kMixedKeys[] = {
    "filament_is_mixed",
    "filament_mixed_components",
    "filament_mixed_sublayer_ratios",
    "filament_mixed_gradient",
    "filament_mixed_gradient_range",
    "filament_mixed_gradient_curve",
    "filament_mixed_gradient_per_part",
};

}
TEST_CASE("set_num_filaments keeps mixed-color arrays in step with the filament count", "[Preset][Bundle][FilamentMixer]")
{
    auto mixed_array_size = [](const DynamicPrintConfig &cfg, const std::string &key) -> size_t {
        if (const auto *b = cfg.option<ConfigOptionBools>(key))
            return b->values.size();
        if (const auto *s = cfg.option<ConfigOptionStrings>(key))
            return s->values.size();
        return size_t(-1);
    };

    PresetBundle bundle;

    const unsigned int n = GENERATE(2u, 4u, 8u);
    bundle.set_num_filaments(n, std::string("#FF0000"));

    REQUIRE(bundle.project_config.option<ConfigOptionStrings>("filament_colour")->values.size() == n);
    for (const char *key : kMixedKeys) {
        DYNAMIC_SECTION("grown: " << key) {
            CHECK(mixed_array_size(bundle.project_config, key) == n);
        }
    }

    SECTION("shrinking keeps them in step too") {
        bundle.set_num_filaments(1, std::string("#00FF00"));
        REQUIRE(bundle.project_config.option<ConfigOptionStrings>("filament_colour")->values.size() == 1);
        for (const char *key : kMixedKeys)
            CHECK(mixed_array_size(bundle.project_config, key) == 1);
    }
}

TEST_CASE("Mixed-color filament metadata is snapshotted per printer, with its filament list", "[Preset][Bundle][FilamentMixer]")
{
    PresetBundle bundle;
    add_inmemory_preset(bundle.printers, "Test Printer");
    bundle.printers.select_preset_by_name("Test Printer", true);
    bundle.set_num_filaments(2u, std::string("#FF0000"));
    bundle.project_config.option<ConfigOptionBools>("filament_is_mixed")->values          = { false, true };
    bundle.project_config.option<ConfigOptionStrings>("filament_mixed_components")->values = { "", "1,2" };
    bundle.project_config.option<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values = { "", "0.5,0.5" };

    AppConfig app_config;
    bundle.export_selections(app_config);

    const std::string printer_name = bundle.printers.get_selected_preset_name();
    for (const char *key : kMixedKeys) {
        DYNAMIC_SECTION("per printer, not global: " << key) {
            CHECK(app_config.has_printer_setting(printer_name, key));
            CHECK_FALSE(app_config.has("presets", key));
        }
    }

    SECTION("with the encoding load_selections reads back") {
        CHECK(app_config.get_printer_setting(printer_name, "filament_is_mixed") == "0,1");
        CHECK(app_config.get_printer_setting(printer_name, "filament_mixed_components") == "|1,2");
        CHECK(app_config.get_printer_setting(printer_name, "filament_mixed_sublayer_ratios") == "|0.5,0.5");
    }
}

TEST_CASE("A multi-point gradient curve survives the app-config snapshot", "[Preset][Bundle][FilamentMixer]")
{
    const std::vector<std::string> curves = { "", "", "0,0|0.5,0.3|1,1" };

    PresetBundle bundle;
    add_inmemory_preset(bundle.printers, "Test Printer");
    bundle.printers.select_preset_by_name("Test Printer", true);
    bundle.set_num_filaments(3u, std::string("#FF0000"));
    bundle.project_config.option<ConfigOptionStrings>("filament_mixed_gradient_curve")->values = curves;

    AppConfig app_config;
    bundle.export_selections(app_config);

    std::vector<std::string> decoded;
    REQUIRE(unescape_strings_cstyle(
        app_config.get_printer_setting(bundle.printers.get_selected_preset_name(), "filament_mixed_gradient_curve"), decoded));
    CHECK(decoded == curves);
}

TEST_CASE("Sizing the filament list to a multi-tool nozzle count keeps mixed slots", "[Preset][Bundle][FilamentMixer]")
{
    const size_t nozzle_count = 4;
    PresetBundle bundle;
    bundle.set_num_filaments(5u, std::string("#FF0000"));
    bundle.project_config.option<ConfigOptionBools>("filament_is_mixed")->values =
        { false, false, false, false, true };
    bundle.project_config.option<ConfigOptionStrings>("filament_mixed_components")->values =
        { "", "", "", "", "2,3" };

    REQUIRE(bundle.num_mixed_filaments() == 1);

    SECTION("nozzle count plus the mixed slots preserves the mix") {
        bundle.set_num_filaments(nozzle_count + bundle.num_mixed_filaments(), std::string("#00FF00"));

        CHECK(bundle.filament_presets.size() == 5);
        CHECK(bundle.num_mixed_filaments() == 1);
        CHECK(bundle.is_mixed_filament(4));
        CHECK(bundle.project_config.option<ConfigOptionStrings>("filament_mixed_components")->values[4] == "2,3");
    }

    SECTION("the nozzle count alone is what truncated it away") {
        bundle.set_num_filaments(nozzle_count, std::string("#00FF00"));

        CHECK(bundle.filament_presets.size() == nozzle_count);
        CHECK(bundle.num_mixed_filaments() == 0);
    }
}

TEST_CASE("Published 3MF overlays only the author-selected process keys onto the edited preset", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000" };
        config.opt_float("layer_height") = 0.28;
        config.opt<ConfigOptionFloats>("wiping_volumes_extruders")->values = { 140., 150. };
        config.opt<ConfigOptionStrings>("post_process")->values = { "script-a", "script-b" };
        config.opt<ConfigOptionInts>("nozzle_temperature")->values = { 220 };
        config.opt_string("print_settings_id", true) = "file process";
        config.opt<ConfigOptionFloats>("flush_multiplier")->values = { 2., 2. };
        config.opt<ConfigOptionFloats>("wipe_tower_x")->values = { 100. };
        config.opt<ConfigOptionFloat>("wipe_tower_rotation_angle")->value = 45.;
        config.option("curr_bed_type")->setInt(BedType::btPC);
        return config;
    };

    const std::vector<std::string> published_keys = {
        "layer_height", "wiping_volumes_extruders", "post_process", "nozzle_temperature", "print_settings_id"
    };

    PresetBundle bundle;
    const std::string pre_load_name = bundle.prints.get_edited_preset().name;
    const size_t      pre_load_size = bundle.prints.size();

    bundle.prints.get_edited_preset().config.opt_float("layer_height") = 0.1;
    bundle.prints.get_edited_preset().config.opt<ConfigOptionFloats>("wiping_volumes_extruders")->values = { 10., 20. };
    bundle.prints.get_edited_preset().config.opt<ConfigOptionStrings>("post_process")->values = { "existing-script" };
    bundle.prints.get_edited_preset().config.opt_string("print_settings_id") = "user process";
    const std::vector<std::string> seed_filament_colour  = bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values;
    const std::vector<double>      seed_flush_multiplier = bundle.project_config.opt<ConfigOptionFloats>("flush_multiplier")->values;
    const int                      seed_bed_type         = bundle.project_config.option("curr_bed_type")->getInt();

    DynamicPrintConfig config = make_file_config();
    Preset::normalize(config);

    PublishedConfig pub;
    pub.published      = true;
    pub.published_keys = published_keys;
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    CHECK_THAT(bundle.prints.get_edited_preset().config.opt_float("layer_height"), Catch::Matchers::WithinAbs(0.28, 0.000001));
    check_double_vector(bundle.prints.get_edited_preset().config.opt<ConfigOptionFloats>("wiping_volumes_extruders")->values, { 140., 150. });
    CHECK(bundle.prints.get_edited_preset().config.opt<ConfigOptionStrings>("post_process")->values == std::vector<std::string>{ "existing-script" });
    CHECK(contains_key(pub.skipped_keys, "post_process"));
    CHECK_FALSE(contains_key(pub.skipped_keys, "layer_height"));
    CHECK_FALSE(contains_key(pub.skipped_keys, "wiping_volumes_extruders"));

    CHECK(bundle.prints.get_edited_preset().config.option("nozzle_temperature") == nullptr);
    CHECK(contains_key(pub.skipped_keys, "nozzle_temperature"));

    CHECK(bundle.prints.get_edited_preset().config.opt_string("print_settings_id") == "user process");
    CHECK_FALSE(contains_key(pub.skipped_keys, "print_settings_id"));

    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values == seed_filament_colour);
    CHECK(bundle.project_config.opt<ConfigOptionFloats>("flush_multiplier")->values == seed_flush_multiplier);
    CHECK(bundle.project_config.option("curr_bed_type")->getInt() == seed_bed_type);
    check_double_vector(bundle.project_config.opt<ConfigOptionFloats>("wipe_tower_x")->values, { 100. });
    CHECK_THAT(bundle.project_config.opt<ConfigOptionFloat>("wipe_tower_rotation_angle")->value, Catch::Matchers::WithinAbs(45., 0.000001));

    CHECK(bundle.prints.get_edited_preset().name == pre_load_name);
    CHECK(bundle.prints.size() == pre_load_size);

    PresetBundle     control_bundle;
    const size_t     control_pre_size = control_bundle.prints.size();
    PublishedConfig  control_pub;
    control_pub.published      = false;
    control_pub.published_keys = published_keys;
    DynamicPrintConfig control_config = make_file_config();
    Preset::normalize(control_config);
    control_bundle.load_config_model("test.3mf", std::move(control_config), Semver(), &control_pub);

    CHECK(control_pub.skipped_keys.empty());
    CHECK(control_bundle.prints.size() > control_pre_size);
    CHECK_THAT(control_bundle.prints.get_edited_preset().config.opt_float("layer_height"), Catch::Matchers::WithinAbs(0.28, 0.000001));
}

TEST_CASE("Published 3MF overlays only the allowlisted retraction and z-hop keys onto the edited printer preset", "[Preset][Bundle][Published]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000" };
    Preset::normalize(config);
    config.opt<ConfigOptionFloats>("retraction_length")->values = { 1.4 };
    config.opt<ConfigOptionFloats>("retraction_speed")->values = { 45., 55. };
    config.opt_string("machine_start_gcode") = "G28 ; from file";

    PresetBundle bundle;
    bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_length")->values = { 0.8 };
    bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_speed")->values = { 33. };
    bundle.printers.get_edited_preset().config.opt_string("machine_start_gcode") = "G28 ; user";

    PublishedConfig pub;
    pub.published      = true;
    pub.published_keys = { "retraction_length", "retraction_speed", "machine_start_gcode" };
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    check_double_vector(bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_length")->values, { 1.4 });
    check_double_vector(bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_speed")->values, { 33. });
    CHECK(contains_key(pub.skipped_keys, "retraction_speed"));
    CHECK(bundle.printers.get_edited_preset().config.opt_string("machine_start_gcode") == "G28 ; user");
    CHECK_FALSE(contains_key(pub.skipped_keys, "machine_start_gcode"));
}

TEST_CASE("Published 3MF applies positional material keys onto the receiver's material presets", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard", "Direct Drive Standard" };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00FF00" };
        config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA", "PETG" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values = { "GFL99", "GFT99" };
        config.option<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.9, 1.2 };
        config.option<ConfigOptionFloatsNullable>("filament_z_hop", true)->values = { 0.2, 0.3 };
        return config;
    };

    PresetBundle bundle;
    Preset &pla  = add_inmemory_preset(bundle.filaments, "My PLA");
    pla.filament_id = "GFL99";
    pla.config.opt_string("filament_type", 0u)   = "PLA";
    pla.config.opt_string("filament_vendor", 0u) = "Generic";
    pla.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
    pla.config.opt<ConfigOptionStrings>("filament_settings_id")->values = { "receiver-pla" };
    Preset &petg = add_inmemory_preset(bundle.filaments, "My PETG");
    petg.filament_id = "GFT99";
    petg.config.opt_string("filament_type", 0u)   = "PETG";
    petg.config.opt_string("filament_vendor", 0u) = "Generic";
    petg.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.6 };
    petg.config.opt<ConfigOptionFloatsNullable>("filament_z_hop", true)->values = { 0.1 };
    bundle.filament_presets = { "My PLA", "My PETG" };

    PublishedMaterialEntry pla_entry;
    pla_entry.filament_id = "GFL99";
    pla_entry.slot        = 0;
    pla_entry.keys        = { "filament_retraction_length", "filament_settings_id" };
    PublishedMaterialEntry petg_entry;
    petg_entry.filament_id = "GFT99";
    petg_entry.slot        = 1;
    petg_entry.keys        = { "filament_retraction_length", "filament_z_hop" };
    PublishedMaterialEntry noslot_entry;
    noslot_entry.filament_type = "ABS";
    noslot_entry.keys          = { "filament_retraction_length" };

    PublishedConfig pub;
    pub.published      = true;
    pub.material_keys  = { pla_entry, petg_entry, noslot_entry };
    DynamicPrintConfig config = make_file_config();
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    check_double_vector(bundle.filaments.find_preset("My PLA")->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
    check_double_vector(bundle.filaments.find_preset("My PETG")->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 1.2 });
    check_double_vector(bundle.filaments.find_preset("My PETG")->config.opt<ConfigOptionFloatsNullable>("filament_z_hop")->values, { 0.3 });
    CHECK(bundle.filaments.find_preset("My PLA")->config.opt<ConfigOptionStrings>("filament_settings_id")->values == std::vector<std::string>{ "receiver-pla" });
    CHECK_FALSE(contains_key(pub.skipped_keys, "material:GFL99 (filament_settings_id)"));
    CHECK(pub.skipped_keys.empty());
}

TEST_CASE("Published 3MF full-published slots are imported as standalone detached copies", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard", "Direct Drive Standard" };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00FF00" };
        config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA", "PETG" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values = { "GFL99", "GFT99" };
        config.option<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.9, 1.2 };
        return config;
    };
    auto make_full_abs_entry = [] {
        PublishedMaterialEntry entry;
        entry.slot                = 0;
        entry.full                = true;
        entry.publish_type        = true;
        entry.publish_type_value  = "ABS";
        entry.full_keys           = { "filament_retraction_length" };
        return entry;
    };

    SECTION("type match still creates a detached copy instead of mutating the receiver material") {
        PresetBundle bundle;
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        pla.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
        bundle.filament_presets = { "My PLA", "My PLA" };

        PublishedMaterialEntry full = make_full_abs_entry();
        full.publish_type_value     = "PLA";

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { full };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        CHECK(bundle.filament_presets[0] == "PLA");
        Preset *copy = bundle.filaments.find_preset("PLA", false, true);
        REQUIRE(copy != nullptr);
        check_double_vector(copy->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
        check_double_vector(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.5 });
        CHECK(pub.skipped_keys.empty());
        REQUIRE(pub.material_replacements.size() == 1);
        CHECK(pub.material_replacements[0] == "slot 0: My PLA -> PLA");
    }

    SECTION("type mismatch also detaches: a fresh same-type copy replaces the slot") {
        PresetBundle bundle;
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        pla.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
        Preset &abs = add_inmemory_preset(bundle.filaments, "My ABS");
        abs.config.opt_string("filament_type", 0u) = "ABS";
        abs.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.3 };
        bundle.filament_presets = { "My PLA" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { make_full_abs_entry() };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 1);
        CHECK(bundle.filament_presets[0] == "ABS");
        Preset *copy = bundle.filaments.find_preset("ABS", false, true);
        REQUIRE(copy != nullptr);
        check_double_vector(copy->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
        check_double_vector(bundle.filaments.find_preset("My ABS", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.3 });
        check_double_vector(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.5 });
        CHECK(pub.skipped_keys.empty());
        REQUIRE(pub.material_replacements.size() == 1);
        CHECK(pub.material_replacements[0] == "slot 0: My PLA -> ABS");
    }

    SECTION("the author's identity rides on the copy when the type has no library match") {
        PresetBundle bundle;
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        pla.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
        Preset &other = add_inmemory_preset(bundle.filaments, "Other PLA");
        other.config.opt_string("filament_type", 0u) = "PLA";
        other.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.7 };
        bundle.filament_presets = { "My PLA" };

        PublishedMaterialEntry full = make_full_abs_entry();
        full.full_keys = { "filament_retraction_length", "filament_type", "filament_vendor" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { full };
        DynamicPrintConfig config = make_file_config();
        config.opt<ConfigOptionStrings>("filament_type")->values = { "ABS", "PETG" };
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        CHECK(bundle.filament_presets[0] == "ABS");
        Preset *copy = bundle.filaments.find_preset("ABS", false, true);
        REQUIRE(copy != nullptr);
        check_double_vector(copy->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
        CHECK(copy->config.opt_string("filament_type", 0u) == "ABS");
        CHECK(copy->config.opt_string("filament_vendor", 0u) == "Generic");
        check_double_vector(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.5 });
        CHECK(bundle.filaments.find_preset("My PLA", false, true)->config.opt_string("filament_type", 0u) == "PLA");
        check_double_vector(bundle.filaments.find_preset("Other PLA", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.7 });
        CHECK(pub.skipped_keys.empty());
        REQUIRE(pub.material_replacements.size() == 1);
        CHECK(pub.material_replacements[0] == "slot 0: My PLA -> ABS");
    }
}

TEST_CASE("Published 3MF imports a full material under the author's stripped name", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard" };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000" };
        config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values = { "OGFL99" };
        config.option<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.9 };
        return config;
    };

    auto add_pla = [](PresetBundle &bundle, const char *name, const char *id, const char *vendor, const char *setting_id) {
        Preset &preset = add_inmemory_preset(bundle.filaments, name);
        preset.filament_id = id;
        preset.setting_id  = setting_id;
        preset.config.opt_string("filament_type", 0u) = "PLA";
        preset.config.opt_string("filament_vendor", 0u) = vendor;
        preset.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
        return &preset;
    };

    SECTION("an exact-name library preset exists: a detached copy is created beside it") {
        PresetBundle bundle;
        Preset &petg = add_inmemory_preset(bundle.filaments, "My PETG");
        petg.config.opt_string("filament_type", 0u) = "PETG";
        petg.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.6 };
        add_pla(bundle, "Generic PLA @System", "OGFL99", "Generic", "RcBNzytWgwRrwXXz");
        add_pla(bundle, "Bambu PLA Basic @System", "OGFA00", "Bambu Lab", "zkc85XTKi4cb6cOw");
        bundle.filament_presets = { "My PETG" };

        PublishedMaterialEntry entry;
        entry.slot               = 0;
        entry.full               = true;
        entry.publish_type       = true;
        entry.publish_type_value = "PLA";
        entry.filament_id        = "OGFL99";
        entry.filament_vendor    = "Generic";
        entry.setting_id         = "RcBNzytWgwRrwXXz";
        entry.preset_name        = "Generic PLA @System";
        entry.full_keys          = { "filament_retraction_length" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { entry };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        CHECK(bundle.filament_presets[0] == "Generic PLA");
        check_double_vector(bundle.filaments.find_preset("Generic PLA", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
        check_double_vector(bundle.filaments.find_preset("Generic PLA @System", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.5 });
        check_double_vector(bundle.filaments.find_preset("My PETG", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.6 });
        REQUIRE(pub.material_replacements.size() == 1);
        CHECK(pub.material_replacements[0] == "slot 0: My PETG -> Generic PLA");
        CHECK(pub.skipped_keys.empty());
    }

    SECTION("only the name is present (broken/stale ids): the copy is still created") {
        PresetBundle bundle;
        Preset &petg = add_inmemory_preset(bundle.filaments, "My PETG");
        petg.config.opt_string("filament_type", 0u) = "PETG";
        petg.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.6 };
        add_pla(bundle, "Generic PLA @System", "OGFL99", "Generic", "RcBNzytWgwRrwXXz");
        add_pla(bundle, "Bambu PLA Basic @System", "OGFA00", "Bambu Lab", "zkc85XTKi4cb6cOw");
        bundle.filament_presets = { "My PETG" };

        PublishedMaterialEntry entry;
        entry.slot               = 0;
        entry.full               = true;
        entry.publish_type       = true;
        entry.publish_type_value = "PLA";
        entry.preset_name        = "Generic PLA @System";
        entry.full_keys          = { "filament_retraction_length" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { entry };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        CHECK(bundle.filament_presets[0] == "Generic PLA");
        check_double_vector(bundle.filaments.find_preset("Bambu PLA Basic @System", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.5 });
        REQUIRE(pub.material_replacements.size() == 1);
        CHECK(pub.material_replacements[0] == "slot 0: My PETG -> Generic PLA");
        CHECK(pub.skipped_keys.empty());
    }

    SECTION("grown slot (author slot 1) receives its own detached copy") {
        auto two_slot_config = [] {
            DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
            config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75 };
            config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2 };
            config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard", "Direct Drive Standard" };
            config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#FFFF00" };
            config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA", "PLA" };
            config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic" };
            config.opt<ConfigOptionStrings>("filament_ids")->values = { "OGFL99", "OGFL99" };
            config.option<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.9, 0.8 };
            return config;
        };

        PresetBundle bundle;
        Preset &petg = add_inmemory_preset(bundle.filaments, "My PETG");
        petg.config.opt_string("filament_type", 0u) = "PETG";
        petg.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.6 };
        add_pla(bundle, "Generic PLA @System", "OGFL99", "Generic", "RcBNzytWgwRrwXXz");
        add_pla(bundle, "Bambu PLA Basic @System", "OGFA00", "Bambu Lab", "zkc85XTKi4cb6cOw");
        bundle.filament_presets = { "My PETG" };

        PublishedMaterialEntry entry;
        entry.slot               = 1;
        entry.full               = true;
        entry.publish_type       = true;
        entry.publish_type_value = "PLA";
        entry.preset_name        = "Generic PLA @System";
        entry.full_keys          = { "filament_retraction_length" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { entry };
        DynamicPrintConfig config = two_slot_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 2);
        CHECK(bundle.filament_presets[0] == "My PETG");
        CHECK(bundle.filament_presets[1] == "Generic PLA");
        check_double_vector(bundle.filaments.find_preset("Generic PLA", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.8 });
        check_double_vector(bundle.filaments.find_preset("Generic PLA @System", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.5 });
        REQUIRE(pub.material_replacements.size() == 1);
        CHECK(pub.material_replacements[0] == "slot 1: My PETG -> Generic PLA");
        CHECK(pub.skipped_keys.empty());
    }
}

TEST_CASE("Published 3MF uniquifies an imported full material name on collision", "[Preset][Bundle][Published]")
{
    PresetBundle bundle;
    Preset &petg = add_inmemory_preset(bundle.filaments, "My PETG");
    petg.config.opt_string("filament_type", 0u) = "PETG";
    petg.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.6 };
    Preset &bare = add_inmemory_preset(bundle.filaments, "Generic PLA");
    bare.config.opt_string("filament_type", 0u) = "PLA";
    bare.config.opt_string("filament_vendor", 0u) = "Generic";
    bare.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
    Preset &qidi = add_inmemory_preset(bundle.filaments, "Generic PLA @Qidi Q2 0.4 nozzle");
    qidi.config.opt_string("filament_type", 0u) = "PLA";
    qidi.config.opt_string("filament_vendor", 0u) = "Generic";
    qidi.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
    Preset &sys = add_inmemory_preset(bundle.filaments, "Generic PLA @System");
    sys.config.opt_string("filament_type", 0u) = "PLA";
    sys.config.opt_string("filament_vendor", 0u) = "Generic";
    sys.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
    bundle.filament_presets = { "My PETG" };

    PublishedMaterialEntry entry;
    entry.slot               = 0;
    entry.full               = true;
    entry.publish_type       = true;
    entry.publish_type_value = "PLA";
    entry.preset_name        = "Generic PLA @Qidi Q2 0.4 nozzle";
    entry.full_keys          = { "filament_retraction_length" };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { entry };
    DynamicPrintConfig config = published_pla_file_config();
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    CHECK(bundle.filament_presets[0] == "Generic PLA (Published)");
    check_double_vector(bundle.filaments.find_preset("Generic PLA (Published)", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
    check_double_vector(bundle.filaments.find_preset("Generic PLA", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.5 });
    check_double_vector(bundle.filaments.find_preset("Generic PLA @Qidi Q2 0.4 nozzle", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.5 });
    check_double_vector(bundle.filaments.find_preset("Generic PLA @System", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.5 });
    REQUIRE(pub.material_replacements.size() == 1);
    CHECK(pub.material_replacements[0] == "slot 0: My PETG -> Generic PLA (Published)");
    CHECK(pub.skipped_keys.empty());
}

TEST_CASE("Published 3MF writes to the stored preset when the edited layer is re-selected away", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard", "Direct Drive Standard" };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00FF00" };
        config.opt<ConfigOptionStrings>("filament_type")->values = { "PETG", "PLA" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values = { "GFT99", "GFL99" };
        config.option<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.6, 0.9 };
        return config;
    };

    PresetBundle bundle;
    Preset &petg = add_inmemory_preset(bundle.filaments, "My PETG");
    petg.config.opt_string("filament_type", 0u) = "PETG";
    petg.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.6 };
    Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
    pla.config.opt_string("filament_type", 0u) = "PLA";
    pla.config.opt<ConfigOptionStrings>("filament_colour", true)->values = { "#123456" };
    pla.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
    bundle.filament_presets = { "My PETG", "My PLA" };
    REQUIRE(bundle.filaments.select_preset_by_name("My PLA", false));

    PublishedMaterialEntry entry;
    entry.slot                = 1;
    entry.publish_type        = true;
    entry.publish_type_value  = "PLA";
    entry.publish_color       = true;
    entry.color               = "#ABCDEF";
    entry.keys                = { "filament_retraction_length" };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { entry };
    DynamicPrintConfig config = make_file_config();
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    Preset *stored = bundle.filaments.find_preset("My PLA", false, true);
    REQUIRE(stored != nullptr);
    CHECK(stored->config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#ABCDEF" });
    check_double_vector(stored->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
    CHECK(bundle.filaments.get_edited_preset().name == "My PETG");
    REQUIRE(bundle.filaments.select_preset_by_name("My PLA", false));
    CHECK(bundle.filaments.get_edited_preset().config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#ABCDEF" });
    check_double_vector(bundle.filaments.get_edited_preset().config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
    CHECK(pub.skipped_keys.empty());
}

TEST_CASE("publish_material_base_name strips the variant tail from a published preset name", "[Preset][Bundle][Published]")
{
    CHECK(publish_material_base_name("Generic PLA @System") == "Generic PLA");
    CHECK(publish_material_base_name("Generic PLA @Qidi Q2 0.4 nozzle") == "Generic PLA");
    CHECK(publish_material_base_name("Generic PLA  @System") == "Generic PLA");
    CHECK(publish_material_base_name("Voron Generic PLA") == "Voron Generic PLA");
    CHECK(publish_material_base_name("") == "");
    CHECK(publish_material_base_name("@System").empty());
}

TEST_CASE("Published 3MF imports a full material as a detached project-embedded preset", "[Preset][Bundle][Published]")
{
    PresetBundle bundle;
    Preset &petg = add_inmemory_preset(bundle.filaments, "My PETG");
    petg.config.opt_string("filament_type", 0u) = "PETG";
    petg.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.6 };
    Preset &spare = add_inmemory_preset(bundle.filaments, "Spare PLA");
    spare.config.opt_string("filament_type", 0u) = "PLA";
    spare.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.4 };
    bundle.filament_presets = { "My PETG" };

    PublishedMaterialEntry entry;
    entry.slot                = 0;
    entry.full                = true;
    entry.publish_type        = true;
    entry.publish_type_value  = "PLA";
    entry.publish_color       = true;
    entry.color               = "#ABCDEF";
    entry.filament_id         = "AFL01";
    entry.setting_id          = "Sid000111222";
    entry.preset_name         = "Author PLA @Vendor";
    entry.full_keys           = { "filament_retraction_length" };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { entry };
    DynamicPrintConfig config = published_pla_file_config();
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    CHECK(bundle.filament_presets[0] == "Author PLA");
    Preset *copy = bundle.filaments.find_preset("Author PLA", false, true);
    REQUIRE(copy != nullptr);
    CHECK(copy->is_project_embedded);
    CHECK(copy->inherits().empty());
    CHECK(copy->setting_id.empty());
    CHECK(copy->vendor == nullptr);
    CHECK_FALSE(copy->is_system);
    CHECK_FALSE(copy->is_default);
    CHECK_FALSE(copy->is_external);
    CHECK(copy->is_visible);
    CHECK(copy->filament_id == "AFL01");
    CHECK(copy->config.opt<ConfigOptionStrings>("filament_settings_id")->values == std::vector<std::string>{ "Author PLA" });
    check_double_vector(copy->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
    CHECK(copy->config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#ABCDEF" });
    CHECK(copy->config.opt<ConfigOptionStrings>("compatible_printers")->values.empty());
    CHECK(copy->config.opt<ConfigOptionStrings>("compatible_prints")->values.empty());
    CHECK(copy->config.opt<ConfigOptionString>("compatible_printers_condition")->value.empty());
    CHECK(copy->config.opt<ConfigOptionString>("compatible_prints_condition")->value.empty());
    check_double_vector(bundle.filaments.find_preset("My PETG", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.6 });
    check_double_vector(bundle.filaments.find_preset("Spare PLA", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.4 });
    CHECK(pub.skipped_keys.empty());
    REQUIRE(pub.material_replacements.size() == 1);
    CHECK(pub.material_replacements[0] == "slot 0: My PETG -> Author PLA");
}

TEST_CASE("Published 3MF clears printer restrictions on the imported full material", "[Preset][Bundle][Published]")
{
    PresetBundle bundle;
    Preset &restricted = add_inmemory_preset(bundle.filaments, "Restricted PLA");
    restricted.config.opt_string("filament_type", 0u) = "PLA";
    restricted.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
    restricted.config.set_key_value("compatible_printers", new ConfigOptionStrings({ "Unrelated Printer" }));
    restricted.config.set_key_value("compatible_prints", new ConfigOptionStrings({ "Unrelated Print" }));
    restricted.config.option<ConfigOptionString>("compatible_printers_condition", true)->value = "printer_settings_id==\"Nope\"";
    restricted.config.option<ConfigOptionString>("compatible_prints_condition", true)->value = "print_settings_id==\"Nope\"";
    bundle.filament_presets = { "Restricted PLA" };

    PublishedMaterialEntry entry;
    entry.slot                = 0;
    entry.full                = true;
    entry.publish_type        = true;
    entry.publish_type_value  = "PLA";
    entry.full_keys           = { "filament_retraction_length" };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { entry };
    DynamicPrintConfig config = published_pla_file_config();
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    CHECK(bundle.filament_presets[0] == "PLA");
    Preset *copy = bundle.filaments.find_preset("PLA", false, true);
    REQUIRE(copy != nullptr);
    CHECK(copy->config.opt<ConfigOptionStrings>("compatible_printers")->values.empty());
    CHECK(copy->config.opt<ConfigOptionStrings>("compatible_prints")->values.empty());
    CHECK(copy->config.opt<ConfigOptionString>("compatible_printers_condition")->value.empty());
    CHECK(copy->config.opt<ConfigOptionString>("compatible_prints_condition")->value.empty());
    const Preset *original = bundle.filaments.find_preset("Restricted PLA", false, true);
    REQUIRE(original != nullptr);
    CHECK(original->config.opt<ConfigOptionStrings>("compatible_printers")->values == std::vector<std::string>{ "Unrelated Printer" });
    CHECK(original->config.opt<ConfigOptionString>("compatible_printers_condition")->value == "printer_settings_id==\"Nope\"");
    CHECK(pub.skipped_keys.empty());
}

TEST_CASE("Published 3MF shares one imported copy between identical full slots", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard", "Direct Drive Standard" };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00FF00" };
        config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA", "PLA" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values = { "AFL01", "AFL01" };
        config.option<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.9, 0.8 };
        return config;
    };
    auto make_entry = [](int slot) {
        PublishedMaterialEntry entry;
        entry.slot                = slot;
        entry.full                = true;
        entry.publish_type        = true;
        entry.publish_type_value  = "PLA";
        entry.filament_id         = "AFL01";
        entry.setting_id          = "Sid000111222";
        entry.preset_name         = "Author PLA @Vendor";
        entry.full_keys           = { "filament_retraction_length" };
        return entry;
    };

    PresetBundle bundle;
    Preset &petg = add_inmemory_preset(bundle.filaments, "My PETG");
    petg.config.opt_string("filament_type", 0u) = "PETG";
    petg.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.6 };
    Preset &other = add_inmemory_preset(bundle.filaments, "Other PETG");
    other.config.opt_string("filament_type", 0u) = "PETG";
    other.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.65 };
    bundle.filament_presets = { "My PETG", "Other PETG" };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { make_entry(0), make_entry(1) };
    DynamicPrintConfig config = make_file_config();
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    REQUIRE(bundle.filament_presets.size() == 2);
    CHECK(bundle.filament_presets[0] == "Author PLA");
    CHECK(bundle.filament_presets[1] == "Author PLA");
    CHECK(bundle.filaments.find_preset("Author PLA", false, true) != nullptr);
    CHECK(bundle.filaments.find_preset("Author PLA (Published)", false, true) == nullptr);
    check_double_vector(bundle.filaments.find_preset("Author PLA", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
    REQUIRE(pub.material_replacements.size() == 2);
    CHECK(pub.material_replacements[0] == "slot 0: My PETG -> Author PLA");
    CHECK(pub.material_replacements[1] == "slot 1: Other PETG -> Author PLA");
    check_double_vector(bundle.filaments.find_preset("My PETG", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.6 });
    check_double_vector(bundle.filaments.find_preset("Other PETG", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.65 });
    CHECK(pub.skipped_keys.empty());
}

TEST_CASE("Published 3MF names unidentified full materials from their fallback fields", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard", "Direct Drive Standard" };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00FF00" };
        config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA", "PLA" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values = { "GFL99", "GFT99" };
        config.option<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.9, 1.2 };
        return config;
    };

    SECTION("empty preset_name falls back to the filament_id") {
        PresetBundle bundle;
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        pla.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
        bundle.filament_presets = { "My PLA" };

        PublishedMaterialEntry entry;
        entry.slot                = 0;
        entry.full                = true;
        entry.publish_type        = true;
        entry.publish_type_value  = "PLA";
        entry.filament_id         = "AFL01";
        entry.full_keys           = { "filament_retraction_length" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { entry };
        DynamicPrintConfig config = published_pla_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        CHECK(bundle.filament_presets[0] == "AFL01");
        CHECK(bundle.filaments.find_preset("AFL01", false, true) != nullptr);
        CHECK(pub.skipped_keys.empty());
    }

    SECTION("fully anonymous entries get slot-scoped copies") {
        PresetBundle bundle;
        Preset &first = add_inmemory_preset(bundle.filaments, "First PLA");
        first.config.opt_string("filament_type", 0u) = "PLA";
        first.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
        Preset &second = add_inmemory_preset(bundle.filaments, "Second PLA");
        second.config.opt_string("filament_type", 0u) = "PLA";
        second.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.55 };
        bundle.filament_presets = { "First PLA", "Second PLA" };

        PublishedMaterialEntry entry0;
        entry0.slot                = 0;
        entry0.full                = true;
        entry0.publish_type        = true;
        entry0.publish_type_value  = "ABS";
        entry0.full_keys           = { "filament_retraction_length" };
        PublishedMaterialEntry entry1 = entry0;
        entry1.slot                = 1;

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { entry0, entry1 };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        CHECK(bundle.filament_presets[0] == "ABS");
        CHECK(bundle.filament_presets[1] == "ABS (Published)");
        CHECK(bundle.filaments.find_preset("ABS", false, true) != nullptr);
        CHECK(bundle.filaments.find_preset("ABS (Published)", false, true) != nullptr);
        CHECK(pub.skipped_keys.empty());
    }
}

TEST_CASE("Re-importing a published full material uniquifies the second copy", "[Preset][Bundle][Published]")
{
    PresetBundle bundle;
    Preset &petg = add_inmemory_preset(bundle.filaments, "My PETG");
    petg.config.opt_string("filament_type", 0u) = "PETG";
    petg.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.6 };
    bundle.filament_presets = { "My PETG" };

    auto make_entry = [] {
        PublishedMaterialEntry entry;
        entry.slot                = 0;
        entry.full                = true;
        entry.publish_type        = true;
        entry.publish_type_value  = "PLA";
        entry.filament_id         = "AFL01";
        entry.setting_id          = "Sid000111222";
        entry.preset_name         = "Author PLA @Vendor";
        entry.full_keys           = { "filament_retraction_length" };
        return entry;
    };

    for (int round = 0; round < 2; ++round) {
        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { make_entry() };
        DynamicPrintConfig config = published_pla_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        if (round == 0) {
            CHECK(bundle.filament_presets[0] == "Author PLA");
            CHECK(bundle.filaments.find_preset("Author PLA (Published)", false, true) == nullptr);
        } else {
            CHECK(bundle.filament_presets[0] == "Author PLA (Published)");
            check_double_vector(bundle.filaments.find_preset("Author PLA (Published)", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
            check_double_vector(bundle.filaments.find_preset("Author PLA", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
            REQUIRE(pub.material_replacements.size() == 1);
            CHECK(pub.material_replacements[0] == "slot 0: Author PLA -> Author PLA (Published)");
        }
        CHECK(pub.skipped_keys.empty());
    }
}

TEST_CASE("Published 3MF partial slots apply colour and gate keys by the published type", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard", "Direct Drive Standard" };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00FF00" };
        config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA", "PETG" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values = { "GFL99", "GFT99" };
        config.option<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.9, 1.2 };
        return config;
    };

    SECTION("matching type applies the keys and the colour") {
        PresetBundle bundle;
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        pla.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
        pla.config.opt<ConfigOptionStrings>("filament_colour", true)->values = { "#123456" };
        bundle.filament_presets = { "My PLA" };

        PublishedMaterialEntry entry;
        entry.slot                = 0;
        entry.publish_type        = true;
        entry.publish_type_value  = "PLA";
        entry.publish_color       = true;
        entry.color               = "#ABCDEF";
        entry.keys                = { "filament_retraction_length" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { entry };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        Preset *pla_preset = bundle.filaments.find_preset("My PLA", false, true);
        REQUIRE(pla_preset != nullptr);
        check_double_vector(pla_preset->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
        CHECK(pla_preset->config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#ABCDEF" });
        CHECK(pub.skipped_keys.empty());
        CHECK(pub.material_replacements.empty());
    }

    SECTION("type mismatch without a replacement keeps the material and skips the keys") {
        PresetBundle bundle;
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        pla.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
        pla.config.opt<ConfigOptionStrings>("filament_colour", true)->values = { "#123456" };
        bundle.filament_presets = { "My PLA" };

        PublishedMaterialEntry entry;
        entry.slot                = 0;
        entry.publish_type        = true;
        entry.publish_type_value  = "ABS";
        entry.publish_color       = true;
        entry.color               = "#ABCDEF";
        entry.keys                = { "filament_retraction_length" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { entry };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        CHECK(bundle.filament_presets[0] == "My PLA");
        Preset *pla_preset = bundle.filaments.find_preset("My PLA", false, true);
        REQUIRE(pla_preset != nullptr);
        check_double_vector(pla_preset->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.5 });
        CHECK(pla_preset->config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#ABCDEF" });
        CHECK(contains_key(pub.skipped_keys, "material:ABS (filament_retraction_length)"));
        CHECK(pub.material_replacements.empty());
    }

    SECTION("receiver slot count grows to fit the highest published slot and assigns matching type preset") {
        PresetBundle bundle;
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        pla.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
        pla.config.opt<ConfigOptionStrings>("filament_colour", true)->values = { "#123456" };
        Preset &petg = add_inmemory_preset(bundle.filaments, "My PETG");
        petg.config.opt_string("filament_type", 0u) = "PETG";
        petg.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.8 };
        bundle.filament_presets = { "My PLA" };

        PublishedMaterialEntry entry;
        entry.slot                = 1;
        entry.publish_type        = true;
        entry.publish_type_value  = "PETG";
        entry.keys                = { "filament_retraction_length" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { entry };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 2);
        CHECK(bundle.filament_presets[1] == "My PETG");
        check_double_vector(bundle.filaments.find_preset("My PETG", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 1.2 });
        check_double_vector(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.5 });
    }
}

TEST_CASE("Published 3MF grows the receiver's slots only as far as the published slots", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75, 1.75, 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2, 3, 4 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard" };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00FF00", "#0000FF", "#FFFF00" };
        config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA", "PLA", "PLA", "PLA" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic", "Generic", "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values = { "GFL99", "GFL99", "GFL99", "GFL99" };
        return config;
    };
    auto add_pla_preset = [](PresetBundle &bundle) {
        Preset &preset = add_inmemory_preset(bundle.filaments, "My PLA");
        preset.config.opt_string("filament_type", 0u) = "PLA";
        preset.config.opt<ConfigOptionStrings>("filament_colour", true)->values = { "#123456" };
        return &preset;
    };
    auto make_color_entry = [](int slot) {
        PublishedMaterialEntry entry;
        entry.slot          = slot;
        entry.publish_color = true;
        entry.color         = "#ABCDEF";
        return entry;
    };

    {
        PresetBundle bundle;
        add_pla_preset(bundle);
        bundle.filament_presets = { "My PLA" };

        PublishedConfig pub;
        pub.published = true;
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        CHECK(bundle.filament_presets.size() == 1);
        CHECK(bundle.filaments.find_preset("My PLA")->config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#123456" });
        CHECK(pub.skipped_keys.empty());
    }

    {
        PresetBundle bundle;
        add_pla_preset(bundle);
        bundle.filament_presets = { "My PLA" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { make_color_entry(0) };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        CHECK(bundle.filament_presets.size() == 1);
        CHECK(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#ABCDEF" });
    }

    {
        PresetBundle bundle;
        add_pla_preset(bundle);
        bundle.filament_presets = { "My PLA" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { make_color_entry(3) };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 4);
        CHECK(bundle.filament_presets[1] == "My PLA");
        CHECK(bundle.filament_presets[2] == "My PLA");
        CHECK(bundle.filament_presets[3] == "My PLA");
        CHECK(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#ABCDEF" });
        CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values.size() == 4);
        CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values[1] == "#123456");
        CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values[3] == "#ABCDEF");
        CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_multi_colour")->values.size() == 4);
        CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour_type")->values.size() == 4);
        CHECK(bundle.project_config.opt<ConfigOptionInts>("filament_map")->values.size() == 4);
        CHECK(bundle.project_config.opt<ConfigOptionFloats>("flush_volumes_matrix")->values.size() == 16);
    }

    {
        PresetBundle bundle;
        add_pla_preset(bundle);
        bundle.filament_presets = { "My PLA" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { make_color_entry(0), make_color_entry(2) };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 3);
        CHECK(bundle.filament_presets[1] == "My PLA");
    }

    {
        PresetBundle bundle;
        add_pla_preset(bundle);
        bundle.filament_presets = { "My PLA", "My PLA", "My PLA" };
        bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values       = { "#111111", "#222222", "#333333" };
        bundle.project_config.opt<ConfigOptionStrings>("filament_multi_colour")->values = { "#111111", "#222222", "#333333" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { make_color_entry(0) };
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values          = { 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values          = { 1 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard" };
        config.opt<ConfigOptionStrings>("filament_colour")->values           = { "#FF0000" };
        config.opt<ConfigOptionStrings>("filament_type")->values             = { "PLA" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values           = { "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values              = { "GFL99" };
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 3);
        CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#ABCDEF", "#222222", "#333333" });
        CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_multi_colour")->values == std::vector<std::string>{ "#ABCDEF", "#222222", "#333333" });
        CHECK(bundle.project_config.opt<ConfigOptionInts>("filament_map")->values.size() == 3);
        CHECK(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#ABCDEF" });
    }
}

TEST_CASE("Published 3MF grows published slots to the receiver's last preset and recolors it in place", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75, 1.75, 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2, 3, 4 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard" };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00FF00", "#0000FF", "#FFFF00" };
        config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA", "PLA", "PLA", "PLA" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic", "Generic", "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values = { "GFL99", "GFL99", "GFL99", "GFL99" };
        return config;
    };

    PresetBundle bundle;
    Preset &mine = add_inmemory_preset(bundle.filaments, "My PLA");
    mine.config.opt_string("filament_type", 0u) = "PLA";
    mine.config.opt<ConfigOptionStrings>("filament_colour", true)->values = { "#123456" };
    Preset &other = add_inmemory_preset(bundle.filaments, "Other PLA");
    other.config.opt_string("filament_type", 0u) = "PLA";
    other.config.opt<ConfigOptionStrings>("filament_colour", true)->values = { "#654321" };
    bundle.filament_presets = { "My PLA" };

    PublishedMaterialEntry entry;
    entry.slot          = 3;
    entry.publish_color = true;
    entry.color         = "#ABCDEF";
    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { entry };
    DynamicPrintConfig config = make_file_config();
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    REQUIRE(bundle.filament_presets.size() == 4);
    CHECK(bundle.filament_presets[1] == "My PLA");
    CHECK(bundle.filament_presets[2] == "My PLA");
    CHECK(bundle.filament_presets[3] == "My PLA");
    CHECK(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#ABCDEF" });
    CHECK(bundle.filaments.find_preset("Other PLA", false, true)->config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#654321" });
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values.size() == 4);
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values[1] == "#123456");
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values[3] == "#ABCDEF");
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_multi_colour")->values.size() == 4);
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour_type")->values.size() == 4);
    CHECK(bundle.project_config.opt<ConfigOptionInts>("filament_map")->values.size() == 4);
}

TEST_CASE("Published 3MF re-points an aliased grown slot by published identity or family without a type requirement", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75, 1.75, 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2, 3, 4 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard" };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00FF00", "#0000FF", "#FFFF00" };
        config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA", "PLA", "PLA", "PLA" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic", "Generic", "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values = { "GFL99", "GFL99", "GFL99", "GFL99" };
        return config;
    };

    PublishedMaterialEntry entry;
    entry.slot            = 2;
    entry.filament_type   = "PLA";
    entry.filament_vendor = "Generic";
    entry.keys            = { "filament_retraction_length" };
    PresetBundle bundle;
    Preset &mine     = add_inmemory_preset(bundle.filaments, "My PLA");
    mine.config.opt_string("filament_type", 0u) = "PLA";
    Preset &arbitrary = add_inmemory_preset(bundle.filaments, "Aaa PLA");
    arbitrary.config.opt_string("filament_type", 0u) = "PLA";
    bundle.filament_presets = { "My PLA" };

    SECTION("an exact filament_id outranks the first unused preset") {
        Preset &authored = add_inmemory_preset(bundle.filaments, "Zzz PLA");
        authored.config.opt_string("filament_type", 0u) = "PLA";
        authored.filament_id = "GFA00";
        entry.filament_id    = "GFA00";

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { entry };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 3);
        CHECK(bundle.filament_presets[1] == "My PLA");
        CHECK(bundle.filament_presets[2] == "Zzz PLA");
        REQUIRE(pub.material_replacements.size() == 1);
        CHECK(pub.material_replacements[0] == "slot 2: My PLA -> Zzz PLA");
    }

    SECTION("a family-only entry picks an unused preset of that family") {
        Preset &petg = add_inmemory_preset(bundle.filaments, "Bbb PETG");
        petg.config.opt_string("filament_type", 0u) = "PETG";
        entry.filament_type = "PETG";

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { entry };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 3);
        CHECK(bundle.filament_presets[2] == "Bbb PETG");
        REQUIRE(pub.material_replacements.size() == 1);
        CHECK(pub.material_replacements[0] == "slot 2: My PLA -> Bbb PETG");
    }
}

TEST_CASE("Published 3MF refreshes the edited preset so the applied material values surface", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard" };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000" };
        config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values = { "GFL99" };
        config.option<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.9 };
        return config;
    };
    auto make_entry = [] {
        PublishedMaterialEntry entry;
        entry.slot                = 0;
        entry.publish_type        = true;
        entry.publish_type_value  = "PLA";
        entry.publish_color       = true;
        entry.color               = "#ABCDEF";
        entry.keys                = { "filament_retraction_length" };
        return entry;
    };

    SECTION("the edited preset carries the applied colour and keys") {
        PresetBundle bundle;
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        pla.config.opt<ConfigOptionStrings>("filament_colour", true)->values = { "#123456" };
        pla.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
        bundle.filament_presets = { "My PLA" };
        REQUIRE(bundle.filaments.select_preset_by_name("My PLA", false));

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { make_entry() };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        const Preset &edited = bundle.filaments.get_edited_preset();
        CHECK(edited.name == "My PLA");
        CHECK(edited.config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#ABCDEF" });
        check_double_vector(edited.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
        CHECK(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#123456" });
        check_double_vector(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.5 });
        CHECK(bundle.filaments.current_is_dirty());
        CHECK(pub.skipped_keys.empty());
    }

    SECTION("a slot replacement is reflected in the edited preset") {
        PresetBundle bundle;
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        pla.config.opt<ConfigOptionStrings>("filament_colour", true)->values = { "#123456" };
        pla.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
        Preset &abs = add_inmemory_preset(bundle.filaments, "My ABS");
        abs.config.opt_string("filament_type", 0u) = "ABS";
        abs.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.3 };
        bundle.filament_presets = { "My PLA" };
        REQUIRE(bundle.filaments.select_preset_by_name("My PLA", false));

        PublishedMaterialEntry entry = make_entry();
        entry.publish_type_value     = "ABS";
        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { entry };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets[0] == "My ABS");
        const Preset &edited = bundle.filaments.get_edited_preset();
        CHECK(edited.name == "My ABS");
        CHECK(edited.config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#ABCDEF" });
        check_double_vector(edited.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
    }
}

TEST_CASE("Published 3MF preserves unsaved edits on the edited filament preset", "[Preset][Bundle][Published]")
{
    PresetBundle bundle;
    Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
    pla.config.opt_string("filament_type", 0u) = "PLA";
    pla.config.opt<ConfigOptionStrings>("filament_colour", true)->values = { "#123456" };
    pla.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
    pla.config.opt<ConfigOptionFloatsNullable>("filament_z_hop", true)->values = { 0.1 };
    bundle.filament_presets = { "My PLA" };
    REQUIRE(bundle.filaments.select_preset_by_name("My PLA", false));

    bundle.filaments.get_edited_preset().config.opt<ConfigOptionFloatsNullable>("filament_z_hop")->values = { 0.7 };

    PublishedMaterialEntry entry;
    entry.slot                = 0;
    entry.publish_type        = true;
    entry.publish_type_value  = "PLA";
    entry.publish_color       = true;
    entry.color               = "#ABCDEF";
    entry.keys                = { "filament_retraction_length" };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { entry };
    DynamicPrintConfig config = published_pla_file_config();
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    const Preset &edited = bundle.filaments.get_edited_preset();
    CHECK(edited.name == "My PLA");
    CHECK(edited.config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#ABCDEF" });
    check_double_vector(edited.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
    check_double_vector(edited.config.opt<ConfigOptionFloatsNullable>("filament_z_hop")->values, { 0.7 });
    Preset *stored = bundle.filaments.find_preset("My PLA", false, true);
    REQUIRE(stored != nullptr);
    CHECK(stored->config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#123456" });
    check_double_vector(stored->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.5 });
    check_double_vector(stored->config.opt<ConfigOptionFloatsNullable>("filament_z_hop")->values, { 0.1 });
    CHECK(bundle.filaments.current_is_dirty());
    CHECK(pub.skipped_keys.empty());
    CHECK(pub.material_replacements.empty());
}

TEST_CASE("Published 3MF rejects out-of-range vector variants and variant-suffixed scalar keys", "[Preset][Bundle][Published]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000" };
    config.opt<ConfigOptionFloats>("wiping_volumes_extruders")->values = { 140., 150. };
    config.opt_float("layer_height") = 0.28;
    Preset::normalize(config);

    PresetBundle bundle;
    bundle.prints.get_edited_preset().config.opt<ConfigOptionFloats>("wiping_volumes_extruders")->values = { 10., 20. };
    bundle.prints.get_edited_preset().config.opt_float("layer_height") = 0.1;

    PublishedConfig pub;
    pub.published      = true;
    pub.published_keys = { "wiping_volumes_extruders#5", "wiping_volumes_extruders#1", "wiping_volumes_extruders#abc", "layer_height#0" };
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    check_double_vector(bundle.prints.get_edited_preset().config.opt<ConfigOptionFloats>("wiping_volumes_extruders")->values, { 10., 150. });
    CHECK(bundle.prints.get_edited_preset().config.opt<ConfigOptionFloats>("wiping_volumes_extruders")->values.size() == 2);
    CHECK(contains_key(pub.skipped_keys, "wiping_volumes_extruders#5"));
    CHECK(contains_key(pub.skipped_keys, "wiping_volumes_extruders#abc"));
    CHECK(contains_key(pub.skipped_keys, "layer_height#0"));
    CHECK_FALSE(contains_key(pub.skipped_keys, "wiping_volumes_extruders#1"));
    CHECK_THAT(bundle.prints.get_edited_preset().config.opt_float("layer_height"), Catch::Matchers::WithinAbs(0.1, 0.000001));
}

TEST_CASE("Published 3MF reports type-mismatched keys as skipped instead of aborting", "[Preset][Bundle][Published]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000" };
    config.set_key_value("wiping_volumes_extruders", new ConfigOptionStrings({ "140", "150" }));
    config.opt_float("layer_height") = 0.28;

    PresetBundle bundle;
    bundle.prints.get_edited_preset().config.set_key_value("wiping_volumes_extruders", new ConfigOptionFloats({ 10., 20. }));
    bundle.prints.get_edited_preset().config.opt_float("layer_height") = 0.1;

    PublishedConfig pub;
    pub.published      = true;
    pub.published_keys = { "wiping_volumes_extruders", "layer_height" };
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    CHECK(contains_key(pub.skipped_keys, "wiping_volumes_extruders"));
    check_double_vector(bundle.prints.get_edited_preset().config.opt<ConfigOptionFloats>("wiping_volumes_extruders")->values, { 10., 20. });
    CHECK_THAT(bundle.prints.get_edited_preset().config.opt_float("layer_height"), Catch::Matchers::WithinAbs(0.28, 0.000001));
    CHECK_FALSE(contains_key(pub.skipped_keys, "layer_height"));
}

TEST_CASE("Published 3MF survives a receiver preset missing its material identity", "[Preset][Bundle][Published]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75 };
    config.opt<ConfigOptionInts>("filament_self_index")->values = { 1 };
    config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard" };
    config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000" };
    config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA" };
    config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic" };
    config.opt<ConfigOptionStrings>("filament_ids")->values = { "GFL99" };
    config.option<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.9 };
    Preset::normalize(config);

    PresetBundle bundle;
    Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
    pla.config.erase("filament_type");
    pla.config.erase("filament_vendor");
    pla.config.opt<ConfigOptionStrings>("filament_colour", true)->values = { "#123456" };
    pla.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
    bundle.filament_presets = { "My PLA" };

    PublishedMaterialEntry entry;
    entry.filament_type        = "PLA";
    entry.filament_vendor      = "Generic";
    entry.filament_id          = "GFL99";
    entry.slot                 = 0;
    entry.publish_type         = true;
    entry.publish_type_value   = "ABS";
    entry.publish_color        = true;
    entry.color                = "#ABCDEF";
    entry.keys                 = { "filament_retraction_length" };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { entry };
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    CHECK(contains_key(pub.skipped_keys, "material:GFL99 (filament_retraction_length)"));
    CHECK(bundle.filament_presets[0] == "My PLA");
    Preset *mine_preset = bundle.filaments.find_preset("My PLA", false, true);
    REQUIRE(mine_preset != nullptr);
    CHECK(mine_preset->config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#ABCDEF" });
}

TEST_CASE("Printer publishable allowlist matches the printer tab's Retraction and Z-Hop optgroups", "[Preset][Bundle][Published]")
{
    auto keys_of = [](const std::vector<PublishablePrinterOption>& opts) {
        std::vector<std::string> keys;
        keys.reserve(opts.size());
        for (const PublishablePrinterOption& opt : opts)
            keys.emplace_back(opt.key);
        return keys;
    };

    const std::vector<std::string> expected_retraction = {
        "retraction_length", "retract_restart_extra", "retraction_speed", "deretraction_speed",
        "retraction_minimum_travel", "retract_when_changing_layer", "wipe", "wipe_distance",
        "retract_before_wipe", "retract_after_wipe"
    };
    const std::vector<std::string> expected_z_hop = {
        "retract_lift_enforce", "z_hop_types", "z_hop", "travel_slope", "retract_lift_above",
        "retract_lift_below"
    };

    CHECK(keys_of(publishable_printer_retraction_options()) == expected_retraction);
    CHECK(keys_of(publishable_printer_z_hop_options()) == expected_z_hop);

    std::set<std::string> expected_union(expected_retraction.begin(), expected_retraction.end());
    expected_union.insert(expected_z_hop.begin(), expected_z_hop.end());
    CHECK(publishable_printer_keys() == expected_union);
}

TEST_CASE("Published 3MF reloading does not compound values on the receiver's presets", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard" };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000" };
        config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values = { "GFL99" };
        config.option<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.9 };
        return config;
    };
    auto make_entry = [] {
        PublishedMaterialEntry entry;
        entry.slot          = 0;
        entry.publish_color = true;
        entry.color         = "#ABCDEF";
        entry.keys          = { "filament_retraction_length" };
        return entry;
    };

    PresetBundle bundle;
    Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
    pla.config.opt_string("filament_type", 0u) = "PLA";
    pla.config.opt<ConfigOptionStrings>("filament_colour", true)->values = { "#123456" };
    pla.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
    bundle.filament_presets = { "My PLA" };

    auto load = [&] {
        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { make_entry() };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);
    };
    load();
    CHECK(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#ABCDEF" });
    load();
    CHECK(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#ABCDEF" });
    check_double_vector(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
}

TEST_CASE("Published 3MF gives each published slot its own preset on an aliased receiver", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75, 1.75, 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2, 3, 4 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard" };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00FF00", "#0000FF", "#FFFF00" };
        config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA", "PLA", "PLA", "PLA" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic", "Generic", "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values = { "GFL99", "GFL99", "GFL99", "GFL99" };
        config.option<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.6, 0.9, 1.2, 1.5 };
        return config;
    };
    auto make_key_entry = [](int slot) {
        PublishedMaterialEntry entry;
        entry.slot = slot;
        entry.keys = { "filament_retraction_length" };
        return entry;
    };

    PresetBundle bundle;
    Preset &mine = add_inmemory_preset(bundle.filaments, "My PLA");
    mine.config.opt_string("filament_type", 0u) = "PLA";
    mine.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
    for (const char *name : { "Extra PLA A", "Extra PLA B", "Extra PLA C" }) {
        Preset &extra = add_inmemory_preset(bundle.filaments, name);
        extra.config.opt_string("filament_type", 0u) = "PLA";
        extra.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
    }
    bundle.filament_presets = { "My PLA", "My PLA", "My PLA", "My PLA" };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { make_key_entry(0), make_key_entry(1), make_key_entry(2), make_key_entry(3) };
    DynamicPrintConfig config = make_file_config();
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    REQUIRE(bundle.filament_presets.size() == 4);
    CHECK(bundle.filament_presets[0] == "My PLA");
    CHECK(bundle.filament_presets[1] != bundle.filament_presets[0]);
    CHECK(bundle.filament_presets[2] != bundle.filament_presets[0]);
    CHECK(bundle.filament_presets[2] != bundle.filament_presets[1]);
    CHECK(bundle.filament_presets[3] != bundle.filament_presets[0]);
    CHECK(bundle.filament_presets[3] != bundle.filament_presets[1]);
    CHECK(bundle.filament_presets[3] != bundle.filament_presets[2]);
    const std::vector<double> expected = { 0.6, 0.9, 1.2, 1.5 };
    for (size_t slot = 0; slot < 4; ++slot) {
        Preset *preset = bundle.filaments.find_preset(bundle.filament_presets[slot], false, true);
        REQUIRE(preset != nullptr);
        check_double_vector(preset->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { expected[slot] });
    }
    CHECK(pub.skipped_keys.empty());
}

TEST_CASE("Published 3MF de-aliases an aliased slot by published identity without a type requirement", "[Preset][Bundle][Published]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75 };
    config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2 };
    config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard", "Direct Drive Standard" };
    config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00FF00" };
    config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA", "PLA" };
    config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic" };
    config.opt<ConfigOptionStrings>("filament_ids")->values = { "GFL99", "GFL99" };
    config.option<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.6, 0.9 };

    PresetBundle bundle;
    Preset &mine = add_inmemory_preset(bundle.filaments, "My PLA");
    mine.config.opt_string("filament_type", 0u) = "PLA";
    mine.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
    Preset &spare = add_inmemory_preset(bundle.filaments, "Aaa PLA");
    spare.config.opt_string("filament_type", 0u) = "PLA";
    spare.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
    Preset &match = add_inmemory_preset(bundle.filaments, "Zzz PLA");
    match.config.opt_string("filament_type", 0u) = "PLA";
    match.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
    match.filament_id = "GFA00";
    bundle.filament_presets = { "My PLA", "My PLA" };

    PublishedMaterialEntry entry;
    entry.slot            = 1;
    entry.filament_type   = "PLA";
    entry.filament_vendor = "Generic";
    entry.filament_id     = "GFA00";
    entry.keys            = { "filament_retraction_length" };
    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { entry };
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    REQUIRE(bundle.filament_presets.size() == 2);
    CHECK(bundle.filament_presets[0] == "My PLA");
    CHECK(bundle.filament_presets[1] == "Zzz PLA");
    Preset *target = bundle.filaments.find_preset("Zzz PLA", false, true);
    REQUIRE(target != nullptr);
    check_double_vector(target->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values, { 0.9 });
    REQUIRE(pub.material_replacements.size() == 1);
    CHECK(pub.material_replacements[0] == "slot 1: My PLA -> Zzz PLA");
    CHECK(pub.skipped_keys.empty());
}

TEST_CASE("Published 3MF applies per-extruder printer keys across extruder-count mismatches", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000" };
        config.opt<ConfigOptionFloats>("retraction_length")->values = { 0.6, 0.9, 1.2, 1.5 };
        Preset::normalize(config);
        return config;
    };

    {
        PresetBundle bundle;
        bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_length")->values = { 0.8 };
        PublishedConfig pub;
        pub.published      = true;
        pub.published_keys = { "retraction_length#0", "retraction_length#1", "retraction_length#2", "retraction_length#3" };
        DynamicPrintConfig config = make_file_config();
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        check_double_vector(bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_length")->values, { 0.6 });
        CHECK(contains_key(pub.skipped_keys, "retraction_length#1"));
        CHECK(contains_key(pub.skipped_keys, "retraction_length#2"));
        CHECK(contains_key(pub.skipped_keys, "retraction_length#3"));
        CHECK_FALSE(contains_key(pub.skipped_keys, "retraction_length#0"));
    }

    {
        PresetBundle bundle;
        bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_length")->values = { 0.8, 0.8, 0.8, 0.8 };
        PublishedConfig pub;
        pub.published      = true;
        pub.published_keys = { "retraction_length#0" };
        DynamicPrintConfig config = make_file_config();
        config.opt<ConfigOptionFloats>("retraction_length")->values = { 0.7 };
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        check_double_vector(bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_length")->values, { 0.7, 0.8, 0.8, 0.8 });
        CHECK(pub.skipped_keys.empty());
    }
}

TEST_CASE("Published 3MF applies a mixed filament definition onto the receiver's project config", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75, 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2, 3 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = {
            "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard"
        };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#0000FF", "#800080" };
        config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA", "PETG", "PLA" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic", "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values = { "GFL99", "GFT99", "GFL99" };
        config.opt<ConfigOptionBools>("filament_is_mixed")->values = { 0, 0, 1 };
        config.opt<ConfigOptionStrings>("filament_mixed_components")->values = { "", "", "1,2" };
        config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values = { "", "", "0.6,0.4" };
        config.opt<ConfigOptionBools>("filament_mixed_gradient")->values = { 0, 0, 1 };
        config.opt<ConfigOptionStrings>("filament_mixed_gradient_range")->values = { "", "", "0.9,0.1" };
        config.opt<ConfigOptionStrings>("filament_mixed_gradient_curve")->values = { "", "", "0,0.1|1,0.9" };
        config.opt<ConfigOptionBools>("filament_mixed_gradient_per_part")->values = { 0, 0, 1 };
        return config;
    };

    {
        PresetBundle bundle;
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        Preset &petg = add_inmemory_preset(bundle.filaments, "My PETG");
        petg.config.opt_string("filament_type", 0u) = "PETG";
        bundle.filament_presets = { "My PLA", "My PETG", "My PLA" };

        bundle.set_num_filaments(3);
        bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values[2]                = 1;
        bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values[2]      = "1,1";
        bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values[2] = "0.5,0.5";

        PublishedMaterialEntry mix;
        mix.filament_type   = "PLA";
        mix.filament_vendor = "Generic";
        mix.filament_id     = "GFL99";
        mix.slot            = 2;
        mix.publish_color   = true;
        mix.color           = "#800080";
        mix.keys            = { "filament_is_mixed",       "filament_mixed_components",
                                "filament_mixed_sublayer_ratios", "filament_mixed_gradient",
                                "filament_mixed_gradient_range",  "filament_mixed_gradient_curve",
                                "filament_mixed_gradient_per_part" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { mix };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
        REQUIRE(is_mixed.size() == 3);
        CHECK(is_mixed[2]);
        const auto &components = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values;
        REQUIRE(components.size() == 3);
        CHECK(components[2] == "1,2");
        const auto &ratios = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values;
        REQUIRE(ratios.size() == 3);
        CHECK(ratios[2] == "0.6,0.4");
        const auto &gradient = bundle.project_config.opt<ConfigOptionBools>("filament_mixed_gradient")->values;
        CHECK(gradient[2]);
        const auto &range = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_gradient_range")->values;
        CHECK(range[2] == "0.9,0.1");
        const auto &curve = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_gradient_curve")->values;
        CHECK(curve[2] == "0,0.1|1,0.9");
        const auto &per_part = bundle.project_config.opt<ConfigOptionBools>("filament_mixed_gradient_per_part")->values;
        CHECK(per_part[2]);
        const auto &colour = bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values;
        REQUIRE(colour.size() == 3);
        CHECK(colour[2] == "#800080");
        CHECK_FALSE(is_mixed[0]);
        CHECK_FALSE(is_mixed[1]);
        CHECK(pub.skipped_keys.empty());
        CHECK(pub.material_replacements.empty());
    }

    {
        PresetBundle bundle;
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        bundle.filament_presets = { "My PLA" };

        PublishedMaterialEntry mix;
        mix.slot = 2;
        mix.keys = { "filament_is_mixed", "filament_mixed_components", "filament_mixed_sublayer_ratios" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { mix };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 3);
        const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
        REQUIRE(is_mixed.size() == 3);
        CHECK(is_mixed[2]);
        const auto &components = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values;
        REQUIRE(components.size() == 3);
        CHECK(components[2] == "1,2");
        CHECK(pub.skipped_keys.empty());
    }
}

TEST_CASE("Published 3MF reports an unappliable mixed filament definition as skipped", "[Preset][Bundle][Published]")
{
    PresetBundle bundle;
    Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
    pla.config.opt_string("filament_type", 0u) = "PLA";
    bundle.filament_presets = { "My PLA" };

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75 };
    config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00FF00" };
    config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA", "PLA" };
    config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic" };

    PublishedMaterialEntry mix;
    mix.slot = 1;
    mix.keys = { "filament_mixed_components", "filament_mixed_sublayer_ratios" };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { mix };
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    REQUIRE(bundle.filament_presets.size() == 2);
    CHECK(contains_key(pub.skipped_keys, "material: (filament_mixed_components)"));
    CHECK(contains_key(pub.skipped_keys, "material: (filament_mixed_sublayer_ratios)"));
}

TEST_CASE("Published 3MF relocates a mixed filament instead of overwriting a physical slot", "[Preset][Bundle][Published]")
{
    auto make_file_config = [](size_t num_author_slots, size_t num_tail_mixes = 1) {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        std::vector<double> diameters(num_author_slots, 1.75);
        std::vector<int> self_index;
        std::vector<std::string> variants;
        std::vector<std::string> types;
        for (size_t i = 0; i < num_author_slots; ++i) {
            self_index.push_back(int(i + 1));
            variants.emplace_back("Direct Drive Standard");
            types.push_back(i % 2 == 0 ? "PLA" : "PETG");
        }
        config.opt<ConfigOptionFloats>("filament_diameter")->values          = diameters;
        config.opt<ConfigOptionInts>("filament_self_index")->values          = self_index;
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = variants;
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00AA00", "#0000FF", "#FFFF00", "#800080" };
        config.opt<ConfigOptionStrings>("filament_colour")->values.resize(num_author_slots, "#808080");
        config.opt<ConfigOptionStrings>("filament_type")->values     = types;
        config.opt<ConfigOptionStrings>("filament_vendor")->values.assign(num_author_slots, "Generic");
        config.opt<ConfigOptionStrings>("filament_ids")->values.resize(num_author_slots);
        const size_t first_mix_slot                                        = num_author_slots - num_tail_mixes;
        config.opt<ConfigOptionBools>("filament_is_mixed")->values.assign(num_author_slots, 0);
        config.opt<ConfigOptionStrings>("filament_mixed_components")->values.assign(num_author_slots, "");
        config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values.assign(num_author_slots, "");
        for (size_t i = first_mix_slot; i < num_author_slots; ++i) {
            config.opt<ConfigOptionBools>("filament_is_mixed")->values[i]       = 1;
            config.opt<ConfigOptionStrings>("filament_mixed_components")->values[i] =
                i % 2 == 0 ? std::string("1,2") : std::string("1,3");
            config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values[i] =
                i % 2 == 0 ? std::string("0.6,0.4") : std::string("0.3,0.7");
        }
        return config;
    };

    {
        PresetBundle bundle;
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        bundle.filament_presets = { "My PLA", "My PLA", "My PLA", "My PLA", "My PLA" };
        bundle.set_num_filaments(5, "#123456");
        const std::vector<std::string> receiver_colours =
            bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values;

        PublishedMaterialEntry mix;
        mix.filament_type   = "PLA";
        mix.filament_vendor = "Generic";
        mix.slot            = 4;
        mix.publish_color   = true;
        mix.color           = "#800080";
        mix.keys            = { "filament_is_mixed",       "filament_mixed_components",
                                "filament_mixed_sublayer_ratios", "filament_mixed_gradient",
                                "filament_mixed_gradient_range",  "filament_mixed_gradient_curve",
                                "filament_mixed_gradient_per_part" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { mix };
        DynamicPrintConfig config = make_file_config(5);
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 6);
        const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
        REQUIRE(is_mixed.size() == 6);
        CHECK_FALSE(is_mixed[0]);
        CHECK_FALSE(is_mixed[1]);
        CHECK_FALSE(is_mixed[2]);
        CHECK_FALSE(is_mixed[3]);
        CHECK_FALSE(is_mixed[4]);
        CHECK(is_mixed[5]);
        CHECK(std::equal(receiver_colours.begin(), receiver_colours.end(),
                         bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values.begin()));
        CHECK(bundle.filament_presets[0] == "My PLA");
        CHECK(bundle.filament_presets[4] == "My PLA");
        const auto &components = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values;
        REQUIRE(components.size() == 6);
        CHECK(components[5] == "1,2");
        const auto &ratios = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values;
        REQUIRE(ratios.size() == 6);
        CHECK(ratios[5] == "0.6,0.4");
        const auto &colour = bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values;
        REQUIRE(colour.size() == 6);
        CHECK(colour[5] == "#800080");
        bool relocated_reported = false;
        for (const std::string &message : pub.material_replacements)
            if (message.find("slot 4 -> slot 5") != std::string::npos)
                relocated_reported = true;
        CHECK(relocated_reported);
        CHECK(pub.skipped_keys.empty());
    }

    {
        PresetBundle bundle;
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        bundle.filament_presets = { "My PLA", "My PLA", "My PLA" };
        bundle.set_num_filaments(3);
        bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values[2]                = 1;
        bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values[2]      = "1,1";
        bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values[2] = "0.9,0.1";

        PublishedMaterialEntry mix;
        mix.slot            = 2;
        mix.publish_color   = true;
        mix.color           = "#800080";
        mix.keys            = { "filament_is_mixed",       "filament_mixed_components",
                                "filament_mixed_sublayer_ratios" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { mix };
        DynamicPrintConfig config = make_file_config(3);
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        CHECK(bundle.filament_presets.size() == 3);
        const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
        REQUIRE(is_mixed.size() == 3);
        CHECK_FALSE(is_mixed[0]);
        CHECK_FALSE(is_mixed[1]);
        CHECK(is_mixed[2]);
        const auto &components = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values;
        REQUIRE(components.size() == 3);
        CHECK(components[2] == "1,2");
        const auto &ratios = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values;
        REQUIRE(ratios.size() == 3);
        CHECK(ratios[2] == "0.6,0.4");
        CHECK(pub.skipped_keys.empty());
        CHECK(pub.material_replacements.empty());
    }

    {
        PresetBundle bundle;
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        bundle.filament_presets = { "My PLA", "My PLA", "My PLA", "My PLA", "My PLA" };
        bundle.set_num_filaments(5, "#123456");
        const std::vector<std::string> receiver_colours =
            bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values;

        auto make_mix_entry = [](int authored_slot, const char *color) {
            PublishedMaterialEntry entry;
            entry.slot          = authored_slot;
            entry.publish_color = true;
            entry.color         = color;
            entry.keys          = { "filament_is_mixed", "filament_mixed_components", "filament_mixed_sublayer_ratios" };
            return entry;
        };
        PublishedMaterialEntry mix_a = make_mix_entry(4, "#800080");
        PublishedMaterialEntry mix_b = make_mix_entry(5, "#FF69B4");

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { mix_a, mix_b };
        DynamicPrintConfig config = make_file_config(6, 2);
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 7);
        const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
        REQUIRE(is_mixed.size() == 7);
        for (size_t i = 0; i < 5; ++i)
            CHECK_FALSE(is_mixed[i]);
        CHECK(is_mixed[5]);
        CHECK(is_mixed[6]);
        const auto &components = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values;
        REQUIRE(components.size() == 7);
        CHECK(components[5] == "1,2");
        CHECK(components[6] == "1,3");
        const auto &ratios = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values;
        REQUIRE(ratios.size() == 7);
        CHECK(ratios[5] == "0.6,0.4");
        CHECK(ratios[6] == "0.3,0.7");
        const auto &colour = bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values;
        REQUIRE(colour.size() == 7);
        CHECK(std::equal(receiver_colours.begin(), receiver_colours.end(), colour.begin()));
        CHECK(colour[5] == "#800080");
        CHECK(colour[6] == "#FF69B4");
        bool a_reported = false, b_reported = false;
        for (const std::string &message : pub.material_replacements) {
            if (message.find("slot 4 -> slot 5") != std::string::npos)
                a_reported = true;
            if (message.find("slot 5 -> slot 6") != std::string::npos)
                b_reported = true;
        }
        CHECK(a_reported);
        CHECK(b_reported);
        CHECK(pub.skipped_keys.empty());
        REQUIRE(pub.mixed_slot_relocations.size() == 2);
        CHECK(pub.mixed_slot_relocations.at(4) == 5);
        CHECK(pub.mixed_slot_relocations.at(5) == 6);
    }
}

TEST_CASE("Published 3MF relocates the receiver's mixed filament past the incoming physical slots", "[Preset][Bundle][Published]")
{
    auto make_receiver = [](PresetBundle &bundle, const std::string &components, const std::string &ratios) {
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        bundle.filament_presets = { "My PLA", "My PLA", "My PLA" };
        bundle.set_num_filaments(3, "#123456");
        bundle.printers.get_edited_preset().config.opt<ConfigOptionBool>("single_extruder_multi_material", true)->value = true;
        bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values[2]          = 1;
        bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values[2] = components;
        bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values[2] = ratios;
        bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values[2]        = "#800080";
        bundle.project_config.opt<ConfigOptionStrings>("filament_multi_colour")->values[2] = "#800080";
    };
    auto make_real_entry = [](int slot, const char *color) {
        PublishedMaterialEntry entry;
        entry.slot            = slot;
        entry.filament_type   = "PLA";
        entry.filament_vendor = "Generic";
        entry.publish_color   = true;
        entry.color           = color;
        return entry;
    };
    auto make_config_4_real = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75, 1.75, 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2, 3, 4 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = {
            "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard"
        };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#000000", "#FFFFFF", "#FFFF00" };
        config.opt<ConfigOptionStrings>("filament_type")->values   = { "PLA", "PLA", "PLA", "PLA" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic", "Generic", "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values    = { "GFL99", "GFL99", "GFL99", "GFL99" };
        return config;
    };

    {
        PresetBundle bundle;
        make_receiver(bundle, "1,2", "0.5,0.5");

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { make_real_entry(0, "#FF0000"), make_real_entry(1, "#000000"),
                              make_real_entry(2, "#FFFFFF"), make_real_entry(3, "#FFFF00") };
        DynamicPrintConfig config = make_config_4_real();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 5);
        const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
        REQUIRE(is_mixed.size() == 5);
        for (size_t i = 0; i < 4; ++i)
            CHECK_FALSE(is_mixed[i]);
        CHECK(is_mixed[4]);
        const auto &components = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values;
        REQUIRE(components.size() == 5);
        CHECK(components[4] == "1,2");
        CHECK(components[2].empty());
        const auto &colour = bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values;
        REQUIRE(colour.size() == 5);
        CHECK(colour[4] == "#800080");
        CHECK(colour[2] == "#FFFFFF");
        CHECK(pub.mixed_slot_relocations.at(2) == 4);
        bool relocated_reported = false;
        for (const std::string &message : pub.material_replacements)
            if (message.find("slot 2 -> slot 4") != std::string::npos &&
                message.find("mixed filament") != std::string::npos)
                relocated_reported = true;
        CHECK(relocated_reported);
        CHECK(pub.skipped_keys.empty());
    }

    {
        PresetBundle bundle;
        make_receiver(bundle, "1,2", "0.5,0.5");

        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values         = { 1.75, 1.75, 1.75, 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values         = { 1, 2, 3, 4 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = {
            "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard"
        };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#000000", "#0000FF", "#800080" };
        config.opt<ConfigOptionStrings>("filament_type")->values   = { "PLA", "PLA", "PLA", "PLA" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic", "Generic", "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values    = { "GFL99", "GFL99", "GFL99", "GFL99" };
        config.opt<ConfigOptionBools>("filament_is_mixed")->values                     = { 0, 0, 0, 1 };
        config.opt<ConfigOptionStrings>("filament_mixed_components")->values           = { "", "", "", "1,3" };
        config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values      = { "", "", "", "0.6,0.4" };
        config.opt<ConfigOptionBools>("filament_mixed_gradient")->values               = { 0, 0, 0, 1 };
        config.opt<ConfigOptionStrings>("filament_mixed_gradient_range")->values       = { "", "", "", "0.9,0.1" };
        config.opt<ConfigOptionStrings>("filament_mixed_gradient_curve")->values       = { "", "", "", "0,0.1|1,0.9" };
        config.opt<ConfigOptionBools>("filament_mixed_gradient_per_part")->values      = { 0, 0, 0, 1 };

        PublishedMaterialEntry mix;
        mix.slot          = 3;
        mix.filament_type = "PLA";
        mix.publish_color = true;
        mix.color         = "#800080";
        mix.keys          = { "filament_is_mixed", "filament_mixed_components", "filament_mixed_sublayer_ratios",
                              "filament_mixed_gradient", "filament_mixed_gradient_range", "filament_mixed_gradient_curve",
                              "filament_mixed_gradient_per_part" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { make_real_entry(0, "#FF0000"), make_real_entry(1, "#000000"),
                              make_real_entry(2, "#0000FF"), mix };
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 5);
        const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
        REQUIRE(is_mixed.size() == 5);
        CHECK_FALSE(is_mixed[0]);
        CHECK_FALSE(is_mixed[1]);
        CHECK_FALSE(is_mixed[2]);
        CHECK(is_mixed[3]);
        CHECK(is_mixed[4]);
        const auto &components = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values;
        REQUIRE(components.size() == 5);
        CHECK(components[3] == "1,2");
        CHECK(components[4] == "1,3");
        REQUIRE(pub.mixed_slot_relocations.size() == 2);
        CHECK(pub.mixed_slot_relocations.at(2) == 3);
        CHECK(pub.mixed_slot_relocations.at(3) == 4);
        CHECK(pub.skipped_keys.empty());
    }
}

TEST_CASE("Published 3MF rebalances several receiver mixed slots past the physical region", "[Preset][Bundle][Published]")
{
    PresetBundle bundle;
    Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
    pla.config.opt_string("filament_type", 0u) = "PLA";
    bundle.filament_presets = { "My PLA", "My PLA", "My PLA" };
    bundle.set_num_filaments(3, "#123456");
    bundle.printers.get_edited_preset().config.opt<ConfigOptionBool>("single_extruder_multi_material", true)->value = true;
    bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values[1]             = 1;
    bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values[2]             = 1;
    bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values[1]   = "1,2";
    bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values[2]   = "1,3";
    bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values[1] = "0.5,0.5";
    bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values[2] = "0.4,0.6";

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionFloats>("filament_diameter")->values         = { 1.75, 1.75, 1.75 };
    config.opt<ConfigOptionInts>("filament_self_index")->values         = { 1, 2, 3 };
    config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = {
        "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard"
    };
    config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00AA00", "#0000FF" };
    config.opt<ConfigOptionStrings>("filament_type")->values   = { "PLA", "PLA", "PLA" };
    config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic", "Generic" };
    config.opt<ConfigOptionStrings>("filament_ids")->values    = { "GFL99", "GFL99", "GFL99" };

    auto make_real_entry = [](int slot, const char *color) {
        PublishedMaterialEntry entry;
        entry.slot            = slot;
        entry.filament_type   = "PLA";
        entry.filament_vendor = "Generic";
        entry.publish_color   = true;
        entry.color           = color;
        return entry;
    };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { make_real_entry(0, "#FF0000"), make_real_entry(1, "#00AA00"), make_real_entry(2, "#0000FF") };
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    REQUIRE(bundle.filament_presets.size() == 5);
    const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
    REQUIRE(is_mixed.size() == 5);
    for (size_t i = 0; i < 3; ++i)
        CHECK_FALSE(is_mixed[i]);
    CHECK(is_mixed[3]);
    CHECK(is_mixed[4]);
    const auto &components = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values;
    REQUIRE(components.size() == 5);
    CHECK(components[3] == "1,2");
    CHECK(components[4] == "1,3");
    REQUIRE(pub.mixed_slot_relocations.size() == 2);
    CHECK(pub.mixed_slot_relocations.at(1) == 3);
    CHECK(pub.mixed_slot_relocations.at(2) == 4);
    CHECK(pub.skipped_keys.empty());
}

TEST_CASE("Published 3MF turns a surplus slot past the printer's filament capacity into an empty mixed placeholder", "[Preset][Bundle][Published]")
{
    auto make_file_config = [](size_t num_author_slots) {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        std::vector<double> diameters(num_author_slots, 1.75);
        std::vector<int> self_index;
        std::vector<std::string> variants;
        for (size_t i = 0; i < num_author_slots; ++i) {
            self_index.push_back(int(i + 1));
            variants.emplace_back("Direct Drive Standard");
        }
        config.opt<ConfigOptionFloats>("filament_diameter")->values          = diameters;
        config.opt<ConfigOptionInts>("filament_self_index")->values          = self_index;
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = variants;
        config.opt<ConfigOptionStrings>("filament_colour")->values.resize(num_author_slots, "#808080");
        config.opt<ConfigOptionStrings>("filament_type")->values.assign(num_author_slots, "PLA");
        config.opt<ConfigOptionStrings>("filament_vendor")->values.assign(num_author_slots, "Generic");
        config.opt<ConfigOptionStrings>("filament_ids")->values.resize(num_author_slots);
        return config;
    };
    auto make_receiver = [](PresetBundle &bundle, size_t nozzles, size_t slots) {
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        bundle.filament_presets.assign(slots, "My PLA");
        bundle.set_num_filaments(slots, "#123456");
        auto &printer_config                                    = bundle.printers.get_edited_preset().config;
        printer_config.opt<ConfigOptionBool>("single_extruder_multi_material", true)->value = false;
        printer_config.opt<ConfigOptionFloats>("nozzle_diameter", true)->values.assign(nozzles, 0.4);
    };
    auto make_physical_entry = [](int slot, const char *color) {
        PublishedMaterialEntry entry;
        entry.slot            = slot;
        entry.filament_type   = "PLA";
        entry.filament_vendor = "Generic";
        entry.publish_color   = true;
        entry.color           = color;
        return entry;
    };

    {
        PresetBundle bundle;
        make_receiver(bundle, 4, 4);
        const std::vector<std::string> receiver_colours =
            bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values;

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { make_physical_entry(4, "#ABCDEF") };
        DynamicPrintConfig config = make_file_config(5);
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 5);
        const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
        REQUIRE(is_mixed.size() == 5);
        for (size_t i = 0; i < 4; ++i)
            CHECK_FALSE(is_mixed[i]);
        CHECK(is_mixed[4]);
        const auto &components = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values;
        REQUIRE(components.size() == 5);
        CHECK(components[4].empty());
        CHECK(std::equal(receiver_colours.begin(), receiver_colours.end(),
                         bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values.begin()));
        CHECK(bundle.filament_presets[0] == "My PLA");
        CHECK(bundle.filament_presets[3] == "My PLA");
        CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values[4] == "#ABCDEF");
        bool placeholder_reported = false;
        for (const std::string &message : pub.material_replacements)
            if (message.find("unassigned mixed filament") != std::string::npos)
                placeholder_reported = true;
        CHECK(placeholder_reported);
        CHECK(pub.skipped_keys.empty());
        CHECK(pub.mixed_slot_relocations.empty());
    }

    {
        PresetBundle bundle;
        make_receiver(bundle, 4, 4);

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { make_physical_entry(4, "#ABCDEF"), make_physical_entry(5, "#F0F0F0") };
        DynamicPrintConfig config = make_file_config(6);
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 6);
        const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
        REQUIRE(is_mixed.size() == 6);
        for (size_t i = 0; i < 4; ++i)
            CHECK_FALSE(is_mixed[i]);
        CHECK(is_mixed[4]);
        CHECK(is_mixed[5]);
        const auto &components = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values;
        REQUIRE(components.size() == 6);
        CHECK(components[4].empty());
        CHECK(components[5].empty());
        const auto &colour = bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values;
        REQUIRE(colour.size() == 6);
        CHECK(colour[4] == "#ABCDEF");
        CHECK(colour[5] == "#F0F0F0");
        CHECK(pub.skipped_keys.empty());
        CHECK(pub.mixed_slot_relocations.empty());
    }

    {
        PresetBundle bundle;
        make_receiver(bundle, 2, 2);

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { make_physical_entry(3, "#ABCDEF") };
        DynamicPrintConfig config = make_file_config(4);
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 3);
        const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
        REQUIRE(is_mixed.size() == 3);
        CHECK_FALSE(is_mixed[0]);
        CHECK_FALSE(is_mixed[1]);
        CHECK(is_mixed[2]);
        const auto &components = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values;
        REQUIRE(components.size() == 3);
        CHECK(components[2].empty());
        REQUIRE(pub.mixed_slot_relocations.size() == 1);
        CHECK(pub.mixed_slot_relocations.at(3) == 2);
        bool relocation_reported = false;
        for (const std::string &message : pub.material_replacements)
            if (message.find("slot 3 -> slot 2") != std::string::npos &&
                message.find("unassigned mixed filament") != std::string::npos)
                relocation_reported = true;
        CHECK(relocation_reported);
        CHECK(pub.skipped_keys.empty());
    }

    {
        PresetBundle bundle;
        make_receiver(bundle, 4, 4);

        PublishedMaterialEntry entry = make_physical_entry(4, "#ABCDEF");
        entry.full                   = true;
        entry.preset_name            = "Generic PLA @System";
        entry.filament_id            = "GFL99";
        entry.full_keys              = { "filament_retraction_length" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { entry };
        DynamicPrintConfig config = make_file_config(5);
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 5);
        const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
        REQUIRE(is_mixed.size() == 5);
        CHECK(is_mixed[4]);
        CHECK(bundle.filaments.find_preset("Generic PLA", false, true) == nullptr);
        CHECK(bundle.filaments.find_preset("Generic PLA (Published)", false, true) == nullptr);
        CHECK(pub.skipped_keys.empty());
    }

    {
        PresetBundle bundle;
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        bundle.filament_presets = { "My PLA", "My PLA", "My PLA", "My PLA" };
        bundle.set_num_filaments(4, "#123456");

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { make_physical_entry(4, "#ABCDEF") };
        DynamicPrintConfig config = make_file_config(5);
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 5);
        const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
        REQUIRE(is_mixed.size() == 5);
        for (size_t i = 0; i < 5; ++i)
            CHECK_FALSE(is_mixed[i]);
        CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values[4] == "#ABCDEF");
        CHECK(pub.skipped_keys.empty());
    }

    {
        PresetBundle bundle;
        make_receiver(bundle, 4, 5);

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { make_physical_entry(4, "#ABCDEF") };
        DynamicPrintConfig config = make_file_config(5);
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 5);
        const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
        REQUIRE(is_mixed.size() == 5);
        for (size_t i = 0; i < 5; ++i)
            CHECK_FALSE(is_mixed[i]);
        CHECK(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionStrings>("filament_colour")->values ==
              std::vector<std::string>{ "#ABCDEF" });
        CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values[4] == "#ABCDEF");
        CHECK(pub.skipped_keys.empty());
    }

    {
        PresetBundle bundle;
        make_receiver(bundle, 1, 1);

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { make_physical_entry(1, "#ABCDEF") };
        DynamicPrintConfig config = make_file_config(2);
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        CHECK(bundle.filament_presets.size() == 1);
        const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
        REQUIRE(is_mixed.size() == 1);
        CHECK_FALSE(is_mixed[0]);
        REQUIRE(pub.skipped_keys.size() == 1);
        CHECK(pub.skipped_keys.front().find("printer supports only 1") != std::string::npos);
    }

    {
        PresetBundle bundle;
        make_receiver(bundle, 4, 4);

        PublishedMaterialEntry mix;
        mix.slot          = 4;
        mix.publish_color = true;
        mix.color         = "#800080";
        mix.keys          = { "filament_is_mixed", "filament_mixed_components", "filament_mixed_sublayer_ratios" };

        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { mix };
        DynamicPrintConfig config = make_file_config(5);
        config.opt<ConfigOptionBools>("filament_is_mixed")->values.assign(5, 0);
        config.opt<ConfigOptionStrings>("filament_mixed_components")->values.assign(5, "");
        config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values.assign(5, "");
        config.opt<ConfigOptionBools>("filament_is_mixed")->values[4]                = 1;
        config.opt<ConfigOptionStrings>("filament_mixed_components")->values[4]      = "1,2";
        config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values[4] = "0.6,0.4";
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 5);
        const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
        REQUIRE(is_mixed.size() == 5);
        CHECK(is_mixed[4]);
        const auto &components = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values;
        REQUIRE(components.size() == 5);
        CHECK(components[4] == "1,2");
        const auto &ratios = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values;
        REQUIRE(ratios.size() == 5);
        CHECK(ratios[4] == "0.6,0.4");
        CHECK(pub.skipped_keys.empty());
    }
}

TEST_CASE("Published 3MF collapses a multi-extruder publish onto a single-extruder receiver", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000" };
        config.opt<ConfigOptionFloats>("retraction_length")->values = { 0.6, 0.9 };
        config.opt<ConfigOptionFloats>("retraction_speed")->values  = { 30.0, 40.0 };
        Preset::normalize(config);
        return config;
    };

    {
        PresetBundle bundle;
        bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_length")->values = { 0.8 };
        bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_speed")->values  = { 25.0 };
        PublishedConfig pub;
        pub.published      = true;
        pub.published_keys = { "retraction_length#0", "retraction_length#1", "retraction_speed#0", "retraction_speed#1" };
        DynamicPrintConfig config = make_file_config();
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        check_double_vector(bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_length")->values, { 0.6 });
        check_double_vector(bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_speed")->values, { 30.0 });
        CHECK(contains_key(pub.skipped_keys, "retraction_length#1"));
        CHECK(contains_key(pub.skipped_keys, "retraction_speed#1"));
        CHECK_FALSE(contains_key(pub.skipped_keys, "retraction_length#0"));
        CHECK_FALSE(contains_key(pub.skipped_keys, "retraction_speed#0"));
    }

    {
        PresetBundle bundle;
        bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_length")->values = { 0.8 };
        PublishedConfig pub;
        pub.published      = true;
        pub.published_keys = { "retraction_length#1" };
        DynamicPrintConfig config = make_file_config();
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        check_double_vector(bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_length")->values, { 0.9 });
        CHECK(pub.skipped_keys.empty());
    }
}

TEST_CASE("Published 3MF overrides each extruder slot on a similar multi-extruder receiver", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00FF00" };
        config.opt<ConfigOptionFloats>("retraction_length")->values = { 0.6, 0.9 };
        Preset::normalize(config);
        return config;
    };

    {
        PresetBundle bundle;
        bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_length")->values = { 0.8, 0.8 };
        PublishedConfig pub;
        pub.published      = true;
        pub.published_keys = { "retraction_length#0", "retraction_length#1" };
        DynamicPrintConfig config = make_file_config();
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        check_double_vector(bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_length")->values, { 0.6, 0.9 });
        CHECK(pub.skipped_keys.empty());
    }

    {
        PresetBundle bundle;
        bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_length")->values = { 0.8, 0.8, 0.7 };
        PublishedConfig pub;
        pub.published      = true;
        pub.published_keys = { "retraction_length#0", "retraction_length#1" };
        DynamicPrintConfig config = make_file_config();
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        check_double_vector(bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("retraction_length")->values, { 0.6, 0.9, 0.7 });
        CHECK(pub.skipped_keys.empty());
    }
}

TEST_CASE("Loading incomplete mixed metadata normalizes slots before adding a filament", "[Preset][Bundle][FilamentMixer]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    const std::vector<std::string> colors = { "#000000", "#FFFFFF", "#5E5C64" };
    config.option<ConfigOptionStrings>("filament_colour")->values = colors;
    config.option<ConfigOptionBool>("single_extruder_multi_material")->value = true;
    config.option<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75, 1.75 };
    config.option<ConfigOptionStrings>("filament_settings_id", true)->values = { "Test PETG", "Test PLA", "Test TPU" };
    const std::vector<std::string> bool_keys = {
        "filament_is_mixed", "filament_mixed_gradient", "filament_mixed_gradient_per_part"
    };
    const std::vector<std::string> string_keys = {
        "filament_mixed_components", "filament_mixed_sublayer_ratios",
        "filament_mixed_gradient_range", "filament_mixed_gradient_curve"
    };
    const size_t metadata_size = GENERATE(0u, 1u, 4u);
    for (const auto &key : bool_keys) {
        if (metadata_size == 0)
            config.erase(key);
        else {
            auto &values = config.option<ConfigOptionBools>(key)->values;
            values.assign(metadata_size, false);
            if (metadata_size > colors.size())
                values.back() = true;
        }
    }
    for (const auto &key : string_keys) {
        if (metadata_size == 0)
            config.erase(key);
        else {
            auto &values = config.option<ConfigOptionStrings>(key)->values;
            values.assign(metadata_size, "");
            if (metadata_size > colors.size())
                values.back() = "stale";
        }
    }
    Preset::normalize(config);

    PresetBundle bundle;
    bundle.load_config_model("test.3mf", std::move(config), Semver());
    const auto presets = bundle.filament_presets;
    REQUIRE(presets.size() == colors.size());
    REQUIRE(presets[0] != presets[1]);
    REQUIRE(presets[1] != presets[2]);
    REQUIRE(presets[0] != presets[2]);
    for (const auto &key : bool_keys) {
        CAPTURE(key, metadata_size);
        CHECK(bundle.project_config.option<ConfigOptionBools>(key)->values ==
              std::vector<unsigned char>(colors.size(), false));
    }
    for (const auto &key : string_keys) {
        CAPTURE(key, metadata_size);
        CHECK(bundle.project_config.option<ConfigOptionStrings>(key)->values ==
              std::vector<std::string>(colors.size(), ""));
    }
    REQUIRE(bundle.num_physical_filaments() == colors.size());
    REQUIRE(bundle.num_mixed_filaments() == 0);

    bundle.set_num_filaments(bundle.num_physical_filaments() + bundle.num_mixed_filaments() + 1, "#FF0000");
    REQUIRE(bundle.filament_presets.size() == presets.size() + 1);
    const auto &actual_colors = bundle.project_config.option<ConfigOptionStrings>("filament_colour")->values;
    REQUIRE(actual_colors.size() == colors.size() + 1);
    for (size_t i = 0; i < presets.size(); ++i) {
        CHECK(bundle.filament_presets[i] == presets[i]);
        CHECK(actual_colors[i] == colors[i]);
    }
    CHECK(bundle.num_physical_filaments() == colors.size() + 1);
    CHECK_FALSE(bundle.is_mixed_filament(colors.size()));
}

TEST_CASE("Loading a project preserves existing mixed filament definitions", "[Preset][Bundle][FilamentMixer]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.option<ConfigOptionBool>("single_extruder_multi_material")->value = true;
    config.option<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75, 1.75 };
    config.option<ConfigOptionStrings>("filament_colour")->values = { "#000000", "#FFFFFF", "#808080" };
    config.option<ConfigOptionBools>("filament_is_mixed")->values = { false, false, true };
    config.option<ConfigOptionStrings>("filament_mixed_components")->values = { "", "", "1,2" };
    config.option<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values = { "", "", "1,2" };
    config.option<ConfigOptionBools>("filament_mixed_gradient")->values = { false, false, true };
    config.option<ConfigOptionStrings>("filament_mixed_gradient_range")->values = { "", "", "0,100" };
    config.option<ConfigOptionStrings>("filament_mixed_gradient_curve")->values = { "", "", "0,0.1|1,0.9" };
    config.option<ConfigOptionBools>("filament_mixed_gradient_per_part")->values = { false, false, true };
    Preset::normalize(config);
    const auto original = config;

    PresetBundle bundle;
    bundle.load_config_model("test.3mf", std::move(config), Semver());
    for (const auto *key : kMixedKeys) {
        CAPTURE(key);
        CHECK(*bundle.project_config.option(key) == *original.option(key));
    }
    CHECK(bundle.num_physical_filaments() == 2);
    CHECK(bundle.num_mixed_filaments() == 1);
}

TEST_CASE("Adding a filament preserves slots with incomplete mixed metadata", "[Preset][Bundle][FilamentMixer]")
{
    PresetBundle bundle;
    bundle.set_num_filaments(3u, std::string("#000000"));
    auto *colors = bundle.project_config.option<ConfigOptionStrings>("filament_colour");
    colors->values = { "#000000", "#FFFFFF", "#5E5C64" };
    bundle.filament_presets = { "Test PETG", "Test PLA", "Test TPU" };
    const auto original_presets = bundle.filament_presets;
    const auto original_colors = colors->values;
    auto *flags = bundle.project_config.option<ConfigOptionBools>("filament_is_mixed");
    flags->values = GENERATE(std::vector<unsigned char>{}, std::vector<unsigned char>{ false },
                            std::vector<unsigned char>{ false, false, false, true });
    const std::vector<std::string> string_keys = {
        "filament_mixed_components", "filament_mixed_sublayer_ratios",
        "filament_mixed_gradient_range", "filament_mixed_gradient_curve"
    };
    const std::vector<std::string> bool_keys = {
        "filament_mixed_gradient", "filament_mixed_gradient_per_part"
    };
    for (const auto &key : string_keys)
        bundle.project_config.option<ConfigOptionStrings>(key)->values = { "", "", "", "stale" };
    for (const auto &key : bool_keys)
        bundle.project_config.option<ConfigOptionBools>(key)->values = { false, false, false, true };

    REQUIRE(bundle.num_physical_filaments() == original_colors.size());
    REQUIRE(bundle.num_mixed_filaments() == 0);
    bundle.set_num_filaments(bundle.num_physical_filaments() + bundle.num_mixed_filaments() + 1, "#FF0000");

    REQUIRE(bundle.filament_presets.size() == original_presets.size() + 1);
    REQUIRE(colors->values.size() == original_colors.size() + 1);
    for (size_t i = 0; i < original_presets.size(); ++i) {
        CHECK(bundle.filament_presets[i] == original_presets[i]);
        CHECK(colors->values[i] == original_colors[i]);
    }
    CHECK(colors->values.back() == "#FF0000");
    CHECK(bundle.num_physical_filaments() == 4);
    CHECK(bundle.num_mixed_filaments() == 0);
    REQUIRE(flags->values.size() == 4);
    CHECK_FALSE(bundle.is_mixed_filament(3));
    for (const auto &key : string_keys) {
        CAPTURE(key);
        CHECK(bundle.project_config.option<ConfigOptionStrings>(key)->values ==
              std::vector<std::string>{ "", "", "", "" });
    }
    for (const auto &key : bool_keys) {
        CAPTURE(key);
        CHECK(bundle.project_config.option<ConfigOptionBools>(key)->values ==
              std::vector<unsigned char>{ false, false, false, false });
    }
}

TEST_CASE("The physical filament count is not fooled by a lone filament_presets top-up", "[Preset][Bundle][FilamentMixer]")
{
    PresetBundle bundle;

    SECTION("no mixed slots") {
        bundle.set_num_filaments(4u, std::string("#FF0000"));
        bundle.printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter", true)->values =
            { 0.4, 0.4, 0.4, 0.4, 0.4 };
        bundle.update_multi_material_filament_presets();

        REQUIRE(bundle.filament_presets.size() == 5);
        REQUIRE(bundle.project_config.option<ConfigOptionStrings>("filament_colour")->values.size() == 4);
        CHECK(bundle.num_physical_filaments() == 4);
    }

    SECTION("behind a mixed tail") {
        bundle.set_num_filaments(5u, std::string("#FF0000"));
        bundle.project_config.option<ConfigOptionBools>("filament_is_mixed")->values =
            { false, false, false, false, true };
        bundle.printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter", true)->values =
            { 0.4, 0.4, 0.4, 0.4, 0.4, 0.4 };
        bundle.update_multi_material_filament_presets();

        REQUIRE(bundle.filament_presets.size() == 6);
        REQUIRE(bundle.project_config.option<ConfigOptionStrings>("filament_colour")->values.size() == 5);
        CHECK(bundle.num_physical_filaments() == 4);
        CHECK(bundle.num_mixed_filaments() == 1);
    }
}

TEST_CASE("New filament colours are placed by array position", "[Preset][Bundle][FilamentMixer]")
{
    PresetBundle bundle;
    bundle.set_num_filaments(4u, std::string("#FF0000"));
    bundle.printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter", true)->values =
        { 0.4, 0.4, 0.4, 0.4, 0.4 };
    bundle.update_multi_material_filament_presets();
    REQUIRE(bundle.filament_presets.size() == 5);
    REQUIRE(bundle.project_config.option<ConfigOptionStrings>("filament_colour")->values.size() == 4);

    bundle.set_num_filaments(5u, std::string("#00FF00"));

    const auto &colours = bundle.project_config.option<ConfigOptionStrings>("filament_colour")->values;
    REQUIRE(colours.size() == 5);
    CHECK(colours[4] == "#00FF00");
}

TEST_CASE("A saved mix is still a mix after an app restart", "[Preset][Bundle][FilamentMixer]")
{
    AppConfig app_config;

    {
        PresetBundle bundle;
        add_inmemory_preset(bundle.printers, "Test Printer");
        bundle.printers.select_preset_by_name("Test Printer", true);
        add_inmemory_preset(bundle.filaments, "Test Filament");
        bundle.filaments.select_preset_by_name("Test Filament", true);
        bundle.set_num_filaments(5u, std::string("#FF0000"));
        bundle.filament_presets.assign(5, "Test Filament");
        bundle.project_config.option<ConfigOptionBools>("filament_is_mixed")->values =
            { false, false, false, false, true };
        bundle.project_config.option<ConfigOptionStrings>("filament_mixed_components")->values =
            { "", "", "", "", "2,3" };
        bundle.export_selections(app_config);

        REQUIRE(app_config.get_printer_setting("Test Printer", "filament_is_mixed") == "0,0,0,0,1");
    }

    PresetBundle bundle;
    add_inmemory_preset(bundle.printers, "Test Printer");
    add_inmemory_preset(bundle.filaments, "Test Filament");
    bundle.load_selections(app_config);

    CHECK(bundle.filament_presets.size() == 5);
    CHECK(bundle.num_mixed_filaments() == 1);
    CHECK(bundle.is_mixed_filament(4));
    CHECK(bundle.project_config.option<ConfigOptionStrings>("filament_mixed_components")->values[4] == "2,3");
}

TEST_CASE("A saved mix survives a restart on a multi-tool printer", "[Preset][Bundle][FilamentMixer]")
{
    auto make_toolchanger = [](PresetBundle &bundle) -> Preset & {
        Preset &p = add_inmemory_preset(bundle.printers, "Tool Changer");
        p.config.option<ConfigOptionFloats>("nozzle_diameter", true)->values = { 0.4, 0.4, 0.4, 0.4 };
        p.config.option<ConfigOptionBool>("single_extruder_multi_material", true)->value = false;
        return p;
    };

    AppConfig app_config;
    {
        PresetBundle bundle;
        make_toolchanger(bundle);
        bundle.printers.select_preset_by_name("Tool Changer", true);
        add_inmemory_preset(bundle.filaments, "Test Filament");
        bundle.filaments.select_preset_by_name("Test Filament", true);
        bundle.set_num_filaments(5u, std::string("#FF0000"));
        bundle.filament_presets.assign(5, "Test Filament");
        bundle.project_config.option<ConfigOptionBools>("filament_is_mixed")->values =
            { false, false, false, false, true };
        bundle.project_config.option<ConfigOptionStrings>("filament_mixed_components")->values =
            { "", "", "", "", "1,2" };
        bundle.export_selections(app_config);
        REQUIRE(app_config.get_printer_setting("Tool Changer", "filament_is_mixed") == "0,0,0,0,1");
    }

    PresetBundle bundle;
    make_toolchanger(bundle);
    add_inmemory_preset(bundle.filaments, "Test Filament");
    bundle.load_selections(app_config);

    CHECK(bundle.filament_presets.size() == 5);
    CHECK(bundle.num_mixed_filaments() == 1);
    CHECK(bundle.is_mixed_filament(4));

    SECTION("and through the GUI startup calls that follow it") {
        const size_t target = 4u + bundle.num_mixed_filaments();
        if (target > bundle.filament_presets.size())
            bundle.set_num_filaments(target);
        CHECK(bundle.num_mixed_filaments() == 1);

        bundle.on_extruders_count_changed(4);
        CHECK(bundle.num_mixed_filaments() == 1);

        bundle.update_selections(app_config);
        CHECK(bundle.filament_presets.size() == 5);
        CHECK(bundle.num_mixed_filaments() == 1);
        CHECK(bundle.is_mixed_filament(4));
    }
}

TEST_CASE("Sizing down to the nozzle count plus mixes is what eats the mixed tail", "[Preset][Bundle][FilamentMixer]")
{
    const size_t nozzle_count = 4;
    PresetBundle bundle;
    bundle.set_num_filaments(6u, std::string("#FF0000"));
    bundle.project_config.option<ConfigOptionBools>("filament_is_mixed")->values =
        { false, false, false, false, false, true };
    bundle.project_config.option<ConfigOptionStrings>("filament_mixed_components")->values =
        { "", "", "", "", "", "1,2" };
    REQUIRE(bundle.num_physical_filaments() == 5);

    const size_t target = nozzle_count + bundle.num_mixed_filaments();
    REQUIRE(target < bundle.filament_presets.size());

    SECTION("applied as written, the mix is gone and every slot reads physical") {
        bundle.set_num_filaments(target);

        CHECK(bundle.filament_presets.size() == target);
        CHECK(bundle.num_mixed_filaments() == 0);
        CHECK(bundle.num_physical_filaments() == target);
    }

    SECTION("applied as a floor, the mix is left alone") {
        if (target > bundle.filament_presets.size())
            bundle.set_num_filaments(target);

        CHECK(bundle.filament_presets.size() == 6);
        CHECK(bundle.num_mixed_filaments() == 1);
        CHECK(bundle.is_mixed_filament(5));
        CHECK(bundle.project_config.option<ConfigOptionStrings>("filament_mixed_components")->values[5] == "1,2");
    }
}

TEST_CASE("remap_model_filament_slots repoints extruder configs and color painting", "[Preset][Bundle][Published]")
{
    auto make_model = [] {
        Model model;
        ModelObject *object_a = model.add_object();
        object_a->name        = "relocated mix";
        ModelVolume *vol_a    = object_a->add_volume(make_cube(10., 10., 10.));
        vol_a->config.set_key_value("extruder", new ConfigOptionInt(5));
        {
            TriangleSelector selector(vol_a->mesh());
            selector.set_facet(0, EnforcerBlockerType(5));
            selector.set_facet(1, EnforcerBlockerType(2));
            vol_a->mmu_segmentation_facets.set_data(selector.serialize());
        }
        ModelObject *object_b = model.add_object();
        object_b->name        = "untouched";
        object_b->config.set_key_value("extruder", new ConfigOptionInt(1));
        ModelVolume *vol_b = object_b->add_volume(make_cube(5., 5., 5.));
        vol_b->config.set_key_value("extruder", new ConfigOptionInt(2));
        {
            TriangleSelector selector(vol_b->mesh());
            selector.set_facet(0, EnforcerBlockerType(2));
            vol_b->mmu_segmentation_facets.set_data(selector.serialize());
        }
        return model;
    };

    const std::map<int, int> relocations = {{4, 5}};

    Model model = make_model();
    Slic3r::remap_model_filament_slots(model, relocations);

    const ModelVolume *vol_a = model.objects[0]->volumes.front();
    CHECK(vol_a->config.extruder() == 6);
    REQUIRE(TriangleSelector::has_facets(vol_a->mmu_segmentation_facets.get_data(), EnforcerBlockerType(6)));
    REQUIRE_FALSE(TriangleSelector::has_facets(vol_a->mmu_segmentation_facets.get_data(), EnforcerBlockerType(5)));
    CHECK(TriangleSelector::has_facets(vol_a->mmu_segmentation_facets.get_data(), EnforcerBlockerType(2)));

    const ModelVolume *vol_b = model.objects[1]->volumes.front();
    CHECK(vol_b->config.extruder() == 2);
    CHECK(TriangleSelector::has_facets(vol_b->mmu_segmentation_facets.get_data(), EnforcerBlockerType(2)));

    Model chained = make_model();
    chained.objects[0]->volumes.front()->config.set_key_value("extruder", new ConfigOptionInt(4));
    Slic3r::remap_model_filament_slots(chained, std::map<int, int>{{3, 4}, {4, 6}});
    CHECK(chained.objects[0]->volumes.front()->config.extruder() == 5);
    CHECK(TriangleSelector::has_facets(chained.objects[0]->volumes.front()->mmu_segmentation_facets.get_data(),
                                       EnforcerBlockerType(7)));
    CHECK_FALSE(TriangleSelector::has_facets(chained.objects[0]->volumes.front()->mmu_segmentation_facets.get_data(),
                                             EnforcerBlockerType(5)));
}

TEST_CASE("Published 3MF drops a mixed filament that does not fit the slot limit and reports it", "[Preset][Bundle][Published]")
{
    PresetBundle bundle;
    Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
    pla.config.opt_string("filament_type", 0u) = "PLA";
    bundle.filament_presets = { "My PLA" };
    bundle.set_num_filaments(unsigned(EnforcerBlockerType::ExtruderMax), "#123456");
    const std::vector<std::string> receiver_colours =
        bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values;

    PublishedMaterialEntry mix;
    mix.filament_type   = "PLA";
    mix.filament_vendor = "Generic";
    mix.slot            = int(EnforcerBlockerType::ExtruderMax) + 6;
    mix.publish_color   = true;
    mix.color           = "#800080";
    mix.keys            = { "filament_is_mixed",       "filament_mixed_components",
                            "filament_mixed_sublayer_ratios", "filament_mixed_gradient",
                            "filament_mixed_gradient_range",  "filament_mixed_gradient_curve",
                            "filament_mixed_gradient_per_part" };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { mix };
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000" };
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    REQUIRE(bundle.filament_presets.size() == size_t(EnforcerBlockerType::ExtruderMax));
    for (size_t i = 0; i < bundle.filament_presets.size(); ++i)
        CHECK_FALSE(bundle.is_mixed_filament(i));
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values == receiver_colours);
    REQUIRE(contains_key(pub.skipped_keys, "material:PLA (mixed filament definition: filament slot limit reached)"));
    CHECK(pub.material_replacements.empty());
}

TEST_CASE("Published 3MF rejects a mixed filament definition with impossible components", "[Preset][Bundle][Published]")
{
    auto make_file_config = [](const std::string &components) {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75, 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2, 3 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = {
            "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard"
        };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#0000FF", "#800080" };
        config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA", "PETG", "PLA" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic", "Generic" };
        config.opt<ConfigOptionBools>("filament_is_mixed")->values = { 0, 0, 1 };
        config.opt<ConfigOptionStrings>("filament_mixed_components")->values = { "", "", components };
        config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values = { "", "", "0.6,0.4" };
        return config;
    };

    PublishedMaterialEntry mix;
    mix.filament_type   = "PLA";
    mix.filament_vendor = "Generic";
    mix.filament_id     = "GFL99";
    mix.slot            = 2;
    mix.publish_color   = true;
    mix.color           = "#800080";
    mix.keys            = { "filament_is_mixed", "filament_mixed_components", "filament_mixed_sublayer_ratios" };

    for (const char *components : { "1,4", "1,3", "1" }) {
        INFO("components = " << components);
        PresetBundle bundle;
        Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
        pla.config.opt_string("filament_type", 0u) = "PLA";
        pla.config.opt<ConfigOptionStrings>("filament_colour", true)->values = { "#123456" };
        bundle.filament_presets = { "My PLA" };

        mix.keys = { "filament_is_mixed", "filament_mixed_components", "filament_mixed_sublayer_ratios" };
        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { mix };
        DynamicPrintConfig config = make_file_config(components);
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 3);
        CHECK(bundle.is_mixed_filament(2));
        CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values[2].empty());
        CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values[2].empty());
        if (std::string(components) == "1")
            CHECK(contains_key(pub.skipped_keys, "material:GFL99 (mixed filament definition: needs at least two components)"));
        else
            CHECK(contains_key(pub.skipped_keys, "material:GFL99 (mixed filament definition: components reference missing slots)"));
        REQUIRE(pub.material_replacements.size() == 1);
        CHECK(pub.material_replacements[0].find("slot 2: mixed filament definition could not be imported") != std::string::npos);
        CHECK(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionStrings>("filament_colour")->values ==
              std::vector<std::string>{ "#123456" });
    }
}

TEST_CASE("Published 3MF finalizes a mixed filament rejected over a placeholder component as an empty placeholder", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config                                              = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values            = std::vector<double>(8, 1.75);
        config.opt<ConfigOptionInts>("filament_self_index")->values            = { 1, 2, 3, 4, 5, 6, 7, 8 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values   = std::vector<std::string>(8, "Direct Drive Standard");
        config.opt<ConfigOptionStrings>("filament_colour")->values             = { "#FF0000", "#00FF00", "#0000FF", "#FFFF00",
                                                                                   "#FF00FF", "#00FFFF", "#800080", "#804000" };
        config.opt<ConfigOptionStrings>("filament_type")->values.assign(8, "PLA");
        config.opt<ConfigOptionStrings>("filament_vendor")->values.assign(8, "Generic");
        config.opt<ConfigOptionBools>("filament_is_mixed")->values             = { 0, 0, 0, 0, 0, 0, 1, 1 };
        config.opt<ConfigOptionStrings>("filament_mixed_components")->values   = { "", "", "", "", "", "", "1,2", "2,6" };
        config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values = { "", "", "", "", "", "", "0.6,0.4", "0.5,0.5" };
        config.opt<ConfigOptionBools>("filament_mixed_gradient")->values       = { 0, 0, 0, 0, 0, 0, 0, 0 };
        config.opt<ConfigOptionStrings>("filament_mixed_gradient_range")->values.assign(8, "");
        config.opt<ConfigOptionStrings>("filament_mixed_gradient_curve")->values.assign(8, "");
        config.opt<ConfigOptionBools>("filament_mixed_gradient_per_part")->values = { 0, 0, 0, 0, 0, 0, 0, 0 };
        return config;
    };
    PresetBundle bundle;
    Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
    pla.config.opt_string("filament_type", 0u) = "PLA";
    bundle.filament_presets.assign(4, "My PLA");
    bundle.set_num_filaments(4, "#123456");
    auto &printer_config = bundle.printers.get_edited_preset().config;
    printer_config.opt<ConfigOptionBool>("single_extruder_multi_material", true)->value = false;
    printer_config.opt<ConfigOptionFloats>("nozzle_diameter", true)->values.assign(4, 0.4);

    auto make_full_entry = [](int slot) {
        PublishedMaterialEntry entry;
        entry.slot            = slot;
        entry.filament_type   = "PLA";
        entry.filament_vendor = "Generic";
        entry.full            = true;
        entry.full_keys       = { "filament_retraction_length" };
        return entry;
    };
    auto make_mix_entry = [](int slot, const char *color) {
        PublishedMaterialEntry entry;
        entry.slot            = slot;
        entry.filament_type   = "PLA";
        entry.filament_vendor = "Generic";
        entry.publish_color   = true;
        entry.color           = color;
        entry.keys            = { "filament_is_mixed",       "filament_mixed_components",
                                  "filament_mixed_sublayer_ratios", "filament_mixed_gradient",
                                  "filament_mixed_gradient_range",  "filament_mixed_gradient_curve",
                                  "filament_mixed_gradient_per_part" };
        return entry;
    };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { make_full_entry(0), make_full_entry(1), make_full_entry(4), make_full_entry(5),
                          make_mix_entry(6, "#800080"), make_mix_entry(7, "#804000") };
    DynamicPrintConfig config = make_file_config();
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    REQUIRE(bundle.filament_presets.size() == 8);
    const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
    REQUIRE(is_mixed.size() == 8);
    for (size_t i = 0; i < 4; ++i)
        CHECK_FALSE(is_mixed[i]);
    CHECK(is_mixed[4]);
    CHECK(is_mixed[5]);
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values[4].empty());
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values[5].empty());
    CHECK(is_mixed[6]);
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values[6] == "1,2");
    CHECK(is_mixed[7]);
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values[7].empty());
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values[7].empty());
    REQUIRE(pub.skipped_keys.size() == 1);
    CHECK(pub.skipped_keys[0] == "material:PLA (mixed filament definition: components reference missing slots)");
    REQUIRE(pub.material_replacements.size() == 5);
    CHECK(std::any_of(pub.material_replacements.begin(), pub.material_replacements.end(),
                      [](const std::string &line) {
                          return line.find("slot 7: mixed filament definition could not be imported") != std::string::npos;
                      }));
}

TEST_CASE("Published 3MF turns an unpublished gap slot past the printer's capacity into an empty mixed placeholder", "[Preset][Bundle][Published]")
{
    PresetBundle bundle;
    Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
    pla.config.opt_string("filament_type", 0u) = "PLA";
    bundle.filament_presets.assign(4, "My PLA");
    bundle.set_num_filaments(4, "#123456");
    auto &printer_config                                    = bundle.printers.get_edited_preset().config;
    printer_config.opt<ConfigOptionBool>("single_extruder_multi_material", true)->value = false;
    printer_config.opt<ConfigOptionFloats>("nozzle_diameter", true)->values.assign(4, 0.4);

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionFloats>("filament_diameter")->values = std::vector<double>(8, 1.75);
    config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2, 3, 4, 5, 6, 7, 8 };
    config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = std::vector<std::string>(8, "Direct Drive Standard");
    config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00FF00", "#0000FF", "#FFFF00",
                                                                   "#FF00FF", "#00FFFF", "#800080", "#804000" };
    config.opt<ConfigOptionStrings>("filament_type")->values.assign(8, "PLA");
    config.opt<ConfigOptionStrings>("filament_vendor")->values.assign(8, "Generic");
    config.opt<ConfigOptionBools>("filament_is_mixed")->values             = { 0, 0, 0, 0, 0, 0, 0, 1 };
    config.opt<ConfigOptionStrings>("filament_mixed_components")->values   = { "", "", "", "", "", "", "", "1,2" };
    config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values = { "", "", "", "", "", "", "", "0.5,0.5" };
    config.opt<ConfigOptionBools>("filament_mixed_gradient")->values       = std::vector<unsigned char>(8, 0);
    config.opt<ConfigOptionStrings>("filament_mixed_gradient_range")->values.assign(8, "");
    config.opt<ConfigOptionStrings>("filament_mixed_gradient_curve")->values.assign(8, "");
    config.opt<ConfigOptionBools>("filament_mixed_gradient_per_part")->values = std::vector<unsigned char>(8, 0);

    auto make_full_entry = [](int slot) {
        PublishedMaterialEntry entry;
        entry.slot          = slot;
        entry.filament_type = "PLA";
        entry.full          = true;
        entry.full_keys     = { "filament_retraction_length" };
        return entry;
    };
    auto make_mix_entry = [](int slot, const char *color) {
        PublishedMaterialEntry entry;
        entry.slot            = slot;
        entry.filament_type   = "PLA";
        entry.publish_color   = true;
        entry.color           = color;
        entry.keys            = { "filament_is_mixed",       "filament_mixed_components",
                                  "filament_mixed_sublayer_ratios", "filament_mixed_gradient",
                                  "filament_mixed_gradient_range",  "filament_mixed_gradient_curve",
                                  "filament_mixed_gradient_per_part" };
        return entry;
    };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { make_full_entry(0), make_full_entry(1), make_full_entry(2), make_full_entry(3),
                          make_full_entry(4), make_full_entry(5), make_mix_entry(7, "#804000") };
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    REQUIRE(bundle.filament_presets.size() == 8);
    const auto &is_mixed = bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values;
    REQUIRE(is_mixed.size() == 8);
    for (size_t i = 0; i < 4; ++i)
        CHECK_FALSE(is_mixed[i]);
    CHECK(is_mixed[4]);
    CHECK(is_mixed[5]);
    CHECK(is_mixed[6]);
    CHECK(is_mixed[7]);
    const auto &components = bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values;
    REQUIRE(components.size() == 8);
    CHECK(components[4].empty());
    CHECK(components[5].empty());
    CHECK(components[6].empty());
    CHECK(components[7] == "1,2");
    size_t physical_count = 0;
    for (bool mixed : is_mixed)
        if (!mixed)
            ++physical_count;
    CHECK(physical_count == 4);
    bool gap_reported = false;
    for (const std::string &message : pub.material_replacements)
        if (message.find("slot 6: unassigned mixed filament") != std::string::npos)
            gap_reported = true;
    CHECK(gap_reported);
    CHECK(pub.skipped_keys.empty());
}

TEST_CASE("Published 3MF reports a relocated mixed filament whose payload cells are missing", "[Preset][Bundle][Published]")
{
    PresetBundle bundle;
    Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
    pla.config.opt_string("filament_type", 0u) = "PLA";
    bundle.filament_presets = { "My PLA", "My PLA", "My PLA", "My PLA", "My PLA" };
    bundle.set_num_filaments(5, "#123456");
    const std::vector<std::string> receiver_colours =
        bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values;

    PublishedMaterialEntry mix;
    mix.filament_type   = "PLA";
    mix.filament_vendor = "Generic";
    mix.filament_id     = "GFL99";
    mix.slot            = 3;
    mix.publish_color   = true;
    mix.color           = "#800080";
    mix.keys            = { "filament_is_mixed",       "filament_mixed_components",
                            "filament_mixed_sublayer_ratios", "filament_mixed_gradient",
                            "filament_mixed_gradient_range",  "filament_mixed_gradient_curve",
                            "filament_mixed_gradient_per_part" };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { mix };
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75 };
    config.opt<ConfigOptionInts>("filament_self_index")->values = { 1 };
    config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard" };
    config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000" };
    config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA" };
    config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic" };
    config.opt<ConfigOptionBools>("filament_is_mixed")->values = { 0 };
    config.opt<ConfigOptionStrings>("filament_mixed_components")->values = { "" };
    config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values = { "" };
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    REQUIRE(pub.mixed_slot_relocations.size() == 1);
    CHECK(pub.mixed_slot_relocations.at(3) == 5);
    REQUIRE(pub.material_replacements.size() == 2);
    CHECK(pub.material_replacements[0].find("slot 3 -> slot 5") != std::string::npos);
    REQUIRE(bundle.filament_presets.size() == 6);
    CHECK(bundle.is_mixed_filament(5));
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values[5].empty());
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values[5].empty());
    CHECK(contains_key(pub.skipped_keys, "material:GFL99 (mixed filament definition: needs at least two components)"));
    CHECK(pub.material_replacements[1].find("slot 5: mixed filament definition could not be imported") != std::string::npos);
    CHECK(std::equal(receiver_colours.begin(), receiver_colours.end(),
                     bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values.begin()));
}

TEST_CASE("Published 3MF re-points an aliased grown slot's material by identity tiers", "[Preset][Bundle][Published]")
{
    auto make_file_config = [] {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75, 1.75, 1.75 };
        config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2, 3, 4 };
        config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = {
            "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard"
        };
        config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00FF00", "#0000FF", "#FFFF00" };
        config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA", "PLA", "PLA", "PLA" };
        config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic", "Generic", "Generic" };
        config.opt<ConfigOptionStrings>("filament_ids")->values = { "GFL99", "GFL99", "GFL99", "GFL99" };
        return config;
    };

    PublishedMaterialEntry entry;
    entry.slot          = 2;
    entry.filament_type = "PLA";

    SECTION("an exact preset name outranks the bare-name form")
    {
        PresetBundle bundle;
        Preset &mine  = add_inmemory_preset(bundle.filaments, "My PLA");
        mine.config.opt_string("filament_type", 0u) = "PLA";
        Preset &bare  = add_inmemory_preset(bundle.filaments, "Authored PLA");
        bare.config.opt_string("filament_type", 0u) = "PLA";
        Preset &exact = add_inmemory_preset(bundle.filaments, "Authored PLA @Vendor");
        exact.config.opt_string("filament_type", 0u) = "PLA";
        bundle.filament_presets = { "My PLA" };

        entry.preset_name = "Authored PLA @Vendor";
        entry.keys        = { "filament_retraction_length" };
        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { entry };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 3);
        CHECK(bundle.filament_presets[1] == "My PLA");
        CHECK(bundle.filament_presets[2] == "Authored PLA @Vendor");
        REQUIRE(pub.material_replacements.size() == 1);
        CHECK(pub.material_replacements[0] == "slot 2: My PLA -> Authored PLA @Vendor");
    }

    SECTION("a bare name outranks an exact setting_id")
    {
        PresetBundle bundle;
        Preset &mine = add_inmemory_preset(bundle.filaments, "My PLA");
        mine.config.opt_string("filament_type", 0u) = "PLA";
        Preset &bare = add_inmemory_preset(bundle.filaments, "Authored PLA");
        bare.config.opt_string("filament_type", 0u) = "PLA";
        Preset &sid  = add_inmemory_preset(bundle.filaments, "Bbb PLA");
        sid.config.opt_string("filament_type", 0u) = "PLA";
        sid.setting_id = "SID123";
        bundle.filament_presets = { "My PLA" };

        entry.preset_name = "Authored PLA @Vendor";
        entry.setting_id  = "SID123";
        entry.keys        = { "filament_retraction_length" };
        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { entry };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 3);
        CHECK(bundle.filament_presets[1] == "My PLA");
        CHECK(bundle.filament_presets[2] == "Authored PLA");
        REQUIRE(pub.material_replacements.size() == 1);
        CHECK(pub.material_replacements[0] == "slot 2: My PLA -> Authored PLA");
    }

    SECTION("an exact setting_id outranks an exact filament_id")
    {
        PresetBundle bundle;
        Preset &mine = add_inmemory_preset(bundle.filaments, "My PLA");
        mine.config.opt_string("filament_type", 0u) = "PLA";
        Preset &sid  = add_inmemory_preset(bundle.filaments, "Bbb PLA");
        sid.config.opt_string("filament_type", 0u) = "PLA";
        sid.setting_id = "SID123";
        Preset &fid  = add_inmemory_preset(bundle.filaments, "Ccc PLA");
        fid.config.opt_string("filament_type", 0u) = "PLA";
        fid.filament_id = "GFA00";
        bundle.filament_presets = { "My PLA" };

        entry.preset_name = "Authored PLA @Vendor";
        entry.setting_id  = "SID123";
        entry.filament_id = "GFA00";
        entry.keys        = { "filament_retraction_length" };
        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { entry };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 3);
        CHECK(bundle.filament_presets[1] == "My PLA");
        CHECK(bundle.filament_presets[2] == "Bbb PLA");
        REQUIRE(pub.material_replacements.size() == 1);
        CHECK(pub.material_replacements[0] == "slot 2: My PLA -> Bbb PLA");
    }

    SECTION("a vendor+type match is reported as a substitute")
    {
        PresetBundle bundle;
        Preset &mine  = add_inmemory_preset(bundle.filaments, "My PETG");
        mine.config.opt_string("filament_type", 0u) = "PETG";
        Preset &exact_vendor = add_inmemory_preset(bundle.filaments, "Aaa PLA");
        exact_vendor.config.opt_string("filament_type", 0u) = "PLA";
        exact_vendor.config.opt_string("filament_vendor", 0u) = "Generic";
        Preset &other_vendor = add_inmemory_preset(bundle.filaments, "Zzz PLA");
        other_vendor.config.opt_string("filament_type", 0u) = "PLA";
        other_vendor.config.opt_string("filament_vendor", 0u) = "Other";
        bundle.filament_presets = { "My PETG" };

        entry.filament_vendor = "Generic";
        entry.publish_type    = true;
        entry.publish_type_value = "PLA";
        PublishedConfig pub;
        pub.published     = true;
        pub.material_keys = { entry };
        DynamicPrintConfig config = make_file_config();
        Preset::normalize(config);
        bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

        REQUIRE(bundle.filament_presets.size() == 3);
        CHECK(bundle.filament_presets[1] == "My PETG");
        CHECK(bundle.filament_presets[2] == "Aaa PLA");
        REQUIRE(pub.material_replacements.size() == 1);
        CHECK(pub.material_replacements[0] == "slot 2: My PETG -> Aaa PLA (substitute)");
    }
}

TEST_CASE("Published 3MF silently ignores structural keys in published_keys", "[Preset][Bundle][Published]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000" };
    config.opt_float("layer_height") = 0.28;
    Preset::normalize(config);

    PresetBundle bundle;
    bundle.prints.get_edited_preset().config.opt_float("layer_height") = 0.1;
    const std::vector<std::string> ids_before =
        bundle.filaments.get_edited_preset().config.opt<ConfigOptionStrings>("filament_settings_id")->values;

    PublishedConfig pub;
    pub.published      = true;
    pub.published_keys = { "filament_ids", "inherits", "printer_settings_id", "layer_height", "not_a_setting" };
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    CHECK_THAT(bundle.prints.get_edited_preset().config.opt_float("layer_height"), Catch::Matchers::WithinAbs(0.28, 1e-6));
    CHECK_FALSE(contains_key(pub.skipped_keys, "layer_height"));
    CHECK_FALSE(contains_key(pub.skipped_keys, "filament_ids"));
    CHECK_FALSE(contains_key(pub.skipped_keys, "inherits"));
    CHECK_FALSE(contains_key(pub.skipped_keys, "printer_settings_id"));
    CHECK(bundle.filaments.get_edited_preset().config.opt<ConfigOptionStrings>("filament_settings_id")->values == ids_before);
    CHECK(contains_key(pub.skipped_keys, "not_a_setting"));
}

TEST_CASE("Published 3MF skips a whole-vector key whose size does not match the receiver", "[Preset][Bundle][Published]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000" };
    config.opt_float("layer_height") = 0.28;
    config.set_key_value("wiping_volumes_extruders", new ConfigOptionFloats({ 10., 20., 30. }));
    Preset::normalize(config);

    PresetBundle bundle;
    bundle.prints.get_edited_preset().config.set_key_value("wiping_volumes_extruders", new ConfigOptionFloats({ 40., 50. }));
    bundle.prints.get_edited_preset().config.opt_float("layer_height") = 0.1;

    PublishedConfig pub;
    pub.published      = true;
    pub.published_keys = { "wiping_volumes_extruders", "layer_height" };
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    check_double_vector(bundle.prints.get_edited_preset().config.opt<ConfigOptionFloats>("wiping_volumes_extruders")->values,
                        { 40., 50. });
    CHECK(contains_key(pub.skipped_keys, "wiping_volumes_extruders"));
    CHECK_FALSE(contains_key(pub.skipped_keys, "layer_height"));
    CHECK_THAT(bundle.prints.get_edited_preset().config.opt_float("layer_height"), Catch::Matchers::WithinAbs(0.28, 1e-6));
}

TEST_CASE("Published 3MF uniquifies a second imported full material as (Published 2)", "[Preset][Bundle][Published]")
{
    PresetBundle bundle;
    Preset &petg = add_inmemory_preset(bundle.filaments, "My PETG");
    petg.config.opt_string("filament_type", 0u) = "PETG";
    petg.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.6 };
    Preset &bare = add_inmemory_preset(bundle.filaments, "Generic PLA");
    bare.config.opt_string("filament_type", 0u) = "PLA";
    bare.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
    Preset &pub1 = add_inmemory_preset(bundle.filaments, "Generic PLA (Published)");
    pub1.config.opt_string("filament_type", 0u) = "PLA";
    pub1.config.opt<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = { 0.5 };
    bundle.filament_presets = { "My PETG" };

    PublishedMaterialEntry entry;
    entry.slot               = 0;
    entry.full               = true;
    entry.publish_type       = true;
    entry.publish_type_value = "PLA";
    entry.preset_name        = "Generic PLA @Qidi Q2 0.4 nozzle";
    entry.full_keys          = { "filament_retraction_length" };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { entry };
    DynamicPrintConfig config = published_pla_file_config();
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    CHECK(bundle.filament_presets[0] == "Generic PLA (Published 2)");
    check_double_vector(bundle.filaments.find_preset("Generic PLA (Published 2)", false, true)
                            ->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values,
                        { 0.9 });
    check_double_vector(bundle.filaments.find_preset("Generic PLA", false, true)
                            ->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values,
                        { 0.5 });
    check_double_vector(bundle.filaments.find_preset("Generic PLA (Published)", false, true)
                            ->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values,
                        { 0.5 });
    REQUIRE(pub.material_replacements.size() == 1);
    CHECK(pub.material_replacements[0] == "slot 0: My PETG -> Generic PLA (Published 2)");
    CHECK(pub.skipped_keys.empty());
}

TEST_CASE("Published 3MF never writes a mixed filament's blended colour into the slot's preset", "[Preset][Bundle][Published]")
{
    PresetBundle bundle;
    Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
    pla.config.opt_string("filament_type", 0u) = "PLA";
    pla.config.opt<ConfigOptionStrings>("filament_colour", true)->values = { "#123456" };
    Preset &petg = add_inmemory_preset(bundle.filaments, "My PETG");
    petg.config.opt_string("filament_type", 0u) = "PETG";
    bundle.filament_presets = { "My PLA", "My PLA", "My PLA" };
    bundle.set_num_filaments(3);
    bundle.project_config.opt<ConfigOptionBools>("filament_is_mixed")->values[2]                = 1;
    bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values[2]      = "1,1";
    bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values[2] = "0.5,0.5";

    PublishedMaterialEntry mix;
    mix.filament_type   = "PLA";
    mix.filament_vendor = "Generic";
    mix.filament_id     = "GFL99";
    mix.slot            = 2;
    mix.publish_color   = true;
    mix.color           = "#800080";
    mix.keys            = { "filament_is_mixed",       "filament_mixed_components",
                            "filament_mixed_sublayer_ratios", "filament_mixed_gradient",
                            "filament_mixed_gradient_range",  "filament_mixed_gradient_curve",
                            "filament_mixed_gradient_per_part" };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { mix };
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75, 1.75 };
    config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 2, 3 };
    config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = {
        "Direct Drive Standard", "Direct Drive Standard", "Direct Drive Standard"
    };
    config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#0000FF", "#800080" };
    config.opt<ConfigOptionStrings>("filament_type")->values = { "PLA", "PETG", "PLA" };
    config.opt<ConfigOptionStrings>("filament_vendor")->values = { "Generic", "Generic", "Generic" };
    config.opt<ConfigOptionBools>("filament_is_mixed")->values = { 0, 0, 1 };
    config.opt<ConfigOptionStrings>("filament_mixed_components")->values = { "", "", "1,2" };
    config.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values = { "", "", "0.6,0.4" };
    config.opt<ConfigOptionBools>("filament_mixed_gradient")->values = { 0, 0, 1 };
    config.opt<ConfigOptionStrings>("filament_mixed_gradient_range")->values = { "", "", "0.9,0.1" };
    config.opt<ConfigOptionStrings>("filament_mixed_gradient_curve")->values = { "", "", "0,0.1|1,0.9" };
    config.opt<ConfigOptionBools>("filament_mixed_gradient_per_part")->values = { 0, 0, 1 };
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    CHECK(bundle.filament_presets.size() == 3);
    CHECK(bundle.is_mixed_filament(2));
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_mixed_components")->values[2] == "1,2");
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values[2] == "#800080");
    CHECK(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionStrings>("filament_colour")->values ==
          std::vector<std::string>{ "#123456" });
    CHECK(bundle.filament_presets[0] == "My PLA");
    CHECK(bundle.filament_presets[1] == "My PLA");
    CHECK(pub.skipped_keys.empty());
    CHECK(pub.material_replacements.empty());
}

TEST_CASE("Published 3MF applies duplicate entries for one slot last-wins", "[Preset][Bundle][Published]")
{
    PresetBundle bundle;
    Preset &pla = add_inmemory_preset(bundle.filaments, "My PLA");
    pla.config.opt_string("filament_type", 0u) = "PLA";
    pla.config.opt<ConfigOptionStrings>("filament_colour", true)->values = { "#000000" };
    bundle.filament_presets = { "My PLA" };

    auto make_entry = [](const char *color) {
        PublishedMaterialEntry entry;
        entry.slot          = 0;
        entry.publish_color = true;
        entry.color         = color;
        entry.keys          = { "filament_retraction_length" };
        return entry;
    };

    PublishedConfig pub;
    pub.published     = true;
    pub.material_keys = { make_entry("#AA0000"), make_entry("#BB0000") };
    DynamicPrintConfig config = published_pla_file_config();
    Preset::normalize(config);
    bundle.load_config_model("test.3mf", std::move(config), Semver(), &pub);

    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values[0] == "#BB0000");
    CHECK(bundle.filaments.find_preset("My PLA", false, true)->config.opt<ConfigOptionStrings>("filament_colour")->values ==
          std::vector<std::string>{ "#BB0000" });
    check_double_vector(bundle.filaments.find_preset("My PLA", false, true)
                            ->config.opt<ConfigOptionFloatsNullable>("filament_retraction_length")->values,
                        { 0.9 });
    CHECK(pub.skipped_keys.empty());
    CHECK(pub.material_replacements.empty());
}

TEST_CASE("normalize_filament_type strips a space modifier but keeps dash types", "[Preset][Bundle][Published]")
{
    CHECK(normalize_filament_type("PLA High Speed") == "PLA");
    CHECK(normalize_filament_type("PA-CF") == "PA-CF");
    CHECK(normalize_filament_type("PETG-CF") == "PETG-CF");
    CHECK(normalize_filament_type("PLA") == "PLA");
    CHECK(normalize_filament_type("ABC") == "ABC");
    CHECK(normalize_filament_type("") == "");
}

TEST_CASE("collect_dirty_settings_keys unions the dirty settings from all three presets", "[Preset][Bundle][Published]")
{
    PresetBundle bundle;
    bundle.prints.get_edited_preset().config.opt_float("layer_height") = 0.28;
    bundle.filaments.get_edited_preset().config.opt<ConfigOptionStrings>("filament_type", true)->values = { "ABS" };
    bundle.printers.get_edited_preset().config.opt<ConfigOptionFloats>("nozzle_diameter", true)->values = { 0.6 };

    const std::vector<std::string> dirty = collect_dirty_settings_keys(bundle);
    for (const char *key : { "layer_height", "filament_type#0", "nozzle_diameter#0" })
        CHECK(contains_key(dirty, key));
}

TEST_CASE("Published 3MF denylist and mixed-key sets match the import/export contract", "[Preset][Bundle][Published]")
{
    const std::set<std::string>& structural = publish_structural_keys();
    for (const char *key : { "printer_settings_id", "filament_settings_id", "print_settings_id",
                             "compatible_printers", "compatible_prints", "compatible_printers_condition",
                             "compatible_prints_condition", "default_filament_profile", "default_print_profile",
                             "inherits", "extruder_count", "printer_model", "filament_ids" })
        CHECK(structural.count(key) == 1);
    CHECK(structural.count("filament_retraction_length") == 0);
    CHECK(structural.count("filament_colour") == 0);

    const std::set<std::string>& mixed = publish_mixed_keys();
    CHECK(mixed == std::set<std::string>{
        "filament_is_mixed", "filament_mixed_components", "filament_mixed_sublayer_ratios",
        "filament_mixed_gradient", "filament_mixed_gradient_range", "filament_mixed_gradient_curve",
        "filament_mixed_gradient_per_part" });
    for (const std::string &key : mixed)
        CHECK(structural.count(key) == 0);
}

namespace {

struct ScopedDataDir
{
    std::string previous = data_dir();
    explicit ScopedDataDir(const fs::path &dir) { set_data_dir(dir.string()); }
    ~ScopedDataDir() { set_data_dir(previous); }
};

struct ScopedResourcesDir
{
    std::string previous = resources_dir();
    explicit ScopedResourcesDir(const fs::path &dir) { set_resources_dir(dir.string()); }
    ~ScopedResourcesDir() { set_resources_dir(previous); }
};

void write_acme_printer_vendor(const fs::path &root, double dist_to_rod)
{
    const fs::path machine_dir = root / "Acme" / "machine";
    fs::create_directories(machine_dir / "nested");
    std::ofstream((root / "Acme.json").string())
        << R"({"version":"1.0.0","name":"Acme",)"
        << R"("machine_model_list":[{"name":"Acme One","sub_path":"machine/model.json"}],"machine_list":[)"
        << R"({"name":"fdm_acme_common","sub_path":"machine/base.json"},)"
        << R"({"name":"Acme Printer","sub_path":"machine/nested/printer.json"}]})";
    std::ofstream((machine_dir / "model.json").string())
        << R"({"type":"machine_model","name":"Acme One","nozzle_diameter":"0.4"})";
    std::ofstream((machine_dir / "base.json").string())
        << R"({"type":"machine","name":"fdm_acme_common","from":"system","instantiation":"false",)"
        << R"("extruder_clearance_dist_to_rod":")" << dist_to_rod << R"("})";
    std::ofstream((machine_dir / "nested" / "printer.json").string())
        << R"({"type":"machine","name":"Acme Printer","from":"system","instantiation":"true","inherits":"fdm_acme_common",)"
        << R"("printer_model":"Acme One","printer_variant":"0.4"})";
}

std::string read_file(const fs::path &file)
{
    std::ifstream in(file.string(), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_zip(const fs::path &zip_file, const std::vector<std::pair<std::string, std::string>> &entries)
{
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    REQUIRE(open_zip_writer(&zip, zip_file.string()));
    for (const auto &[name, content] : entries)
        REQUIRE(mz_zip_writer_add_mem(&zip, name.c_str(), content.data(), content.size(), MZ_DEFAULT_COMPRESSION));
    REQUIRE(mz_zip_writer_finalize_archive(&zip));
    REQUIRE(close_zip_writer(&zip));
}

bool any_filename_contains(const fs::path &root, const std::string &needle)
{
    for (fs::recursive_directory_iterator it(root), end; it != end; ++it)
        if (it->path().filename().string().find(needle) != std::string::npos)
            return true;
    return false;
}

}

TEST_CASE("Config import confines zip entries, preset names and bundle ids to the preset directory", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir temp_dir;
    const fs::path     data_root = temp_dir.path() / "datadir";
    const fs::path     src_dir   = temp_dir.path() / "src";
    fs::create_directories(src_dir);
    ScopedDataDir scoped_data_dir(data_root);

    PresetBundle bundle;
    AppConfig    app_config;
    const auto   confirm = [](std::string const &) { return 1; };
    const auto   import  = [&](const fs::path &file) {
        std::vector<std::string> files{file.string()};
        bundle.import_presets(files, confirm, ForwardCompatibilitySubstitutionRule::Disable, app_config);
        return files;
    };

    const fs::path good_file = src_dir / "Good.json";
    write_print_preset(bundle.prints.default_preset().config, good_file, "Good");
    const std::string good_json = read_file(good_file);

    const std::string up     = "../../../../";
    const std::string up_win = "..\\..\\..\\..\\";

    SECTION("zip entry names with either separator are reduced to a basename") {
        const fs::path zip = src_dir / "bundle.zip";
        write_zip(zip, {{up + "zip-escape.json", "{}"}, {up_win + "zip-escape.json", "{}"}, {"presets/Good.json", good_json}});
        import(zip);
        CHECK(bundle.prints.find_preset("Good") != nullptr);
        CHECK_FALSE(any_filename_contains(temp_dir.path(), "zip-escape"));
    }

    SECTION("a preset name that walks out of the preset directory is rejected") {
        for (const std::string &name : {up + "name-escape", up_win + "name-escape"}) {
            const fs::path file = src_dir / "escape.json";
            write_print_preset(bundle.prints.default_preset().config, file, name);
            CHECK(import(file).empty());
            CHECK_FALSE(any_filename_contains(temp_dir.path(), "name-escape"));
        }
    }

    SECTION("a bundle id that walks out of the bundle directory is rejected") {
        const fs::path zip = src_dir / "bundle.zip";
        write_zip(zip, {{BUNDLE_STRUCTURE_JSON_NAME, "{\"id\": \"" + up + "bundle-escape\"}"}, {"Good.json", good_json}});
        CHECK(import(zip).empty());
        CHECK_FALSE(any_filename_contains(temp_dir.path(), "bundle-escape"));
    }
}

TEST_CASE("A saved printer preset reloads the tool layout it was saved with", "[Preset][Bundle][IMEX]")
{
    ScopedTemporaryDir         temp_dir;
    PresetBundle               bundle;
    PresetsConfigSubstitutions substitutions;

    DynamicPrintConfig config(bundle.printers.default_preset().config);
    config.set_deserialize_strict("imex_tool_layout", "rear-left");
    config.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "";
    const fs::path file = temp_dir.path() / PRESET_PRINTER_NAME / "ImexPrinter.json";
    fs::create_directories(file.parent_path());
    config.save_to_json(file.string(), "ImexPrinter", "User", "1.0.0");

    bundle.printers.load_presets(temp_dir.path().string(), PRESET_PRINTER_NAME, substitutions,
                                 ForwardCompatibilitySubstitutionRule::Disable);

    const Preset *loaded = bundle.printers.find_preset("ImexPrinter");
    REQUIRE(loaded != nullptr);
    CHECK(int(imex_cfg_enum<ImexToolLayout>(loaded->config, "imex_tool_layout")) == int(ImexToolLayout::RearLeft));
}

TEST_CASE("A project saved with pressure advance per filament applies it to every variant of the filament", "[Preset][Bundle]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionStrings>("filament_colour")->values = { "#FF0000", "#00FF00" };
    config.opt<ConfigOptionFloats>("filament_diameter")->values = { 1.75, 1.75 };
    config.option<ConfigOptionStrings>("filament_settings_id", true)->values = { "Project PLA", "Project PETG" };
    config.option<ConfigOptionStrings>("extruder_variant_list", true)->values = { "Direct Drive Standard,Direct Drive High Flow" };
    config.opt<ConfigOptionStrings>("filament_extruder_variant")->values = { "Direct Drive Standard", "Direct Drive High Flow", "Direct Drive Standard" };
    config.opt<ConfigOptionInts>("filament_self_index")->values = { 1, 1, 2 };
    config.opt<ConfigOptionFloatsNullable>("filament_flow_ratio")->values = { 0.95, 0.96, 0.97 };
    config.load_from_ini_string("pressure_advance = 0.021,0.043", ForwardCompatibilitySubstitutionRule::Disable);
    check_double_vector(config.opt<ConfigOptionFloats>("pressure_advance")->values, { 0.021, 0.021, 0.043 });
    check_double_vector(config.opt<ConfigOptionFloatsNullable>("filament_flow_ratio")->values, { 0.95, 0.96, 0.97 });
    Preset::normalize(config);

    PresetBundle bundle;
    bundle.load_config_model("test.3mf", std::move(config));

    REQUIRE(bundle.filament_presets.size() == 2);
    const DynamicPrintConfig &pla  = bundle.filaments.find_preset(bundle.filament_presets[0], false, true)->config;
    const DynamicPrintConfig &petg = bundle.filaments.find_preset(bundle.filament_presets[1], false, true)->config;
    check_double_vector(pla.opt<ConfigOptionFloats>("pressure_advance")->values, { 0.021, 0.021 });
    check_double_vector(petg.opt<ConfigOptionFloats>("pressure_advance")->values, { 0.043 });
    check_double_vector(pla.opt<ConfigOptionFloatsNullable>("filament_flow_ratio")->values, { 0.95, 0.96 });
    check_double_vector(petg.opt<ConfigOptionFloatsNullable>("filament_flow_ratio")->values, { 0.97 });
}

TEST_CASE("A multi-toolhead project saved without filament self indices loads every filament", "[Preset][Bundle]")
{
    const std::vector<std::string> colors = { "#FF0000", "#000000", "#FFFFFF", "#FFFF00" };
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionStrings>("filament_colour")->values = colors;
    config.opt<ConfigOptionFloats>("nozzle_diameter")->values = std::vector<double>(colors.size(), 0.4);
    config.option<ConfigOptionBool>("single_extruder_multi_material")->value = false;
    Preset::normalize(config);

    PresetBundle bundle;
    REQUIRE_NOTHROW(bundle.load_config_model("test.3mf", std::move(config)));
    CHECK(bundle.filament_presets.size() == colors.size());
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values == colors);
}

TEST_CASE("A system preset resolves by name from the bundled profiles", "[Preset][Bundle]")
{
    ScopedTemporaryDir temp_dir;
    ScopedDataDir      data(temp_dir.path() / "data");
    ScopedResourcesDir resources(temp_dir.path() / "resources");
    write_acme_printer_vendor(temp_dir.path() / "resources" / "profiles", 33.);

    PresetBundle       bundle;
    DynamicPrintConfig config;
    std::string        error;
    REQUIRE(bundle.resolve_system_preset(config, Preset::TYPE_PRINTER, "Acme Printer",
                                         ForwardCompatibilitySubstitutionRule::EnableSilent, error));
    CHECK(error.empty());
    CHECK_THAT(config.opt_float("extruder_clearance_dist_to_rod"), Catch::Matchers::WithinAbs(33., 1e-6));
}

TEST_CASE("A system preset resolves from the data directory copy of its vendor", "[Preset][Bundle]")
{
    ScopedTemporaryDir temp_dir;
    ScopedDataDir      data(temp_dir.path() / "data");
    ScopedResourcesDir resources(temp_dir.path() / "resources");
    write_acme_printer_vendor(temp_dir.path() / "resources" / "profiles", 33.);
    write_acme_printer_vendor(temp_dir.path() / "data" / PRESET_SYSTEM_DIR, 35.);

    PresetBundle       bundle;
    DynamicPrintConfig config;
    std::string        error;
    REQUIRE(bundle.resolve_system_preset(config, Preset::TYPE_PRINTER, "Acme Printer",
                                         ForwardCompatibilitySubstitutionRule::EnableSilent, error));
    CHECK_THAT(config.opt_float("extruder_clearance_dist_to_rod"), Catch::Matchers::WithinAbs(35., 1e-6));
}

TEST_CASE("A system preset resolves from a vendor shipped as its preset cache alone", "[Preset][Bundle]")
{
    ScopedTemporaryDir temp_dir;
    ScopedDataDir      data(temp_dir.path() / "data");
    ScopedResourcesDir resources(temp_dir.path() / "resources");
    const fs::path     profiles = temp_dir.path() / "resources" / "profiles";
    write_acme_printer_vendor(profiles, 33.);

    PresetBundle writer;
    writer.set_generate_vendor_caches(true);
    writer.load_vendor_configs_from_json(profiles.string(), "Acme", PresetBundle::LoadSystem,
                                         ForwardCompatibilitySubstitutionRule::EnableSilent);
    REQUIRE(fs::exists(profiles / "Acme.opc"));
    fs::remove(profiles / "Acme.json");
    fs::remove_all(profiles / "Acme");

    PresetBundle       bundle;
    DynamicPrintConfig config;
    std::string        error;
    REQUIRE(bundle.resolve_system_preset(config, Preset::TYPE_PRINTER, "Acme Printer",
                                         ForwardCompatibilitySubstitutionRule::EnableSilent, error));
    CHECK_THAT(config.opt_float("extruder_clearance_dist_to_rod"), Catch::Matchers::WithinAbs(33., 1e-6));
}

TEST_CASE("A system preset no vendor lists is not resolved", "[Preset][Bundle]")
{
    ScopedTemporaryDir temp_dir;
    ScopedDataDir      data(temp_dir.path() / "data");
    ScopedResourcesDir resources(temp_dir.path() / "resources");
    write_acme_printer_vendor(temp_dir.path() / "resources" / "profiles", 33.);

    PresetBundle       bundle;
    DynamicPrintConfig config;
    std::string        error;
    CHECK_FALSE(bundle.resolve_system_preset(config, Preset::TYPE_PRINTER, "Unknown Printer",
                                             ForwardCompatibilitySubstitutionRule::EnableSilent, error));
    CHECK_FALSE(error.empty());
}

namespace {

void reduce_vendors_to_caches(const fs::path &dir, const std::vector<std::string> &vendor_ids)
{
    const std::string lib(PresetBundle::ORCA_FILAMENT_LIBRARY);
    PresetBundle      library;
    if (fs::exists(dir / (lib + ".json"))) {
        library.set_generate_vendor_caches(true);
        library.load_vendor_configs_from_json(dir.string(), lib, PresetBundle::LoadSystem,
                                              ForwardCompatibilitySubstitutionRule::EnableSilent);
    }
    for (const std::string &vendor_id : vendor_ids) {
        if (vendor_id == lib)
            continue;
        PresetBundle writer;
        writer.set_generate_vendor_caches(true);
        writer.load_vendor_configs_from_json(dir.string(), vendor_id, PresetBundle::LoadSystem,
                                             ForwardCompatibilitySubstitutionRule::EnableSilent, &library);
    }
    for (const std::string &vendor_id : vendor_ids) {
        REQUIRE(fs::exists(dir / (vendor_id + ".opc")));
        fs::remove(dir / (vendor_id + ".json"));
        fs::remove_all(dir / vendor_id);
    }
}

void write_library_and_acme_filament(const fs::path &root)
{
    const std::string lib(PresetBundle::ORCA_FILAMENT_LIBRARY);
    fs::create_directories(root / lib / "filament");
    std::ofstream((root / (lib + ".json")).string())
        << R"({"version":"1.0.0","name":")" << lib << R"(",)"
        << R"("filament_list":[{"name":"Generic PLA","sub_path":"filament/generic_pla.json"}]})";
    std::ofstream((root / lib / "filament" / "generic_pla.json").string())
        << R"({"type":"filament","name":"Generic PLA","from":"system","instantiation":"false","filament_id":"GFL99","filament_cost":"27"})";
    fs::create_directories(root / "Acme" / "filament");
    std::ofstream((root / "Acme.json").string())
        << R"({"version":"1.0.0","name":"Acme","filament_list":[{"name":"Acme PLA","sub_path":"filament/pla.json"}]})";
    std::ofstream((root / "Acme" / "filament" / "pla.json").string())
        << R"({"type":"filament","name":"Acme PLA","from":"system","instantiation":"true","inherits":"Generic PLA"})";
}

}

TEST_CASE("A read-only load resolves a user preset against vendors installed as their cache alone", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir temp_dir;
    const fs::path     data   = temp_dir.path() / "data";
    const fs::path     system = data / PRESET_SYSTEM_DIR;
    ScopedDataDir      scoped_data(data);
    ScopedResourcesDir scoped_resources(temp_dir.path() / "resources");
    write_acme_printer_vendor(system, 33.);
    reduce_vendors_to_caches(system, {"Acme"});

    fs::create_directories(data / PRESET_USER_DIR / DEFAULT_USER_FOLDER_NAME / PRESET_PRINTER_NAME);
    std::ofstream((data / PRESET_USER_DIR / DEFAULT_USER_FOLDER_NAME / PRESET_PRINTER_NAME / "My Acme.json").string())
        << R"({"type":"machine","name":"My Acme","from":"User","version":"2.3.0.0","inherits":"Acme Printer","printable_height":"123"})";

    AppConfig    app_config;
    PresetBundle bundle;
    std::string  errors;
    bundle.load_presets(app_config, ForwardCompatibilitySubstitutionRule::EnableSilent, PresetBundle::PresetPreferences(),
                        &errors, true);
    CHECK(errors.empty());
    const Preset *preset = bundle.printers.find_preset("My Acme");
    REQUIRE(preset != nullptr);
    CHECK_THAT(preset->config.opt_float("printable_height"), Catch::Matchers::WithinAbs(123., 1e-6));
    CHECK_THAT(preset->config.opt_float("extruder_clearance_dist_to_rod"), Catch::Matchers::WithinAbs(33., 1e-6));
}

TEST_CASE("A read-only load writes no preset cache", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir temp_dir;
    const fs::path     system = temp_dir.path() / "data" / PRESET_SYSTEM_DIR;
    ScopedDataDir      scoped_data(temp_dir.path() / "data");
    ScopedResourcesDir scoped_resources(temp_dir.path() / "resources");
    write_acme_printer_vendor(system, 33.);

    AppConfig    app_config;
    PresetBundle bundle;
    std::string  errors;
    bundle.load_presets(app_config, ForwardCompatibilitySubstitutionRule::EnableSilent, PresetBundle::PresetPreferences(),
                        &errors, true);
    CHECK(errors.empty());
    CHECK(bundle.printers.find_preset("Acme Printer") != nullptr);
    CHECK_FALSE(fs::exists(system / "Acme.opc"));
}

TEST_CASE("A vendor updated over the air resolves against the library installed as its cache alone", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir temp_dir;
    const fs::path     system = temp_dir.path() / "data" / PRESET_SYSTEM_DIR;
    ScopedDataDir      scoped_data(temp_dir.path() / "data");
    ScopedResourcesDir scoped_resources(temp_dir.path() / "resources");
    write_library_and_acme_filament(temp_dir.path() / "resources" / PRESET_PROFILES_DIR);
    write_library_and_acme_filament(system);
    reduce_vendors_to_caches(system, {PresetBundle::ORCA_FILAMENT_LIBRARY, "Acme"});
    write_library_and_acme_filament(temp_dir.path() / "update");
    fs::copy_file(temp_dir.path() / "update" / "Acme.json", system / "Acme.json");
    fs::create_directories(system / "Acme" / "filament");
    fs::copy_file(temp_dir.path() / "update" / "Acme" / "filament" / "pla.json", system / "Acme" / "filament" / "pla.json");

    SECTION("by name") {
        PresetBundle       bundle;
        DynamicPrintConfig config;
        std::string        error;
        REQUIRE(bundle.resolve_system_preset(config, Preset::TYPE_FILAMENT, "Acme PLA",
                                             ForwardCompatibilitySubstitutionRule::EnableSilent, error));
        CHECK_THAT(config.opt<ConfigOptionFloats>("filament_cost")->values.front(), Catch::Matchers::WithinAbs(27., 1e-6));
    }
    SECTION("by its source file") {
        PresetBundle       bundle;
        DynamicPrintConfig config;
        config.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = "Generic PLA";
        std::string error;
        REQUIRE(bundle.resolve_preset_config(config, Preset::TYPE_FILAMENT, (system / "Acme" / "filament" / "pla.json").string(),
                                             ForwardCompatibilitySubstitutionRule::EnableSilent, error));
        CHECK_THAT(config.opt<ConfigOptionFloats>("filament_cost")->values.front(), Catch::Matchers::WithinAbs(27., 1e-6));
    }
}

TEST_CASE("The vendor and filament scans read the bundled vendors that ship as their cache alone", "[Preset][Bundle][Regression]")
{
    ScopedTemporaryDir temp_dir;
    const fs::path     profiles = temp_dir.path() / "resources" / PRESET_PROFILES_DIR;
    ScopedResourcesDir scoped_resources(temp_dir.path() / "resources");
    const std::string  lib(PresetBundle::ORCA_FILAMENT_LIBRARY);
    fs::create_directories(profiles / lib / "filament");
    std::ofstream((profiles / (lib + ".json")).string())
        << R"({"version":"1.0.0","name":")" << lib << R"(",)"
        << R"("filament_list":[{"name":"Generic PLA","sub_path":"filament/generic_pla.json"}]})";
    std::ofstream((profiles / lib / "filament" / "generic_pla.json").string())
        << R"({"type":"filament","name":"Generic PLA","from":"system","instantiation":"false","filament_id":"GFL99"})";
    fs::create_directories(profiles / "Acme" / "machine");
    fs::create_directories(profiles / "Acme" / "filament");
    std::ofstream((profiles / "Acme.json").string())
        << R"({"version":"1.0.0","name":"Acme",)"
        << R"("machine_model_list":[{"name":"Acme One","sub_path":"machine/model.json"}],)"
        << R"("machine_list":[{"name":"Acme Printer","sub_path":"machine/printer.json"}],)"
        << R"("filament_list":[{"name":"Acme PLA","sub_path":"filament/pla.json"}]})";
    std::ofstream((profiles / "Acme" / "machine" / "model.json").string())
        << R"({"type":"machine_model","name":"Acme One","nozzle_diameter":"0.4"})";
    std::ofstream((profiles / "Acme" / "machine" / "printer.json").string())
        << R"({"type":"machine","name":"Acme Printer","from":"system","instantiation":"true","printer_model":"Acme One","printer_variant":"0.4"})";
    std::ofstream((profiles / "Acme" / "filament" / "pla.json").string())
        << R"({"type":"filament","name":"Acme PLA","from":"system","instantiation":"true","inherits":"Generic PLA","filament_id":"P0000001"})";

    auto installed = [](const PresetCollection &presets) {
        return std::count_if(presets.get_presets().begin(), presets.get_presets().end(), [](const Preset &preset) { return !preset.is_default; });
    };
    auto scan = [&installed] {
        std::vector<std::string> found;
        PresetBundle             models;
        models.load_system_models_from_json(ForwardCompatibilitySubstitutionRule::EnableSilent);
        CHECK(installed(models.printers) + installed(models.prints) + installed(models.filaments) == 0);
        for (const auto &[vendor_id, vendor] : models.vendors)
            for (const VendorProfile::PrinterModel &model : vendor.models)
                found.push_back(vendor_id + " model " + model.id);
        PresetBundle filaments;
        filaments.load_system_filaments_json(ForwardCompatibilitySubstitutionRule::EnableSilent);
        CHECK(installed(filaments.printers) + installed(filaments.prints) == 0);
        for (const Preset &preset : filaments.filaments.get_presets())
            if (!preset.is_default)
                found.push_back(preset.name + " " + preset.filament_id);
        return found;
    };
    const std::vector<std::string> expected{"Acme model Acme One", "Acme PLA P0000001"};
    REQUIRE(scan() == expected);
    reduce_vendors_to_caches(profiles, {lib, "Acme"});
    CHECK(scan() == expected);
}

namespace {

DynamicPrintConfig external_default_config(Preset::Type type)
{
    DynamicPrintConfig config;
    config.apply_only(static_cast<const PrintRegionConfig &>(FullPrintConfig::defaults()),
                      type == Preset::TYPE_PRINTER ? Preset::printer_options() : Preset::print_options());
    Preset::inherits(config);
    return config;
}

auto base_finder(DynamicPrintConfig *base, int &calls)
{
    return [base, &calls](const std::string &) {
        ++calls;
        return base;
    };
}

}

TEST_CASE("A key missing from a project takes the default preset value when there is no base", "[Preset][ExternalPreset]")
{
    DynamicPrintConfig defaults = external_default_config(Preset::TYPE_PRINT);
    defaults.set("wall_loops", 7, true);
    const DynamicPrintConfig project;
    int calls = 0;

    const DynamicPrintConfig config = Preset::load_external_config(Preset::TYPE_PRINT, defaults, project, {"sparse_infill_density"},
                                                                   base_finder(nullptr, calls));
    CHECK(calls == 1);
    CHECK(config.opt_int("wall_loops") == 7);
}

TEST_CASE("A key missing from a project takes its base preset value", "[Preset][ExternalPreset]")
{
    DynamicPrintConfig defaults = external_default_config(Preset::TYPE_PRINT);
    defaults.set("wall_loops", 7, true);
    DynamicPrintConfig base = defaults;
    base.set("wall_loops", 5, true);
    const DynamicPrintConfig project;
    int calls = 0;

    const DynamicPrintConfig config = Preset::load_external_config(Preset::TYPE_PRINT, defaults, project, {"sparse_infill_density"},
                                                                   base_finder(&base, calls));
    CHECK(config.opt_int("wall_loops") == 5);
}

TEST_CASE("A project key listed as different keeps the project value", "[Preset][ExternalPreset]")
{
    const DynamicPrintConfig defaults = external_default_config(Preset::TYPE_PRINT);
    DynamicPrintConfig       base     = defaults;
    base.set("wall_loops", 5, true);
    DynamicPrintConfig project;
    project.set("wall_loops", 4, true);
    int calls = 0;

    const DynamicPrintConfig config = Preset::load_external_config(Preset::TYPE_PRINT, defaults, project, {"wall_loops"},
                                                                   base_finder(&base, calls));
    CHECK(config.opt_int("wall_loops") == 4);
}

TEST_CASE("A project key not listed as different is refreshed to the base value", "[Preset][ExternalPreset]")
{
    const DynamicPrintConfig defaults = external_default_config(Preset::TYPE_PRINT);
    DynamicPrintConfig       base     = defaults;
    base.set("wall_loops", 5, true);
    DynamicPrintConfig project;
    project.set("wall_loops", 4, true);
    project.set("inherits", "Base Process", true);
    std::string inherits;

    const DynamicPrintConfig config = Preset::load_external_config(Preset::TYPE_PRINT, defaults, project, {"sparse_infill_density"},
                                                                   [&base, &inherits](const std::string &name) {
                                                                       inherits = name;
                                                                       return &base;
                                                                   });
    CHECK(inherits == "Base Process");
    CHECK(config.opt_int("wall_loops") == 5);
}

TEST_CASE("An empty different settings list keeps the project values without a base", "[Preset][ExternalPreset]")
{
    DynamicPrintConfig defaults = external_default_config(Preset::TYPE_PRINT);
    defaults.set("top_shell_layers", 7, true);
    DynamicPrintConfig base = defaults;
    base.set("wall_loops", 5, true);
    base.set("top_shell_layers", 9, true);
    DynamicPrintConfig project;
    project.set("wall_loops", 4, true);
    int calls = 0;

    const DynamicPrintConfig config = Preset::load_external_config(Preset::TYPE_PRINT, defaults, project, {}, base_finder(&base, calls));
    CHECK(calls == 0);
    CHECK(config.opt_int("wall_loops") == 4);
    CHECK(config.opt_int("top_shell_layers") == 7);
}

TEST_CASE("Print-host keys are never taken from a project", "[Preset][ExternalPreset]")
{
    DynamicPrintConfig defaults = external_default_config(Preset::TYPE_PRINTER);
    defaults.set("print_host", "", true);
    defaults.set("printhost_apikey", "", true);
    DynamicPrintConfig project;
    project.set("print_host", "http://project-host", true);
    project.set("printhost_apikey", "project-key", true);
    project.set("printer_notes", "project notes", true);
    t_config_option_keys keys;
    int calls = 0;

    const DynamicPrintConfig config = Preset::load_external_config(Preset::TYPE_PRINTER, defaults, project, {"printer_notes"},
                                                                   base_finder(nullptr, calls), &keys);
    CHECK(config.opt_string("print_host").empty());
    CHECK(config.opt_string("printhost_apikey").empty());
    CHECK(config.opt_string("printer_notes") == "project notes");
    CHECK_FALSE(contains_key(keys, "print_host"));
    CHECK_FALSE(contains_key(keys, "printhost_apikey"));
    CHECK(contains_key(keys, "printer_notes"));
}

TEST_CASE("A per-variant project value maps onto its base preset's variant layout", "[Preset][ExternalPreset]")
{
    const DynamicPrintConfig defaults = external_default_config(Preset::TYPE_PRINT);
    DynamicPrintConfig       base     = defaults;
    base.set_key_value("print_extruder_id", new ConfigOptionInts({1, 1}));
    base.set_key_value("print_extruder_variant", new ConfigOptionStrings({"Direct Drive Standard", "Direct Drive High Flow"}));
    base.set_key_value("outer_wall_speed", new ConfigOptionFloats({200., 300.}));
    base.set_key_value("inner_wall_speed", new ConfigOptionFloats({250., 350.}));
    DynamicPrintConfig project;
    project.set_key_value("print_extruder_id", new ConfigOptionInts({1}));
    project.set_key_value("print_extruder_variant", new ConfigOptionStrings({"Direct Drive Standard"}));
    project.set_key_value("outer_wall_speed", new ConfigOptionFloats({100.}));
    project.set_key_value("inner_wall_speed", new ConfigOptionFloats({150.}));
    int calls = 0;

    const DynamicPrintConfig config = Preset::load_external_config(Preset::TYPE_PRINT, defaults, project, {"outer_wall_speed"},
                                                                   base_finder(&base, calls));
    CHECK(config.option<ConfigOptionStrings>("print_extruder_variant")->values ==
          std::vector<std::string>{"Direct Drive Standard", "Direct Drive High Flow"});
    check_double_vector(config.option<ConfigOptionFloats>("outer_wall_speed")->values, {100., 100.});
    check_double_vector(config.option<ConfigOptionFloats>("inner_wall_speed")->values, {250., 350.});
}

TEST_CASE("A project's different settings always keep the preset bookkeeping keys", "[Preset][ExternalPreset]")
{
    const std::set<std::string> keys = PresetBundle::project_different_keys(escape_strings_cstyle({"wall_loops", "top_shell_layers"}));
    for (const char *key : {"wall_loops", "top_shell_layers", "inherits", "print_settings_id", "filament_settings_id", "printer_settings_id"})
        CHECK(keys.count(key) == 1);
    CHECK(PresetBundle::project_different_keys(std::string()).count("inherits") == 1);
}

TEST_CASE("A filament's variant index follows the extruder type and nozzle volume type", "[Preset]")
{
    DynamicPrintConfig filament;
    filament.option<ConfigOptionStrings>("filament_extruder_variant", true)->values = {"Direct Drive Standard", "Direct Drive High Flow",
                                                                                       "Bowden Standard"};
    DynamicPrintConfig printer;
    printer.option<ConfigOptionEnumsGeneric>("extruder_type", true)->values = {etDirectDrive, etBowden};

    CHECK(PresetBundle::get_filament_variant_index(filament, printer, 0, nvtStandard) == 0);
    CHECK(PresetBundle::get_filament_variant_index(filament, printer, 0, nvtHighFlow) == 1);
    CHECK(PresetBundle::get_filament_variant_index(filament, printer, 1, nvtStandard) == 2);
    CHECK(PresetBundle::get_filament_variant_index(filament, printer, 1, nvtHighFlow) == 0);
}

TEST_CASE("A user preset saved over by another instance while its directory loads is read again under the lock", "[Preset][Bundle][InstanceLock]")
{
    ScopedTemporaryDir   temp_dir;
    ScopedDataDir        data_dir_scope(temp_dir.path());
    RenameTestCollection coll;

    Preset &parent = add_inmemory_preset(coll, "Parent Process");
    parent.config.option<ConfigOptionFloat>("layer_height", true)->value = 0.24;
    parent.is_system = true;

    constexpr int  children = 8;
    static_assert(children <= int(resolve_batch_size));
    const fs::path dir      = temp_dir.path() / PRESET_PRINT_NAME;
    for (int i = 0; i < children; ++ i)
        write_minimal_child(dir / ("Child " + std::to_string(i) + ".json"), "Child " + std::to_string(i), "Parent Process");

    std::string first;
    auto save_the_others = [&](Preset &preset) {
        if (! first.empty())
            return;
        first = preset.name;
        for (int i = 0; i < children; ++ i) {
            const std::string name = "Child " + std::to_string(i);
            if (name != first)
                std::ofstream((dir / (name + ".json")).string())
                    << R"({"type":"process","name":")" << name
                    << R"(","from":"User","version":"1.0.0","inherits":"Parent Process","layer_height":"0.3"})";
        }
    };
    PresetsConfigSubstitutions substitutions;
    coll.load_presets(temp_dir.path().string(), PRESET_PRINT_NAME, substitutions,
                      ForwardCompatibilitySubstitutionRule::Disable, save_the_others);

    REQUIRE_FALSE(first.empty());
    CHECK(coll.error_count() == 0);
    for (int i = 0; i < children; ++ i) {
        const std::string name  = "Child " + std::to_string(i);
        const Preset     *child = coll.find_preset(name);
        REQUIRE(child != nullptr);
        CHECK_THAT(child->config.opt_float("layer_height"), Catch::Matchers::WithinAbs(name == first ? 0.24 : 0.3, 1e-9));
    }
}

TEST_CASE("A user preset removed by another instance while its directory loads is not installed", "[Preset][Bundle][InstanceLock]")
{
    ScopedTemporaryDir   temp_dir;
    ScopedDataDir        data_dir_scope(temp_dir.path());
    RenameTestCollection coll;

    Preset &parent = add_inmemory_preset(coll, "Parent Process");
    parent.is_system = true;

    constexpr int  children = 8;
    static_assert(children <= int(resolve_batch_size));
    const fs::path dir      = temp_dir.path() / PRESET_PRINT_NAME;
    for (int i = 0; i < children; ++ i)
        write_minimal_child(dir / ("Child " + std::to_string(i) + ".json"), "Child " + std::to_string(i), "Parent Process");

    std::string first;
    auto remove_the_others = [&](Preset &preset) {
        if (! first.empty())
            return;
        first = preset.name;
        for (int i = 0; i < children; ++ i)
            if (const std::string name = "Child " + std::to_string(i); name != first)
                fs::remove(dir / (name + ".json"));
    };
    PresetsConfigSubstitutions substitutions;
    coll.load_presets(temp_dir.path().string(), PRESET_PRINT_NAME, substitutions,
                      ForwardCompatibilitySubstitutionRule::Disable, remove_the_others);

    REQUIRE_FALSE(first.empty());
    CHECK(coll.error_count() == 0);
    CHECK(coll.size() == 3);
    CHECK(coll.find_preset(first) != nullptr);
}

TEST_CASE("Without the instance lock a user preset loads but its file is not written back", "[Preset][Bundle][InstanceLock]")
{
    ScopedTemporaryDir temp_dir;
    ScopedDataDir      data_dir_scope(temp_dir.path());
    fs::create_directories(fs::path(user_presets_lock_path()));
    PresetBundle       bundle;
    const fs::path     file = temp_dir.path() / PRESET_FILAMENT_NAME / "My PLA @Test Printer.json";
    write_preset_with_inherits(bundle.filaments.default_preset().config, file, "My PLA @Test Printer", std::string());
    const std::string before = read_file(file);

    PresetsConfigSubstitutions substitutions;
    bundle.filaments.load_presets(temp_dir.path().string(), PRESET_FILAMENT_NAME, substitutions,
                                  ForwardCompatibilitySubstitutionRule::EnableSilent);

    const Preset *preset = bundle.filaments.find_preset("My PLA @Test Printer");
    REQUIRE(preset != nullptr);
    CHECK(preset->config.option<ConfigOptionStrings>("compatible_printers")->values == std::vector<std::string>{ "Test Printer" });
    CHECK(read_file(file) == before);
}

#ifndef _WIN32
TEST_CASE("A user preset that cannot be read under the instance lock is counted and removed", "[Preset][Bundle][InstanceLock]")
{
    if (::geteuid() == 0)
        SKIP("file permissions do not stop root");
    ScopedTemporaryDir   temp_dir;
    ScopedDataDir        data_dir_scope(temp_dir.path());
    RenameTestCollection coll;

    Preset &parent = add_inmemory_preset(coll, "Parent Process");
    parent.is_system = true;
    const fs::path file = temp_dir.path() / PRESET_PRINT_NAME / "Unreadable.json";
    write_minimal_child(file, "Unreadable", "Parent Process");
    fs::permissions(file, fs::no_perms);

    PresetsConfigSubstitutions substitutions;
    coll.load_presets(temp_dir.path().string(), PRESET_PRINT_NAME, substitutions,
                      ForwardCompatibilitySubstitutionRule::Disable);

    CHECK(coll.error_count() == 1);
    CHECK(coll.find_preset("Unreadable") == nullptr);
    CHECK_FALSE(fs::exists(file));
}
#endif

#ifndef _WIN32
TEST_CASE("A user preset that is a symlink to itself is counted and removed while the rest still load", "[Preset][Bundle][InstanceLock]")
{
    ScopedTemporaryDir   temp_dir;
    ScopedDataDir        data_dir_scope(temp_dir.path());
    RenameTestCollection coll;

    Preset &parent = add_inmemory_preset(coll, "Parent Process");
    parent.is_system = true;
    const fs::path dir = temp_dir.path() / PRESET_PRINT_NAME;
    write_minimal_child(dir / "Good A.json", "Good A", "Parent Process");
    write_minimal_child(dir / "Good B.json", "Good B", "Parent Process");
    const fs::path loop = dir / "Loop.json";
    fs::create_symlink(loop.filename(), loop);

    PresetsConfigSubstitutions substitutions;
    REQUIRE_NOTHROW(coll.load_presets(temp_dir.path().string(), PRESET_PRINT_NAME, substitutions,
                                      ForwardCompatibilitySubstitutionRule::Disable));

    CHECK(coll.error_count() == 1);
    CHECK(coll.find_preset("Good A") != nullptr);
    CHECK(coll.find_preset("Good B") != nullptr);
    CHECK(coll.find_preset("Loop") == nullptr);
    boost::system::error_code ec;
    CHECK(fs::symlink_status(loop, ec).type() == fs::file_not_found);
}
#endif

#ifndef _WIN32
TEST_CASE("A user preset symlinked to a missing target is counted but the link is kept", "[Preset][Bundle][InstanceLock]")
{
    ScopedTemporaryDir   temp_dir;
    ScopedDataDir        data_dir_scope(temp_dir.path());
    RenameTestCollection coll;

    const fs::path dir  = temp_dir.path() / PRESET_PRINT_NAME;
    const fs::path link = dir / "Linked.json";
    fs::create_directories(dir);
    fs::create_symlink(temp_dir.path() / "unmounted" / "Linked.json", link);

    PresetsConfigSubstitutions substitutions;
    coll.load_presets(temp_dir.path().string(), PRESET_PRINT_NAME, substitutions,
                      ForwardCompatibilitySubstitutionRule::Disable);

    CHECK(coll.error_count() == 1);
    CHECK(coll.find_preset("Linked") == nullptr);
    boost::system::error_code ec;
    CHECK(fs::symlink_status(link, ec).type() == fs::symlink_file);
}
#endif

#ifndef _WIN32
TEST_CASE("A user filament whose derived compatible printer cannot be written back still loads with the failure counted", "[Preset][Bundle]")
{
    if (::geteuid() == 0)
        SKIP("a read-only directory does not stop root");
    ScopedTemporaryDir temp_dir;
    PresetBundle       bundle;
    const fs::path     dir  = temp_dir.path() / PRESET_FILAMENT_NAME;
    const fs::path     file = dir / "My PLA @Test Printer.json";
    write_preset_with_inherits(bundle.filaments.default_preset().config, file, "My PLA @Test Printer", std::string());
    const std::string before = read_file(file);

    fs::permissions(file, fs::owner_read);
    fs::permissions(dir, fs::owner_read | fs::owner_exe);
    PresetsConfigSubstitutions substitutions;
    bundle.filaments.load_presets(temp_dir.path().string(), PRESET_FILAMENT_NAME, substitutions,
                                  ForwardCompatibilitySubstitutionRule::EnableSilent);
    fs::permissions(dir, fs::owner_all);
    fs::permissions(file, fs::owner_read | fs::owner_write);

    const Preset *preset = bundle.filaments.find_preset("My PLA @Test Printer");
    REQUIRE(preset != nullptr);
    CHECK(preset->config.option<ConfigOptionStrings>("compatible_printers")->values == std::vector<std::string>{ "Test Printer" });
    CHECK(bundle.filaments.error_count() == 1);
    CHECK(read_file(file) == before);
}
#endif
