#include <boost/filesystem/operations.hpp>
#include <catch2/catch_all.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include "libslic3r/FilamentGroupUtils.hpp"
#include "libslic3r/MultiNozzleUtils.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode/ToolOrdering.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "test_utils.hpp"

#include <algorithm>
#include "libslic3r/Config.hpp"
#include "libslic3r/GCode/ToolOrderUtils.hpp"
#include "libslic3r/PrintBase.hpp"
#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

#include <boost/filesystem.hpp>
#include "libslic3r/Point.hpp"

using namespace Slic3r;
using namespace Slic3r::MultiNozzleUtils;

namespace {
std::vector<NozzleInfo> single_nozzle_per_extruder(int extruder_count)
{
    std::vector<NozzleInfo> nozzle_list;
    for (int e = 0; e < extruder_count; ++e) {
        NozzleInfo n;
        n.diameter    = "0.4";
        n.volume_type = nvtStandard;
        n.extruder_id = e;
        n.group_id    = e;
        nozzle_list.push_back(n);
    }
    return nozzle_list;
}
}

TEST_CASE("Multi-nozzle gate predicate mirrors BambuStudio", "[ToolOrdering][H2C]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();

    auto *opt = config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");
    REQUIRE(opt != nullptr);
    REQUIRE(config.option<ConfigOptionStrings>("extruder_nozzle_stats") != nullptr);

    auto has_multiple_nozzle = [](const std::vector<int> &values) {
        return std::any_of(values.begin(), values.end(), [](int v) { return v > 1; });
    };

    REQUIRE_FALSE(has_multiple_nozzle(opt->values));

    REQUIRE(has_multiple_nozzle(std::vector<int>{1, 6}));
}

TEST_CASE("Single-nozzle grouping: every filament maps to its extruder nozzle", "[ToolOrdering][H2C]")
{
    SECTION("single extruder => all filaments map to nozzle 0")
    {
        auto nozzle_list = single_nozzle_per_extruder(1);
        std::vector<int>          filament_nozzle_map = {0, 0, 0};
        std::vector<unsigned int> used_filaments      = {0, 1, 2};

        auto group_opt = LayeredNozzleGroupResult::create(filament_nozzle_map, nozzle_list, used_filaments);
        REQUIRE(group_opt.has_value());
        auto &group = *group_opt;

        for (int f = 0; f < 3; ++f) {
            REQUIRE(group.get_nozzle_id(f) == 0);
            REQUIRE(group.get_extruder_id(f) == 0);
            auto first = group.get_first_nozzle_for_filament(f);
            REQUIRE(first.has_value());
            REQUIRE(first->group_id == 0);
        }
        REQUIRE_FALSE(group.is_support_dynamic_nozzle_map());
    }

    SECTION("dual extruder => nozzle id equals the classic extruder grouping")
    {
        auto nozzle_list = single_nozzle_per_extruder(2);
        std::vector<int>          filament_map   = {0, 1, 0, 1};
        std::vector<unsigned int> used_filaments = {0, 1, 2, 3};

        auto group_opt = LayeredNozzleGroupResult::create(filament_map, nozzle_list, used_filaments);
        REQUIRE(group_opt.has_value());
        auto &group = *group_opt;

        REQUIRE(group.get_nozzle_id(0) == 0);
        REQUIRE(group.get_nozzle_id(1) == 1);
        REQUIRE(group.get_nozzle_id(2) == 0);
        REQUIRE(group.get_nozzle_id(3) == 1);
        for (int f = 0; f < 4; ++f)
            REQUIRE(group.get_nozzle_id(f) == group.get_extruder_id(f));
    }
}

TEST_CASE("H2C multi-nozzle: filaments get distinct nozzles on the 6-nozzle extruder", "[ToolOrdering][H2C]")
{
    std::vector<unsigned int> used_filaments      = {0, 1, 2, 3};
    std::vector<int>          filament_map        = {1, 1, 1, 1};
    std::vector<int>          filament_volume_map = {0, 0, 0, 0};
    std::vector<int>          filament_nozzle_map = {0, 1, 2, 3};

    std::vector<std::map<NozzleVolumeType, int>> nozzle_count(2);
    nozzle_count[0] = {};
    nozzle_count[1] = {{nvtStandard, 6}};

    auto group_opt = LayeredNozzleGroupResult::create(
        used_filaments, filament_map, filament_volume_map, filament_nozzle_map, nozzle_count, 0.4f);
    REQUIRE(group_opt.has_value());
    auto &group = *group_opt;

    std::set<int> distinct_nozzles;
    for (int f = 0; f < 4; ++f) {
        REQUIRE(group.get_extruder_id(f) == 1);
        int nid = group.get_nozzle_id(f);
        REQUIRE(nid >= 0);
        distinct_nozzles.insert(nid);
    }
    REQUIRE(distinct_nozzles.size() == 4);

    for (int f = 0; f < 4; ++f) {
        int base = group.get_nozzle_id(f, -1);
        REQUIRE(group.get_nozzle_id(f, 0) == base);
        REQUIRE(group.get_nozzle_id(f, 5) == base);
    }

    for (int f = 0; f < 4; ++f) {
        auto first = group.get_first_nozzle_for_filament(f);
        REQUIRE(first.has_value());
        REQUIRE(first->extruder_id == 1);
        REQUIRE(first->group_id == group.get_nozzle_id(f));
    }
}

TEST_CASE("Grouping context spans the filament count with mis-sized config arrays", "[ToolOrdering][H2C]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.option<ConfigOptionFloats>("nozzle_diameter", true)->values = {0.4};
    config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count", true)->values = {6};
    config.option<ConfigOptionStrings>("extruder_nozzle_stats", true)->values = {"Standard#6"};

    config.option<ConfigOptionStrings>("filament_colour", true)->values = {"#FF0000", "#00FF00", "#0000FF", "#FFFF00"};
    config.option<ConfigOptionStrings>("filament_type", true)->values = {"PLA"};
    config.option<ConfigOptionBools>("filament_is_support", true)->values = {0};
    config.option<ConfigOptionFloats>("filament_diameter", true)->values = {1.75, 1.75, 1.75, 1.75};
    config.option<ConfigOptionInts>("filament_map", true)->values = {1, 1, 1, 1};
    config.option<ConfigOptionFloats>("flush_volumes_matrix", true)->values = std::vector<double>(16, 140.);
    config.option<ConfigOptionFloats>("flush_multiplier", true)->values = {1.};

    Model model;
    model.add_object("cube", "", make_cube(20, 20, 20))->add_instance();

    Print print;
    print.apply(model, config);
    REQUIRE(print.config().filament_type.values.size() < print.config().filament_colour.values.size());

    std::vector<std::vector<unsigned int>> layer_filaments = {{0, 1}, {1, 2}, {2, 3}};

    SECTION("short per-filament arrays still yield one entry per filament") {
        auto result = ToolOrdering::get_recommended_filament_maps(layer_filaments, &print, FilamentMapMode::fmmAutoForFlush, {}, {});
        REQUIRE(result.get_extruder_map(false).size() == 4);
        for (int f = 0; f < 4; ++f)
            REQUIRE(result.get_extruder_id(f) == 0);
    }

    SECTION("filament_ids longer than the filament count is truncated, not paired past the end") {
        config.option<ConfigOptionStrings>("filament_ids", true)->values = {"a", "b", "c", "d", "e", "f"};
        print.apply(model, config);
        auto result = ToolOrdering::get_recommended_filament_maps(layer_filaments, &print, FilamentMapMode::fmmAutoForFlush, {}, {});
        REQUIRE(result.get_extruder_map(false).size() == 4);
    }
}

TEST_CASE("H2C dynamic selector: per-layer nozzle ids reach the g-code surface", "[ToolOrdering][H2C][Dynamic]")
{
    std::vector<NozzleInfo> nozzle_list;
    for (int g = 0; g < 4; ++g) {
        NozzleInfo n;
        n.diameter    = "0.4";
        n.volume_type = nvtStandard;
        n.extruder_id = (g == 0) ? 0 : 1;
        n.group_id    = g;
        nozzle_list.push_back(n);
    }

    std::vector<std::vector<int>> layer_filament_nozzle_maps = {
        {0, 1, 2},
        {0, 1, 2},
        {0, 1, 3},
        {0, 1, 3},
    };
    std::vector<std::vector<unsigned int>> layer_filament_sequences = {
        {0, 1, 2}, {0, 1, 2}, {0, 1, 2}, {0, 1, 2},
    };
    std::vector<unsigned int> used_filaments = {0, 1, 2};

    auto group_opt = LayeredNozzleGroupResult::create(layer_filament_nozzle_maps, nozzle_list, used_filaments, layer_filament_sequences);
    REQUIRE(group_opt.has_value());
    auto &group = *group_opt;

    REQUIRE(group.is_support_dynamic_nozzle_map());

    REQUIRE(group.get_nozzle_id(2, 0) == 2);
    REQUIRE(group.get_nozzle_id(2, 1) == 2);
    REQUIRE(group.get_nozzle_id(2, 2) == 3);
    REQUIRE(group.get_nozzle_id(2, 3) == 3);
    REQUIRE(group.get_extruder_id(2, 0) == 1);
    REQUIRE(group.get_extruder_id(2, 2) == 1);

    REQUIRE(group.get_nozzle_id(0, 0) == 0);
    REQUIRE(group.get_nozzle_id(0, 3) == 0);
    REQUIRE(group.get_nozzle_id(1, 0) == 1);
    REQUIRE(group.get_nozzle_id(1, 3) == 1);

    auto first2 = group.get_first_nozzle_for_filament(2);
    REQUIRE(first2.has_value());
    REQUIRE(first2->group_id == 2);

    std::set<int> fil2_nozzles;
    for (const auto &n : group.get_nozzles_for_filament(2))
        fil2_nozzles.insert(n.group_id);
    REQUIRE(fil2_nozzles == std::set<int>({2, 3}));
}

TEST_CASE("Multi-nozzle reorder tolerates a filament with no nozzle (RL-48)", "[ToolOrdering][H2C][Dynamic]")
{
    auto             nozzle_list         = single_nozzle_per_extruder(2);
    std::vector<int> filament_nozzle_map = {0};
    auto             group_opt           = LayeredNozzleGroupResult::create(filament_nozzle_map, nozzle_list, std::vector<unsigned int>{0});
    REQUIRE(group_opt.has_value());

    std::vector<unsigned int>              filament_lists  = {3};
    std::vector<std::vector<unsigned int>> layer_filaments = {{3}, {3}};
    std::vector<std::vector<std::vector<float>>> flush_matrix(2, {{0.f}});
    std::vector<std::vector<unsigned int>> sequences;

    REQUIRE_NOTHROW(reorder_filaments_for_multi_nozzle_extruder(filament_lists, *group_opt, layer_filaments, flush_matrix, nullptr, &sequences));
    REQUIRE(sequences.size() == layer_filaments.size());
    REQUIRE(sequences[0] == std::vector<unsigned int>{3});
    REQUIRE(sequences[1] == std::vector<unsigned int>{3});
}

TEST_CASE("extruder_nozzle_stats round-trips through save/parse", "[ToolOrdering][H2C]")
{
    std::vector<std::map<NozzleVolumeType, int>> stats = {
        {{nvtStandard, 1}},
        {{nvtStandard, 5}, {nvtHighFlow, 1}},
    };
    REQUIRE(get_extruder_nozzle_stats(save_extruder_nozzle_stats_to_string(stats)) == stats);
}

TEST_CASE("Filament-change-time model matches the BBS analytic simulation", "[MultiNozzle][H2C][ChangeTime]")
{
    using Catch::Matchers::WithinAbs;

    FilamentChangeTimeParams params;
    params.selector_load_time   = 1.0f;
    params.selector_unload_time = 1.0f;
    params.standard_load_time   = 3.0f;
    params.standard_unload_time = 2.0f;

    std::vector<NozzleInfo> nozzle_list(1);
    nozzle_list[0].diameter    = "0.4";
    nozzle_list[0].volume_type = nvtStandard;
    nozzle_list[0].extruder_id = 0;
    nozzle_list[0].group_id    = 0;

    std::vector<int> logical_filaments  = {0, 1};
    std::vector<int> group_of_filament  = {0, 1};
    std::vector<int> filament_change_seq = {0, 1, 0};
    std::vector<int> nozzle_change_seq   = {0, 0, 0};

    SECTION("no AMS pre-load: each change is a full AMS<->extruder transport")
    {
        auto r = simulate_filament_change_time(
            logical_filaments, nozzle_list, filament_change_seq, nozzle_change_seq,
            group_of_filament, params, {}, true);
        REQUIRE_THAT(r.actual_time, WithinAbs(13.0, 1e-6));
        REQUIRE_THAT(r.sliced_time, WithinAbs(13.0, 1e-6));
    }

    SECTION("AMS pre-load overlaps transport, shrinking the actual time")
    {
        std::vector<bool> preload = {true, true};
        auto r = simulate_filament_change_time(
            logical_filaments, nozzle_list, filament_change_seq, nozzle_change_seq,
            group_of_filament, params, preload, false);
        REQUIRE_THAT(r.actual_time, WithinAbs(7.0, 1e-6));
    }

    SECTION("degenerate inputs return zero")
    {
        auto r = simulate_filament_change_time({}, nozzle_list, filament_change_seq,
                                               nozzle_change_seq, {}, params);
        REQUIRE_THAT(r.actual_time, WithinAbs(0.0, 1e-6));
        REQUIRE_THAT(r.sliced_time, WithinAbs(0.0, 1e-6));
    }
}

TEST_CASE("NozzleStatusRecorder tracks nozzle/extruder occupancy", "[MultiNozzle][H2C][ChangeTime]")
{
    NozzleStatusRecorder rec;
    REQUIRE(rec.is_nozzle_empty(0));
    REQUIRE(rec.get_filament_in_nozzle(0) == -1);
    REQUIRE(rec.get_nozzle_in_extruder(0) == -1);

    rec.set_nozzle_status(2, 5, 1);
    REQUIRE_FALSE(rec.is_nozzle_empty(2));
    REQUIRE(rec.get_filament_in_nozzle(2) == 5);
    REQUIRE(rec.get_nozzle_in_extruder(1) == 2);

    rec.clear_nozzle_status(2);
    REQUIRE(rec.is_nozzle_empty(2));
    REQUIRE(rec.get_filament_in_nozzle(2) == -1);
    REQUIRE(rec.get_nozzle_in_extruder(1) == 2);
}

TEST_CASE("Hybrid nozzle stats resolve to concrete volume types", "[ToolOrdering][H2C]")
{
    auto stats = get_extruder_nozzle_stats({"Standard#1", "Standard#1|High Flow#1"});
    REQUIRE(stats.size() == 2);
    REQUIRE(stats[1].size() == 2);

    std::vector<unsigned int> used_filaments = {0, 1, 2};
    std::vector<int> filament_map    = {0, 1, 1};
    std::vector<int> volume_requests = {(int) nvtStandard, (int) nvtHighFlow, (int) nvtStandard};
    std::vector<int> nozzle_requests = {0, 1, 2};

    auto group = LayeredNozzleGroupResult::create(used_filaments, filament_map, volume_requests, nozzle_requests, stats, 0.4f);
    REQUIRE(group.has_value());

    auto volume_map = group->get_volume_map();
    REQUIRE(volume_map == volume_requests);
    for (auto fid : used_filaments)
        REQUIRE(volume_map[fid] != (int) nvtHybrid);

    std::vector<int> hybrid_requests = {(int) nvtStandard, (int) nvtHybrid, (int) nvtStandard};
    REQUIRE_FALSE(LayeredNozzleGroupResult::create(used_filaments, filament_map, hybrid_requests, nozzle_requests, stats, 0.4f).has_value());
}

TEST_CASE("update_used_filament_values merges only used filaments", "[ToolOrdering][H2C]")
{
    std::vector<int>          old_values = {1, 1, 2, 1};
    std::vector<int>          new_values = {2, 2, 1, 2};
    std::vector<unsigned int> used       = {0, 2};

    auto merged = FilamentGroupUtils::update_used_filament_values(old_values, new_values, used);
    REQUIRE(merged == std::vector<int>{2, 1, 1, 1});

    REQUIRE(FilamentGroupUtils::update_used_filament_values(old_values, new_values, {}) == old_values);
}

TEST_CASE("Print config-index resolvers pick per-filament Hybrid slots", "[Print][H2C]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.option<ConfigOptionFloats>("nozzle_diameter", true)->values = {0.4, 0.4};
    config.option<ConfigOptionStrings>("extruder_nozzle_stats", true)->values = {"Standard#1", "Standard#1|High Flow#2"};
    config.option<ConfigOptionEnumsGeneric>("extruder_type", true)->values = {etDirectDrive, etDirectDrive};
    config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type", true)->values = {nvtStandard, nvtHybrid};
    config.option<ConfigOptionStrings>("extruder_variant_list", true)->values = {"Direct Drive Standard,Direct Drive High Flow",
                                                                                 "Direct Drive Standard,Direct Drive High Flow"};
    config.option<ConfigOptionInts>("print_extruder_id", true)->values = {1, 1, 2, 2};
    config.option<ConfigOptionStrings>("print_extruder_variant", true)->values = {"Direct Drive Standard", "Direct Drive High Flow",
                                                                                  "Direct Drive Standard", "Direct Drive High Flow"};
    config.option<ConfigOptionFloats>("outer_wall_speed", true)->values = {30., 200., 50., 500.};

    config.option<ConfigOptionFloats>("filament_diameter", true)->values = {1.75, 1.75, 1.75};
    config.option<ConfigOptionStrings>("filament_colour", true)->values = {"#FF0000", "#00FF00", "#0000FF"};
    config.option<ConfigOptionInts>("filament_map", true)->values = {1, 2, 2};
    config.option<ConfigOptionInts>("filament_volume_map", true)->values = {(int) nvtStandard, (int) nvtStandard, (int) nvtHighFlow};

    Model model;
    model.add_object("cube", "", make_cube(20, 20, 20))->add_instance();

    Print print;
    print.apply(model, config);

    std::vector<NozzleInfo> nozzle_list;
    {
        NozzleInfo n;
        n.diameter = "0.4";
        n.volume_type = nvtStandard; n.extruder_id = 0; n.group_id = 0; nozzle_list.push_back(n);
        n.volume_type = nvtStandard; n.extruder_id = 1; n.group_id = 1; nozzle_list.push_back(n);
        n.volume_type = nvtHighFlow; n.extruder_id = 1; n.group_id = 2; nozzle_list.push_back(n);
    }
    std::vector<unsigned int> used_filaments = {0, 1, 2};
    auto group = LayeredNozzleGroupResult::create(std::vector<int>{0, 1, 2}, nozzle_list, used_filaments);
    REQUIRE(group.has_value());
    print.set_nozzle_group_result(std::make_shared<LayeredNozzleGroupResult>(*group));

    print.update_filament_maps_to_config({1, 2, 2}, {(int) nvtStandard, (int) nvtStandard, (int) nvtHighFlow}, {0, 1, 2});

    const auto &region_config = print.default_region_config();
    REQUIRE(region_config.print_extruder_variant.values ==
            std::vector<std::string>({"Direct Drive Standard", "Direct Drive Standard", "Direct Drive High Flow"}));
    REQUIRE(region_config.print_extruder_id.values == std::vector<int>({1, 2, 2}));

    SECTION("each filament resolves to its own (extruder x volume type) slot") {
        REQUIRE(print.get_nozzle_config_index(0, 0) == 0);
        REQUIRE(print.get_nozzle_config_index(1, 0) == 1);
        REQUIRE(print.get_nozzle_config_index(2, 0) == 2);
    }

    SECTION("without a group result the resolver falls back to the filament's extruder slot") {
        print.set_nozzle_group_result(nullptr);
        REQUIRE(print.get_nozzle_config_index(0, 0) == 0);
        REQUIRE(print.get_nozzle_config_index(1, 0) == 1);
        REQUIRE(print.get_nozzle_config_index(2, 0) == 1);
    }
}

TEST_CASE("Regrouping or rewriting the filament maps changes the config-index generation", "[Print][H2C]")
{
    Model model;
    model.add_object("cube", "", make_cube(20, 20, 20))->add_instance();
    Print print;
    print.apply(model, DynamicPrintConfig::full_print_config());

    size_t generation = print.config_index_generation();
    print.set_nozzle_group_result(nullptr);
    REQUIRE(print.config_index_generation() != generation);

    generation = print.config_index_generation();
    print.update_filament_maps_to_config({1}, {(int) nvtStandard}, {0});
    REQUIRE(print.config_index_generation() != generation);
}

TEST_CASE("Re-applying an unchanged config after slicing keeps the result valid", "[Print][H2C]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.option<ConfigOptionFloats>("nozzle_diameter", true)->values = {0.4, 0.4};
    config.option<ConfigOptionStrings>("extruder_nozzle_stats", true)->values = {"Standard#1", "Standard#1|High Flow#2"};
    config.option<ConfigOptionEnumsGeneric>("extruder_type", true)->values = {etDirectDrive, etDirectDrive};
    config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type", true)->values = {nvtStandard, nvtHybrid};
    config.option<ConfigOptionStrings>("extruder_variant_list", true)->values = {"Direct Drive Standard,Direct Drive High Flow",
                                                                                 "Direct Drive Standard,Direct Drive High Flow"};
    config.option<ConfigOptionInts>("print_extruder_id", true)->values = {1, 1, 2, 2};
    config.option<ConfigOptionStrings>("print_extruder_variant", true)->values = {"Direct Drive Standard", "Direct Drive High Flow",
                                                                                  "Direct Drive Standard", "Direct Drive High Flow"};
    config.option<ConfigOptionFloats>("filament_diameter", true)->values = {1.75, 1.75, 1.75};
    config.option<ConfigOptionStrings>("filament_colour", true)->values = {"#FF0000", "#00FF00", "#0000FF"};
    config.option<ConfigOptionInts>("filament_map", true)->values = {1, 2, 2};
    config.option<ConfigOptionInts>("filament_volume_map", true)->values = {(int) nvtStandard, (int) nvtStandard, (int) nvtHighFlow};

    Model model;
    ModelObject *object = model.add_object("cube", "", make_cube(20, 20, 20));
    object->add_instance()->set_offset(Vec3d(100., 100., 0.));

    Print print;
    print.apply(model, config);
    print.process();
    REQUIRE(print.is_step_done(psSlicingFinished));

    auto status = print.apply(model, config);
    REQUIRE(status != PrintBase::APPLY_STATUS_INVALIDATED);
    REQUIRE(print.is_step_done(psSlicingFinished));
}

TEST_CASE("A degenerate process variant map on a custom multi-extruder printer slices to a stable result", "[Print][Regression]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_num_extruders(5);
    config.option<ConfigOptionFloats>("nozzle_diameter", true)->values = {0.4, 0.4, 0.4, 0.4, 0.4};
    config.option<ConfigOptionPercents>("retract_before_wipe", true)->values = {100., 70., 70., 70., 100.};
    config.option<ConfigOptionEnumsGeneric>("z_hop_types", true)->values = {zhtSlope, zhtNormal, zhtNormal, zhtNormal, zhtSlope};
    config.option<ConfigOptionPercentsNullable>("filament_retract_before_wipe", true)->values =
        std::vector<double>(5, ConfigOptionPercentsNullable::nil_value());
    config.option<ConfigOptionEnumsGenericNullable>("filament_z_hop_types", true)->values =
        std::vector<int>(5, ConfigOptionEnumsGenericNullable::nil_value());
    config.option<ConfigOptionFloats>("filament_diameter", true)->values = std::vector<double>(5, 1.75);
    config.option<ConfigOptionStrings>("filament_colour", true)->values = {"#FF0000", "#00FF00", "#0000FF", "#FFFF00", "#00FFFF"};
    config.option<ConfigOptionInts>("filament_map", true)->values = {1, 2, 3, 4, 1};

    Model model;
    model.add_object("cube", "", make_cube(20, 20, 20))->add_instance()->set_offset(Vec3d(100., 100., 0.));

    Print print;
    print.apply(model, config);
    print.process();
    REQUIRE(print.is_step_done(psSlicingFinished));

    config.option<ConfigOptionInts>("filament_map", true)->values = print.get_filament_maps();
    config.option<ConfigOptionInts>("filament_volume_map", true)->values = print.get_filament_volume_maps();
    config.option<ConfigOptionInts>("filament_nozzle_map", true)->values = print.get_filament_nozzle_maps();

    auto status = print.apply(model, config);
    REQUIRE(status == PrintBase::APPLY_STATUS_UNCHANGED);
    REQUIRE(print.is_step_done(psSlicingFinished));
    REQUIRE(print.config().retract_before_wipe.values == std::vector<double>({100., 70., 70., 70., 100.}));
    REQUIRE(print.config().z_hop_types.values == std::vector<int>({zhtSlope, zhtNormal, zhtNormal, zhtNormal, zhtSlope}));
}

TEST_CASE("normalize_nozzle_map_per_layer makes per-filament assignments gap-free", "[MultiNozzle][H2C][Dynamic]")
{
    SECTION("gaps inherit the last used nozzle, entries on used layers stay untouched") {
        std::vector<std::vector<int>> maps = {
            {0, 1},
            {0, -1},
            {0, -1},
            {0, 2},
        };
        std::vector<std::vector<unsigned int>> filaments = {{0, 1}, {0}, {0}, {0, 1}};

        normalize_nozzle_map_per_layer(maps, filaments);

        REQUIRE(maps[0] == std::vector<int>({0, 1}));
        REQUIRE(maps[1] == std::vector<int>({0, 1}));
        REQUIRE(maps[2] == std::vector<int>({0, 1}));
        REQUIRE(maps[3] == std::vector<int>({0, 2}));
    }

    SECTION("layers before a filament's first use inherit its first nozzle") {
        std::vector<std::vector<int>> maps = {
            {0, -1},
            {0, -1},
            {0, 3},
        };
        std::vector<std::vector<unsigned int>> filaments = {{0}, {0}, {0, 1}};

        normalize_nozzle_map_per_layer(maps, filaments);

        REQUIRE(maps[0] == std::vector<int>({0, 3}));
        REQUIRE(maps[1] == std::vector<int>({0, 3}));
        REQUIRE(maps[2] == std::vector<int>({0, 3}));
    }

    SECTION("empty and ragged inputs are safe no-ops") {
        std::vector<std::vector<int>> empty_maps;
        std::vector<std::vector<unsigned int>> no_filaments;
        REQUIRE_NOTHROW(normalize_nozzle_map_per_layer(empty_maps, no_filaments));
        REQUIRE(empty_maps.empty());

        std::vector<std::vector<int>> ragged = {{0}, {0, 1, 2}};
        std::vector<std::vector<unsigned int>> short_filaments = {{0}};
        REQUIRE_NOTHROW(normalize_nozzle_map_per_layer(ragged, short_filaments));
        REQUIRE(ragged[0] == std::vector<int>({0}));
    }

    SECTION("a single layer is left unchanged") {
        std::vector<std::vector<int>> maps = {{2, 1, 0}};
        std::vector<std::vector<unsigned int>> filaments = {{0, 1, 2}};
        normalize_nozzle_map_per_layer(maps, filaments);
        REQUIRE(maps[0] == std::vector<int>({2, 1, 0}));
    }
}

TEST_CASE("Stitched sequential blocks resolve per-layer after normalization", "[MultiNozzle][H2C][Dynamic]")
{
    std::vector<NozzleInfo> nozzle_list;
    for (int g = 0; g < 3; ++g) {
        NozzleInfo n;
        n.diameter    = "0.4";
        n.volume_type = nvtStandard;
        n.extruder_id = (g == 0) ? 0 : 1;
        n.group_id    = g;
        nozzle_list.push_back(n);
    }

    std::vector<std::vector<int>> stitched_maps = {
        {-1, 1},
        {0, 1},
        {0, 2},
        {0, 2},
    };
    std::vector<std::vector<unsigned int>> stitched_filaments = {{1}, {0, 1}, {0, 1}, {0, 1}};
    std::vector<unsigned int>              used_filaments     = {0, 1};

    normalize_nozzle_map_per_layer(stitched_maps, stitched_filaments);
    REQUIRE(stitched_maps[0] == std::vector<int>({0, 1}));

    auto group_opt = LayeredNozzleGroupResult::create(stitched_maps, nozzle_list, used_filaments, stitched_filaments);
    REQUIRE(group_opt.has_value());
    auto &group = *group_opt;

    REQUIRE(group.is_support_dynamic_nozzle_map());
    REQUIRE(group.get_nozzle_id(1, 0) == 1);
    REQUIRE(group.get_nozzle_id(1, 1) == 1);
    REQUIRE(group.get_nozzle_id(1, 2) == 2);
    REQUIRE(group.get_nozzle_id(1, 3) == 2);
    REQUIRE(group.get_nozzle_id(0, 999) == 0);
    REQUIRE(group.get_nozzle_id(1, 999) == 1);
}

TEST_CASE("Sequential selector prints publish a stitched result and cache the plans", "[Print][H2C][Dynamic]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.option<ConfigOptionFloats>("nozzle_diameter", true)->values = {0.4, 0.4};
    config.option<ConfigOptionStrings>("extruder_nozzle_stats", true)->values = {"Standard#1", "Standard#1|High Flow#2"};
    config.option<ConfigOptionEnumsGeneric>("extruder_type", true)->values = {etDirectDrive, etDirectDrive};
    config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type", true)->values = {nvtStandard, nvtStandard};
    config.option<ConfigOptionStrings>("extruder_variant_list", true)->values = {"Direct Drive Standard,Direct Drive High Flow",
                                                                                 "Direct Drive Standard,Direct Drive High Flow"};
    config.option<ConfigOptionInts>("print_extruder_id", true)->values = {1, 1, 2, 2};
    config.option<ConfigOptionStrings>("print_extruder_variant", true)->values = {"Direct Drive Standard", "Direct Drive High Flow",
                                                                                  "Direct Drive Standard", "Direct Drive High Flow"};
    config.option<ConfigOptionFloats>("filament_diameter", true)->values = {1.75, 1.75};
    config.option<ConfigOptionStrings>("filament_colour", true)->values = {"#FF0000", "#00FF00"};
    config.option<ConfigOptionInts>("filament_map", true)->values = {1, 2};
    config.option<ConfigOptionInts>("filament_volume_map", true)->values = {(int) nvtStandard, (int) nvtStandard};
    config.set_key_value("enable_filament_dynamic_map", new ConfigOptionBool(true));
    config.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode", true)->value = FilamentMapMode::fmmAutoForFlush;
    config.option<ConfigOptionEnum<PrintSequence>>("print_sequence", true)->value = PrintSequence::ByObject;
    config.option<ConfigOptionFloats>("flush_volumes_matrix", true)->values = std::vector<double>(8, 140.);
    config.option<ConfigOptionFloats>("flush_multiplier", true)->values = {1., 1.};

    Model model;
    ModelObject *object_a = model.add_object("cube_a", "", make_cube(20, 20, 20));
    ModelInstance *instance_a = object_a->add_instance();
    instance_a->set_offset(Vec3d(70., 100., 0.));
    ModelObject *object_b = model.add_object("cube_b", "", make_cube(20, 20, 20));
    object_b->config.set_key_value("extruder", new ConfigOptionInt(2));
    ModelInstance *instance_b = object_b->add_instance();
    instance_b->set_offset(Vec3d(150., 100., 0.));
    instance_a->arrange_order = 1;
    instance_b->arrange_order = 2;

    Print print;
    print.apply(model, config);
    REQUIRE(print.objects().size() == 2);
    print.process();
    REQUIRE(print.is_step_done(psSlicingFinished));

    auto result = print.get_layered_nozzle_group_result();
    REQUIRE(result != nullptr);
    REQUIRE(print.sequential_dynamic_orderings().size() == 2);
    REQUIRE(result->get_layer_count() > 0);
    REQUIRE(print.config().filament_map.values == result->get_extruder_map(false));
    REQUIRE(print.config().filament_extruder_variant.values.size() == print.config().filament_self_index.values.size());
    REQUIRE(print.config().filament_self_index.values.size() >= print.config().filament_map.values.size());

    ScopedTemporaryFile gcode(".gcode");
    REQUIRE_NOTHROW(print.export_gcode(gcode.string(), nullptr, nullptr));
    REQUIRE(boost::filesystem::exists(gcode.path()));
}

TEST_CASE("Per-variant expansion gives migrating filaments one slot per variant", "[PrintConfig][H2C][Dynamic]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.option<ConfigOptionEnumsGeneric>("extruder_type", true)->values = {etDirectDrive, etDirectDrive};
    config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type", true)->values = {nvtStandard, nvtHybrid};
    config.option<ConfigOptionStrings>("extruder_variant_list", true)->values = {"Direct Drive Standard,Direct Drive High Flow",
                                                                                 "Direct Drive Standard,Direct Drive High Flow"};
    config.option<ConfigOptionInts>("print_extruder_id", true)->values = {1, 1, 2, 2};
    config.option<ConfigOptionStrings>("print_extruder_variant", true)->values = {"Direct Drive Standard", "Direct Drive High Flow",
                                                                                  "Direct Drive Standard", "Direct Drive High Flow"};
    config.option<ConfigOptionInts>("filament_map", true)->values = {1, 2};
    config.option<ConfigOptionInts>("filament_volume_map", true)->values = {(int) nvtStandard, (int) nvtHighFlow};
    config.option<ConfigOptionInts>("filament_self_index", true)->values = {1, 1, 2, 2};
    config.option<ConfigOptionStrings>("filament_extruder_variant", true)->values = {"Direct Drive Standard", "Direct Drive High Flow",
                                                                                     "Direct Drive Standard", "Direct Drive High Flow"};
    config.option<ConfigOptionInts>("nozzle_temperature", true)->values = {220, 230, 240, 250};

    std::set<std::string> key_set = {"filament_self_index", "filament_extruder_variant", "nozzle_temperature"};

    auto make_use = [](ExtruderType et, NozzleVolumeType nvt, int extruder_id) {
        FilamentVariantUse use;
        use.extruder_type      = et;
        use.nozzle_volume_type = nvt;
        use.extruder_id        = extruder_id;
        return use;
    };

    SECTION("a migrating filament expands, machine slots track each output slot") {
        std::unordered_map<int, std::vector<FilamentVariantUse>> uses;
        uses[0] = {make_use(etDirectDrive, nvtStandard, 0), make_use(etDirectDrive, nvtHighFlow, 1)};
        uses[1] = {make_use(etDirectDrive, nvtHighFlow, 1)};
        std::vector<int> slot_machine_indices;
        config.update_filament_config_values_for_multiple_extruders(config, uses, 2, 3, key_set,
                                                                    "filament_self_index", "filament_extruder_variant",
                                                                    &slot_machine_indices);
        REQUIRE(config.option<ConfigOptionInts>("filament_self_index")->values == std::vector<int>{1, 1, 2});
        REQUIRE(config.option<ConfigOptionStrings>("filament_extruder_variant")->values ==
                std::vector<std::string>({"Direct Drive Standard", "Direct Drive High Flow", "Direct Drive High Flow"}));
        REQUIRE(config.option<ConfigOptionInts>("nozzle_temperature")->values == std::vector<int>{220, 230, 250});
        REQUIRE(slot_machine_indices == std::vector<int>{0, 3, 3});
    }

    SECTION("filaments absent from the uses fall back to their static assignment") {
        std::unordered_map<int, std::vector<FilamentVariantUse>> uses;
        uses[0] = {make_use(etDirectDrive, nvtStandard, 0)};
        config.update_filament_config_values_for_multiple_extruders(config, uses, 2, 3, key_set,
                                                                    "filament_self_index", "filament_extruder_variant");
        REQUIRE(config.option<ConfigOptionInts>("filament_self_index")->values == std::vector<int>{1, 2});
        REQUIRE(config.option<ConfigOptionInts>("nozzle_temperature")->values == std::vector<int>{220, 250});
    }

    SECTION("a mis-sized filament_volume_map is ignored") {
        config.option<ConfigOptionInts>("filament_volume_map", true)->values = {(int) nvtHighFlow};
        std::unordered_map<int, std::vector<FilamentVariantUse>> uses;
        uses[0] = {make_use(etDirectDrive, nvtStandard, 0)};
        config.update_filament_config_values_for_multiple_extruders(config, uses, 2, 3, key_set,
                                                                    "filament_self_index", "filament_extruder_variant");
        REQUIRE(config.option<ConfigOptionInts>("nozzle_temperature")->values == std::vector<int>{220, 240});
    }
}

TEST_CASE("Selector write-back expands migrating filaments and survives re-apply", "[Print][H2C][Dynamic]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.option<ConfigOptionFloats>("nozzle_diameter", true)->values = {0.4, 0.4};
    config.option<ConfigOptionStrings>("extruder_nozzle_stats", true)->values = {"Standard#1", "Standard#1|High Flow#2"};
    config.option<ConfigOptionEnumsGeneric>("extruder_type", true)->values = {etDirectDrive, etDirectDrive};
    config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type", true)->values = {nvtStandard, nvtHybrid};
    config.option<ConfigOptionStrings>("extruder_variant_list", true)->values = {"Direct Drive Standard,Direct Drive High Flow",
                                                                                 "Direct Drive Standard,Direct Drive High Flow"};
    config.option<ConfigOptionInts>("print_extruder_id", true)->values = {1, 1, 2, 2};
    config.option<ConfigOptionStrings>("print_extruder_variant", true)->values = {"Direct Drive Standard", "Direct Drive High Flow",
                                                                                  "Direct Drive Standard", "Direct Drive High Flow"};
    config.option<ConfigOptionStrings>("filament_type", true)->values = {"PLA", "PLA", "PLA"};
    config.option<ConfigOptionFloats>("filament_diameter", true)->values = {1.75, 1.75, 1.75};
    config.option<ConfigOptionStrings>("filament_colour", true)->values = {"#FF0000", "#00FF00", "#0000FF"};
    config.option<ConfigOptionInts>("filament_map", true)->values = {1, 2, 2};
    config.option<ConfigOptionInts>("filament_volume_map", true)->values = {(int) nvtStandard, (int) nvtStandard, (int) nvtStandard};
    config.option<ConfigOptionInts>("filament_self_index", true)->values = {1, 1, 2, 2, 3, 3};
    config.option<ConfigOptionStrings>("filament_extruder_variant", true)->values = {"Direct Drive Standard", "Direct Drive High Flow",
                                                                                     "Direct Drive Standard", "Direct Drive High Flow",
                                                                                     "Direct Drive Standard", "Direct Drive High Flow"};
    config.option<ConfigOptionInts>("nozzle_temperature", true)->values = {200, 210, 220, 230, 240, 250};
    config.option<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values =
        {0.5, 0.5, 0.6, 0.6, ConfigOptionFloatsNullable::nil_value(), 1.2};
    config.option<ConfigOptionFloats>("retraction_length", true)->values = {0.8, 0.9, 1.0, 1.1};

    Model model;
    model.add_object("cube", "", make_cube(20, 20, 20))->add_instance();

    Print print;
    print.apply(model, config);

    std::vector<NozzleInfo> nozzle_list;
    {
        NozzleInfo n;
        n.diameter = "0.4";
        n.volume_type = nvtStandard; n.extruder_id = 0; n.group_id = 0; nozzle_list.push_back(n);
        n.volume_type = nvtStandard; n.extruder_id = 1; n.group_id = 1; nozzle_list.push_back(n);
        n.volume_type = nvtHighFlow; n.extruder_id = 1; n.group_id = 2; nozzle_list.push_back(n);
    }
    std::vector<std::vector<int>>          layer_maps = {{0, 1, 1}, {0, 1, 2}};
    std::vector<std::vector<unsigned int>> layer_seqs = {{0, 1, 2}, {0, 1, 2}};
    auto group = LayeredNozzleGroupResult::create(layer_maps, nozzle_list, {0, 1, 2}, layer_seqs);
    REQUIRE(group.has_value());
    REQUIRE(group->is_support_dynamic_nozzle_map());
    print.set_nozzle_group_result(std::make_shared<LayeredNozzleGroupResult>(*group));

    print.update_to_config_by_nozzle_group_result(*group);

    REQUIRE(print.config().filament_map.values == group->get_extruder_map(false));
    REQUIRE(print.config().filament_self_index.values == std::vector<int>{1, 2, 3, 3});
    REQUIRE(print.config().nozzle_temperature.values == std::vector<int>{200, 220, 240, 250});
    REQUIRE(print.get_filament_config_indx(2, 0) == 2);
    REQUIRE(print.get_filament_config_indx(2, 1) == 3);
    const auto &machine_retract = print.full_print_config().option<ConfigOptionFloats>("retraction_length")->values;
    int f2_std_machine_slot = print.full_print_config().get_index_for_extruder(2, "print_extruder_id", etDirectDrive, nvtStandard,
                                                                               "print_extruder_variant");
    REQUIRE(f2_std_machine_slot >= 0);
    const std::vector<double> merged_retract = print.config().retraction_length.values;
    REQUIRE(merged_retract.size() == 4);
    REQUIRE_THAT(merged_retract[0], Catch::Matchers::WithinAbs(0.5, 1e-9));
    REQUIRE_THAT(merged_retract[1], Catch::Matchers::WithinAbs(0.6, 1e-9));
    REQUIRE_THAT(merged_retract[2], Catch::Matchers::WithinAbs(machine_retract[f2_std_machine_slot], 1e-9));
    REQUIRE_THAT(merged_retract[3], Catch::Matchers::WithinAbs(1.2, 1e-9));

    auto status = print.apply(model, config);
    REQUIRE(status != PrintBase::APPLY_STATUS_INVALIDATED);
    REQUIRE(print.config().filament_self_index.values == std::vector<int>{1, 2, 3, 3});
    REQUIRE(print.config().nozzle_temperature.values == std::vector<int>{200, 220, 240, 250});
    REQUIRE(print.config().retraction_length.values == merged_retract);
}

TEST_CASE("Filaments ordered after a migrator shift columns and the resolver tracks them", "[Print][H2C][Dynamic]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.option<ConfigOptionFloats>("nozzle_diameter", true)->values = {0.4, 0.4};
    config.option<ConfigOptionStrings>("extruder_nozzle_stats", true)->values = {"Standard#1", "Standard#1|High Flow#2"};
    config.option<ConfigOptionEnumsGeneric>("extruder_type", true)->values = {etDirectDrive, etDirectDrive};
    config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type", true)->values = {nvtStandard, nvtHybrid};
    config.option<ConfigOptionStrings>("extruder_variant_list", true)->values = {"Direct Drive Standard,Direct Drive High Flow",
                                                                                 "Direct Drive Standard,Direct Drive High Flow"};
    config.option<ConfigOptionInts>("print_extruder_id", true)->values = {1, 1, 2, 2};
    config.option<ConfigOptionStrings>("print_extruder_variant", true)->values = {"Direct Drive Standard", "Direct Drive High Flow",
                                                                                  "Direct Drive Standard", "Direct Drive High Flow"};
    config.option<ConfigOptionStrings>("filament_type", true)->values = {"PLA", "PLA", "PLA"};
    config.option<ConfigOptionFloats>("filament_diameter", true)->values = {1.75, 1.75, 1.75};
    config.option<ConfigOptionStrings>("filament_colour", true)->values = {"#FF0000", "#00FF00", "#0000FF"};
    config.option<ConfigOptionInts>("filament_map", true)->values = {1, 2, 2};
    config.option<ConfigOptionInts>("filament_volume_map", true)->values = {(int) nvtStandard, (int) nvtStandard, (int) nvtStandard};
    config.option<ConfigOptionInts>("filament_self_index", true)->values = {1, 1, 2, 2, 3, 3};
    config.option<ConfigOptionStrings>("filament_extruder_variant", true)->values = {"Direct Drive Standard", "Direct Drive High Flow",
                                                                                     "Direct Drive Standard", "Direct Drive High Flow",
                                                                                     "Direct Drive Standard", "Direct Drive High Flow"};
    config.option<ConfigOptionInts>("nozzle_temperature", true)->values = {200, 210, 220, 230, 240, 250};
    config.option<ConfigOptionFloatsNullable>("filament_retraction_length", true)->values = {0.5, 0.5, 0.7, 0.9, 1.4, 1.4};
    config.option<ConfigOptionFloats>("retraction_length", true)->values = {0.8, 0.9, 1.0, 1.1};
    config.option<ConfigOptionFloats>("fan_max_speed", true)->values = {10, 10, 20, 60, 30, 30};
    config.option<ConfigOptionInts>("additional_cooling_fan_speed", true)->values = {1, 1, 2, 6, 3, 3};
    config.option<ConfigOptionInts>("nozzle_temperature_range_high", true)->values = {230, 230, 240, 280, 250, 250};

    Model model;
    model.add_object("cube", "", make_cube(20, 20, 20))->add_instance();

    Print print;
    print.apply(model, config);

    std::vector<NozzleInfo> nozzle_list;
    {
        NozzleInfo n;
        n.diameter = "0.4";
        n.volume_type = nvtStandard; n.extruder_id = 0; n.group_id = 0; nozzle_list.push_back(n);
        n.volume_type = nvtStandard; n.extruder_id = 1; n.group_id = 1; nozzle_list.push_back(n);
        n.volume_type = nvtHighFlow; n.extruder_id = 1; n.group_id = 2; nozzle_list.push_back(n);
    }
    std::vector<std::vector<int>>          layer_maps = {{0, 1, 1}, {0, 2, 1}};
    std::vector<std::vector<unsigned int>> layer_seqs = {{0, 1, 2}, {0, 1, 2}};
    auto group = LayeredNozzleGroupResult::create(layer_maps, nozzle_list, {0, 1, 2}, layer_seqs);
    REQUIRE(group.has_value());
    REQUIRE(group->is_support_dynamic_nozzle_map());
    print.set_nozzle_group_result(std::make_shared<LayeredNozzleGroupResult>(*group));

    print.update_to_config_by_nozzle_group_result(*group);

    REQUIRE(print.config().filament_self_index.values == std::vector<int>{1, 2, 2, 3});
    REQUIRE(print.config().nozzle_temperature.values == std::vector<int>{200, 220, 230, 240});
    REQUIRE(print.get_filament_config_indx(1, 0) == 1);
    REQUIRE(print.get_filament_config_indx(1, 1) == 2);
    REQUIRE(print.get_filament_config_indx(2, 0) == 3);
    REQUIRE(print.get_filament_config_indx(2, 1) == 3);
    const std::vector<double> merged = print.config().retraction_length.values;
    REQUIRE(merged.size() == 4);
    REQUIRE_THAT(merged[3], Catch::Matchers::WithinAbs(1.4, 1e-9));
    REQUIRE_THAT(merged[2], Catch::Matchers::WithinAbs(0.9, 1e-9));

    const PrintConfig &resolved = print.config();
    REQUIRE(resolved.fan_max_speed.values == std::vector<double>{10, 20, 60, 30});
    CHECK(resolved.fan_max_speed.get_at(print.get_filament_config_indx(1, 0)) == 20);
    CHECK(resolved.fan_max_speed.get_at(print.get_filament_config_indx(1, 1)) == 60);
    CHECK(resolved.additional_cooling_fan_speed.get_at(print.get_filament_config_indx(2, 1)) == 3);
    CHECK(resolved.nozzle_temperature_range_high.get_at(print.get_filament_config_indx(2, 1)) == 250);
    ToolOrdering ordering;
    ordering.layer_tools().emplace_back(0.2);
    ordering.layer_tools().back().extruders = {0, 2};
    CHECK(ordering.cal_max_additional_fan(resolved) == 3);
    ordering.layer_tools().back().extruders = {1};
    CHECK(ordering.cal_max_additional_fan(resolved) == 6);
}

TEST_CASE("Selector slicing keeps the result valid across re-apply", "[Print][H2C][Dynamic]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.option<ConfigOptionFloats>("nozzle_diameter", true)->values = {0.4, 0.4};
    config.option<ConfigOptionStrings>("extruder_nozzle_stats", true)->values = {"Standard#1", "Standard#1|High Flow#2"};
    config.option<ConfigOptionEnumsGeneric>("extruder_type", true)->values = {etDirectDrive, etDirectDrive};
    config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type", true)->values = {nvtStandard, nvtHybrid};
    config.option<ConfigOptionStrings>("extruder_variant_list", true)->values = {"Direct Drive Standard,Direct Drive High Flow",
                                                                                 "Direct Drive Standard,Direct Drive High Flow"};
    config.option<ConfigOptionInts>("print_extruder_id", true)->values = {1, 1, 2, 2};
    config.option<ConfigOptionStrings>("print_extruder_variant", true)->values = {"Direct Drive Standard", "Direct Drive High Flow",
                                                                                  "Direct Drive Standard", "Direct Drive High Flow"};
    config.option<ConfigOptionFloats>("filament_diameter", true)->values = {1.75, 1.75, 1.75};
    config.option<ConfigOptionStrings>("filament_colour", true)->values = {"#FF0000", "#00FF00", "#0000FF"};
    config.option<ConfigOptionInts>("filament_map", true)->values = {1, 2, 2};
    config.option<ConfigOptionInts>("filament_volume_map", true)->values = {(int) nvtStandard, (int) nvtStandard, (int) nvtHighFlow};
    config.set_key_value("enable_filament_dynamic_map", new ConfigOptionBool(true));
    config.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode", true)->value = FilamentMapMode::fmmAutoForFlush;

    Model model;
    ModelObject *object = model.add_object("cube", "", make_cube(20, 20, 20));
    object->add_instance()->set_offset(Vec3d(100., 100., 0.));

    Print print;
    print.apply(model, config);
    print.process();
    REQUIRE(print.is_step_done(psSlicingFinished));

    auto status = print.apply(model, config);
    REQUIRE(status != PrintBase::APPLY_STATUS_INVALIDATED);
    REQUIRE(print.is_step_done(psSlicingFinished));
}

TEST_CASE("parse_cyclic_order parses user cyclic toolchange sequences", "[ToolOrdering][Cyclic]")
{
    SECTION("well-formed sequence") {
        REQUIRE(parse_cyclic_order("3,2,1,4", 4) == std::vector<unsigned int>({2, 1, 0, 3}));
    }

    SECTION("surrounding whitespace is tolerated") {
        REQUIRE(parse_cyclic_order(" 3 , 2 ,1, 4 ", 4) == std::vector<unsigned int>({2, 1, 0, 3}));
    }

    SECTION("out-of-range and non-positive entries are dropped") {
        REQUIRE(parse_cyclic_order("0,5,-1,2", 4) == std::vector<unsigned int>({1}));
    }

    SECTION("duplicates keep only the first occurrence") {
        REQUIRE(parse_cyclic_order("2,2,1,2", 4) == std::vector<unsigned int>({1, 0}));
    }

    SECTION("garbage tokens are ignored") {
        REQUIRE(parse_cyclic_order("3,abc,,2,x1", 4) == std::vector<unsigned int>({2, 1}));
    }

    SECTION("tokens that only start with a number are ignored") {
        REQUIRE(parse_cyclic_order("3,2x,1", 4) == std::vector<unsigned int>({2, 0}));
    }

    SECTION("empty string yields an empty order") {
        REQUIRE(parse_cyclic_order("", 4).empty());
    }

    SECTION("a partial sequence only names the filaments it lists") {
        REQUIRE(parse_cyclic_order("3,1", 4) == std::vector<unsigned int>({2, 0}));
    }
}
