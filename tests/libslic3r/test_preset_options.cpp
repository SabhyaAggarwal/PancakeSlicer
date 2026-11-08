#include <catch2/catch_all.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "libslic3r/Config.hpp"
#include <set>
#include <string>

using namespace Slic3r;

namespace {

const std::set<std::string> kDeprecatedRegionFields = {
    "ironing_direction",
    "wall_infill_order",
};

void check_keys_are_in_a_preset(const t_config_option_keys& keys, const std::string& class_name)
{
    REQUIRE_FALSE(keys.empty());
    const auto& print_options    = Preset::print_options();
    const auto& filament_options = Preset::filament_options();
    const std::set<std::string> in_print(print_options.begin(), print_options.end());
    const std::set<std::string> in_filament(filament_options.begin(), filament_options.end());
    for (const std::string& key : keys) {
        DYNAMIC_SECTION(class_name << "::" << key)
        {
            INFO("'" << key << "' on " << class_name
                     << " is missing from "
                        "Preset::print_options()/filament_options(); add it to "
                        "s_Preset_print_options (or s_Preset_filament_options) in Preset.cpp.");
            const bool registered = in_print.count(key) || in_filament.count(key) || kDeprecatedRegionFields.count(key);
            REQUIRE(registered);
        }
    }
}

}
// clang-format off
TEST_CASE("Every PrintRegionConfig field is registered in a preset key list", "[Preset][Config]")
{
    check_keys_are_in_a_preset(PrintRegionConfig::defaults().keys(), "PrintRegionConfig");
}

TEST_CASE("Every PrintObjectConfig field is registered in a preset key list", "[Preset][Config]")
{
    check_keys_are_in_a_preset(PrintObjectConfig::defaults().keys(), "PrintObjectConfig");
}
// clang-format on
