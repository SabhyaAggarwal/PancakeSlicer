// Original CuraEngine copyright:
// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef slic3r_TreeModelVolumes_hpp
#define slic3r_TreeModelVolumes_hpp

#include <cstddef>
#include <cstdint>
#include <functional>
#include <cassert>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>

#include <boost/functional/hash.hpp>
#include <vector>
#include <utility>

#include "TreeSupportCommon.hpp"

#include "../Point.hpp"
#include "../Polygon.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r
{

class BuildVolume;
class PrintObject;

namespace TreeSupport3D
{

static constexpr const double  SUPPORT_TREE_EXPONENTIAL_FACTOR = 1.5;
#define SUPPORT_TREE_EXPONENTIAL_THRESHOLD  scaled<coord_t>(1. * SUPPORT_TREE_EXPONENTIAL_FACTOR)
#define SUPPORT_TREE_COLLISION_RESOLUTION  scaled<coord_t>(0.5)
static constexpr const bool    SUPPORT_TREE_AVOID_SUPPORT_BLOCKER = true;

class TreeModelVolumes
{
public:
    TreeModelVolumes() = default;
    explicit TreeModelVolumes(const PrintObject &print_object, const BuildVolume &build_volume,
        coord_t max_move, coord_t max_move_slow, size_t current_mesh_idx, 
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
        double progress_multiplier, 
        double progress_offset, 
#endif
        const std::vector<Polygons> &additional_excluded_areas = {});
    TreeModelVolumes(TreeModelVolumes&&) = default;
    TreeModelVolumes& operator=(TreeModelVolumes&&) = default;

    TreeModelVolumes(const TreeModelVolumes&) = delete;
    TreeModelVolumes& operator=(const TreeModelVolumes&) = delete;

    void clear() { 
        this->clear_all_but_object_collision();
        m_collision_cache.clear();
        m_placeable_areas_cache.clear();
    }
    void clear_all_but_object_collision() { 
        m_collision_cache_holefree.clear();
        m_avoidance_cache.clear();
        m_avoidance_cache_slow.clear();
        m_avoidance_cache_to_model.clear();
        m_avoidance_cache_to_model_slow.clear();
        m_placeable_areas_cache.clear_all_but_radius0();
        m_avoidance_cache_holefree.clear();
        m_avoidance_cache_holefree_to_model.clear();
        m_wall_restrictions_cache.clear();
        m_wall_restrictions_cache_min.clear();
    }

    enum class AvoidanceType : int8_t
    {
        Slow,
        FastSafe,
        Fast,
        Count
    };

    void precalculate(const PrintObject& print_object, const coord_t max_layer, std::function<void()> throw_on_cancel);

    const Polygons& getCollision(const coord_t radius, LayerIndex layer_idx, bool min_xy_dist) const;

    std::optional<std::pair<coord_t, std::reference_wrapper<const Polygons>>> get_collision_lower_bound_area(LayerIndex layer_id, coord_t max_radius) const;

    const Polygons& getAvoidance(coord_t radius, LayerIndex layer_idx, AvoidanceType type, bool to_model, bool min_xy_dist) const;
    const Polygons& getPlaceableAreas(coord_t radius, LayerIndex layer_idx, std::function<void()> throw_on_cancel) const;
    const Polygons& getWallRestriction(coord_t radius, LayerIndex layer_idx, bool min_xy_dist) const;
    coord_t ceilRadius(const coord_t radius, const bool min_xy_dist) const {
        assert(radius >= 0);
        return min_xy_dist ? 
            this->ceilRadius(radius) :
            radius > 0 ? this->ceilRadius(radius + m_current_min_xy_dist_delta) : m_current_min_xy_dist_delta;
    }
    coord_t getRadiusNextCeil(coord_t radius, bool min_xy_dist) const {
        assert(radius > 0);
        return min_xy_dist ?
            this->ceilRadius(radius) :
            this->ceilRadius(radius + m_current_min_xy_dist_delta) - m_current_min_xy_dist_delta;
    }

    Polygon m_bed_area;
    std::vector<Polygons> m_belt_floor;

private:
    class LayerPolygonCache {
    public:
        void allocate(LayerIndex aidx_begin, LayerIndex aidx_end) {
            m_idx_begin = aidx_begin;
            m_idx_end = aidx_end;
            m_polygons.assign(aidx_end - aidx_begin, {});
        }

        LayerIndex begin() const { return m_idx_begin; }
        LayerIndex end()   const { return m_idx_end; }
        size_t     size()  const { return m_polygons.size(); }

        bool      has(LayerIndex idx) const { return idx >= m_idx_begin && idx < m_idx_end; }
        Polygons& operator[](LayerIndex idx) { assert(idx >= m_idx_begin && idx < m_idx_end); return m_polygons[idx - m_idx_begin]; }
        std::vector<Polygons>& polygons_mutable() { return m_polygons; }

    private:
        std::vector<Polygons> m_polygons;
        LayerIndex            m_idx_begin;
        LayerIndex            m_idx_end;
    };

    using RadiusLayerPair             = std::pair<coord_t, LayerIndex>;
    class RadiusLayerPolygonCache {
        using LayerData = std::map<coord_t, Polygons>;
        using Layers = std::vector<LayerData>;
    public:
        RadiusLayerPolygonCache() = default;
        RadiusLayerPolygonCache(RadiusLayerPolygonCache &&rhs) : m_data(std::move(rhs.m_data)) {}
        RadiusLayerPolygonCache& operator=(RadiusLayerPolygonCache &&rhs) { m_data = std::move(rhs.m_data); return *this; }

        RadiusLayerPolygonCache(const RadiusLayerPolygonCache&) = delete;
        RadiusLayerPolygonCache& operator=(const RadiusLayerPolygonCache&) = delete;

        void insert(std::vector<std::pair<RadiusLayerPair, Polygons>> &&in) {
            std::lock_guard<std::mutex> guard(m_mutex);
            for (auto &d : in)
                this->get_allocate_layer_data(d.first.second).emplace(d.first.first, std::move(d.second));
        }
        void insert(std::vector<std::pair<coord_t, Polygons>> &&in, coord_t radius) {
            std::lock_guard<std::mutex> guard(m_mutex);
            for (auto &d : in)
                this->get_allocate_layer_data(d.first).emplace(radius, std::move(d.second));
        }
        void insert(std::vector<Polygons> &&in, coord_t first_layer_idx, coord_t radius) {
            std::lock_guard<std::mutex> guard(m_mutex);
            allocate_layers(first_layer_idx + in.size());
            for (auto &d : in)
                m_data[first_layer_idx ++].emplace(radius, std::move(d));
        }
        void insert(LayerPolygonCache &&in, coord_t radius) {
            std::lock_guard<std::mutex> guard(m_mutex);
            LayerIndex i = in.begin();
            allocate_layers(i + LayerIndex(in.size()));
            for (auto &d : in.polygons_mutable())
                m_data[i ++].emplace(radius, std::move(d));
        }
        std::optional<std::reference_wrapper<const Polygons>> getArea(const TreeModelVolumes::RadiusLayerPair &key) const {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (key.second >= LayerIndex(m_data.size()))
                return std::optional<std::reference_wrapper<const Polygons>>{};
            const auto &layer = m_data[key.second];
            auto it = layer.find(key.first);
            return it == layer.end() ? 
                std::optional<std::reference_wrapper<const Polygons>>{} : std::optional<std::reference_wrapper<const Polygons>>{ it->second };
        }
        std::optional<std::pair<coord_t, std::reference_wrapper<const Polygons>>> get_lower_bound_area(const TreeModelVolumes::RadiusLayerPair &key) const {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (key.second >= LayerIndex(m_data.size()))
                return {};
            const auto &layer = m_data[key.second];
            if (layer.empty())
                return {};
            auto it = layer.lower_bound(key.first);
            if (it == layer.end() || it->first != key.first) {
                if (it == layer.begin())
                    return {};
                -- it;
            }
            return std::make_pair(it->first, std::reference_wrapper<const Polygons>(it->second));
        }
        LayerIndex getMaxCalculatedLayer(coord_t radius) const {
            std::lock_guard<std::mutex> guard(m_mutex);
            auto layer_idx = LayerIndex(m_data.size()) - 1;
            for (; layer_idx > 0; -- layer_idx)
                if (const auto &layer = m_data[layer_idx]; layer.find(radius) != layer.end())
                    break;
            return layer_idx == 0 ? -1 : layer_idx;
        }

        [[nodiscard]] std::vector<std::pair<RadiusLayerPair, std::reference_wrapper<const Polygons>>> sorted() const;

        void clear() { m_data.clear(); }
        void clear_all_but_radius0() { 
            for (LayerData &l : m_data) {
                auto begin = l.begin();
                auto end = l.end();
                if (begin != end && ++ begin != end)
                    l.erase(begin, end);
            }
        }

    private:
        LayerData&          get_allocate_layer_data(LayerIndex layer_idx) {
            allocate_layers(layer_idx + 1);
            return m_data[layer_idx];
        }
        void                allocate_layers(size_t num_layers);

        Layers              m_data;
        mutable std::mutex  m_mutex;
    };


    const Polygons& getCollisionHolefree(coord_t radius, LayerIndex layer_idx) const;

    coord_t ceilRadius(const coord_t radius) const;

    void calculateCollision(const std::vector<RadiusLayerPair> &keys, std::function<void()> throw_on_cancel);
    void calculateCollision(const coord_t radius, const LayerIndex max_layer_idx, std::function<void()> throw_on_cancel);
    void calculateCollisionHolefree(const std::vector<RadiusLayerPair> &keys, std::function<void()> throw_on_cancel);

    void calculateCollisionHolefree(RadiusLayerPair key)
    {
        calculateCollisionHolefree(std::vector<RadiusLayerPair>{ RadiusLayerPair(key) }, []{});
    }

    void calculateAvoidance(const std::vector<RadiusLayerPair> &keys, bool to_build_plate, bool to_model, std::function<void()> throw_on_cancel);

    void calculateAvoidance(RadiusLayerPair key, bool to_build_plate, bool to_model)
    {
        calculateAvoidance(std::vector<RadiusLayerPair>{ RadiusLayerPair(key) }, to_build_plate, to_model, []{});
    }

    void calculatePlaceables(const coord_t radius, const LayerIndex max_required_layer, std::function<void()> throw_on_cancel);


    void calculatePlaceables(const std::vector<RadiusLayerPair> &keys, std::function<void()> throw_on_cancel);

    void calculateWallRestrictions(const std::vector<RadiusLayerPair> &keys, std::function<void()> throw_on_cancel);

    void calculateWallRestrictions(RadiusLayerPair key)
    {
        calculateWallRestrictions(std::vector<RadiusLayerPair>{ RadiusLayerPair(key) }, []{});
    }

    coord_t m_max_move;
    coord_t m_max_move_slow;
    coord_t m_min_resolution;

    bool m_precalculated = false;
    size_t m_current_outline_idx;
    coord_t m_current_min_xy_dist;
    coord_t m_current_min_xy_dist_delta;
    bool m_support_rests_on_model;
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    coord_t m_precalculation_progress = 0;
    double m_progress_multiplier;
    double m_progress_offset;
#endif
    coord_t m_increase_until_radius;

    Polygons m_machine_border;
    std::vector<std::pair<TreeSupportMeshGroupSettings, std::vector<Polygons>>> m_layer_outlines;
    std::vector<Polygons> m_anti_overhang;
    std::vector<coord_t> m_ignorable_radii;

    coord_t m_radius_0;

    std::vector<double>         m_raft_layers;

    RadiusLayerPolygonCache     m_collision_cache;
    RadiusLayerPolygonCache     m_collision_cache_holefree;
    RadiusLayerPolygonCache     m_avoidance_cache;
    RadiusLayerPolygonCache     m_avoidance_cache_slow;
    RadiusLayerPolygonCache     m_avoidance_cache_to_model;
    RadiusLayerPolygonCache     m_avoidance_cache_to_model_slow;
    RadiusLayerPolygonCache     m_placeable_areas_cache;

    RadiusLayerPolygonCache     m_avoidance_cache_holefree;
    RadiusLayerPolygonCache     m_avoidance_cache_holefree_to_model;

    RadiusLayerPolygonCache& avoidance_cache(const AvoidanceType type, const bool to_model) {
        if (to_model) {
            switch (type) {
            case AvoidanceType::Fast:       return m_avoidance_cache_to_model;
            case AvoidanceType::Slow:       return m_avoidance_cache_to_model_slow;
            case AvoidanceType::Count:      assert(false);
            case AvoidanceType::FastSafe:   return m_avoidance_cache_holefree_to_model;
            }
        } else {
            switch (type) {
            case AvoidanceType::Fast:       return m_avoidance_cache;
            case AvoidanceType::Slow:       return m_avoidance_cache_slow;
            case AvoidanceType::Count:      assert(false);
            case AvoidanceType::FastSafe:   return m_avoidance_cache_holefree;
            }
        }
        assert(false);
        return m_avoidance_cache;
    }
    const RadiusLayerPolygonCache& avoidance_cache(const AvoidanceType type, const bool to_model) const {
        return const_cast<TreeModelVolumes*>(this)->avoidance_cache(type, to_model);
    }

    RadiusLayerPolygonCache     m_wall_restrictions_cache;

    RadiusLayerPolygonCache     m_wall_restrictions_cache_min;

#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    std::unique_ptr<std::mutex> m_critical_progress { std::make_unique<std::mutex>() };
#endif
};

}
}

#endif
