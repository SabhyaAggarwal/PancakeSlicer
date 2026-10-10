// Original CuraEngine copyright:
// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef slic3r_TreeSupport_hpp
#define slic3r_TreeSupport_hpp

#include "libslic3r/Polygon.hpp"
#include "SupportLayer.hpp"
#include "TreeModelVolumes.hpp"
#include "TreeSupportCommon.hpp"

#include "../Point.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/container/small_vector.hpp>
#include <cstdint>
#include <limits>
#include <cstddef>
#include <utility>
#include <deque>
#include <vector>
#include <functional>

namespace Slic3r { class TreeSupport; }


#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    #define TREE_PROGRESS_TOTAL 10000
    #define TREE_PROGRESS_PRECALC_COLL TREE_PROGRESS_TOTAL * 0.1
    #define TREE_PROGRESS_PRECALC_AVO TREE_PROGRESS_TOTAL * 0.4
    #define TREE_PROGRESS_GENERATE_NODES TREE_PROGRESS_TOTAL * 0.1
    #define TREE_PROGRESS_AREA_CALC TREE_PROGRESS_TOTAL * 0.3
    #define TREE_PROGRESS_DRAW_AREAS TREE_PROGRESS_TOTAL * 0.1
    #define TREE_PROGRESS_GENERATE_BRANCH_AREAS TREE_PROGRESS_DRAW_AREAS / 3
    #define TREE_PROGRESS_SMOOTH_BRANCH_AREAS TREE_PROGRESS_DRAW_AREAS / 3
    #define TREE_PROGRESS_FINALIZE_BRANCH_AREAS TREE_PROGRESS_DRAW_AREAS / 3
#endif

namespace Slic3r
{

class Print;
class PrintObject;
struct SlicingParameters;

namespace TreeSupport3D
{


struct AreaIncreaseSettings
{
    AreaIncreaseSettings(
        TreeModelVolumes::AvoidanceType type = TreeModelVolumes::AvoidanceType::Fast, coord_t increase_speed = 0, 
        bool increase_radius = false, bool no_error = false, bool use_min_distance = false, bool move = false) :
        increase_speed{ increase_speed }, type{ type }, increase_radius{ increase_radius }, no_error{ no_error }, use_min_distance{ use_min_distance }, move{ move } {}

    coord_t         increase_speed;
    TreeModelVolumes::AvoidanceType type;
    bool            increase_radius  : 1;
    bool            no_error         : 1;
    bool            use_min_distance : 1;
    bool            move             : 1;
    bool operator==(const AreaIncreaseSettings& other) const
    {
        return type             == other.type               &&
               increase_speed   == other.increase_speed     &&
               increase_radius  == other.increase_radius    &&
               no_error         == other.no_error           &&
               use_min_distance == other.use_min_distance   &&
               move             == other.move;
    }
};

#define TREE_SUPPORTS_TRACK_LOST

struct SupportElementStateBits {
    SupportElementStateBits() :
        to_buildplate(false),
        to_model_gracious(false),
        use_min_xy_dist(false),
        supports_roof(false),
        can_use_safe_radius(false),
        skip_ovalisation(false),
#ifdef TREE_SUPPORTS_TRACK_LOST
        lost(false),
        verylost(false),
#endif
        deleted(false),
        marked(false)
        {}

    bool to_buildplate : 1;

    bool to_model_gracious : 1;

    bool use_min_xy_dist : 1;

    bool supports_roof : 1;

    bool can_use_safe_radius : 1;

    bool skip_ovalisation : 1;

#ifdef TREE_SUPPORTS_TRACK_LOST
    bool lost : 1;
    bool verylost : 1;
#endif
    bool deleted : 1;

    bool marked : 1;
};

struct SupportElementState : public SupportElementStateBits
{
    int type = 0;
    coordf_t radius = 0;
    float print_z = 0;

    LayerIndex  target_height;

    Point       target_position;

    Point       next_position;

    LayerIndex  layer_idx;

    uint32_t    effective_radius_height;

    uint32_t    distance_to_top;

    Point result_on_layer { std::numeric_limits<coord_t>::max(), std::numeric_limits<coord_t>::max() };
    bool  result_on_layer_is_set() const { return this->result_on_layer != Point{ std::numeric_limits<coord_t>::max(), std::numeric_limits<coord_t>::max() }; }
    void  result_on_layer_reset() { this->result_on_layer = Point{ std::numeric_limits<coord_t>::max(), std::numeric_limits<coord_t>::max() }; }
    coord_t     increased_to_model_radius;
    double      elephant_foot_increases;

    uint32_t    dont_move_until;

    AreaIncreaseSettings last_area_increase;

    uint32_t    missing_roof_layers = 0;

    uint32_t    roof_recovery_dtt = 0;

    void set_pending_roof_recovery(uint32_t pending_layers, uint32_t recovery_depth)
    {
        this->missing_roof_layers = pending_layers;
        this->roof_recovery_dtt = pending_layers > 0 ? recovery_depth : 0;
    }

    [[nodiscard]] static SupportElementState propagate_down(const SupportElementState &src)
    {
        SupportElementState dst{ src };
        ++ dst.distance_to_top;
        -- dst.layer_idx;
        if (dst.has_pending_roof_recovery()) {
            -- dst.missing_roof_layers;
            ++ dst.roof_recovery_dtt;
        }
        dst.result_on_layer_reset();
        dst.skip_ovalisation = false;
        return dst;
    }

    [[nodiscard]] bool locked() const { return this->distance_to_top < this->dont_move_until; }
    [[nodiscard]] bool has_pending_roof_recovery() const { return this->missing_roof_layers > 0; }
};

[[nodiscard]] inline size_t getEffectiveDTT(const TreeSupportSettings &settings, const SupportElementState &elem)
{
    return elem.effective_radius_height < settings.increase_radius_until_layer ? 
        (elem.distance_to_top < settings.increase_radius_until_layer ? elem.distance_to_top : settings.increase_radius_until_layer) : 
        elem.effective_radius_height;
}

[[nodiscard]] inline coord_t support_element_radius(const TreeSupportSettings &settings, const SupportElementState &elem)
{ 
    return settings.getRadius(getEffectiveDTT(settings, elem), elem.elephant_foot_increases);
}

[[nodiscard]] inline coord_t support_element_collision_radius(const TreeSupportSettings &settings, const SupportElementState &elem)
{
    return settings.getRadius(elem.effective_radius_height, elem.elephant_foot_increases);
}

struct SupportElement
{
    using ParentIndices =
#ifdef NDEBUG
        boost::container::small_vector<int32_t, 4>;
#else
        std::vector<int32_t>;
#endif
    SupportElement(const SupportElementState &state, Polygons &&influence_area) : state(state), influence_area(std::move(influence_area)) {}
    SupportElement(const SupportElementState &state, ParentIndices &&parents, Polygons &&influence_area) :
        state(state), parents(std::move(parents)), influence_area(std::move(influence_area)) {}

    SupportElementState         state;

    ParentIndices               parents;

    Polygons                    influence_area;
};

using SupportElements = std::deque<SupportElement>;

[[nodiscard]] inline coord_t support_element_radius(const TreeSupportSettings &settings, const SupportElement &elem)
{
    return support_element_radius(settings, elem.state);
}

[[nodiscard]] inline coord_t support_element_collision_radius(const TreeSupportSettings &settings, const SupportElement &elem)
{
    return support_element_collision_radius(settings, elem.state);
}

void organic_draw_branches(
    PrintObject                     &print_object,
    TreeModelVolumes                &volumes, 
    const TreeSupportSettings       &config,
    std::vector<SupportElements>    &move_bounds,

    SupportGeneratorLayersPtr       &bottom_contacts,
    SupportGeneratorLayersPtr       &top_contacts,
    InterfacePlacer                 &interface_placer,

    SupportGeneratorLayersPtr       &intermediate_layers,
    SupportGeneratorLayerStorage    &layer_storage,

    std::function<void()> throw_on_cancel);

}

void generate_tree_support_3D(PrintObject &print_object, TreeSupport* tree_support, std::function<void()> throw_on_cancel = []{});

}

#endif
