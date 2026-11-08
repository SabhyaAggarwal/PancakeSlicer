#include <catch2/catch_all.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <algorithm>
#include "libslic3r/Config.hpp"
#include <vector>
#include <cstddef>
#include <string>

using namespace Slic3r;

TEST_CASE("deep_diff flags new vector entries that duplicate values[0]", "[PresetDiff][Config]")
{
    Preset reference(Preset::TYPE_PRINTER, "ref");
    reference.config.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4});

    Preset edited(Preset::TYPE_PRINTER, "edited");
    edited.config.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4, 0.4});

    std::vector<std::string> diff =
        PresetCollection::dirty_options(&edited, &reference, true);

    REQUIRE(std::find(diff.begin(), diff.end(), "nozzle_diameter#1") != diff.end());

    REQUIRE(std::find(diff.begin(), diff.end(), "nozzle_diameter#0") == diff.end());
}

TEST_CASE("deep_diff distinguishes absolute and percentage speeds for each variant", "[PresetDiff][Config]")
{
    const size_t changed_index = GENERATE(size_t(0), size_t(1));
    Preset reference(Preset::TYPE_PRINT, "ref");
    reference.config.set_key_value("small_perimeter_speed", new ConfigOptionFloatsOrPercents{{50., false}, {50., false}});

    Preset edited = reference;
    edited.config.option<ConfigOptionFloatsOrPercents>("small_perimeter_speed")->values[changed_index].percent = true;

    const auto diff = PresetCollection::dirty_options(&edited, &reference, true);
    REQUIRE(diff == std::vector<std::string>{"small_perimeter_speed#" + std::to_string(changed_index)});

    DynamicPrintConfig transferred = reference.config;
    transferred.apply_only(edited.config, diff);
    REQUIRE(*transferred.option("small_perimeter_speed") == *edited.config.option("small_perimeter_speed"));
}
