#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <catch2/catch_test_macros.hpp>

#include <catch2/catch_message.hpp>
#include "slic3r/GUI/ActionRegistry.hpp"
#include "slic3r/GUI/NativeCommands.hpp"
#include "slic3r/GUI/SettingsIndex.hpp"

#include <boost/filesystem.hpp>

#include "libslic3r/Config.hpp"
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

using Slic3r::GUI::AppAction;
using Slic3r::GUI::AppActionRunResult;
using Slic3r::GUI::ActionRegistry;

namespace {

class TestAppAction final : public AppAction
{
public:
    TestAppAction() : AppAction("test", "Action title", "src-key", "Action source") {}

    AppActionRunResult run(const std::string& param = {}) const override { return {}; }
};

}

TEST_CASE("AppAction composes a stable id from prefix:title:source_key", "[ActionSource][SpeedDial]")
{
    CHECK(AppAction::compose_id("test", "Action title", "src-key") == "test:Action title:src-key");
    CHECK(AppAction::compose_id("script", "Do Thing", "pack.py") == "script:Do Thing:pack.py");
}

TEST_CASE("AppAction definitions are immutable after construction", "[ActionSource][SpeedDial]")
{
    using StringAccessor = const std::string& (AppAction::*) () const;

    STATIC_CHECK(std::is_same_v<decltype(&AppAction::id), StringAccessor>);
    STATIC_CHECK(std::is_same_v<decltype(&AppAction::title), StringAccessor>);
    STATIC_CHECK(std::is_same_v<decltype(&AppAction::source_key), StringAccessor>);
    STATIC_CHECK(std::is_same_v<decltype(&AppAction::source_name), StringAccessor>);

    const TestAppAction action;
    CHECK(action.id() == "test:Action title:src-key");
    CHECK(action.title() == "Action title");
    CHECK(action.source_key() == "src-key");
    CHECK(action.source_name() == "Action source");
}

TEST_CASE("ActionRegistry takes exclusive ownership of published actions", "[ActionSource][SpeedDial]")
{
    using ExpectedUpsert = void (ActionRegistry::*)(std::unique_ptr<AppAction>);

    STATIC_CHECK(std::is_same_v<decltype(&ActionRegistry::upsert), ExpectedUpsert>);
}

TEST_CASE("Go-to-plate actions are keyed by index, not title", "[ActionSource][SpeedDial]")
{
    CHECK(AppAction::compose_id("orca_plate_goto", "0", "orca") == "orca_plate_goto:0:orca");
    CHECK(AppAction::compose_id("orca_plate_goto", "2", "orca") == "orca_plate_goto:2:orca");
}

TEST_CASE("Recent-project actions are keyed by path, not title", "[ActionSource][SpeedDial]")
{
    CHECK(AppAction::compose_id("orca_recent_project", "/a/b/project.3mf", "orca") ==
          "orca_recent_project:/a/b/project.3mf:orca");
    CHECK(AppAction::compose_id("orca_recent_project", "C:/Data/cube.3mf", "orca") ==
          "orca_recent_project:C:/Data/cube.3mf:orca");
}

TEST_CASE("Command actions are keyed by catalog key, not display title", "[ActionSource][SpeedDial]")
{
    CHECK(AppAction::compose_id("orca_command", "save_project", "orca") == "orca_command:save_project:orca");
    CHECK(AppAction::compose_id("orca_command", "save_project", "orca") !=
          AppAction::compose_id("orca_command", "load_project", "orca"));
}

TEST_CASE("Command action construction keys by catalog key", "[ActionSource][SpeedDial]")
{
    const std::vector<Slic3r::GUI::NativeCommand>& commands = Slic3r::GUI::NativeCommands::catalog();
    REQUIRE_FALSE(commands.empty());
    const Slic3r::GUI::NativeCommand& c = commands.front();

    std::unique_ptr<AppAction> action = Slic3r::GUI::NativeCommands::make_action(c);
    REQUIRE(action != nullptr);
    CHECK(action->id() == AppAction::compose_id("orca_command", c.key, "orca"));
    CHECK(action->id() != AppAction::compose_id("orca_command", c.title, "orca"));
    CHECK(action->title() == c.title);
    CHECK(action->group == c.group);
    CHECK(action->input == c.input);
    CHECK(action->icon == c.icon);
}

TEST_CASE("Actions default to no description or wiki link", "[ActionSource][SpeedDial]")
{
    const TestAppAction action;
    CHECK(action.tooltip.empty());
    CHECK(action.help_url.empty());

    REQUIRE_FALSE(Slic3r::GUI::NativeCommands::catalog().empty());
    std::unique_ptr<AppAction> command = Slic3r::GUI::NativeCommands::make_action(Slic3r::GUI::NativeCommands::catalog().front());
    REQUIRE(command != nullptr);
    CHECK(command->tooltip.empty());
    CHECK(command->help_url.empty());
}

TEST_CASE("Two-phase commands declare their input phase", "[ActionSource][SpeedDial]")
{
    auto input_of = [](const std::string& key) -> std::string {
        for (const auto& c : Slic3r::GUI::NativeCommands::catalog())
            if (c.key == key)
                return c.input;
        return {};
    };
    CHECK(input_of("go_to_layer") == "percent");
    CHECK(input_of("go_to_tab") == "tab");
}

TEST_CASE("Command input tokens stay in the known vocabulary", "[ActionSource][SpeedDial]")
{
    for (const auto& c : Slic3r::GUI::NativeCommands::catalog()) {
        INFO(c.key << " input=" << c.input);
        CHECK((c.input.empty() || c.input == "percent" || c.input == "tab"));
    }
}

static_assert(Slic3r::GUI::ActionRegistry::kFavLimit == 10, "kFavLimit must stay 10");

TEST_CASE("Favourite lists are capped and deduped preserving order", "[ActionSource][SpeedDial]")
{
    using Slic3r::GUI::cap_favourites;

    CHECK(cap_favourites({}, 10) == std::vector<std::string>{});
    CHECK(cap_favourites({"a", "b", "a"}, 10) == std::vector<std::string>{"a", "b"});
    CHECK(cap_favourites({"c", "a", "b", "c"}, 3) == std::vector<std::string>{"c", "a", "b"});
    CHECK(cap_favourites({"a", "b"}, 0) == std::vector<std::string>{});
}

TEST_CASE("Native command catalog has unique keys and present titles", "[ActionSource][SpeedDial]")
{
    const std::vector<Slic3r::GUI::NativeCommand>& commands = Slic3r::GUI::NativeCommands::catalog();
    CHECK_FALSE(commands.empty());

    std::set<std::string> seen;
    for (const auto& c : commands) {
        CHECK_FALSE(c.key.empty());
        CHECK_FALSE(c.title.empty());
        CHECK(seen.insert(c.key).second);
    }
}

TEST_CASE("Native command icons resolve to shipped SVGs", "[ActionSource][SpeedDial]")
{
    const std::vector<Slic3r::GUI::NativeCommand>& commands = Slic3r::GUI::NativeCommands::catalog();
    auto icon_of = [&commands](const std::string& key) -> const std::string* {
        for (const auto& c : commands)
            if (c.key == key)
                return &c.icon;
        return nullptr;
    };

    struct Expected
    {
        const char* key;
        const char* icon;
    };
    for (const Expected& e : {Expected{"load_project", "menu_open"},
                              Expected{"save_project", "menu_save"},
                              Expected{"sync_ams", "ams_fila_sync"},
                              Expected{"mode_simple", "advanced"},
                              Expected{"calib_temperature", "param_temperature"},
                              Expected{"calib_cornering", "param_precision"},
                              Expected{"plate_add", "toolbar_add_plate"},
                              Expected{"add_primitive_cube", "menu_obj_cube"},
                              Expected{"obj_delete", "delete"},
                              Expected{"export_gcode", "custom-gcode_gcode"},
                              Expected{"import_file", "menu_open"},
                              Expected{"help_open_config_folder", "open_project"},
                              Expected{"help_check_updates", "refresh"},
                              Expected{"help_about", "OrcaSlicer_gradient_circle"},
                              Expected{"go_to_tab", ""}}) {
        const std::string* icon = icon_of(e.key);
        INFO(e.key);
        REQUIRE(icon != nullptr);
        CHECK(*icon == e.icon);
    }

    const boost::filesystem::path images = boost::filesystem::path(PROFILES_DIR).parent_path() / "images";
    for (const auto& c : commands) {
        if (c.icon.empty())
            continue;
        INFO(c.key << " -> " << c.icon);
        CHECK(boost::filesystem::exists(images / (c.icon + ".svg")));
    }
}

TEST_CASE("Native command catalog includes the Help and developer-mode commands", "[ActionSource][SpeedDial]")
{
    const std::vector<Slic3r::GUI::NativeCommand>& commands = Slic3r::GUI::NativeCommands::catalog();
    auto find = [&commands](const std::string& key) -> const Slic3r::GUI::NativeCommand* {
        for (const auto& c : commands)
            if (c.key == key)
                return &c;
        return nullptr;
    };

    const Slic3r::GUI::NativeCommand* first = find("help_keyboard_shortcuts");
    REQUIRE(first != nullptr);
    for (const char* key : {"help_setup_wizard", "help_open_config_folder", "help_troubleshoot", "help_network_test",
                            "help_benchmark_3d_scene", "help_tip_of_the_day", "help_check_updates", "help_about", "open_wiki",
                            "open_youtube"}) {
        const Slic3r::GUI::NativeCommand* c = find(key);
        REQUIRE(c != nullptr);
        CHECK(c->group == first->group);
    }

    const Slic3r::GUI::NativeCommand* mode_simple = find("mode_simple");
    const Slic3r::GUI::NativeCommand* dev_mode    = find("toggle_developer_mode");
    REQUIRE(mode_simple != nullptr);
    REQUIRE(dev_mode != nullptr);
    CHECK(dev_mode->group == mode_simple->group);
}

TEST_CASE("Native command catalog covers the Add menus", "[ActionSource][SpeedDial]")
{
    const std::vector<Slic3r::GUI::NativeCommand>& commands = Slic3r::GUI::NativeCommands::catalog();
    auto group_of = [&commands](const std::string& key) -> const std::string* {
        for (const auto& c : commands)
            if (c.key == key)
                return &c.group;
        return nullptr;
    };

    const std::string* primitive_group = group_of("add_primitive_cube");
    REQUIRE(primitive_group != nullptr);
    for (const char* key : {"add_primitive_cylinder", "add_primitive_sphere", "add_primitive_cone", "add_primitive_disc",
                            "add_primitive_torus", "add_primitive_text", "add_primitive_svg"}) {
        const std::string* group = group_of(key);
        INFO(key);
        REQUIRE(group != nullptr);
        CHECK(*group == *primitive_group);
    }

    const std::string* handy_group = group_of("add_handy_orca_cube");
    REQUIRE(handy_group != nullptr);
    for (const char* key : {"add_handy_orcasliced_combo", "add_handy_orca_badge", "add_handy_orca_tolerance_test",
                            "add_handy_3dbenchy", "add_handy_cali_cat", "add_handy_autodesk_fdm_test", "add_handy_voron_cube",
                            "add_handy_stanford_bunny", "add_handy_orca_string_hell"}) {
        const std::string* group = group_of(key);
        INFO(key);
        REQUIRE(group != nullptr);
        CHECK(*group == *handy_group);
    }
}

TEST_CASE("Setting display labels mirror the settings row", "[ActionSource][SpeedDial]")
{
    using Slic3r::Search::compose_display_label;

    CHECK(compose_display_label(L"Reverse on even", L"Reverse on even", false) == L"Reverse on even");
    CHECK(compose_display_label(L"", L"Outer wall", false) == L"Outer wall");
    CHECK(compose_display_label(L"Cool Plate", L"First layer", true) == wxString(L"Cool Plate \u2013 First layer"));
    CHECK(compose_display_label(L"Cool Plate", L"Other layers", true) == wxString(L"Cool Plate \u2013 Other layers"));
    CHECK(compose_display_label(L"Skirt loops", L"Skirt loops", true) == L"Skirt loops");

    using Slic3r::Search::resolve_setting_title;

    CHECK(resolve_setting_title(L"Brim width", L"Brim ear radius", false) == L"Brim ear radius");
    CHECK(resolve_setting_title(L"Cool Plate \u2013 First layer", L"Cool Plate", true) ==
          wxString(L"Cool Plate \u2013 First layer"));
    CHECK(resolve_setting_title(L"Reverse on even", L"", false) == L"Reverse on even");
    CHECK(resolve_setting_title(L"", L"", false).IsEmpty());
}

TEST_CASE("Settings above the current mode require a switch", "[ActionSource][SpeedDial]")
{
    using Slic3r::GUI::requires_mode_switch;
    using Slic3r::comAdvanced;
    using Slic3r::comDevelop;
    using Slic3r::comExpert;
    using Slic3r::comSimple;

    CHECK(requires_mode_switch(comAdvanced, comSimple));
    CHECK(requires_mode_switch(comExpert, comSimple));
    CHECK(requires_mode_switch(comExpert, comAdvanced));
    CHECK(requires_mode_switch(comDevelop, comSimple));
    CHECK(requires_mode_switch(comDevelop, comAdvanced));
    CHECK(requires_mode_switch(comDevelop, comExpert));

    CHECK_FALSE(requires_mode_switch(comSimple, comSimple));
    CHECK_FALSE(requires_mode_switch(comSimple, comAdvanced));
    CHECK_FALSE(requires_mode_switch(comAdvanced, comAdvanced));
    CHECK_FALSE(requires_mode_switch(comAdvanced, comExpert));
    CHECK_FALSE(requires_mode_switch(comExpert, comExpert));
    CHECK_FALSE(requires_mode_switch(comDevelop, comDevelop));
}
