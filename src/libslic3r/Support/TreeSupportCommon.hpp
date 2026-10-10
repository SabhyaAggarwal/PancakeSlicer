// Original CuraEngine copyright:
// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef slic3r_TreeSupportCommon_hpp
#define slic3r_TreeSupportCommon_hpp

#include "../libslic3r.h"
#include "../Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Support/SupportParameters.hpp"
#include "libslic3r/Support/SupportLayer.hpp"
#include "SupportCommon.hpp"
#include "libslic3r/Utils.hpp"

#include <algorithm>
#include <cstddef>
#include <math.h>
#include <cmath>
#include <limits>
#include <cassert>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <string_view>
#include <vector>
#include <utility>

namespace Slic3r
{
    static constexpr const size_t SUPPORT_TREE_CIRCLE_RESOLUTION = 25;
namespace TreeSupport3D
{
using LayerIndex = int;

enum class InterfacePreference
{
    InterfaceAreaOverwritesSupport,
    SupportAreaOverwritesInterface,
    InterfaceLinesOverwriteSupport,
    SupportLinesOverwriteInterface,
    Nothing
};

struct TreeSupportMeshGroupSettings {
    TreeSupportMeshGroupSettings() = default;
    explicit TreeSupportMeshGroupSettings(const PrintObject &print_object)
    {
        const PrintConfig       &print_config       = print_object.print()->config();
        const PrintObjectConfig &config             = print_object.config();
        const SlicingParameters &slicing_params     = print_object.slicing_parameters();
        coordf_t external_perimeter_width = 0.;
        for (size_t region_id = 0; region_id < print_object.num_printing_regions(); ++ region_id) {
            const PrintRegion &region = print_object.printing_region(region_id);
            external_perimeter_width = std::max<coordf_t>(external_perimeter_width, region.flow(print_object, frExternalPerimeter, config.layer_height).width());
        }

        this->layer_height              = scaled<coord_t>(config.layer_height.value);
        this->resolution                = scaled<coord_t>(print_config.resolution.value);
        this->min_feature_size          = scaled<coord_t>(config.min_feature_size.value);
        this->support_angle             = 0.5 * M_PI - std::clamp<double>((config.support_threshold_angle + 1) * M_PI / 180., 0., 0.5 * M_PI);
        this->support_line_width        = support_material_flow(&print_object, config.layer_height).scaled_width();
        this->support_roof_line_width   = support_material_interface_flow(&print_object, config.layer_height).scaled_width();
        const int bottom_interface_layers = number_of_support_interface_bottom_layers(config);
        this->support_bottom_enable     = bottom_interface_layers > 0;
        this->support_bottom_height     = this->support_bottom_enable ?
            bottom_interface_layers * this->layer_height :
            0;
        this->support_material_buildplate_only = config.support_on_build_plate_only;
        this->support_xy_distance       = scaled<coord_t>(config.support_object_xy_distance.value);
        this->support_xy_distance_1st_layer = scaled<coord_t>(config.support_object_first_layer_gap.value);
        this->support_xy_distance_overhang = std::min(this->support_xy_distance, scaled<coord_t>(0.5 * external_perimeter_width));
        this->support_top_distance      = scaled<coord_t>(slicing_params.gap_support_object);
        this->support_bottom_distance   = scaled<coord_t>(slicing_params.gap_object_support);
        this->support_roof_enable       = config.support_interface_top_layers.value > 0;
        this->support_roof_layers       = config.support_interface_top_layers.value;
        this->support_floor_enable      = bottom_interface_layers > 0;
        this->support_floor_layers      = bottom_interface_layers;
        this->support_roof_pattern      = config.support_interface_pattern;
        this->support_pattern           = config.support_base_pattern;
        this->support_line_spacing      = scaled<coord_t>(config.support_base_pattern_spacing.value);
        this->support_wall_count        = std::max(1, config.tree_support_wall_count.value);
        this->support_roof_line_distance = scaled<coord_t>(config.support_interface_spacing.value) + this->support_roof_line_width;
        this->support_tree_branch_distance = scaled<coord_t>(config.tree_support_branch_distance_organic.value);
        this->support_tree_angle          = std::clamp<double>(config.tree_support_branch_angle_organic * M_PI / 180., 0., 0.5 * M_PI - EPSILON);
        this->support_tree_angle_slow     = std::clamp<double>(config.tree_support_angle_slow * M_PI / 180., 0., this->support_tree_angle - EPSILON);
        this->support_tree_branch_diameter = scaled<coord_t>(config.tree_support_branch_diameter_organic.value);
        this->support_tree_branch_diameter_angle  = std::clamp<double>(config.tree_support_branch_diameter_angle * M_PI / 180., 0., 0.5 * M_PI - EPSILON);
        this->support_tree_top_rate       = config.tree_support_top_rate.value;
        this->support_tree_tip_diameter = std::clamp(scaled<coord_t>(config.tree_support_tip_diameter.value), (coord_t)0, this->support_tree_branch_diameter);
    }

    coord_t                         layer_height                            { scaled<coord_t>(0.15) };
    coord_t                         resolution                              { scaled<coord_t>(0.025) };
    coord_t                         min_feature_size                        { scaled<coord_t>(0.1) };

    double                          support_angle                           { 50. * M_PI / 180. };
    coord_t                         support_line_width                      { scaled<coord_t>(0.4) };
    coord_t                         support_roof_line_width                 { scaled<coord_t>(0.4) };
    bool                            support_bottom_enable                   { false };
    coord_t                         support_bottom_height                   { scaled<coord_t>(1.) };
    bool                            support_material_buildplate_only        { false };
    coord_t                         support_xy_distance                     { scaled<coord_t>(0.7) };
    coord_t                         support_xy_distance_1st_layer           { scaled<coord_t>(0.7) };
    coord_t                         support_xy_distance_overhang            { scaled<coord_t>(0.2) };
    coord_t                         support_top_distance                    { scaled<coord_t>(0.1) };
    coord_t                         support_bottom_distance                 { scaled<coord_t>(0.1) };
    coord_t                         support_interface_skip_height           { scaled<coord_t>(0.3) };
    bool                            support_roof_enable                     { false };
    coord_t                         support_roof_layers                     { 2 };
    bool                            support_floor_enable                    { false };
    coord_t                         support_floor_layers                    { 2 };
    double                          minimum_roof_area                       { scaled<double>(scaled<double>(1.)) };
    std::vector<double>             support_roof_angles                     {};
    SupportMaterialInterfacePattern support_roof_pattern                    { smipAuto };
    SupportMaterialPattern          support_pattern                         { smpRectilinear };
    coord_t                         support_line_spacing                    { scaled<coord_t>(2.66 - 0.4) };
    coord_t                         support_bottom_offset                   { scaled<coord_t>(0.) };
    int                             support_wall_count                      { 1 };
    coord_t                         support_roof_line_distance              { scaled<coord_t>(0.4) };
    coord_t                         minimum_support_area                    { scaled<coord_t>(0.) };
    coord_t                         minimum_bottom_area                     { scaled<coord_t>(1.0) };
    coord_t                         support_offset                          { scaled<coord_t>(0.) };

    double                          support_tree_angle                      { 60. * M_PI / 180. };
    double                          support_tree_branch_diameter_angle      { 5.  * M_PI / 180. };
    coord_t                         support_tree_branch_distance            { scaled<coord_t>(1.) };
    coord_t                         support_tree_branch_diameter            { scaled<coord_t>(2.) };

    double                          support_tree_angle_slow                 { 50. * M_PI / 180. };
    coord_t                         support_tree_max_diameter_increase_by_merges_when_support_to_model { scaled<coord_t>(1.0) };
    coord_t                         support_tree_min_height_to_model        { scaled<coord_t>(1.0) };
    coord_t                         support_tree_bp_diameter                { scaled<coord_t>(7.5) };
    double                          support_tree_top_rate                   { 15. };
    coord_t                         support_tree_tip_diameter               { scaled<coord_t>(0.4) };

};

struct TreeSupportSettings
{
public:
    TreeSupportSettings() = default;
    explicit TreeSupportSettings(const TreeSupportMeshGroupSettings &mesh_group_settings, const SlicingParameters &slicing_params)
        : support_line_width(mesh_group_settings.support_line_width),
          layer_height(mesh_group_settings.layer_height),
          branch_radius(mesh_group_settings.support_tree_branch_diameter / 2),
          min_radius(mesh_group_settings.support_tree_tip_diameter / 2),
          maximum_move_distance((mesh_group_settings.support_tree_angle < M_PI / 2.) ? (coord_t)(tan(mesh_group_settings.support_tree_angle) * layer_height) : std::numeric_limits<coord_t>::max()),
          maximum_move_distance_slow((mesh_group_settings.support_tree_angle_slow < M_PI / 2.) ? (coord_t)(tan(mesh_group_settings.support_tree_angle_slow) * layer_height) : std::numeric_limits<coord_t>::max()),
          support_bottom_layers(mesh_group_settings.support_bottom_enable ? (mesh_group_settings.support_bottom_height + layer_height / 2) / layer_height : 0),
          tip_layers(std::max((branch_radius - min_radius) / (support_line_width / 3), branch_radius / layer_height)),
          branch_radius_increase_per_layer(tan(mesh_group_settings.support_tree_branch_diameter_angle) * layer_height),
          max_to_model_radius_increase(mesh_group_settings.support_tree_max_diameter_increase_by_merges_when_support_to_model / 2),
          min_dtt_to_model(round_up_divide(mesh_group_settings.support_tree_min_height_to_model, layer_height)),
          increase_radius_until_radius(mesh_group_settings.support_tree_branch_diameter / 2),
          increase_radius_until_layer(increase_radius_until_radius <= branch_radius ? tip_layers * (increase_radius_until_radius / branch_radius) : (increase_radius_until_radius - branch_radius) / branch_radius_increase_per_layer),
          support_rests_on_model(! mesh_group_settings.support_material_buildplate_only),
          xy_distance(mesh_group_settings.support_xy_distance),
          xy_min_distance(std::min(mesh_group_settings.support_xy_distance, mesh_group_settings.support_xy_distance_overhang)),
          bp_radius(mesh_group_settings.support_tree_bp_diameter / 2),
          bp_radius_increase_per_layer(std::min(tan(0.7) * layer_height, 0.5 * support_line_width)),
          z_distance_bottom_layers(size_t(round(double(mesh_group_settings.support_bottom_distance) / double(layer_height)))),
          z_distance_top_layers(size_t(round(double(mesh_group_settings.support_top_distance) / double(layer_height)))),
          support_roof_angles(mesh_group_settings.support_roof_angles),
          roof_pattern(mesh_group_settings.support_roof_pattern),
          support_pattern(mesh_group_settings.support_pattern),
          support_roof_line_width(mesh_group_settings.support_roof_line_width),
          support_line_spacing(mesh_group_settings.support_line_spacing),
          support_bottom_offset(mesh_group_settings.support_bottom_offset),
          support_wall_count(mesh_group_settings.support_wall_count),
          resolution(mesh_group_settings.resolution),
          support_roof_line_distance(mesh_group_settings.support_roof_line_distance),
          settings(mesh_group_settings),
          min_feature_size(mesh_group_settings.min_feature_size)
    {
        assert(tip_layers > 0);
    
        layer_start_bp_radius = (bp_radius - branch_radius) / bp_radius_increase_per_layer;
    
        if (TreeSupportSettings::zero_top_z_gap) {
            xy_min_distance = std::max(xy_min_distance, scaled<coord_t>(0.1));
            xy_distance     = std::max(xy_distance, xy_min_distance);
        }
    
        interface_preference = InterfacePreference::InterfaceAreaOverwritesSupport;
    
        if (slicing_params.raft_layers() > 0) {
            double z = slicing_params.first_print_layer_height;
            this->raft_layers.emplace_back(z);
            for (size_t i = 1; i < slicing_params.base_raft_layers; ++ i) {
                z += slicing_params.base_raft_layer_height;
                this->raft_layers.emplace_back(z);
            }
            for (size_t i = 0; i + 1 < slicing_params.interface_raft_layers; ++ i) {
                z += slicing_params.interface_raft_layer_height;
                this->raft_layers.emplace_back(z);
            }
            if (slicing_params.raft_layers() > 1) {
                z = slicing_params.raft_contact_top_z;
                this->raft_layers.emplace_back(z);
            }
            if (double dist_to_go = slicing_params.object_print_z_min - z; dist_to_go > EPSILON) {
                auto nsteps = int(ceil((dist_to_go - EPSILON) / slicing_params.max_suport_layer_height));
                double step = dist_to_go / nsteps;
                for (int i = 0; i < nsteps; ++ i) {
                    z += step;
                    this->raft_layers.emplace_back(z);
                }
            }
        }
    }

    inline static bool zero_top_z_gap = false;
    coord_t support_line_width;
    coord_t layer_height;
    coord_t branch_radius;
    coord_t min_radius;
    coord_t maximum_move_distance;
    coord_t maximum_move_distance_slow;
    size_t support_bottom_layers;
    size_t tip_layers;
    double branch_radius_increase_per_layer;
    coord_t max_to_model_radius_increase;
    size_t min_dtt_to_model;
    coord_t increase_radius_until_radius;
    size_t increase_radius_until_layer;
    bool support_rests_on_model;
    coord_t xy_distance;
    coord_t bp_radius;
    LayerIndex layer_start_bp_radius;
    double bp_radius_increase_per_layer;
    coord_t xy_min_distance;
    size_t z_distance_top_layers;
    size_t z_distance_bottom_layers;
    std::vector<double> support_roof_angles;
    SupportMaterialInterfacePattern roof_pattern;
    SupportMaterialPattern support_pattern;
    coord_t support_roof_line_width;
    coord_t support_line_spacing;
    coord_t support_bottom_offset;
    int support_wall_count;
    coord_t resolution;
    coord_t support_roof_line_distance;
    InterfacePreference interface_preference;

    TreeSupportMeshGroupSettings settings;

    coord_t min_feature_size;

    std::vector<coordf_t> raft_layers;

public:
    bool operator==(const TreeSupportSettings& other) const
    {
        return branch_radius == other.branch_radius && tip_layers == other.tip_layers && branch_radius_increase_per_layer == other.branch_radius_increase_per_layer && layer_start_bp_radius == other.layer_start_bp_radius && bp_radius == other.bp_radius && 
               bp_radius_increase_per_layer == other.bp_radius_increase_per_layer && min_radius == other.min_radius && xy_min_distance == other.xy_min_distance &&
               xy_distance - xy_min_distance == other.xy_distance - other.xy_min_distance &&
               support_rests_on_model == other.support_rests_on_model && increase_radius_until_layer == other.increase_radius_until_layer && min_dtt_to_model == other.min_dtt_to_model && max_to_model_radius_increase == other.max_to_model_radius_increase && maximum_move_distance == other.maximum_move_distance && maximum_move_distance_slow == other.maximum_move_distance_slow && z_distance_bottom_layers == other.z_distance_bottom_layers && support_line_width == other.support_line_width && 
               support_line_spacing == other.support_line_spacing && support_roof_line_width == other.support_roof_line_width &&
               support_bottom_offset == other.support_bottom_offset && support_wall_count == other.support_wall_count && support_pattern == other.support_pattern && roof_pattern == other.roof_pattern &&
               support_roof_angles == other.support_roof_angles && 
               increase_radius_until_radius == other.increase_radius_until_radius && support_bottom_layers == other.support_bottom_layers && layer_height == other.layer_height && z_distance_top_layers == other.z_distance_top_layers && resolution == other.resolution &&
               support_roof_line_distance == other.support_roof_line_distance && interface_preference == other.interface_preference
               && min_feature_size == other.min_feature_size
#if 0
                && (interface_preference == InterfacePreference::InterfaceAreaOverwritesSupport || interface_preference == InterfacePreference::SupportAreaOverwritesInterface
                   || 
                        (settings.get<bool>("fill_outline_gaps") == other.settings.get<bool>("fill_outline_gaps") && 
                         settings.get<coord_t>("min_bead_width") == other.settings.get<coord_t>("min_bead_width") && 
                         settings.get<double>("wall_transition_angle") == other.settings.get<double>("wall_transition_angle") && 
                         settings.get<coord_t>("wall_transition_length") == other.settings.get<coord_t>("wall_transition_length") && 
                         settings.get<Ratio>("wall_split_middle_threshold") == other.settings.get<Ratio>("wall_split_middle_threshold") && 
                         settings.get<Ratio>("wall_add_middle_threshold") == other.settings.get<Ratio>("wall_add_middle_threshold") && 
                         settings.get<int>("wall_distribution_count") == other.settings.get<int>("wall_distribution_count") && 
                         settings.get<coord_t>("wall_transition_filter_distance") == other.settings.get<coord_t>("wall_transition_filter_distance") && 
                         settings.get<coord_t>("wall_transition_filter_deviation") == other.settings.get<coord_t>("wall_transition_filter_deviation") && 
                         settings.get<coord_t>("wall_line_width_x") == other.settings.get<coord_t>("wall_line_width_x") && 
                         settings.get<int>("meshfix_maximum_extrusion_area_deviation") == other.settings.get<int>("meshfix_maximum_extrusion_area_deviation"))
                    )
#endif
               && raft_layers == other.raft_layers
            ;
    }

    [[nodiscard]] inline coord_t getRadius(size_t distance_to_top, const double elephant_foot_increases = 0) const
    {
        return (distance_to_top <= tip_layers ? min_radius + (branch_radius - min_radius) * distance_to_top / tip_layers :
                       branch_radius +
                       (distance_to_top - tip_layers) * branch_radius_increase_per_layer)
               +
               elephant_foot_increases * (std::max(bp_radius_increase_per_layer - branch_radius_increase_per_layer, 0.0));
    }

    [[nodiscard]] inline coord_t recommendedMinRadius(LayerIndex layer_idx) const
    {
        double num_layers_widened = layer_start_bp_radius - layer_idx;
        return num_layers_widened > 0 ? branch_radius + num_layers_widened * bp_radius_increase_per_layer : 0;
    }

#if 0
    [[nodiscard]] inline coord_t getActualZ(LayerIndex layer_idx)
    {
        return layer_idx < coord_t(known_z.size()) ? known_z[layer_idx] : (layer_idx - known_z.size()) * layer_height + known_z.size() ? known_z.back() : 0;
    }

    void setActualZ(std::vector<coord_t>& z)
    {
        known_z = z;
    }
#endif

private:
};

inline double tiny_area_threshold() { return sqr(scaled<double>(0.001)); }

inline void tree_supports_show_error(std::string_view message, bool critical)
{
    printf("Error: %s, critical: %d\n", message.data(), int(critical));
#ifdef TREE_SUPPORT_SHOW_ERRORS_WIN32
    static bool g_showed_critical_error = false;
    static bool g_showed_performance_warning = false;
    auto bugtype = std::string(critical ? " This is a critical bug. It may cause missing or malformed branches.\n" : "This bug should only decrease performance.\n");
    bool show    = (critical && !g_showed_critical_error) || (!critical && !g_showed_performance_warning);
    (critical ? g_showed_critical_error : g_showed_performance_warning) = true;
    if (show)
        MessageBoxA(nullptr, std::string("TreeSupport_2 MOD detected an error while generating the tree support.\nPlease report this back to me with profile and model.\nRevision 5.0\n" + std::string(message) + "\n" + bugtype).c_str(), 
            "Bug detected!", MB_OK | MB_SYSTEMMODAL | MB_SETFOREGROUND | MB_ICONWARNING);
#endif
}

inline double layer_z(const SlicingParameters &slicing_params, const TreeSupportSettings &config, const size_t layer_idx)
{
    return layer_idx >= config.raft_layers.size() ? 
        slicing_params.object_print_z_min + slicing_params.first_object_layer_height + (layer_idx - config.raft_layers.size()) * slicing_params.layer_height :
        config.raft_layers[layer_idx];
}

inline double first_object_support_layer_z(const SlicingParameters &slicing_params)
{
    return slicing_params.object_print_z_min + slicing_params.first_object_layer_height;
}

inline LayerIndex layer_idx_ceil(const SlicingParameters &slicing_params, const TreeSupportSettings &config, const double z)
{
    const double first_object_z = first_object_support_layer_z(slicing_params);
    if (!config.raft_layers.empty() && z < first_object_z - EPSILON) {
        auto it = std::lower_bound(config.raft_layers.begin(), config.raft_layers.end(), z - EPSILON);
        return LayerIndex(it == config.raft_layers.end() ? config.raft_layers.size() : std::distance(config.raft_layers.begin(), it));
    }
    return LayerIndex(config.raft_layers.size()) +
        std::max<LayerIndex>(0, LayerIndex(std::ceil((z - first_object_z) / slicing_params.layer_height)));
}
inline LayerIndex layer_idx_floor(const SlicingParameters &slicing_params, const TreeSupportSettings &config, const double z)
{
    const double first_object_z = first_object_support_layer_z(slicing_params);
    if (!config.raft_layers.empty() && z < first_object_z - EPSILON) {
        auto it = std::upper_bound(config.raft_layers.begin(), config.raft_layers.end(), z + EPSILON);
        return LayerIndex(it == config.raft_layers.begin() ? 0 : std::distance(config.raft_layers.begin(), it) - 1);
    }
    return LayerIndex(config.raft_layers.size()) +
        std::max<LayerIndex>(0, LayerIndex(std::floor((z - first_object_z) / slicing_params.layer_height)));
}

inline SupportGeneratorLayer& layer_initialize(
    SupportGeneratorLayer     &layer_new,
    const SlicingParameters   &slicing_params,
    const TreeSupportSettings &config, 
    const size_t               layer_idx)
{
    layer_new.print_z  = layer_z(slicing_params, config, layer_idx);
    layer_new.bottom_z = layer_idx > 0 ? layer_z(slicing_params, config, layer_idx - 1) : 0.;
    if (layer_idx == 0 && layer_new.print_z < 0.)
        layer_new.bottom_z = layer_new.print_z - slicing_params.layer_height;
    layer_new.height   = layer_new.print_z - layer_new.bottom_z;
    return layer_new;
}

inline SupportGeneratorLayer& layer_allocate_unguarded(
    SupportGeneratorLayerStorage      &layer_storage,
    SupporLayerType                    layer_type,
    const SlicingParameters           &slicing_params,
    const TreeSupportSettings         &config, 
    size_t                             layer_idx)
{
    SupportGeneratorLayer &layer = layer_storage.allocate_unguarded(layer_type);
    return layer_initialize(layer, slicing_params, config, layer_idx);
}

inline SupportGeneratorLayer& layer_allocate(
    SupportGeneratorLayerStorage      &layer_storage,
    SupporLayerType                    layer_type,
    const SlicingParameters           &slicing_params,
    const TreeSupportSettings         &config, 
    size_t                             layer_idx)
{
    SupportGeneratorLayer &layer = layer_storage.allocate(layer_type);
    return layer_initialize(layer, slicing_params, config, layer_idx);
}

class InterfacePlacer {
public:
    InterfacePlacer(
        const SlicingParameters         &slicing_parameters, 
        const SupportParameters         &support_parameters,
        const TreeSupportSettings       &config, 
        SupportGeneratorLayerStorage    &layer_storage, 
        SupportGeneratorLayersPtr       &top_contacts,
        SupportGeneratorLayersPtr       &top_interfaces,
        SupportGeneratorLayersPtr       &top_base_interfaces) 
    :
        slicing_parameters(slicing_parameters), support_parameters(support_parameters), config(config),
        layer_storage(layer_storage), top_contacts(top_contacts), top_interfaces(top_interfaces), top_base_interfaces(top_base_interfaces)  
    {}
    InterfacePlacer(const InterfacePlacer& rhs) :
        slicing_parameters(rhs.slicing_parameters), support_parameters(rhs.support_parameters), config(rhs.config),
        layer_storage(rhs.layer_storage), top_contacts(rhs.top_contacts), top_interfaces(rhs.top_interfaces), top_base_interfaces(rhs.top_base_interfaces) 
    {}

    const SlicingParameters    &slicing_parameters;
    const SupportParameters    &support_parameters;
    const TreeSupportSettings  &config;
    SupportGeneratorLayersPtr&  top_contacts_mutable() { return this->top_contacts; }

public:
    void add_roofs(std::vector<Polygons> &&new_roofs, const size_t insert_layer_idx)
    {
        if (! new_roofs.empty()) {
            std::lock_guard<std::mutex> lock(m_mutex_layer_storage);
            for (size_t idx = 0; idx < new_roofs.size(); ++ idx)
                if (! new_roofs[idx].empty())
                    add_roof_unguarded(std::move(new_roofs[idx]), insert_layer_idx - idx, idx);
        }
    }

    void add_roof(Polygons &&new_roof, const size_t insert_layer_idx, const size_t dtt_tip)
    {
        std::lock_guard<std::mutex> lock(m_mutex_layer_storage);
        add_roof_unguarded(std::move(new_roof), insert_layer_idx, dtt_tip);
    }

    void add_roof_build_plate(Polygons &&overhang_areas, size_t dtt_roof)
    {
        std::lock_guard<std::mutex> lock(m_mutex_layer_storage);
        this->add_roof_unguarded(std::move(overhang_areas), 0, std::min(dtt_roof, this->support_parameters.num_top_interface_layers));
    }

    void add_roof_unguarded(Polygons &&new_roofs, const size_t insert_layer_idx, const size_t dtt_roof)
    {
        assert(support_parameters.has_top_contacts);
        assert(dtt_roof <= support_parameters.num_top_interface_layers);
        size_t interface_threshold = support_parameters.num_top_interface_layers_only();
        if (interface_threshold > 0 && support_parameters.num_top_base_interface_layers > 0)
            --interface_threshold;
        SupportGeneratorLayersPtr &layers =
            dtt_roof == 0 ? this->top_contacts :
            dtt_roof <= interface_threshold ? this->top_interfaces : this->top_base_interfaces;
        SupportGeneratorLayer*& l = layers[insert_layer_idx];
        if (l == nullptr)
            l = &layer_allocate_unguarded(layer_storage, dtt_roof == 0 ? SupporLayerType::TopContact : SupporLayerType::TopInterface, 
                    slicing_parameters, config, insert_layer_idx);
        append(l->polygons, std::move(new_roofs));
    }

private:
    SupportGeneratorLayerStorage                       &layer_storage;
    SupportGeneratorLayersPtr                          &top_contacts;
    SupportGeneratorLayersPtr                          &top_interfaces;
    SupportGeneratorLayersPtr                          &top_base_interfaces;

    std::mutex                                          m_mutex_layer_storage;
};

enum class LineStatus
{
    INVALID,
    TO_MODEL,
    TO_MODEL_GRACIOUS,
    TO_MODEL_GRACIOUS_SAFE,
    TO_BP,
    TO_BP_SAFE
};


}

}

#endif
