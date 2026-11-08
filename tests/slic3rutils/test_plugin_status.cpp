#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>

#include <slic3r/GUI/PluginStatus.hpp>

using Slic3r::GUI::PluginStatus;
using Slic3r::GUI::resolve_plugin_status;

TEST_CASE("resolve_plugin_status precedence", "[plugin][status]") {
    REQUIRE(resolve_plugin_status( false,  true,  true) == PluginStatus::RuntimeError);

    REQUIRE(resolve_plugin_status(false, true, false) == PluginStatus::Error);

    REQUIRE(resolve_plugin_status(true, true, true) == PluginStatus::Loading);

    REQUIRE(resolve_plugin_status(false, false, true) == PluginStatus::Activated);

    REQUIRE(resolve_plugin_status(false, false, false) == PluginStatus::Inactive);
}
