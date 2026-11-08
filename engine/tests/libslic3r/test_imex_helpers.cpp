#include <catch2/catch_all.hpp>

#include "catch2/matchers/catch_matchers_floating_point.hpp"
#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers.hpp"
#include "catch2/matchers/catch_matchers_string.hpp"
#include "catch2/catch_message.hpp"
#include "catch2/generators/catch_generators.hpp"
#include "libslic3r/IMEXHelpers.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Point.hpp"

#include <cstddef>
#include <iterator>
#include <libslic3r/Config.hpp>
#include <map>
#include <optional>
#include <libslic3r/Polygon.hpp>
#include <libslic3r/BoundingBox.hpp>
#include <set>
#include <vector>
#include <utility>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

static ConfigOptionInts make_pem(std::vector<int> v) {
    ConfigOptionInts o;
    o.values = std::move(v);
    return o;
}

TEST_CASE("effective_physical_extruder_map - authored map wins", "[IMEX]") {
    auto explicit_pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    auto out = effective_physical_extruder_map(&explicit_pem, 7);
    REQUIRE(out.values == std::vector<int>{0, 0, 0, 0, 1, 2, 3});
}

TEST_CASE("effective_physical_extruder_map - a permutation is honoured", "[IMEX]") {
    auto explicit_pem = make_pem({1, 0});
    auto out = effective_physical_extruder_map(&explicit_pem, 2);
    REQUIRE(out.values == std::vector<int>{1, 0});
}

TEST_CASE("effective_physical_extruder_map - unauthored derives the identity", "[IMEX]") {
    auto default_pem = make_pem({0});
    auto out = effective_physical_extruder_map(&default_pem, 2);
    REQUIRE(out.values == std::vector<int>{0, 1});
}

TEST_CASE("effective_physical_extruder_map - result is always one entry per extruder", "[IMEX]") {
    for (int n : { 1, 2, 4, 7 }) {
        DYNAMIC_SECTION("nozzle count " << n) {
            REQUIRE((int) effective_physical_extruder_map(nullptr, n).values.size() == n);
            auto stale = make_pem({0});
            REQUIRE((int) effective_physical_extruder_map(&stale, n).values.size() == n);
        }
    }
}

TEST_CASE("effective_physical_extruder_map - degenerate nozzle count still yields a usable map", "[IMEX]") {
    REQUIRE(effective_physical_extruder_map(nullptr, 0).values == std::vector<int>{0});
}

TEST_CASE("effective_physical_extruder_map - IDEX ghost-color regression guard", "[IMEX]") {
    auto default_pem = make_pem({0});
    auto pem         = effective_physical_extruder_map(&default_pem, 2);
    REQUIRE(first_filament_for_physical_head(pem, 0) == 0);
    REQUIRE(first_filament_for_physical_head(pem, 1) == 1);
}

TEST_CASE("imex_pem_tool_for - non-parallel mode returns -1", "[IMEX]") {
    auto pem = make_pem({0, 1, 2});
    REQUIRE(imex_pem_tool_for(0, "",        pem) == -1);
    REQUIRE(imex_pem_tool_for(1, "primary", pem) == -1);
    REQUIRE(imex_pem_tool_for(2, "primary", pem) == -1);
}

TEST_CASE("imex_pem_tool_for - parallel mode with empty pem returns -1", "[IMEX]") {
    ConfigOptionInts empty_pem;
    REQUIRE(imex_pem_tool_for(0, "copy_mode",   empty_pem) == -1);
    REQUIRE(imex_pem_tool_for(3, "mirror_mode", empty_pem) == -1);
}

TEST_CASE("imex_pem_tool_for - identity pem routes filament to itself", "[IMEX]") {
    auto pem = make_pem({0, 1});
    REQUIRE(imex_pem_tool_for(0, "copy_mode", pem) == 0);
    REQUIRE(imex_pem_tool_for(1, "copy_mode", pem) == 1);
}

TEST_CASE("imex_pem_tool_for - MMU collapse routes multiple logical slots to one physical", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    REQUIRE(imex_pem_tool_for(0, "copy_mode", pem) == 0);
    REQUIRE(imex_pem_tool_for(3, "copy_mode", pem) == 0);
    REQUIRE(imex_pem_tool_for(6, "copy_mode", pem) == 3);
}

TEST_CASE("imex_physical_heater_for - non-IMEX printer passes the logical id through untouched", "[IMEX]") {
    auto pem = make_pem({1, 0});
    REQUIRE(imex_physical_heater_for(false, pem, 0) == 0);
    REQUIRE(imex_physical_heater_for(false, pem, 1) == 1);

    auto afc = make_pem({0, 0, 0, 0, 1, 2, 3});
    REQUIRE(imex_physical_heater_for(false, afc, 3) == 3);
    REQUIRE(imex_physical_heater_for(false, afc, 6) == 6);
}

TEST_CASE("imex_physical_heater_for - IMEX identity map is a no-op", "[IMEX]") {
    auto pem = make_pem({0, 1, 2, 3});
    for (int id = 0; id < 4; ++id)
        REQUIRE(imex_physical_heater_for(true, pem, id) == id);
}

TEST_CASE("imex_physical_heater_for - IMEX permutation remaps to the physical heater", "[IMEX]") {
    auto pem = make_pem({1, 0});
    REQUIRE(imex_physical_heater_for(true, pem, 0) == 1);
    REQUIRE(imex_physical_heater_for(true, pem, 1) == 0);
}

TEST_CASE("imex_physical_heater_for - IMEX MMU collapse targets the shared heater", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    REQUIRE(imex_physical_heater_for(true, pem, 0) == 0);
    REQUIRE(imex_physical_heater_for(true, pem, 3) == 0);
    REQUIRE(imex_physical_heater_for(true, pem, 4) == 1);
    REQUIRE(imex_physical_heater_for(true, pem, 6) == 3);
}

TEST_CASE("imex_physical_heater_for - out-of-range ids pass through instead of clamping", "[IMEX]") {
    auto pem = make_pem({1, 0});
    REQUIRE(imex_physical_heater_for(true, pem, -1) == -1);
    REQUIRE(imex_physical_heater_for(true, pem, 2)  == 2);
    REQUIRE(imex_physical_heater_for(true, pem, 99) == 99);

    auto lone = make_pem({0});
    REQUIRE(imex_physical_heater_for(true, lone, 1) == 1);
}

TEST_CASE("imex_physical_heater_for - empty pem never remaps", "[IMEX]") {
    ConfigOptionInts empty_pem;
    REQUIRE(imex_physical_heater_for(true,  empty_pem, 0) == 0);
    REQUIRE(imex_physical_heater_for(false, empty_pem, 2) == 2);
}

TEST_CASE("imex_suppresses_bare_toolchange - non-IMEX printer never suppresses", "[IMEX]") {
    REQUIRE_FALSE(imex_suppresses_bare_toolchange("", 0));
    REQUIRE_FALSE(imex_suppresses_bare_toolchange("", 1));
    REQUIRE_FALSE(imex_suppresses_bare_toolchange("", 99));
}

TEST_CASE("imex_suppresses_bare_toolchange - Primary mode never suppresses (preserves AFC lane swap)", "[IMEX]") {
    REQUIRE_FALSE(imex_suppresses_bare_toolchange("primary", 0));
    REQUIRE_FALSE(imex_suppresses_bare_toolchange("primary", 1));
    REQUIRE_FALSE(imex_suppresses_bare_toolchange("primary", 50));
}

TEST_CASE("imex_suppresses_bare_toolchange - parallel mode suppresses at print-start", "[IMEX]") {
    REQUIRE(imex_suppresses_bare_toolchange("copy_mode",   0));
    REQUIRE(imex_suppresses_bare_toolchange("copy_mode",   1));
    REQUIRE(imex_suppresses_bare_toolchange("mirror_mode", 0));
    REQUIRE(imex_suppresses_bare_toolchange("iq-copy",     1));
}

TEST_CASE("imex_suppresses_bare_toolchange - parallel mode allows mid-print toolchange", "[IMEX]") {
    REQUIRE_FALSE(imex_suppresses_bare_toolchange("copy_mode",   2));
    REQUIRE_FALSE(imex_suppresses_bare_toolchange("mirror_mode", 5));
    REQUIRE_FALSE(imex_suppresses_bare_toolchange("backup_mode", 100));
}

TEST_CASE("imex_multicolor_block_reason - non-IMEX prints never block", "[IMEX]") {
    auto pem = make_pem({0, 1, 2, 3});
    REQUIRE(imex_multicolor_block_reason("",        "0:P,1:C", 2, {0, 1}, pem).empty());
    REQUIRE(imex_multicolor_block_reason("primary", "0:P,1:C", 2, {0, 1}, pem).empty());
}

TEST_CASE("imex_multicolor_block_reason - single-color prints never block", "[IMEX]") {
    auto pem = make_pem({0, 1, 2, 3});
    REQUIRE(imex_multicolor_block_reason("copy",   "0:P,1:C,2:M,3:M", 2, {0}, pem).empty());
    REQUIRE(imex_multicolor_block_reason("mirror", "0:P,1:C,2:M,3:M", 2, {},  pem).empty());
}

TEST_CASE("imex_multicolor_block_reason - IDEX (1 tool per gantry) blocks multi-color", "[IMEX]") {
    auto pem = make_pem({0, 1});
    const std::string reason = imex_multicolor_block_reason("copy", "0:P,1:C", 1, {0, 1}, pem);
    REQUIRE_FALSE(reason.empty());
    REQUIRE_THAT(reason, Catch::Matchers::ContainsSubstring("Span tool"));
}

TEST_CASE("imex_multicolor_block_reason - single-gantry IMEX mode blocks multi-color", "[IMEX]") {
    auto pem = make_pem({0, 1, 2, 3});
    const std::string reason = imex_multicolor_block_reason("copy", "0:P,1:C", 2, {0, 1}, pem);
    REQUIRE_FALSE(reason.empty());
    REQUIRE_THAT(reason, Catch::Matchers::ContainsSubstring("single gantry"));
}

TEST_CASE("imex_multicolor_block_reason - IQEX 2-tool-active mode blocks multi-color", "[IMEX]") {
    auto pem = make_pem({0, 1, 2, 3});
    const std::string reason = imex_multicolor_block_reason("copy", "0:P,2:C", 2, {0, 2}, pem);
    REQUIRE_FALSE(reason.empty());
    REQUIRE_THAT(reason, Catch::Matchers::ContainsSubstring("Span tool"));
}

TEST_CASE("imex_multicolor_block_reason - IQEX 4-tool independent copies block multi-color", "[IMEX]") {
    auto pem = make_pem({0, 1, 2, 3});
    const std::string reason = imex_multicolor_block_reason("copy", "0:P,1:C,2:M,3:M", 2, {0, 1}, pem);
    REQUIRE_FALSE(reason.empty());
    REQUIRE_THAT(reason, Catch::Matchers::ContainsSubstring("Span tool"));
}

TEST_CASE("imex_multicolor_block_reason - IQEX paired-gantry multicolor allowed with Span", "[IMEX]") {
    auto pem = make_pem({0, 1, 2, 3});
    REQUIRE(imex_multicolor_block_reason("copy", "0:P,1:S,2:M,3:M", 2, {0, 1}, pem).empty());
}

TEST_CASE("A Span mode refuses a color routed to a tool outside the primary's gantry pair", "[IMEX]") {
    auto pem = make_pem({0, 1, 2, 3});
    const std::string reason = imex_multicolor_block_reason("mc-copy", "0:P,1:S,2:C,3:C", 2, {0, 1, 3}, pem);
    REQUIRE_THAT(reason, Catch::Matchers::ContainsSubstring("only T0/T1 print colors, but filament 4 is routed to T3"));
    REQUIRE_THAT(imex_multicolor_block_reason("mc-copy", "0:P,1:S,2:C,3:C", 2, {0, 4}, pem),
                 Catch::Matchers::ContainsSubstring("filament 5 isn't routed to any tool"));
    REQUIRE_FALSE(imex_multicolor_block_reason("mc-copy", "0:P,1:S,3:C", 2, {0, 2}, pem).empty());
    REQUIRE_FALSE(imex_multicolor_block_reason("mc-copy", "0:P,1:S,2:C,3:C", 2, {0, 4}, pem).empty());
    REQUIRE_FALSE(imex_multicolor_block_reason("mc-copy", "0:P,1:S,2:C,3:C,4:C,5:C", 3, {0, 2},
                                               make_pem({0, 1, 2, 3, 4, 5})).empty());
    const auto afc = make_pem({0, 0, 0, 0, 1, 2, 3});
    REQUIRE(imex_multicolor_block_reason("mc-copy", "0:P,1:S,2:C,3:C", 2, {0, 4}, afc).empty());
    REQUIRE_THAT(imex_multicolor_block_reason("mc-copy", "0:P,1:S,2:C,3:C", 2, {0, 5}, afc),
                 Catch::Matchers::ContainsSubstring("filament 6 is routed to T2"));
    REQUIRE(imex_multicolor_block_reason("mc-copy", "0:P,1:S,2:C,3:C", 2, {0, 1}, pem).empty());
    REQUIRE(imex_multicolor_block_reason("mc-copy", "0:P,1:S,2:S,3:C,4:C,5:C", 3, {0, 1, 2},
                                         make_pem({0, 1, 2, 3, 4, 5})).empty());
}

TEST_CASE("imex_multicolor_block_reason - MMU lane sharing blocks multi-color", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    const std::string reason = imex_multicolor_block_reason("copy", "0:P,1:C,2:M,3:M", 2, {0, 1}, pem);
    REQUIRE_FALSE(reason.empty());
    REQUIRE_THAT(reason, Catch::Matchers::ContainsSubstring("physical extruder map"));
}

TEST_CASE("imex_multicolor_block_reason - mode without primary blocks", "[IMEX]") {
    auto pem = make_pem({0, 1, 2, 3});
    const std::string reason = imex_multicolor_block_reason("copy", "1:C,2:M", 2, {0, 1}, pem);
    REQUIRE_FALSE(reason.empty());
    REQUIRE_THAT(reason, Catch::Matchers::ContainsSubstring("primary tool"));
}

TEST_CASE("first_filament_for_physical_head - identity pem", "[IMEX]") {
    auto pem = make_pem({0, 1, 2, 3});
    REQUIRE(first_filament_for_physical_head(pem, 0) == 0);
    REQUIRE(first_filament_for_physical_head(pem, 1) == 1);
    REQUIRE(first_filament_for_physical_head(pem, 2) == 2);
    REQUIRE(first_filament_for_physical_head(pem, 3) == 3);
    REQUIRE(first_filament_for_physical_head(pem, 4) == -1);
}

TEST_CASE("first_filament_for_physical_head - AFC routing", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    REQUIRE(first_filament_for_physical_head(pem, 0) == 0);
    REQUIRE(first_filament_for_physical_head(pem, 1) == 4);
    REQUIRE(first_filament_for_physical_head(pem, 2) == 5);
    REQUIRE(first_filament_for_physical_head(pem, 3) == 6);
    REQUIRE(first_filament_for_physical_head(pem, 5) == -1);
}

TEST_CASE("first_filament_for_physical_head - empty pem", "[IMEX]") {
    ConfigOptionInts pem;
    REQUIRE(first_filament_for_physical_head(pem, 0) == 0);
    REQUIRE(first_filament_for_physical_head(pem, 1) == -1);
}

TEST_CASE("has_mmu - pure IDEX", "[IMEX]") {
    REQUIRE_FALSE(has_mmu(make_pem({0, 1})));
    REQUIRE_FALSE(has_mmu(make_pem({0, 1, 2, 3})));
}

TEST_CASE("has_mmu - MMU on one head", "[IMEX]") {
    REQUIRE(has_mmu(make_pem({0, 0, 0, 0, 1, 2, 3})));
    REQUIRE(has_mmu(make_pem({0, 0})));
}

TEST_CASE("has_mmu - MMU on second head", "[IMEX]") {
    REQUIRE(has_mmu(make_pem({0, 1, 1, 1})));
}

TEST_CASE("has_mmu - empty / single-entry pem", "[IMEX]") {
    REQUIRE_FALSE(has_mmu(make_pem({})));
    REQUIRE_FALSE(has_mmu(make_pem({0})));
}

TEST_CASE("imex_primary_logical_from_objects - AFC primary picks the object's slot", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    REQUIRE(imex_primary_logical_from_objects({3}, pem, 0) == 2);
}

TEST_CASE("imex_primary_logical_from_objects - multi-color AFC primary returns first match", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    REQUIRE(imex_primary_logical_from_objects({1, 3}, pem, 0) == 0);
    REQUIRE(imex_primary_logical_from_objects({3, 1}, pem, 0) == 2);
}

TEST_CASE("imex_primary_logical_from_objects - direct extruder primary unambiguous", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    REQUIRE(imex_primary_logical_from_objects({5}, pem, 1) == 4);
}

TEST_CASE("imex_primary_logical_from_objects - no object routed to primary returns -1", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    REQUIRE(imex_primary_logical_from_objects({5}, pem, 0) == -1);
}

TEST_CASE("imex_primary_logical_from_objects - empty inputs", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    REQUIRE(imex_primary_logical_from_objects({}, pem, 0) == -1);
    ConfigOptionInts empty_pem;
    REQUIRE(imex_primary_logical_from_objects({1, 3}, empty_pem, 0) == -1);
}

TEST_CASE("imex_secondary_logical_slots - copy mode skips primary, falls back to first-routed", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    auto out = imex_secondary_logical_slots({0, 1}, 0, {}, pem);
    REQUIRE(out == std::vector<int>{4});
}

TEST_CASE("imex_secondary_logical_slots - IQEX 4-mode enumerates all secondaries", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    auto out = imex_secondary_logical_slots({0, 1, 2, 3}, 0, {}, pem);
    REQUIRE(out == std::vector<int>{4, 5, 6});
}

TEST_CASE("imex_secondary_logical_slots - per-plate override wins for secondary", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    std::map<int, int> plate_map{{1, 6}};
    auto out = imex_secondary_logical_slots({0, 1}, 0, plate_map, pem);
    REQUIRE(out == std::vector<int>{5});
}

TEST_CASE("imex_secondary_logical_slots - drops unrouted physicals, deduplicates", "[IMEX]") {
    auto pem = make_pem({0, 1});
    auto out = imex_secondary_logical_slots({0, 1, 2}, 0, {}, pem);
    REQUIRE(out == std::vector<int>{1});
    std::map<int, int> dup_map{{1, 1}, {2, 1}};
    auto pem2 = make_pem({0, 0, 0});
    auto out2 = imex_secondary_logical_slots({0, 1, 2}, 0, dup_map, pem2);
    REQUIRE(out2 == std::vector<int>{0});
}

TEST_CASE("imex_secondary_logical_slots - a -1 primary skips nothing", "[IMEX]") {
    ConfigOptionInts pem; pem.values = {0, 1};
    CHECK(imex_secondary_logical_slots({0, 1}, 0, {}, pem) == std::vector<int>{1});
    CHECK(imex_secondary_logical_slots({0, 1}, -1, {}, pem) == std::vector<int>{0, 1});
}

TEST_CASE("imex_primary_tool_for_mode - names the declared primary, -1 when there is none", "[IMEX]") {
    CHECK(imex_primary_tool_for_mode("0:P,1:C") == 0);
    CHECK(imex_primary_tool_for_mode("2:P,0:C,1:C") == 2);
    CHECK(imex_primary_tool_for_mode("0:C,1:C") == -1);
    CHECK(imex_primary_tool_for_mode("") == -1);
}

TEST_CASE("imex_secondary_logical_slots - only-primary-active returns empty", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    auto out = imex_secondary_logical_slots({0}, 0, {}, pem);
    REQUIRE(out.empty());
}

TEST_CASE("parse_imex_head_filament_map - round-trip", "[IMEX]") {
    auto m = parse_imex_head_filament_map("0:3,4:5");
    REQUIRE(m.size() == 2);
    REQUIRE(m[0] == 3);
    REQUIRE(m[4] == 5);
}

TEST_CASE("parse_imex_head_filament_map - whitespace + empty tokens", "[IMEX]") {
    auto m = parse_imex_head_filament_map("  0 : 3 , , 4:5  ");
    REQUIRE(m.size() == 2);
    REQUIRE(m[0] == 3);
    REQUIRE(m[4] == 5);
}

TEST_CASE("parse_imex_head_filament_map - empty string", "[IMEX]") {
    REQUIRE(parse_imex_head_filament_map("").empty());
}

TEST_CASE("parse_imex_head_filament_map - a high byte makes its token unparseable", "[IMEX]") {
    auto m = parse_imex_head_filament_map("\xFF" "0:3,4:5");
    REQUIRE(m.size() == 1);
    REQUIRE(m.count(0) == 0);
    REQUIRE(m[4] == 5);

    REQUIRE(parse_imex_head_filament_map("0:\xFF").empty());
}

TEST_CASE("parse_imex_head_filament_map - absurd indices are rejected at parse", "[IMEX]") {
    REQUIRE(parse_imex_head_filament_map("1:9999").empty());
    REQUIRE(parse_imex_head_filament_map("9999:1").empty());
    REQUIRE(parse_imex_head_filament_map("1:0").empty());
    REQUIRE(parse_imex_head_filament_map("-1:2").empty());
    REQUIRE(parse_imex_head_filament_map("1:-2").empty());

    auto m = parse_imex_head_filament_map("0:9999,1:2");
    REQUIRE(m.size() == 1);
    REQUIRE(m[1] == 2);
}

TEST_CASE("resolve_filament_for_head - override wins", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    std::map<int,int> plate_map{{0, 3}};
    REQUIRE(resolve_filament_for_head(plate_map, pem, 0) == 2);
}

TEST_CASE("resolve_filament_for_head - fallback when unset", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    std::map<int,int> plate_map{};
    REQUIRE(resolve_filament_for_head(plate_map, pem, 0) == 0);
    REQUIRE(resolve_filament_for_head(plate_map, pem, 1) == 4);
}

TEST_CASE("resolve_filament_for_head - no routing for head", "[IMEX]") {
    auto pem = make_pem({0, 1});
    std::map<int,int> plate_map{};
    REQUIRE(resolve_filament_for_head(plate_map, pem, 2) == -1);
}

TEST_CASE("resolve_filament_for_head - override past the filament count is ignored", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    REQUIRE(resolve_filament_for_head({{1, 7}}, pem, 1) == 6);

    REQUIRE(resolve_filament_for_head({{1, 8}},    pem, 1) == 4);
    REQUIRE(resolve_filament_for_head({{1, 9999}}, pem, 1) == 4);

    REQUIRE(resolve_filament_for_head({{5, 9999}}, pem, 5) == -1);
}

TEST_CASE("imex_primary_tool_for_mode - role marker authoritative", "[IMEX]") {
    REQUIRE(imex_primary_tool_for_mode("0:P,1:C,2:C") == 0);
    REQUIRE(imex_primary_tool_for_mode("1:C,0:P,2:M") == 0);
    REQUIRE(imex_primary_tool_for_mode("1:C,2:M,3:P") == 3);
}

TEST_CASE("imex_primary_tool_for_mode - backwards-compat plain index", "[IMEX]") {
    REQUIRE(imex_primary_tool_for_mode("0") == 0);
    REQUIRE(imex_primary_tool_for_mode("2") == 2);
    REQUIRE(imex_primary_tool_for_mode("1,2,3") == 1);
}

TEST_CASE("imex_primary_tool_for_mode - explicit :P beats bare index", "[IMEX]") {
    REQUIRE(imex_primary_tool_for_mode("1,2:P,3") == 2);
}

TEST_CASE("imex_primary_tool_for_mode - empty + malformed", "[IMEX]") {
    REQUIRE(imex_primary_tool_for_mode("") == -1);
    REQUIRE(imex_primary_tool_for_mode(",,") == -1);
    REQUIRE(imex_primary_tool_for_mode("abc:P") == -1);
    REQUIRE(imex_primary_tool_for_mode("-1:P") == -1);
}

TEST_CASE("imex_primary_tool_for_mode - whitespace tolerant", "[IMEX]") {
    REQUIRE(imex_primary_tool_for_mode("  0 : P , 1 : C ") == 0);
}

TEST_CASE("has_non_primary_mmu - MMU on primary only", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 4, 5, 6});
    REQUIRE_FALSE(has_non_primary_mmu(pem, 0));
}

TEST_CASE("has_non_primary_mmu - MMU on secondary", "[IMEX]") {
    auto pem = make_pem({0, 1, 1, 1});
    REQUIRE(has_non_primary_mmu(pem, 0));
}

TEST_CASE("has_non_primary_mmu - pure IDEX", "[IMEX]") {
    REQUIRE_FALSE(has_non_primary_mmu(make_pem({0, 1}), 0));
    REQUIRE_FALSE(has_non_primary_mmu(make_pem({0, 1, 2, 3}), 0));
}

TEST_CASE("has_non_primary_mmu - secondary single-lane", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 4});
    REQUIRE(has_non_primary_mmu(pem, 4));
}

TEST_CASE("has_non_primary_mmu - empty / single-entry pem", "[IMEX]") {
    REQUIRE_FALSE(has_non_primary_mmu(make_pem({}), 0));
    REQUIRE_FALSE(has_non_primary_mmu(make_pem({0}), 0));
}

TEST_CASE("imex_head_transform - copy mode is pure translation", "[IMEX]") {
    const Vec2d offset{120.0, 0.0};
    Transform3d xf = imex_head_transform(0, 1, ImexRole::Copy, offset, Vec2d::Zero(), ImexMirrorAxis::X);
    const Vec3d in{10.0, 20.0, 30.0};
    const Vec3d out = xf * in;
    REQUIRE_THAT(out.x(), WithinAbs(130.0, 1e-9));
    REQUIRE_THAT(out.y(), WithinAbs(20.0,  1e-9));
    REQUIRE_THAT(out.z(), WithinAbs(30.0,  1e-9));
}

TEST_CASE("imex_head_transform - mirror at origin places ghost at gantry offset", "[IMEX]") {
    const Vec2d offset{120.0, 0.0};
    Transform3d xf = imex_head_transform(0, 1, ImexRole::Mirror, offset, Vec2d::Zero(), ImexMirrorAxis::X);
    const Vec3d mapped = xf * Vec3d::Zero();
    REQUIRE_THAT(mapped.x(), WithinAbs(120.0, 1e-9));
    REQUIRE_THAT(mapped.y(), WithinAbs(0.0,   1e-9));
    REQUIRE_THAT(mapped.z(), WithinAbs(0.0,   1e-9));
}

TEST_CASE("imex_head_transform - mirror lands ghost at reflected position in target zone", "[IMEX]") {
    const Vec2d offset{100.0, 0.0};
    const Vec2d primary_zone_center{50.0, 50.0};
    Transform3d xf = imex_head_transform(0, 1, ImexRole::Mirror, offset, primary_zone_center, ImexMirrorAxis::X);

    const Vec3d primary{80.0, 20.0, 0.0};
    const Vec3d ghost_origin = xf * primary;
    REQUIRE_THAT(ghost_origin.x(), WithinAbs(120.0, 1e-9));
    REQUIRE_THAT(ghost_origin.y(), WithinAbs(20.0,  1e-9));

    const Vec3d mapped = xf * (primary + Vec3d(5.0, 0.0, 0.0));
    REQUIRE_THAT(mapped.x(), WithinAbs(115.0, 1e-9));
    REQUIRE_THAT(mapped.y(), WithinAbs(20.0,  1e-9));
}

TEST_CASE("imex_head_transform - mirror reflects primary drag motion", "[IMEX]") {
    const Vec2d offset{120.0, 0.0};
    const Vec2d primary_zone_center{60.0, 50.0};
    Transform3d xf = imex_head_transform(0, 1, ImexRole::Mirror, offset, primary_zone_center, ImexMirrorAxis::X);

    const Vec3d p0{10.0, 20.0, 0.0};
    const Vec3d p1{40.0, 15.0, 0.0};
    const Vec3d ghost0 = xf * p0;
    const Vec3d ghost1 = xf * p1;
    const Vec3d ghost_delta   = ghost1 - ghost0;
    const Vec3d primary_delta = p1 - p0;

    REQUIRE_THAT(ghost_delta.x(), WithinAbs(-primary_delta.x(), 1e-9));
    REQUIRE_THAT(ghost_delta.y(), WithinAbs( primary_delta.y(), 1e-9));
    REQUIRE_THAT(ghost_delta.z(), WithinAbs( primary_delta.z(), 1e-9));
}

TEST_CASE("imex_head_transform - mirror reflects model point across primary origin", "[IMEX]") {
    const Vec2d offset{120.0, 0.0};
    Transform3d xf = imex_head_transform(0, 1, ImexRole::Mirror, offset, Vec2d::Zero(), ImexMirrorAxis::X);
    const Vec3d in{5.0, 7.0, 0.0};
    const Vec3d out = xf * in;
    REQUIRE_THAT(out.x(), WithinAbs(115.0, 1e-9));
    REQUIRE_THAT(out.y(), WithinAbs(7.0,   1e-9));
}

TEST_CASE("imex_head_transform - mirror axis comes from the caller, not the offset direction", "[IMEX]") {
    const Vec2d offset{0.0, 80.0};
    const Vec3d in{3.0, 10.0, 0.0};

    const Vec3d as_x = imex_head_transform(0, 1, ImexRole::Mirror, offset, Vec2d::Zero(),
                                           ImexMirrorAxis::X) * in;
    REQUIRE_THAT(as_x.x(), WithinAbs(-3.0, 1e-9));
    REQUIRE_THAT(as_x.y(), WithinAbs(90.0, 1e-9));

    const Vec3d as_y = imex_head_transform(0, 1, ImexRole::Mirror, offset, Vec2d::Zero(),
                                           ImexMirrorAxis::Y) * in;
    REQUIRE_THAT(as_y.x(), WithinAbs(3.0,  1e-9));
    REQUIRE_THAT(as_y.y(), WithinAbs(70.0, 1e-9));
}

TEST_CASE("imex_head_transform - cross-gantry mirror reflects Y and tracks X", "[IMEX]") {
    const Vec2d offset{0.0, -100.0};
    const Vec2d primary_zone_center{50.0, 150.0};
    Transform3d xf = imex_head_transform(0, 2, ImexRole::Mirror, offset, primary_zone_center,
                                         ImexMirrorAxis::Y);

    const Vec3d primary{20.0, 130.0, 0.0};
    const Vec3d ghost_origin = xf * primary;
    REQUIRE_THAT(ghost_origin.x(), WithinAbs(20.0, 1e-9));
    REQUIRE_THAT(ghost_origin.y(), WithinAbs(70.0, 1e-9));

    const Vec3d mapped = xf * (primary + Vec3d(0.0, 5.0, 0.0));
    REQUIRE_THAT(mapped.y(), WithinAbs(65.0, 1e-9));

    const Vec3d d = (xf * (primary + Vec3d(7.0, 3.0, 0.0))) - ghost_origin;
    REQUIRE_THAT(d.x(), WithinAbs( 7.0, 1e-9));
    REQUIRE_THAT(d.y(), WithinAbs(-3.0, 1e-9));
}

TEST_CASE("imex_head_transform - diagonal cross-gantry mirror translates X, reflects Y", "[IMEX]") {
    const Vec2d offset{200.0, -100.0};
    const Vec2d primary_zone_center{100.0, 150.0};
    Transform3d xf = imex_head_transform(0, 3, ImexRole::Mirror, offset, primary_zone_center,
                                         ImexMirrorAxis::Y);

    const Vec3d primary{40.0, 130.0, 0.0};
    const Vec3d ghost = xf * primary;
    REQUIRE_THAT(ghost.x(), WithinAbs(240.0, 1e-9));
    REQUIRE_THAT(ghost.y(), WithinAbs(70.0,  1e-9));
}

TEST_CASE("imex_head_transform - mirror is a reflection, not a rotation, on both axes", "[IMEX]") {
    const Vec2d offset{120.0, -80.0};
    for (ImexMirrorAxis axis : {ImexMirrorAxis::X, ImexMirrorAxis::Y}) {
        Transform3d xf = imex_head_transform(0, 1, ImexRole::Mirror, offset, Vec2d{10.0, 20.0}, axis);
        REQUIRE_THAT(xf.linear().determinant(), WithinAbs(-1.0, 1e-9));
    }
}

TEST_CASE("imex_head_transform - primary is identity", "[IMEX]") {
    const Vec2d offset{120.0, 30.0};
    Transform3d xf = imex_head_transform(0, 0, ImexRole::Primary, offset, Vec2d::Zero(), ImexMirrorAxis::X);
    REQUIRE(xf.isApprox(Transform3d::Identity()));
}

TEST_CASE("imex_head_transform - span is identity like primary", "[IMEX]") {
    const Vec2d offset{120.0, 30.0};
    Transform3d xf = imex_head_transform(0, 1, ImexRole::Span, offset, Vec2d{50.0, 50.0},
                                         ImexMirrorAxis::X);
    REQUIRE(xf.isApprox(Transform3d::Identity()));

    Transform3d xf_y = imex_head_transform(0, 2, ImexRole::Span, offset, Vec2d{50.0, 50.0},
                                           ImexMirrorAxis::Y);
    REQUIRE(xf_y.isApprox(Transform3d::Identity()));
}

TEST_CASE("imex_head_transform - mirror with zero offset is identity", "[IMEX]") {
    const Vec2d offset{0.0, 0.0};
    Transform3d xf = imex_head_transform(0, 1, ImexRole::Mirror, offset, Vec2d::Zero(), ImexMirrorAxis::X);
    REQUIRE(xf.isApprox(Transform3d::Identity()));
}

TEST_CASE("imex_head_transform - mirror on 2x2 off-row target reflects Y, not X", "[IMEX]") {
    const Vec2d offset{100.0, 100.0};
    const ImexMirrorAxis axis = imex_mirror_axis_for( 0, 3, 2);
    REQUIRE(axis == ImexMirrorAxis::Y);

    Transform3d xf = imex_head_transform(0, 3, ImexRole::Mirror, offset, Vec2d::Zero(), axis);

    const Vec3d mapped = xf * Vec3d::Zero();
    REQUIRE_THAT(mapped.x(), WithinAbs(100.0, 1e-9));
    REQUIRE_THAT(mapped.y(), WithinAbs(100.0, 1e-9));
    REQUIRE_THAT(mapped.z(), WithinAbs(0.0,   1e-9));

    const Vec3d out = xf * Vec3d{5.0, 7.0, 0.0};
    REQUIRE_THAT(out.x(), WithinAbs(105.0, 1e-9));
    REQUIRE_THAT(out.y(), WithinAbs(93.0,  1e-9));
}

TEST_CASE("imex_mirror_axis_for - axis follows the gantry row", "[IMEX]") {
    REQUIRE(imex_mirror_axis_for(0, 1, 2) == ImexMirrorAxis::X);
    REQUIRE(imex_mirror_axis_for(0, 2, 2) == ImexMirrorAxis::Y);
    REQUIRE(imex_mirror_axis_for(0, 3, 2) == ImexMirrorAxis::Y);
    REQUIRE(imex_mirror_axis_for(2, 3, 2) == ImexMirrorAxis::X);
    REQUIRE(imex_mirror_axis_for(0, 1, 4) == ImexMirrorAxis::X);
    REQUIRE(imex_mirror_axis_for(0, 3, 4) == ImexMirrorAxis::X);

    REQUIRE(imex_mirror_axis_for(0, 1, 1) == ImexMirrorAxis::Y);

    REQUIRE(imex_mirror_axis_for(0, 1, 0)  == ImexMirrorAxis::Y);
    REQUIRE(imex_mirror_axis_for(0, 1, -3) == ImexMirrorAxis::Y);
    REQUIRE(imex_mirror_axis_for(0, 0, 0)  == ImexMirrorAxis::X);
}

TEST_CASE("resolve_filament_for_head - no routing returns -1 (ghost color fallback)", "[IMEX]") {
    auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    std::map<int,int> no_override;
    REQUIRE(resolve_filament_for_head(no_override, pem, 5) == -1);

    std::map<int,int> override_on_5 = {{5, 7}};
    REQUIRE(resolve_filament_for_head(override_on_5, pem, 5) == 6);
}

TEST_CASE("imex_role_letter - the historical letters are pinned", "[IMEX]") {
    REQUIRE(imex_role_letter(ImexRole::Primary) == 'P');
    REQUIRE(imex_role_letter(ImexRole::Copy)    == 'C');
    REQUIRE(imex_role_letter(ImexRole::Mirror)  == 'M');
    REQUIRE(imex_role_letter(ImexRole::Span)    == 'S');
}

TEST_CASE("the role table gives every role its own letter", "[IMEX]") {
    std::set<char>     letters;
    std::set<ImexRole> roles;
    for (const ImexRoleDesc& d : kImexRoleTable) {
        INFO("role table row with letter '" << d.letter << "'");
        CHECK(letters.insert(d.letter).second);
        CHECK(roles.insert(d.role).second);
    }
    REQUIRE(letters.size() == std::size(kImexRoleTable));
}

TEST_CASE("imex_role_from_suffix - the historical letters are pinned", "[IMEX]") {
    REQUIRE(imex_role_from_suffix("P") == ImexRole::Primary);
    REQUIRE(imex_role_from_suffix("C") == ImexRole::Copy);
    REQUIRE(imex_role_from_suffix("M") == ImexRole::Mirror);
    REQUIRE(imex_role_from_suffix("S") == ImexRole::Span);
}

TEST_CASE("imex_role_from_suffix - anything unrecognised stays Copy", "[IMEX]") {
    REQUIRE(imex_role_from_suffix("")   == ImexRole::Copy);
    REQUIRE(imex_role_from_suffix("X")  == ImexRole::Copy);
    REQUIRE(imex_role_from_suffix("p")  == ImexRole::Copy);
    REQUIRE(imex_role_from_suffix("PP") == ImexRole::Copy);
}

TEST_CASE("imex_primary_tool_for_mode - only an exact P suffix names the primary", "[IMEX]") {
    REQUIRE(imex_primary_tool_for_mode("0:C,1:P,2:M") == 1);
    REQUIRE(imex_primary_tool_for_mode("0:PP,1:M")    == -1);
    REQUIRE(imex_primary_tool_for_mode("0:p,1:M")     == -1);
    REQUIRE(imex_primary_tool_for_mode("0,1,2")       == 0);
}

TEST_CASE("parse_imex_active_tools - Span role parsed from S suffix", "[IMEX]") {
    auto out = parse_imex_active_tools("0:P,1:S,2:M,3:M");
    REQUIRE(out.size() == 4);
    REQUIRE(out[0].first == 0);
    REQUIRE(out[0].second == ImexRole::Primary);
    REQUIRE(out[1].first == 1);
    REQUIRE(out[1].second == ImexRole::Span);
    REQUIRE(out[2].first == 2);
    REQUIRE(out[2].second == ImexRole::Mirror);
    REQUIRE(out[3].first == 3);
    REQUIRE(out[3].second == ImexRole::Mirror);
}

TEST_CASE("parse_imex_active_tools - Span suffix whitespace tolerant", "[IMEX]") {
    auto out = parse_imex_active_tools("  0 : P , 1 : S ");
    REQUIRE(out.size() == 2);
    REQUIRE(out[1].second == ImexRole::Span);
}

TEST_CASE("A mode's icon kind follows the roles of the heads beside its primary", "[IMEX]") {
    const auto [roster, kind] = GENERATE(table<std::string, ImexModeKind>({
        { "",                ImexModeKind::Primary },
        { "0:P",             ImexModeKind::Primary },
        { "0",               ImexModeKind::Primary },
        { "0:P,0:C",         ImexModeKind::Primary },
        { "0:P,1:C",         ImexModeKind::Copy    },
        { "0:P,1:C,2:C,3:C", ImexModeKind::Copy    },
        { "0,1,2",           ImexModeKind::Copy    },
        { "0:P,1:M",         ImexModeKind::Mirror  },
        { "0:P,1:C,2:M,3:M", ImexModeKind::Mirror  },
        { "0,1:M",           ImexModeKind::Mirror  },
        { "0:P,1:S,2:C,3:C", ImexModeKind::Custom  },
        { "0:P,1:S,2:M,3:M", ImexModeKind::Custom  },
        { "0:P,1:S",         ImexModeKind::Custom  },
    }));
    CAPTURE(roster);
    REQUIRE(imex_mode_kind(roster) == kind);
}

TEST_CASE("group_imex_active_tools_by_gantry - paired-gantry mc-mirror aggregates", "[IMEX]") {
    auto g = group_imex_active_tools_by_gantry("0:P,1:S,2:M,3:M", 2);
    REQUIRE(g.primary_phys == 0);
    REQUIRE(g.primary_gantry == 0);
    REQUIRE(g.span_on_primary);
    REQUIRE(g.groups.size() == 2);

    REQUIRE(g.groups[0].gantry_index == 0);
    REQUIRE_FALSE(g.groups[0].aggregate);
    REQUIRE(g.groups[0].tools.size() == 2);

    REQUIRE(g.groups[1].gantry_index == 1);
    REQUIRE(g.groups[1].aggregate);
    REQUIRE(g.groups[1].representative_phys == 2);
    REQUIRE(g.groups[1].representative_role == ImexRole::Mirror);
}

TEST_CASE("group_imex_active_tools_by_gantry - 4 independent copies (no Span) stay per-tool", "[IMEX]") {
    auto g = group_imex_active_tools_by_gantry("0:P,1:C,2:M,3:M", 2);
    REQUIRE_FALSE(g.span_on_primary);
    REQUIRE(g.groups.size() == 2);
    REQUIRE_FALSE(g.groups[0].aggregate);
    REQUIRE_FALSE(g.groups[1].aggregate);
}

TEST_CASE("group_imex_active_tools_by_gantry - non-primary gantry with single tool stays per-tool", "[IMEX]") {
    auto g = group_imex_active_tools_by_gantry("0:P,1:S,2:M", 2);
    REQUIRE(g.span_on_primary);
    REQUIRE(g.groups.size() == 2);
    REQUIRE(g.groups[1].gantry_index == 1);
    REQUIRE(g.groups[1].tools.size() == 1);
    REQUIRE_FALSE(g.groups[1].aggregate);
}

TEST_CASE("group_imex_active_tools_by_gantry - mixed-role non-primary gantry falls back", "[IMEX]") {
    auto g = group_imex_active_tools_by_gantry("0:P,1:S,2:C,3:M", 2);
    REQUIRE(g.span_on_primary);
    REQUIRE(g.groups.size() == 2);
    REQUIRE_FALSE(g.groups[1].aggregate);
}

TEST_CASE("group_imex_active_tools_by_gantry - IDEX (tpg=1) never aggregates", "[IMEX]") {
    auto g = group_imex_active_tools_by_gantry("0:P,1:M", 1);
    REQUIRE_FALSE(g.span_on_primary);
    REQUIRE(g.groups.size() == 2);
    REQUIRE_FALSE(g.groups[0].aggregate);
    REQUIRE_FALSE(g.groups[1].aggregate);
}

TEST_CASE("group_imex_active_tools_by_gantry - empty / no primary returns empty grouping", "[IMEX]") {
    auto g = group_imex_active_tools_by_gantry("", 2);
    REQUIRE(g.primary_phys == -1);
    REQUIRE(g.groups.empty());
}

TEST_CASE("group_imex_active_tools_by_gantry - column pairing picks correct representative", "[IMEX]") {
    auto g = group_imex_active_tools_by_gantry("0:S,1:P,2:M,3:M", 2);
    REQUIRE(g.primary_phys == 1);
    REQUIRE(g.span_on_primary);
    REQUIRE(g.groups.size() == 2);
    REQUIRE(g.groups[1].gantry_index == 1);
    REQUIRE(g.groups[1].representative_phys == 3);
}

static const BoundingBoxf kPrimaryZoneCenteredAt60_40{
    Vec2d(10.0, 5.0), Vec2d(110.0, 75.0)};

TEST_CASE("compute_imex_slice_offset - flag off returns zero regardless of mode", "[IMEX]") {
    auto z = compute_imex_slice_offset(false, "copy",    kPrimaryZoneCenteredAt60_40);
    REQUIRE_THAT(z.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(z.y(), WithinAbs(0.0, 1e-9));

    z = compute_imex_slice_offset(false, "mirror",  kPrimaryZoneCenteredAt60_40);
    REQUIRE_THAT(z.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(z.y(), WithinAbs(0.0, 1e-9));

    z = compute_imex_slice_offset(false, "primary", kPrimaryZoneCenteredAt60_40);
    REQUIRE_THAT(z.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(z.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("compute_imex_slice_offset - primary mode returns zero even with flag on", "[IMEX]") {
    auto z = compute_imex_slice_offset(true, "primary", kPrimaryZoneCenteredAt60_40);
    REQUIRE_THAT(z.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(z.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("compute_imex_slice_offset - empty mode returns zero even with flag on", "[IMEX]") {
    auto z = compute_imex_slice_offset(true, "", kPrimaryZoneCenteredAt60_40);
    REQUIRE_THAT(z.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(z.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("compute_imex_slice_offset - empty zone box returns zero even with flag on and copy mode", "[IMEX]") {
    auto z = compute_imex_slice_offset(true, "copy", std::nullopt);
    REQUIRE_THAT(z.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(z.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("compute_imex_slice_offset - flag on + copy mode returns primary zone center", "[IMEX]") {
    auto z = compute_imex_slice_offset(true, "copy", kPrimaryZoneCenteredAt60_40);
    REQUIRE_THAT(z.x(), WithinAbs(60.0, 1e-9));
    REQUIRE_THAT(z.y(), WithinAbs(40.0, 1e-9));
}

TEST_CASE("compute_imex_slice_offset - flag on + mirror mode returns primary zone center", "[IMEX]") {
    auto z = compute_imex_slice_offset(true, "mirror", kPrimaryZoneCenteredAt60_40);
    REQUIRE_THAT(z.x(), WithinAbs(60.0, 1e-9));
    REQUIRE_THAT(z.y(), WithinAbs(40.0, 1e-9));
}

TEST_CASE("compute_imex_slice_offset - user-defined non-primary mode names trigger shift", "[IMEX]") {
    auto z = compute_imex_slice_offset(true, "iq-copy", kPrimaryZoneCenteredAt60_40);
    REQUIRE_THAT(z.x(), WithinAbs(60.0, 1e-9));
    REQUIRE_THAT(z.y(), WithinAbs(40.0, 1e-9));
}

TEST_CASE("compute_imex_slice_offset - center value tracks zone box position", "[IMEX]") {
    BoundingBoxf right_half{Vec2d(0.0, -75.0), Vec2d(160.0, 75.0)};
    auto z = compute_imex_slice_offset(true, "copy", right_half);
    REQUIRE_THAT(z.x(), WithinAbs(80.0, 1e-9));
    REQUIRE_THAT(z.y(), WithinAbs(0.0, 1e-9));

    BoundingBoxf left_half{Vec2d(-160.0, -75.0), Vec2d(0.0, 75.0)};
    z = compute_imex_slice_offset(true, "mirror", left_half);
    REQUIRE_THAT(z.x(), WithinAbs(-80.0, 1e-9));
    REQUIRE_THAT(z.y(), WithinAbs(0.0, 1e-9));
}


static Polygon scaled_rect(double x0, double y0, double x1, double y1) {
    Polygon p;
    p.points = {Point(scaled(x0), scaled(y0)), Point(scaled(x1), scaled(y0)),
                Point(scaled(x1), scaled(y1)), Point(scaled(x0), scaled(y1))};
    return p;
}

static BoundingBoxf3 zone(double x0, double y0, double x1, double y1) {
    return BoundingBoxf3(Vec3d(x0, y0, 0.0), Vec3d(x1, y1, 1.0));
}

TEST_CASE("imex_hull_violates_zones - empty hull never violates", "[IMEX]") {
    std::vector<BoundingBoxf3> zones{zone(0, 0, 100, 100)};
    REQUIRE_FALSE(imex_hull_violates_zones(zones, Polygon()));
}

TEST_CASE("imex_hull_violates_zones - empty zone list never violates", "[IMEX]") {
    REQUIRE_FALSE(imex_hull_violates_zones({}, scaled_rect(10, 10, 20, 20)));
}

TEST_CASE("imex_hull_violates_zones - hull fully inside a zone violates", "[IMEX]") {
    std::vector<BoundingBoxf3> zones{zone(0, 0, 100, 100)};
    REQUIRE(imex_hull_violates_zones(zones, scaled_rect(10, 10, 20, 20)));
}

TEST_CASE("imex_hull_violates_zones - hull straddling a zone edge violates", "[IMEX]") {
    std::vector<BoundingBoxf3> zones{zone(100, 0, 200, 100)};
    REQUIRE(imex_hull_violates_zones(zones, scaled_rect(90, 10, 110, 20)));
}

TEST_CASE("imex_hull_violates_zones - hull enclosing a zone violates", "[IMEX]") {
    std::vector<BoundingBoxf3> zones{zone(45, 45, 55, 55)};
    REQUIRE(imex_hull_violates_zones(zones, scaled_rect(0, 0, 100, 100)));
}

TEST_CASE("imex_hull_violates_zones - disjoint hull does not violate", "[IMEX]") {
    std::vector<BoundingBoxf3> zones{zone(100, 0, 200, 100)};
    REQUIRE_FALSE(imex_hull_violates_zones(zones, scaled_rect(0, 0, 50, 50)));
}

TEST_CASE("imex_hull_violates_zones - edge-flush hull does not violate", "[IMEX]") {
    std::vector<BoundingBoxf3> zones{zone(100, 0, 200, 100)};
    REQUIRE_FALSE(imex_hull_violates_zones(zones, scaled_rect(0, 0, 100, 100)));
}

TEST_CASE("imex_hull_violates_zones - violating any one of several zones is enough", "[IMEX]") {
    std::vector<BoundingBoxf3> zones{zone(0, 0, 10, 10), zone(20, 20, 30, 30), zone(40, 40, 50, 50)};
    REQUIRE(imex_hull_violates_zones(zones, scaled_rect(45, 45, 60, 60)));
    REQUIRE_FALSE(imex_hull_violates_zones(zones, scaled_rect(12, 12, 18, 18)));
}

namespace {

DynamicPrintConfig mode_cfg(const std::vector<std::string>& names,
                            const std::vector<std::string>& tools,
                            const std::vector<std::string>& gcodes)
{
    DynamicPrintConfig cfg;
    if (!names.empty())  cfg.set_key_value("imex_mode_names",        new ConfigOptionStrings(names));
    if (!tools.empty())  cfg.set_key_value("imex_mode_active_tools", new ConfigOptionStrings(tools));
    if (!gcodes.empty()) cfg.set_key_value("imex_mode_gcodes",       new ConfigOptionStrings(gcodes));
    return cfg;
}

}

TEST_CASE("find_imex_mode - an exact name yields that row's index, tools and script", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "copy", "iq-copy" },
                                            { "0:P", "0:P,1:C", "0:P,1:C,2:C,3:C" },
                                            { "", "COPY_SCRIPT", "QUAD_SCRIPT" });

    const ImexMode m = find_imex_mode(cfg, "copy");
    REQUIRE(m.found());
    CHECK(m.index == 1);
    CHECK(m.name == "copy");
    CHECK(m.active_tools == "0:P,1:C");
    CHECK(m.gcode == "COPY_SCRIPT");
    CHECK_FALSE(m.ragged);

    const ImexMode last = find_imex_mode(cfg, "iq-copy");
    REQUIRE(last.found());
    CHECK(last.index == 2);
    CHECK(last.active_tools == "0:P,1:C,2:C,3:C");
    CHECK(last.gcode == "QUAD_SCRIPT");
}

TEST_CASE("find_imex_mode - a name no row carries is not found", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "copy" },
                                            { "0:P", "0:P,1:C" },
                                            { "", "COPY_SCRIPT" });

    const ImexMode m = find_imex_mode(cfg, "copy-renamed");
    CHECK_FALSE(m.found());
    CHECK(m.index == -1);
    CHECK(m.name.empty());
    CHECK(m.active_tools.empty());
    CHECK(m.gcode.empty());
    CHECK_FALSE(m.ragged);
    CHECK_FALSE(static_cast<bool>(m));
}

TEST_CASE("find_imex_mode - a short active-tools array pads and reports ragged", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "copy" },
                                            { "0:P" },
                                            { "", "COPY_SCRIPT" });

    const ImexMode m = find_imex_mode(cfg, "copy");
    REQUIRE(m.found());
    CHECK(m.index == 1);
    CHECK(m.active_tools.empty());
    CHECK(m.gcode == "COPY_SCRIPT");
    CHECK(m.ragged);

    CHECK_FALSE(find_imex_mode(cfg, kImexPrimaryMode).ragged);
}

TEST_CASE("find_imex_mode - a short gcodes array pads and reports ragged", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "copy" },
                                            { "0:P", "0:P,1:C" },
                                            { "" });

    const ImexMode m = find_imex_mode(cfg, "copy");
    REQUIRE(m.found());
    CHECK(m.index == 1);
    CHECK(m.active_tools == "0:P,1:C");
    CHECK(m.gcode.empty());
    CHECK(m.ragged);
}

TEST_CASE("find_imex_mode - a missing sibling option is padding, not a failed lookup", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "copy" }, {}, {});

    const ImexMode m = find_imex_mode(cfg, "copy");
    REQUIRE(m.found());
    CHECK(m.index == 1);
    CHECK(m.active_tools.empty());
    CHECK(m.gcode.empty());
    CHECK(m.ragged);
}

TEST_CASE("find_imex_mode - a config with no mode table finds nothing", "[IMEX]") {
    const DynamicPrintConfig empty;
    CHECK_FALSE(find_imex_mode(empty, "copy").found());
    CHECK_FALSE(find_imex_mode(empty, kImexPrimaryMode).found());
    CHECK(imex_mode_table(empty).empty());

    const DynamicPrintConfig no_names = mode_cfg({}, { "0:P,1:C" }, { "COPY_SCRIPT" });
    CHECK_FALSE(find_imex_mode(no_names, "copy").found());
    CHECK(imex_mode_table(no_names).empty());
}

TEST_CASE("find_imex_mode - a duplicated mode name resolves to the first row", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "copy", "copy" },
                                            { "0:P", "0:P,1:C", "0:P,1:M" },
                                            { "", "FIRST", "SECOND" });

    const ImexMode m = find_imex_mode(cfg, "copy");
    REQUIRE(m.found());
    CHECK(m.index == 1);
    CHECK(m.active_tools == "0:P,1:C");
    CHECK(m.gcode == "FIRST");

    const DynamicPrintConfig ragged = mode_cfg({ kImexPrimaryMode, "copy", "copy" },
                                               { "0:P" },
                                               { "", "FIRST", "SECOND" });
    const ImexMode rm = find_imex_mode(ragged, "copy");
    REQUIRE(rm.found());
    CHECK(rm.index == 1);
    CHECK(rm.active_tools.empty());
    CHECK(rm.gcode == "FIRST");
    CHECK(rm.ragged);
}

TEST_CASE("imex_mode_table - one row per name, in config order, padded the same way", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "copy", "mirror" },
                                            { "0:P", "0:P,1:C" },
                                            { "", "COPY_SCRIPT", "MIRROR_SCRIPT", "EXTRA" });

    const std::vector<ImexMode> table = imex_mode_table(cfg);
    REQUIRE(table.size() == 3);

    CHECK(table[0].index == 0);
    CHECK(table[0].name == kImexPrimaryMode);
    CHECK(table[0].active_tools == "0:P");
    CHECK_FALSE(table[0].ragged);

    CHECK(table[1].index == 1);
    CHECK(table[1].name == "copy");
    CHECK(table[1].active_tools == "0:P,1:C");
    CHECK(table[1].gcode == "COPY_SCRIPT");
    CHECK_FALSE(table[1].ragged);

    CHECK(table[2].index == 2);
    CHECK(table[2].name == "mirror");
    CHECK(table[2].active_tools.empty());
    CHECK(table[2].gcode == "MIRROR_SCRIPT");
    CHECK(table[2].ragged);

    for (const ImexMode& m : table)
        CHECK(m.gcode != "EXTRA");
}

TEST_CASE("imex_mode_table - every row agrees with find_imex_mode on that name", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "copy", "iq-copy" },
                                            { "0:P", "0:P,1:C" },
                                            { "", "COPY_SCRIPT" });

    for (const ImexMode& row : imex_mode_table(cfg)) {
        DYNAMIC_SECTION("row " << row.index << " (" << row.name << ")") {
            const ImexMode found = find_imex_mode(cfg, row.name);
            CHECK(found.index == row.index);
            CHECK(found.active_tools == row.active_tools);
            CHECK(found.gcode == row.gcode);
            CHECK(found.ragged == row.ragged);
        }
    }
}

TEST_CASE("A plate offers Primary then each mode name once in table order", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ "copy", "mirror", "copy", "", "primary", "iq-copy" }, {}, {});
    REQUIRE(imex_plate_mode_choices(cfg) == std::vector<std::string>{ kImexPrimaryMode, "copy", "mirror", "iq-copy" });
    REQUIRE(imex_plate_mode_choices(mode_cfg({}, {}, {})) == std::vector<std::string>{ kImexPrimaryMode });
}

TEST_CASE("imex_resolve_routing - a primary mode derives nothing", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "copy" }, { "0:P", "0:P,1:C" }, { "", "S" });
    const auto pem = make_pem({0, 1});

    for (const std::string& mode : { std::string(kImexPrimaryMode), std::string() }) {
        DYNAMIC_SECTION("mode '" << mode << "'") {
            const ImexRouting r = imex_resolve_routing(cfg, mode, { 1 }, pem);
            CHECK_FALSE(r.parallel);
            CHECK(r.active_tools.empty());
            CHECK(r.tools.empty());
            CHECK(r.primary_phys == -1);
            CHECK(r.primary_logical == -1);
            CHECK_FALSE(r.routes_to_primary());
            CHECK_FALSE(r.primary_unrouted);
            CHECK(r.routed_heads.empty());
        }
    }
}

TEST_CASE("imex_resolve_routing - plain IDEX copy mode routes its single filament", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "copy" }, { "0:P", "0:P,1:C" }, { "", "S" });
    const ImexRouting r = imex_resolve_routing(cfg, "copy", { 1 }, make_pem({0, 1}));

    CHECK(r.parallel);
    CHECK(r.active_tools == "0:P,1:C");
    REQUIRE(r.tools.size() == 2);
    CHECK(r.tools[0].first == 0);
    CHECK(r.tools[0].second == ImexRole::Primary);
    CHECK(r.tools[1].first == 1);
    CHECK(r.tools[1].second == ImexRole::Copy);
    CHECK(r.primary_phys == 0);
    CHECK(r.primary_logical == 0);
    CHECK(r.routes_to_primary());
    CHECK_FALSE(r.primary_unrouted);
    CHECK(r.routed_heads == std::vector<int>{0});
}

TEST_CASE("imex_resolve_routing - a plate whose filament misses the primary is unrouted", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "copy" }, { "0:P", "0:P,1:C" }, { "", "S" });
    const ImexRouting r = imex_resolve_routing(cfg, "copy", { 2 }, make_pem({0, 1}));

    CHECK(r.primary_phys == 0);
    CHECK(r.primary_logical == -1);
    CHECK_FALSE(r.routes_to_primary());
    CHECK(r.primary_unrouted);
    CHECK(r.routed_heads == std::vector<int>{1});
}

TEST_CASE("imex_resolve_routing - AFC manifold: primary is a PHYSICAL index, not a logical slot", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "afc-copy" },
                                            { "0:P", "1:P,2:C,3:C" },
                                            { "", "S" });
    const auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});
    const ImexRouting r = imex_resolve_routing(cfg, "afc-copy", { 5 }, pem);

    CHECK(r.primary_phys == 1);
    CHECK(r.primary_logical == 4);
    CHECK(r.primary_logical != r.primary_phys);
    CHECK(r.routes_to_primary());
    CHECK_FALSE(r.primary_unrouted);
    CHECK(r.routed_heads == std::vector<int>{1});
}

TEST_CASE("imex_resolve_routing - AFC manifold: an AFC lane does not satisfy a direct primary", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "afc-copy" },
                                            { "0:P", "1:P,2:C,3:C" },
                                            { "", "S" });
    const ImexRouting r = imex_resolve_routing(cfg, "afc-copy", { 2 }, make_pem({0, 0, 0, 0, 1, 2, 3}));

    CHECK(r.primary_phys == 1);
    CHECK(r.primary_logical == -1);
    CHECK(r.primary_unrouted);
    CHECK(r.routed_heads == std::vector<int>{0});
}

TEST_CASE("imex_resolve_routing - AFC manifold: any lane on the primary head satisfies it", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "afc-copy" },
                                            { "0:P", "0:P,1:C,2:C,3:C" },
                                            { "", "S" });
    const auto pem = make_pem({0, 0, 0, 0, 1, 2, 3});

    CHECK(imex_resolve_routing(cfg, "afc-copy", { 3 }, pem).primary_logical == 2);
    CHECK(imex_resolve_routing(cfg, "afc-copy", { 4, 2 }, pem).primary_logical == 3);
    CHECK(imex_resolve_routing(cfg, "afc-copy", { 5, 2 }, pem).primary_logical == 1);
}

TEST_CASE("imex_resolve_routing - warning and block can never disagree about the primary", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "copy", "quad" },
                                            { "0:P", "0:P,1:C", "0:P,1:S,2:M,3:M" },
                                            { "", "S", "Q" });
    const std::vector<std::vector<int>> slot_sets = { {}, {1}, {2}, {3}, {1, 2}, {2, 3}, {4} };
    const std::vector<ConfigOptionInts> pems = { make_pem({0, 1}), make_pem({0, 0, 1, 2}), make_pem({1, 0}) };

    for (const std::string& mode : { std::string("copy"), std::string("quad") }) {
        for (size_t s = 0; s < slot_sets.size(); ++s) {
            for (size_t p = 0; p < pems.size(); ++p) {
                DYNAMIC_SECTION(mode << " slots#" << s << " pem#" << p) {
                    const ImexRouting r = imex_resolve_routing(cfg, mode, slot_sets[s], pems[p]);
                    REQUIRE(r.primary_phys >= 0);
                    REQUIRE_FALSE(pems[p].values.empty());
                    CHECK(r.primary_unrouted == !r.routes_to_primary());
                }
            }
        }
    }
}

TEST_CASE("imex_resolve_routing - an empty routing map is not an unrouted plate", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "copy" }, { "0:P", "0:P,1:C" }, { "", "S" });
    const ImexRouting r = imex_resolve_routing(cfg, "copy", { 1 }, ConfigOptionInts{});

    CHECK(r.primary_phys == 0);
    CHECK(r.primary_logical == -1);
    CHECK_FALSE(r.primary_unrouted);
    CHECK(r.routed_heads.empty());
}

TEST_CASE("imex_resolve_routing - a mode with no primary marker never blocks", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "broken" }, { "0:P", "0:C,1:C" }, { "", "S" });
    const ImexRouting r = imex_resolve_routing(cfg, "broken", { 2 }, make_pem({0, 1}));

    CHECK(r.parallel);
    CHECK(r.tools.size() == 2);
    CHECK(r.primary_phys == -1);
    CHECK(r.primary_logical == -1);
    CHECK_FALSE(r.primary_unrouted);
}

TEST_CASE("imex_resolve_routing - an unresolved or ragged mode yields an empty roster", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "copy", "ragged" },
                                            { "0:P", "0:P,1:C" },
                                            { "", "S", "R" });
    const auto pem = make_pem({0, 1});

    for (const std::string& mode : { std::string("copy-renamed"), std::string("ragged") }) {
        DYNAMIC_SECTION("mode " << mode) {
            const ImexRouting r = imex_resolve_routing(cfg, mode, { 2 }, pem);
            CHECK(r.parallel);
            CHECK(r.active_tools.empty());
            CHECK(r.tools.empty());
            CHECK(r.primary_phys == -1);
            CHECK_FALSE(r.primary_unrouted);
        }
    }
}

TEST_CASE("imex_resolve_routing - routed_heads is sorted, deduplicated and never clamps", "[IMEX]") {
    const DynamicPrintConfig cfg = mode_cfg({ kImexPrimaryMode, "quad" },
                                            { "0:P", "0:P,1:C,2:C,3:C" },
                                            { "", "Q" });
    const auto pem = make_pem({0, 0, 1, 2});

    const ImexRouting r = imex_resolve_routing(cfg, "quad", { 4, 2, 3, 9, 4 }, pem);
    CHECK(r.routed_heads == std::vector<int>{0, 1, 2});

    const ImexRouting all_out = imex_resolve_routing(cfg, "quad", { 9, 12 }, pem);
    CHECK(all_out.routed_heads.empty());
    CHECK(all_out.primary_unrouted);
}

TEST_CASE("imex_pem_tool_for - a filament id past the end of the map has no tool", "[IMEX]") {
    const auto pem = make_pem({0, 0, 1, 2});

    REQUIRE(imex_pem_tool_for(4, "copy_mode", pem) == -1);
    REQUIRE(imex_pem_tool_for(9, "copy_mode", pem) == -1);
    REQUIRE(imex_pem_tool_for(-1, "copy_mode", pem) == -1);
}

TEST_CASE("resolve_filament_for_head answers in nozzle index space, not filament slots", "[IMEX]") {
    const auto pem = make_pem({0, 1, 2, 3});
    REQUIRE(resolve_filament_for_head({}, pem, 3) == 3);
    REQUIRE(resolve_filament_for_head({}, pem, 2) == 2);

    REQUIRE(resolve_filament_for_head({}, pem, 9) == -1);
}

TEST_CASE("Enum printer keys read back the stored value, not their default", "[IMEX]")
{
    DynamicPrintConfig cfg = DynamicPrintConfig::full_print_config();

    cfg.set_deserialize_strict("imex_tool_layout", "rear-left");
    CHECK(int(imex_cfg_enum<ImexToolLayout>(cfg, "imex_tool_layout")) == int(ImexToolLayout::RearLeft));

    cfg.set_deserialize_strict("imex_viz_theme", "deuteranopia");
    CHECK(int(imex_cfg_enum<ImexVizTheme>(cfg, "imex_viz_theme")) == int(ImexVizTheme::Deuteranopia));

    DynamicPrintConfig bare;
    bare.set_deserialize_strict("imex_tool_layout", "rear-right");
    CHECK(int(imex_cfg_enum<ImexToolLayout>(bare, "imex_tool_layout")) == int(ImexToolLayout::RearRight));
}
