// Original CuraEngine copyright:
// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#include "TreeModelVolumes.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "TreeSupportCommon.hpp"
#include "BeltFloorContext.hpp"

#include "../BuildVolume.hpp"
#include "../ClipperUtils.hpp"
#include "../Layer.hpp"
#include "../Point.hpp"
#include "../Print.hpp"
#include "../Utils.hpp"
#include "../format.hpp"
#include "libslic3r/libslic3r.h"
#include "../PrintConfig.hpp"

#include <cstddef>
#include <algorithm>
#include <cassert>
#include <limits>
#include <functional>
#include <chrono>
#include <optional>
#include <numeric>
#include <cmath>
#include <string_view>

#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>
#include <tbb/task_group.h>
#include <vector>
#include <unordered_map>
#include <utility>

namespace Slic3r::TreeSupport3D
{

using namespace std::literals;

#define error_level_not_in_cache debug

static Polygons calculateMachineBorderCollision(Polygon machine_border)
{
#if 1
    return {};
#else
    Polygons out = offset(machine_border, scaled<float>(1000.), jtMiter, 1.2);
    machine_border.reverse();
    out.emplace_back(std::move(machine_border));
    return out;
#endif
}

TreeModelVolumes::TreeModelVolumes(
    const PrintObject &print_object,
    const BuildVolume &build_volume,
    const coord_t max_move, const coord_t max_move_slow, size_t current_mesh_idx, 
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    double progress_multiplier, double progress_offset, 
#endif
    const std::vector<Polygons>& additional_excluded_areas) :
    m_max_move{ std::max<coord_t>(max_move - 2, 0) }, m_max_move_slow{ std::max<coord_t>(max_move_slow - 2, 0) },
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    m_progress_multiplier{ progress_multiplier }, m_progress_offset{ progress_offset },
#endif
    m_machine_border{ calculateMachineBorderCollision(build_volume.polygon()) }
{
    m_bed_area = build_volume.polygon();
#if 0
    std::unordered_map<size_t, size_t> mesh_to_layeroutline_idx;
    for (size_t mesh_idx = 0; mesh_idx < storage.meshes.size(); ++ mesh_idx) {
        SliceMeshStorage mesh = storage.meshes[mesh_idx];
        bool added = false;
        for (size_t idx = 0; idx < m_layer_outlines.size(); ++ idx)
            if (TreeSupport::TreeSupportSettings(m_layer_outlines[idx].first) == TreeSupport::TreeSupportSettings(mesh.settings)) {
                added = true;
                mesh_to_layeroutline_idx[mesh_idx] = idx;
            }
        if (! added) {
            mesh_to_layeroutline_idx[mesh_idx] = m_layer_outlines.size();
            m_layer_outlines.emplace_back(mesh.settings, std::vector<Polygons>(storage.support.supportLayers.size(), Polygons()));
        }
    }
    for (size_t idx = 0; idx < m_layer_outlines.size(); ++ idx) {
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layer_outlines[idx].second.size()),
            [&](const tbb::blocked_range<size_t> &range) {
            for (const size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx)
                m_layer_outlines[idx].second[layer_idx] = union_(m_layer_outlines[idx].second[layer_idx]); 
        });
    }
    m_current_outline_idx = mesh_to_layeroutline_idx[current_mesh_idx];

#else
    {
        m_anti_overhang = print_object.slice_support_blockers();
        TreeSupportMeshGroupSettings mesh_settings(print_object);
        const TreeSupportSettings config{ mesh_settings, print_object.slicing_parameters() };
        m_current_min_xy_dist = config.xy_min_distance;
        m_current_min_xy_dist_delta = config.xy_distance - m_current_min_xy_dist;
        assert(m_current_min_xy_dist_delta >= 0);
        m_increase_until_radius = config.increase_radius_until_radius;
        m_radius_0 = config.getRadius(0);
        m_raft_layers = config.raft_layers;
        {
            const size_t num_raft = m_raft_layers.size();
            if (num_raft > 0 && ! m_anti_overhang.empty())
                m_anti_overhang.insert(m_anti_overhang.begin(), num_raft, Polygons{});
        }
        m_current_outline_idx = 0;

        m_layer_outlines.emplace_back(mesh_settings, std::vector<Polygons>{});
        std::vector<Polygons> &outlines = m_layer_outlines.front().second;
        size_t num_raft_layers = m_raft_layers.size();
        size_t num_layers = print_object.layer_count() + num_raft_layers;
        outlines.assign(num_layers, Polygons{});
        tbb::parallel_for(tbb::blocked_range<size_t>(num_raft_layers, num_layers, std::min<size_t>(1, std::max<size_t>(16, num_layers / (8 * tbb::this_task_arena::max_concurrency())))),
            [&](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx)
                outlines[layer_idx] = polygons_simplify(to_polygons(print_object.get_layer(layer_idx - num_raft_layers)->lslices), mesh_settings.resolution);
        });

        {
            const auto &slicing_params = print_object.slicing_parameters();
            const auto &pcfg2 = print_object.print()->config();
            BeltFloorContext ctx;
            ctx.init_local(slicing_params, pcfg2, print_object.belt_global_z_offset());
            if (ctx.is_active()) {
                m_belt_floor = ctx.compute_per_layer_floors(num_layers, [&](size_t layer_idx) -> double {
                    if (layer_idx >= num_raft_layers)
                        return print_object.get_layer(layer_idx - num_raft_layers)->print_z
                               - print_object.belt_global_z_offset();
                    return (layer_idx < m_raft_layers.size()) ? m_raft_layers[layer_idx] : 0.;
                });
            }
        }
    }
#endif

    m_support_rests_on_model = false;
    m_min_resolution  = std::numeric_limits<coord_t>::max();
    for (auto data_pair : m_layer_outlines) {
        m_support_rests_on_model |= ! data_pair.first.support_material_buildplate_only;
        m_min_resolution = std::min(m_min_resolution, data_pair.first.resolution);
    }

#if 0
    for (size_t mesh_idx = 0; mesh_idx < storage.meshes.size(); mesh_idx++) {
        SliceMeshStorage mesh = storage.meshes[mesh_idx];
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layer_outlines[mesh_to_layeroutline_idx[mesh_idx]].second.size()),
            [&](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx)
                if (layer_idx < mesh.layer_nr_max_filled_layer) {
                    Polygons outline = extractOutlineFromMesh(mesh, layer_idx);
                    append(m_layer_outlines[mesh_to_layeroutline_idx[mesh_idx]].second[layer_idx], outline);
                }
        });
    }
    if (! additional_excluded_areas.empty()) {
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_anti_overhang.size()),
            [&](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                if (layer_idx < coord_t(additional_excluded_areas.size()))
                    append(m_anti_overhang[layer_idx], additional_excluded_areas[layer_idx]);
                m_anti_overhang[layer_idx] = union_(m_anti_overhang[layer_idx]);
            }
        });
    }
#endif
}

void TreeModelVolumes::precalculate(const PrintObject& print_object, const coord_t max_layer, std::function<void()> throw_on_cancel)
{
    auto t_start = std::chrono::high_resolution_clock::now();
    m_precalculated = true;

    TreeSupportSettings config(m_layer_outlines[m_current_outline_idx].first, print_object.slicing_parameters());

    {
        std::vector<coord_t> possible_tip_radiis;
        for (size_t distance_to_top = 0; distance_to_top <= config.tip_layers; ++ distance_to_top) {
            possible_tip_radiis.emplace_back(ceilRadius(config.getRadius(distance_to_top)));
            possible_tip_radiis.emplace_back(ceilRadius(config.getRadius(distance_to_top) + m_current_min_xy_dist_delta));
        }
        sort_remove_duplicates(possible_tip_radiis);
        for (coord_t radius_eval = m_radius_0; radius_eval <= config.branch_radius; radius_eval = ceilRadius(radius_eval + 1))
            if (! std::binary_search(possible_tip_radiis.begin(), possible_tip_radiis.end(), radius_eval))
                m_ignorable_radii.emplace_back(radius_eval);
    }

    if (throw_on_cancel)
        throw_on_cancel();

    std::unordered_map<coord_t, LayerIndex> radius_until_layer;
    for (LayerIndex distance_to_top = 0; distance_to_top <= max_layer; ++ distance_to_top) {
        const LayerIndex current_layer = max_layer - distance_to_top;
        auto update_radius_until_layer = [&radius_until_layer, current_layer](coord_t r) {
            auto it = radius_until_layer.find(r);
            if (it == radius_until_layer.end())
                radius_until_layer.emplace_hint(it, r, current_layer);
            else
                assert(it->second >= current_layer);
        };
        update_radius_until_layer(ceilRadius(config.getRadius(distance_to_top, 0) + m_current_min_xy_dist_delta));
        update_radius_until_layer(ceilRadius(config.getRadius(distance_to_top, 0)));
        update_radius_until_layer(ceilRadius(config.recommendedMinRadius(current_layer) + m_current_min_xy_dist_delta));
    }

    if (throw_on_cancel)
        throw_on_cancel();

    std::vector<RadiusLayerPair> relevant_avoidance_radiis{ radius_until_layer.begin(), radius_until_layer.end() };

    radius_until_layer[ceilRadius(m_increase_until_radius + m_current_min_xy_dist_delta)] = max_layer;
    radius_until_layer[0] = max_layer;
    if (m_current_min_xy_dist_delta != 0)
        radius_until_layer[m_current_min_xy_dist_delta] = max_layer;

    std::vector<RadiusLayerPair> relevant_collision_radiis{ radius_until_layer.begin(), radius_until_layer.end() };

    calculateCollision(relevant_collision_radiis, throw_on_cancel);

    std::vector<RadiusLayerPair> relevant_hole_collision_radiis;
    for (RadiusLayerPair key : relevant_avoidance_radiis)
        if (key.first < m_increase_until_radius + m_current_min_xy_dist_delta)
            relevant_hole_collision_radiis.emplace_back(key);

    calculateCollisionHolefree(relevant_hole_collision_radiis, throw_on_cancel);
    if (m_support_rests_on_model)
        calculatePlaceables(relevant_avoidance_radiis, throw_on_cancel);

    auto t_coll = std::chrono::high_resolution_clock::now();

    {
        tbb::task_group task_group;
        task_group.run([this, relevant_avoidance_radiis, throw_on_cancel]{ calculateAvoidance(relevant_avoidance_radiis, true, m_support_rests_on_model, throw_on_cancel); });
        task_group.run([this, relevant_avoidance_radiis, throw_on_cancel]{ calculateWallRestrictions(relevant_avoidance_radiis, throw_on_cancel); });
        task_group.wait();
    }
    auto t_end = std::chrono::high_resolution_clock::now();
    auto dur_col = 0.001 * std::chrono::duration_cast<std::chrono::microseconds>(t_coll - t_start).count();
    auto dur_avo = 0.001 * std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_coll).count();

    BOOST_LOG_TRIVIAL(info) << "Precalculating collision took" << dur_col << " ms. Precalculating avoidance took " << dur_avo << " ms.";

#if 0
    auto paint_cache_into_SVGs = [this](const RadiusLayerPolygonCache &cache, std::string_view name) {
        const std::vector<std::pair<RadiusLayerPair, std::reference_wrapper<const Polygons>>> sorted = cache.sorted();
        static constexpr const std::string_view colors[] = {
            "red", "green", "blue", "magenta", "orange"
        };
        static constexpr const size_t num_colors = sizeof(colors) / sizeof(colors[0]);
        for (size_t i = 0; i < sorted.size();) {
            size_t j = i;
            for (++ j; j < sorted.size() && sorted[i].first.second == sorted[j].first.second; ++ j) ;
            std::vector<std::pair<Slic3r::ExPolygons, SVG::ExPolygonAttributes>> expolygons_with_attributes;
            for (int k = int(j - 1); k >= int(i); -- k) {
                std::string legend = format("radius-%1%", unscaled<float>(sorted[k].first.first));
                expolygons_with_attributes.push_back({ union_ex(sorted[k].second), SVG::ExPolygonAttributes(legend, std::string(colors[(k - int(i)) % num_colors]), 1.) });
                SVG::export_expolygons(debug_out_path("treesupport_cache-%s-%d-%s.svg", name.data(), sorted[i].first.second, legend.c_str()), { expolygons_with_attributes.back() });
            }
            SVG::export_expolygons(debug_out_path("treesupport_cache-%s-%d.svg", name.data(), sorted[i].first.second), expolygons_with_attributes);
            i = j;
        }
    };
    paint_cache_into_SVGs(m_collision_cache,                    "collision_cache");
    paint_cache_into_SVGs(m_collision_cache_holefree,           "collision_cache_holefree");
    paint_cache_into_SVGs(m_avoidance_cache,                    "avoidance_cache");
    paint_cache_into_SVGs(m_avoidance_cache_slow,               "avoidance_cache_slow");
    paint_cache_into_SVGs(m_avoidance_cache_to_model,           "avoidance_cache_to_model");
    paint_cache_into_SVGs(m_avoidance_cache_to_model_slow,      "avoidance_cache_to_model_slow");
    paint_cache_into_SVGs(m_placeable_areas_cache,              "placable_areas_cache");
    paint_cache_into_SVGs(m_avoidance_cache_holefree,           "avoidance_cache_holefree");
    paint_cache_into_SVGs(m_avoidance_cache_holefree_to_model,  "avoidance_cache_holefree_to_model");
    paint_cache_into_SVGs(m_wall_restrictions_cache,            "wall_restrictions_cache");
    paint_cache_into_SVGs(m_wall_restrictions_cache_min,        "wall_restrictions_cache_min");
#endif
}

const Polygons& TreeModelVolumes::getCollision(const coord_t orig_radius, LayerIndex layer_idx, bool min_xy_dist) const
{
    const coord_t radius = this->ceilRadius(orig_radius, min_xy_dist);
    if (std::optional<std::reference_wrapper<const Polygons>> result = m_collision_cache.getArea({ radius, layer_idx }); result)
        return (*result).get();
    if (m_precalculated) {
        BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate collision at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
        tree_supports_show_error("Not precalculated Collision requested."sv, false);
    }
    const_cast<TreeModelVolumes*>(this)->calculateCollision(radius, layer_idx, []{});
    return getCollision(orig_radius, layer_idx, min_xy_dist);
}

std::optional<std::pair<coord_t, std::reference_wrapper<const Polygons>>> TreeModelVolumes::get_collision_lower_bound_area(LayerIndex layer_id, coord_t max_radius) const
{
    return m_collision_cache.get_lower_bound_area({ max_radius, layer_id });
}

const Polygons& TreeModelVolumes::getCollisionHolefree(coord_t radius, LayerIndex layer_idx) const
{
    assert(radius == this->ceilRadius(radius));
    assert(radius < m_increase_until_radius + m_current_min_xy_dist_delta);
    if (std::optional<std::reference_wrapper<const Polygons>> result = m_collision_cache_holefree.getArea({ radius, layer_idx }); result)
        return (*result).get();
    if (m_precalculated) {
        BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate collision holefree at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
        tree_supports_show_error("Not precalculated Holefree Collision requested."sv, false);
    }
    const_cast<TreeModelVolumes*>(this)->calculateCollisionHolefree({ radius, layer_idx });
    return getCollisionHolefree(radius, layer_idx);
}

const Polygons& TreeModelVolumes::getAvoidance(const coord_t orig_radius, LayerIndex layer_idx, AvoidanceType type, bool to_model, bool min_xy_dist) const
{
    if (layer_idx == 0)
        return getCollision(orig_radius, layer_idx, min_xy_dist);

    const coord_t radius = this->ceilRadius(orig_radius, min_xy_dist);
    if (type == AvoidanceType::FastSafe && radius >= m_increase_until_radius + m_current_min_xy_dist_delta)
        type = AvoidanceType::Fast;

    if (std::optional<std::reference_wrapper<const Polygons>> result = 
            this->avoidance_cache(type, to_model).getArea({ radius, layer_idx }); 
        result)
        return (*result).get();

    if (m_precalculated) {
        if (to_model) {
            BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate Avoidance to model at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
            tree_supports_show_error("Not precalculated Avoidance(to model) requested."sv, false);
        } else {
            BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate Avoidance at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
            tree_supports_show_error("Not precalculated Avoidance(to buildplate) requested."sv, false);
        }
    }
    const_cast<TreeModelVolumes*>(this)->calculateAvoidance({ radius, layer_idx }, ! to_model, to_model);
    return getAvoidance(orig_radius, layer_idx, type, to_model, min_xy_dist);
}

const Polygons& TreeModelVolumes::getPlaceableAreas(const coord_t orig_radius, LayerIndex layer_idx, std::function<void()> throw_on_cancel) const
{
    const coord_t radius = ceilRadius(orig_radius);
    if (std::optional<std::reference_wrapper<const Polygons>> result = m_placeable_areas_cache.getArea({ radius, layer_idx }); result)
        return (*result).get();
    if (m_precalculated) {
        BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate Placeable Areas at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
        tree_supports_show_error(format("Not precalculated Placeable areas requested, radius %1%, layer %2%", radius, layer_idx), false);
    }
    if (orig_radius == 0)
        return this->getCollision(0, layer_idx, true);
    const_cast<TreeModelVolumes*>(this)->calculatePlaceables(radius, layer_idx, throw_on_cancel);
    return getPlaceableAreas(orig_radius, layer_idx, throw_on_cancel);
}

const Polygons& TreeModelVolumes::getWallRestriction(const coord_t orig_radius, LayerIndex layer_idx, bool min_xy_dist) const
{
    assert(layer_idx > 0);
    if (layer_idx == 0) 
        return getCollision(orig_radius, layer_idx, min_xy_dist);

    min_xy_dist &= m_current_min_xy_dist_delta > 0;

    const coord_t radius = ceilRadius(orig_radius);
    if (std::optional<std::reference_wrapper<const Polygons>> result = 
        (min_xy_dist ? m_wall_restrictions_cache_min : m_wall_restrictions_cache).getArea({ radius, layer_idx });
        result)
        return (*result).get();
    if (m_precalculated) {
        BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate Wall restricions at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
        tree_supports_show_error(
            min_xy_dist ? 
                "Not precalculated Wall restriction of minimum xy distance requested )." :
                "Not precalculated Wall restriction requested )."sv
            , false);
    }
    const_cast<TreeModelVolumes*>(this)->calculateWallRestrictions({ radius, layer_idx });
    return getWallRestriction(orig_radius, layer_idx, min_xy_dist);
}

void TreeModelVolumes::calculateCollision(const std::vector<RadiusLayerPair> &keys, std::function<void()> throw_on_cancel)
{
    tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
        [&](const tbb::blocked_range<size_t> &range) {
        for (size_t ikey = range.begin(); ikey != range.end(); ++ ikey) {
            const LayerIndex radius        = keys[ikey].first;
            const size_t     max_layer_idx = keys[ikey].second;
            calculateCollision(radius, max_layer_idx, throw_on_cancel);
        }
    });
}

void TreeModelVolumes::calculateCollision(const coord_t radius, const LayerIndex max_layer_idx, std::function<void()> throw_on_cancel)
{
    std::vector<size_t> layer_outline_indices(m_layer_outlines.size(), 0);
    std::iota(layer_outline_indices.begin(), layer_outline_indices.end(), 0);
    std::sort(layer_outline_indices.begin(), layer_outline_indices.end(),
        [this](size_t i, size_t j) { return m_layer_outlines[i].second.size() < m_layer_outlines[j].second.size(); });

    LayerPolygonCache           data;
    data.allocate(m_collision_cache.getMaxCalculatedLayer(radius) + 1, max_layer_idx + 1);

    const bool                  calculate_placable = m_support_rests_on_model && radius == 0;
    LayerPolygonCache           data_placeable;
    if (calculate_placable)
        data_placeable.allocate(data.begin(), data.end());

    for (size_t outline_idx : layer_outline_indices)
        if (const std::vector<Polygons> &outlines = m_layer_outlines[outline_idx].second; ! outlines.empty()) {
            const TreeSupportMeshGroupSettings  &settings = m_layer_outlines[outline_idx].first;
            const coord_t       layer_height              = settings.layer_height;
            const int           z_distance_bottom_layers  = int(round(double(settings.support_bottom_distance) / double(layer_height)));
            const int           z_distance_top_layers     = int(round(double(settings.support_top_distance) / double(layer_height)));
            const coord_t       xy_distance               = outline_idx == m_current_outline_idx ? m_current_min_xy_dist : 
                settings.support_xy_distance;

            LayerPolygonCache collision_areas_offsetted;
            collision_areas_offsetted.allocate(
                std::max<LayerIndex>(0, data.begin() - z_distance_bottom_layers),
                std::min<LayerIndex>(outlines.size(), data.end() + z_distance_top_layers));
            tbb::parallel_for(tbb::blocked_range<LayerIndex>(collision_areas_offsetted.begin(), collision_areas_offsetted.end()),
                [&outlines, &machine_border = std::as_const(m_machine_border), offset_value = radius + xy_distance, &collision_areas_offsetted, &throw_on_cancel]
                (const tbb::blocked_range<LayerIndex> &range) {
                for (LayerIndex layer_idx = range.begin(); layer_idx != range.end(); ++ layer_idx) {
                    Polygons collision_areas = machine_border;
                    append(collision_areas, outlines[layer_idx]);
                    collision_areas_offsetted[layer_idx] = offset_value == 0 ?
                            union_(collision_areas) :
                            offset(union_ex(collision_areas), offset_value, jtMiter, 1.2);
                    if(throw_on_cancel)
                        throw_on_cancel();
                }
            });

            const bool processing_last_mesh = outline_idx == layer_outline_indices.back();
            tbb::parallel_for(tbb::blocked_range<LayerIndex>(data.begin(), data.end()),
                [&collision_areas_offsetted, &outlines, &machine_border = m_machine_border, &anti_overhang = m_anti_overhang, radius,
                    xy_distance, z_distance_bottom_layers, z_distance_top_layers, min_resolution = m_min_resolution, &data, processing_last_mesh, &throw_on_cancel]
                (const tbb::blocked_range<LayerIndex>& range) {
                    for (LayerIndex layer_idx = range.begin(); layer_idx != range.end(); ++ layer_idx) {
                        Polygons collisions;
                        for (int i = - z_distance_bottom_layers; i <= 0; ++ i)
                            if (int j = layer_idx + i; collision_areas_offsetted.has(j))
                                append(collisions, collision_areas_offsetted[j]);
                        for (int i = 1; i <= z_distance_top_layers; ++ i)
                            if (int j = layer_idx + i; j < int(outlines.size())) {
                                Polygons collision_areas_original = machine_border;
                                append(collision_areas_original, outlines[j]);

                                coord_t required_range_x =
                                    (xy_distance - ((i - (z_distance_top_layers == 1 ? 0.5 : 0)) * xy_distance / z_distance_top_layers)); 
                                append(collisions, offset(union_ex(collision_areas_original), radius + required_range_x, jtMiter, 1.2));
                            }
                        if (processing_last_mesh) {
                            if (layer_idx < int(anti_overhang.size()))
                                append(collisions, offset(union_ex(anti_overhang[layer_idx]), radius, jtMiter, 1.2));
                        }
                        collisions = union_(collisions);
                        auto &dst = data[layer_idx];
                        if (processing_last_mesh) {
                            if (! dst.empty())
                                collisions = union_(collisions, dst);
                            dst = polygons_simplify(collisions, min_resolution);
                        } else
                            append(dst, std::move(collisions));
                        if (throw_on_cancel)
                            throw_on_cancel();
                    }
                });

            if (calculate_placable) {
                tbb::parallel_for(tbb::blocked_range<LayerIndex>(std::max(z_distance_bottom_layers + 1, data.begin()), data.end()),
                    [&collision_areas_offsetted, &outlines, &anti_overhang = m_anti_overhang, processing_last_mesh,
                     min_resolution = m_min_resolution, z_distance_bottom_layers, xy_distance, &data_placeable, &throw_on_cancel]
                (const tbb::blocked_range<LayerIndex>& range) {
                    for (LayerIndex layer_idx = range.begin(); layer_idx != range.end(); ++ layer_idx) {
                        LayerIndex layer_idx_below = layer_idx - z_distance_bottom_layers - 1;
                        assert(layer_idx_below >= 0);
                        const Polygons &current = collision_areas_offsetted[layer_idx];
                        const Polygons &below   = outlines[layer_idx_below];
                        Polygons placable = diff(
                            offset(below, xy_distance), 
                            layer_idx_below < int(anti_overhang.size()) ? union_(current, anti_overhang[layer_idx_below]) : current);
                        auto &dst     = data_placeable[layer_idx];
                        if (processing_last_mesh) {
                            if (! dst.empty())
                                placable = union_(placable, dst);
                            dst = polygons_simplify(placable, min_resolution);
                        } else
                            append(dst, placable);
                        if (throw_on_cancel)
                            throw_on_cancel();
                    }
                });
            } else {
            }
        }
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    {
        std::lock_guard<std::mutex> critical_section(*m_critical_progress);
        if (m_precalculated && m_precalculation_progress < TREE_PROGRESS_PRECALC_COLL) {
            m_precalculation_progress += TREE_PROGRESS_PRECALC_COLL / keys.size();
            Progress::messageProgress(Progress::Stage::SUPPORT, m_precalculation_progress * m_progress_multiplier + m_progress_offset, TREE_PROGRESS_TOTAL);
        }
    }
#endif
    if (throw_on_cancel)
        throw_on_cancel();
    m_collision_cache.insert(std::move(data), radius);
    if (calculate_placable)
        m_placeable_areas_cache.insert(std::move(data_placeable), radius);
}

void TreeModelVolumes::calculateCollisionHolefree(const std::vector<RadiusLayerPair> &keys, std::function<void()> throw_on_cancel)
{
    LayerIndex max_layer = 0;
    for (long long unsigned int i = 0; i < keys.size(); i++)
        max_layer = std::max(max_layer, keys[i].second);

    tbb::parallel_for(tbb::blocked_range<LayerIndex>(0, max_layer + 1, keys.size()),
        [&](const tbb::blocked_range<LayerIndex> &range) {
        std::vector<std::pair<RadiusLayerPair, Polygons>> data;
        data.reserve(range.size() * keys.size());
        for (LayerIndex layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
            for (RadiusLayerPair key : keys)
                if (layer_idx <= key.second) {
                    coord_t radius = key.first;
                    assert(radius == this->ceilRadius(radius));
                    assert(radius < m_increase_until_radius + m_current_min_xy_dist_delta);
                    coord_t increase_radius_ceil = ceilRadius(m_increase_until_radius, false) - radius;
                    assert(increase_radius_ceil > 0);
                    data.emplace_back(RadiusLayerPair(radius, layer_idx), polygons_simplify(
                        offset(union_ex(this->getCollision(m_increase_until_radius, layer_idx, false)),
                            5 - increase_radius_ceil, jtRound, m_min_resolution),
                        m_min_resolution));
                    if (throw_on_cancel)
                        throw_on_cancel();
                }
        }
        m_collision_cache_holefree.insert(std::move(data));
    });
}

void TreeModelVolumes::calculateAvoidance(const std::vector<RadiusLayerPair> &keys, bool to_build_plate, bool to_model, std::function<void()> throw_on_cancel)
{
    struct AvoidanceTask {
        AvoidanceType   type;
        coord_t         radius;
        LayerIndex      max_required_layer;
        bool            to_model;
        LayerIndex      start_layer;

        bool slow()     const { return this->type == AvoidanceType::Slow; }
        bool holefree() const { return this->type == AvoidanceType::FastSafe; }
    };

    std::vector<AvoidanceTask> avoidance_tasks;
    avoidance_tasks.reserve((int(to_build_plate) + int(to_model)) * keys.size() * size_t(AvoidanceType::Count));

    for (int iter_idx = 0; iter_idx < 2 * int(keys.size()) * int(AvoidanceType::Count); ++ iter_idx) {
        AvoidanceTask task{
            AvoidanceType(iter_idx % int(AvoidanceType::Count)),
            keys[iter_idx / 6].first,
            keys[iter_idx / 6].second,
            ((iter_idx / 3) & 1) != 0
        };
        task.start_layer = std::max<LayerIndex>(1, 1 + avoidance_cache(task.type, task.to_model).getMaxCalculatedLayer(task.radius));
        if (task.start_layer > task.max_required_layer) {
            BOOST_LOG_TRIVIAL(debug) << "Calculation requested for value already calculated?";
            continue;
        }
        if ((task.to_model ? to_model : to_build_plate) &&
            (! task.holefree() || task.radius < m_increase_until_radius + m_current_min_xy_dist_delta))
            avoidance_tasks.emplace_back(task);
    }

    if(throw_on_cancel)
        throw_on_cancel();

    tbb::parallel_for(tbb::blocked_range<size_t>(0, avoidance_tasks.size(), 1),
        [this, &avoidance_tasks, &throw_on_cancel](const tbb::blocked_range<size_t> &range) {
        for (size_t task_idx = range.begin(); task_idx < range.end(); ++ task_idx) {
            const AvoidanceTask &task = avoidance_tasks[task_idx];
            assert(! task.holefree() || task.radius < m_increase_until_radius + m_current_min_xy_dist_delta);
            if (task.to_model)
                getPlaceableAreas(task.radius, task.max_required_layer, throw_on_cancel);
            const bool  collision_holefree = (task.slow() || task.holefree()) && task.radius < m_increase_until_radius + m_current_min_xy_dist_delta;
            const float max_move           = task.slow() ? m_max_move_slow : m_max_move;
            float move_step      = 1.9 * std::max(task.radius, m_current_min_xy_dist);
            if (move_step < EPSILON) return;
            int   move_steps     = round_up_divide<int>(max_move, move_step);
            assert(move_steps > 0);
            float last_move_step = max_move - (move_steps - 1) * move_step;
            if (last_move_step < scaled<float>(0.05)) {
                assert(move_steps > 1);
                if (move_steps > 1) {
                    move_step = max_move / (-- move_steps);
                    last_move_step = move_step;
                }
            }
            Polygons    latest_avoidance   = getAvoidance(task.radius, task.start_layer - 1, task.type, task.to_model, true);
            std::vector<std::pair<RadiusLayerPair, Polygons>> data;
            data.reserve(task.max_required_layer + 1 - task.start_layer);
            for (LayerIndex layer_idx = task.start_layer; layer_idx <= task.max_required_layer; ++ layer_idx) {
                const Polygons &current_layer_collisions = collision_holefree ? getCollisionHolefree(task.radius, layer_idx) : getCollision(task.radius, layer_idx, true);
                for (int istep = 0; istep < move_steps; ++ istep)
                    latest_avoidance = union_(current_layer_collisions,
                        offset(latest_avoidance,
                            istep + 1 == move_steps ? - last_move_step : - move_step,
                            jtRound, m_min_resolution));
                if (task.to_model)
                    latest_avoidance = diff(latest_avoidance, getPlaceableAreas(task.radius, layer_idx, throw_on_cancel));
                latest_avoidance = polygons_simplify(latest_avoidance, m_min_resolution);
                data.emplace_back(RadiusLayerPair{task.radius, layer_idx}, latest_avoidance);
                if (throw_on_cancel)
                    throw_on_cancel();
            }
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
            {
                std::lock_guard<std::mutex> critical_section(*m_critical_progress);
                if (m_precalculated && m_precalculation_progress < TREE_PROGRESS_PRECALC_COLL + TREE_PROGRESS_PRECALC_AVO) {
                    m_precalculation_progress += to_model ? 
                        0.4 * TREE_PROGRESS_PRECALC_AVO / (keys.size() * 3) :
                        m_support_rests_on_model ? 0.4 : 1 * TREE_PROGRESS_PRECALC_AVO / (keys.size() * 3);
                    Progress::messageProgress(Progress::Stage::SUPPORT, m_precalculation_progress * m_progress_multiplier + m_progress_offset, TREE_PROGRESS_TOTAL);
                }
            }
#endif
            avoidance_cache(task.type, task.to_model).insert(std::move(data));
        }
    });
}


void TreeModelVolumes::calculatePlaceables(const std::vector<RadiusLayerPair> &keys, std::function<void()> throw_on_cancel)
{
    tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
        [&, keys](const tbb::blocked_range<size_t>& range) {
            for (size_t key_idx = range.begin(); key_idx < range.end(); ++ key_idx)
                this->calculatePlaceables(keys[key_idx].first, keys[key_idx].second, throw_on_cancel);
        });
}

void TreeModelVolumes::calculatePlaceables(const coord_t radius, const LayerIndex max_required_layer, std::function<void()> throw_on_cancel)
{
    LayerIndex start_layer = 1 + m_placeable_areas_cache.getMaxCalculatedLayer(radius);
    if (start_layer > max_required_layer) {
        BOOST_LOG_TRIVIAL(debug) << "Requested calculation for value already calculated ?";
        return;
    }

    std::vector<Polygons> data(max_required_layer + 1 - start_layer, Polygons{});

    if (start_layer == 0)
        data[0] = diff(m_machine_border, getCollision(radius, 0, true));

    tbb::parallel_for(tbb::blocked_range<LayerIndex>(std::max(1, start_layer), max_required_layer + 1),
        [this, &data, radius, start_layer, &throw_on_cancel](const tbb::blocked_range<LayerIndex>& range) {
            for (LayerIndex layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                data[layer_idx - start_layer] = offset(
                    union_ex(getPlaceableAreas(0, layer_idx, throw_on_cancel)),
                    - (radius + m_current_min_xy_dist + m_current_min_xy_dist_delta),
                    jtMiter, 1.2);
                if(throw_on_cancel)
                    throw_on_cancel();
            }
        });
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    {
        std::lock_guard<std::mutex> critical_section(*m_critical_progress);
        if (m_precalculated && m_precalculation_progress < TREE_PROGRESS_PRECALC_COLL + TREE_PROGRESS_PRECALC_AVO) {
            m_precalculation_progress += 0.2 * TREE_PROGRESS_PRECALC_AVO / (keys.size());
            Progress::messageProgress(Progress::Stage::SUPPORT, m_precalculation_progress * m_progress_multiplier + m_progress_offset, TREE_PROGRESS_TOTAL);
        }
    }
#endif
    m_placeable_areas_cache.insert(std::move(data), start_layer, radius);
}

void TreeModelVolumes::calculateWallRestrictions(const std::vector<RadiusLayerPair> &keys, std::function<void()> throw_on_cancel)
{

    tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
        [&, keys](const tbb::blocked_range<size_t> &range) {
        for (size_t key_idx = range.begin(); key_idx < range.end(); ++ key_idx) {
            const coord_t    radius             = keys[key_idx].first;
            const LayerIndex max_required_layer = keys[key_idx].second;
            const coord_t    min_layer_bottom   = std::max(1, m_wall_restrictions_cache.getMaxCalculatedLayer(radius));
            const size_t     buffer_size        = max_required_layer + 1 - min_layer_bottom;
            std::vector<Polygons> data(buffer_size, Polygons{});
            std::vector<Polygons> data_min;
            if (m_current_min_xy_dist_delta > 0)
                data_min.assign(buffer_size, Polygons{});
            tbb::parallel_for(tbb::blocked_range<LayerIndex>(min_layer_bottom, max_required_layer + 1),
                [this, &data, &data_min, radius, min_layer_bottom, &throw_on_cancel](const tbb::blocked_range<LayerIndex> &range) {
                for (LayerIndex layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    data[layer_idx - min_layer_bottom] = polygons_simplify(
                        intersection(getCollision(0, layer_idx, false), getCollision(radius, layer_idx - 1, true)),
                        m_min_resolution);
                    if (! data_min.empty())
                        data_min[layer_idx - min_layer_bottom] = 
                            polygons_simplify(
                                intersection(getCollision(0, layer_idx, true), getCollision(radius, layer_idx - 1, true)),
                                m_min_resolution);
                    if (throw_on_cancel)
                        throw_on_cancel();
                }
            });
            m_wall_restrictions_cache.insert(std::move(data), min_layer_bottom, radius);
            if (! data_min.empty())
                m_wall_restrictions_cache_min.insert(std::move(data_min), min_layer_bottom, radius);
        }
    });
}

coord_t TreeModelVolumes::ceilRadius(const coord_t radius) const
{
    if (radius == 0)
        return 0;

    coord_t out = m_radius_0;
    if (radius > m_radius_0) {
        coord_t initial_radius_delta = SUPPORT_TREE_EXPONENTIAL_THRESHOLD - m_radius_0;
        auto ignore = [this](coord_t r) { return std::binary_search(m_ignorable_radii.begin(), m_ignorable_radii.end(), r); };
        if (initial_radius_delta > SUPPORT_TREE_COLLISION_RESOLUTION) {
            const int num_steps = round_up_divide(initial_radius_delta, SUPPORT_TREE_EXPONENTIAL_THRESHOLD);
            const int stepsize  = initial_radius_delta / num_steps;
            out += stepsize;
            for (auto step = 0; step < num_steps; ++ step) {
                if (out >= radius && ! ignore(out))
                    return out;
                out += stepsize;
            }
        } else
            out += SUPPORT_TREE_COLLISION_RESOLUTION;
        while (out < radius || ignore(out)) {
            assert(out * SUPPORT_TREE_EXPONENTIAL_FACTOR > out + SUPPORT_TREE_COLLISION_RESOLUTION);
            out = out * SUPPORT_TREE_EXPONENTIAL_FACTOR;
        }
    }
    return out;
}

void TreeModelVolumes::RadiusLayerPolygonCache::allocate_layers(size_t num_layers)
{
    if (num_layers > m_data.size()) {
        if (num_layers > m_data.capacity())
            reserve_power_of_2(m_data, num_layers);
        m_data.resize(num_layers, {});
    }
}

std::vector<std::pair<TreeModelVolumes::RadiusLayerPair, std::reference_wrapper<const Polygons>>> TreeModelVolumes::RadiusLayerPolygonCache::sorted() const
{
    std::vector<std::pair<RadiusLayerPair, std::reference_wrapper<const Polygons>>> out;
    for (auto &layer : m_data) {
        auto layer_idx = LayerIndex(&layer - m_data.data());
        for (auto &radius_polygons : layer)
            out.emplace_back(std::make_pair(radius_polygons.first, layer_idx), radius_polygons.second);
    }
    assert(std::is_sorted(out.begin(), out.end(), [](auto &l, auto &r){ return l.first.second < r.first.second || (l.first.second == r.first.second) && l.first.first < r.first.first; }));
    return out;
}

}
