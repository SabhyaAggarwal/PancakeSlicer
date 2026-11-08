#include <catch2/catch_all.hpp>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include "libslic3r/Preset.hpp"

using namespace Slic3r;

TEST_CASE("preset setting_id matches the Python reference", "[Preset][setting_id]") {
    struct Vec { const char* vendor; const char* type; const char* name; const char* expected; };
    const Vec vectors[] = {
        {"Afinia",   "filament", "Afinia ABS @Afinia H400",                  "TL34qSVkppBvMvgH"},
        {"Afinia",   "process",  "0.20mm Standard @Afinia H400",             "FzmtNsy7XQvpd7w0"},
        {"Afinia",   "machine",  "Afinia H400 0.4 nozzle",                   "r4FZagW0S8uoaJPd"},
        {"Anycubic", "filament", "Generic PLA @Anycubic Kobra 2",            "YIWGGLQ8Oepd30Fv"},
        {"Creality", "process",  "0.16mm Optimal @Creality Ender-3 V3",      "2Nrbq8PxssUPBLza"},
        {"Elegoo",   "machine",  "Elegoo Neptune 4 0.4 nozzle",              "69QdWuRQwAZk9rFu"},
    };

    for (const auto& v : vectors) {
        const std::string id = generate_preset_setting_id(v.vendor, v.type, v.name);
        INFO(v.vendor << "/" << v.type << "/" << v.name);
        CHECK(id.size() == 16);
        CHECK(id == v.expected);
    }
}

TEST_CASE("preset setting_id is deterministic and identity-sensitive", "[Preset][setting_id]") {
    const std::string a = generate_preset_setting_id("VendorX", "filament", "My PLA");
    CHECK(a == generate_preset_setting_id("VendorX", "filament", "My PLA"));
    CHECK(a != generate_preset_setting_id("VendorY", "filament", "My PLA"));
    CHECK(a != generate_preset_setting_id("VendorX", "process",  "My PLA"));
    CHECK(a != generate_preset_setting_id("VendorX", "filament", "My PETG"));
    CHECK(generate_preset_setting_id("", "filament", "My PLA").empty());
    CHECK(generate_preset_setting_id("VendorX", "filament", "").empty());
}
