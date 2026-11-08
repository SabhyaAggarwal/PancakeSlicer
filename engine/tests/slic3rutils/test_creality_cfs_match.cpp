#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/Utils/CrealityPrintAgent.hpp"

using namespace Slic3r;

namespace {

struct FilamentTestCollection : public PresetCollection
{
    FilamentTestCollection()
        : PresetCollection(Preset::TYPE_FILAMENT, Preset::filament_options(),
                           static_cast<const PrintRegionConfig &>(FullPrintConfig::defaults()))
    {}
};

struct FilamentSpec
{
    const char *name;
    const char *filament_id;
    const char *filament_type;
    bool        is_library = false;
    bool        is_system  = true;
};

void populate(PresetCollection &filaments, const std::vector<FilamentSpec> &specs,
              const VendorProfile &vendor, const VendorProfile &library)
{
    for (const FilamentSpec &spec : specs) {
        DynamicPrintConfig config(filaments.default_preset().config);
        config.option<ConfigOptionStrings>("filament_type", true)->values = {spec.filament_type};
        filaments.load_preset(std::string(), spec.name, config, false);
    }
    for (auto it = filaments.begin(); it != filaments.end(); ++it) {
        const auto spec = std::find_if(specs.begin(), specs.end(),
                                       [&](const FilamentSpec &s) { return it->name == s.name; });
        if (spec == specs.end())
            continue;
        it->filament_id  = spec->filament_id;
        it->vendor       = spec->is_library ? &library : &vendor;
        it->is_system    = spec->is_system;
        it->is_default   = false;
        it->is_visible   = true;
        it->is_compatible = true;
    }
}

const std::vector<FilamentSpec> k2_stock_nozzle{
    {"AliZ PLA @System",                       "ALIZ-PLA",     "PLA",  true},
    {"Generic PETG @K2-all",                   "PETG-GENERIC", "PETG"},
    {"Generic PLA @K2-all",                    "PLA-GENERIC",  "PLA"},
    {"Generic PLA High Speed @Creality K2-all","PLA-HS",       "PLA"},
    {"Generic PLA Matte @Creality K2-all",     "PLA-MATTE",    "PLA"},
    {"Generic PLA Silk @Creality K2-all",      "PLA-SILK",     "PLA"},
    {"Hyper PLA @K2-all",                      "PLA-HYPER",    "PLA"},
};

std::string match(const std::vector<FilamentSpec> &specs, const std::string &spool_vendor,
                  const std::string &spool_name, const std::string &base_type)
{
    FilamentTestCollection filaments;
    VendorProfile          vendor("Creality");
    VendorProfile          library(PresetBundle::ORCA_FILAMENT_LIBRARY);
    vendor.name  = "Creality";
    library.name = PresetBundle::ORCA_FILAMENT_LIBRARY;
    populate(filaments, specs, vendor, library);
    return CrealityPrintAgent::match_filament_preset(filaments, spool_vendor, spool_name, base_type);
}

}
TEST_CASE("An unbranded CFS spool maps to the vendor's plain generic, not a subtype", "[CFS][Creality]")
{
    CHECK(match(k2_stock_nozzle, "Creality", "", "PLA") == "PLA-GENERIC");
}

TEST_CASE("A CFS spool matches its vendor's presets even when the name omits the vendor", "[CFS][Creality]")
{
    CHECK(match(k2_stock_nozzle, "Creality", "", "PETG") == "PETG-GENERIC");
}

TEST_CASE("A branded CFS spool still beats the generic", "[CFS][Creality]")
{
    CHECK(match(k2_stock_nozzle, "Creality", "Hyper PLA", "PLA") == "PLA-HYPER");
}

TEST_CASE("A CFS spool that names a subtype gets that subtype", "[CFS][Creality]")
{
    CHECK(match(k2_stock_nozzle, "Creality", "Generic PLA Silk", "PLA") == "PLA-SILK");
    CHECK(match(k2_stock_nozzle, "Creality", "Generic PLA Matte", "PLA") == "PLA-MATTE");
}

TEST_CASE("A CFS spool from an unknown vendor falls back to a preset of the same type", "[CFS][Creality]")
{
    const std::string matched = match(k2_stock_nozzle, "SomeOtherBrand", "", "PLA");
    REQUIRE_FALSE(matched.empty());
    const auto spec = std::find_if(k2_stock_nozzle.begin(), k2_stock_nozzle.end(),
                                   [&](const FilamentSpec &s) { return matched == s.filament_id; });
    REQUIRE(spec != k2_stock_nozzle.end());
    CHECK(std::string(spec->filament_type) == "PLA");
}


TEST_CASE("A third party spool matches its preset inside the printer vendor's bundle", "[CFS][Creality]")
{
    const std::vector<FilamentSpec> with_third_party{
        {"Generic PLA @K2-all",     "PLA-GENERIC", "PLA"},
        {"eSUN PLA+ @K2 Plus-all",  "ESUN-PLA",    "PLA"},
    };
    CHECK(match(with_third_party, "eSUN", "", "PLA") == "ESUN-PLA");
    CHECK(match(with_third_party, "Creality", "", "PLA") == "PLA-GENERIC");
}
