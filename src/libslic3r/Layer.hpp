#ifndef slic3r_Layer_hpp_
#define slic3r_Layer_hpp_

#include "Polyline.hpp"
#include "Polygon.hpp"
#include "Line.hpp"
#include "libslic3r.h"
#include "BoundingBox.hpp"
#include "Flow.hpp"
#include "SurfaceCollection.hpp"
#include "ExtrusionEntityCollection.hpp"
#include <vector>
#include <cstddef>
#include <algorithm>
#include <string>
#include <functional>
#include "ExPolygon.hpp"

namespace Slic3r { class ExtrusionLoop; }
namespace Slic3r { class ExtrusionMultiPath; }
namespace Slic3r { class ExtrusionPath; }
namespace Slic3r {

class ExPolygon;
using ExPolygons = std::vector<ExPolygon>;
class Layer;
using LayerPtrs = std::vector<Layer*>;
class LayerRegion;
using LayerRegionPtrs = std::vector<LayerRegion*>;
class PrintRegion;
class PrintRegionConfig;
class PrintObject;
class Print;

namespace FillAdaptive {
    struct RegionOctrees;
};

namespace FillLightning {
    class Generator;
};

namespace sla {
    class IndexedMesh;
};

class LayerRegion
{
public:
    Layer*                      layer()         { return m_layer; }
    const Layer*                layer() const   { return m_layer; }
    const PrintRegion&          region() const  { return *m_region; }

    const SurfaceCollection& get_slices() const { return slices; }

    SurfaceCollection           slices;
    ExPolygons                  raw_slices;

    ExtrusionEntityCollection   thin_fills;

    ExPolygons                  fill_expolygons;
    SurfaceCollection           fill_surfaces;
    ExPolygons                  fill_no_overlap_expolygons;

    Polylines          			unsupported_bridge_edges;

    ExtrusionEntityCollection   perimeters;

    ExtrusionEntityCollection   fills;

    Flow    flow(FlowRole role) const;
    Flow    flow(FlowRole role, double layer_height) const;
    Flow    bridging_flow(FlowRole role, bool thick_bridge = false) const;

    void    slices_to_fill_surfaces_clipped();
    void    prepare_fill_surfaces();
    void    make_perimeters(const SurfaceCollection &slices, const LayerRegionPtrs &compatible_regions, SurfaceCollection* fill_surfaces, ExPolygons* fill_no_overlap);
    void    process_external_surfaces(const Layer *lower_layer, const Polygons *lower_layer_covered);
    double  infill_area_threshold() const;
    void    trim_surfaces(const Polygons &trimming_polygons);
    void    elephant_foot_compensation_step(const float elephant_foot_compensation_perimeter_step, const Polygons &trimming_polygons);

    void    export_region_slices_to_svg(const char *path) const;
    void    export_region_fill_surfaces_to_svg(const char *path) const;
    void    export_region_slices_to_svg_debug(const char *name) const;
    void    export_region_fill_surfaces_to_svg_debug(const char *name) const;

    bool    has_extrusions() const { return ! this->perimeters.entities.empty() || ! this->fills.entities.empty(); }
    void    simplify_infill_extrusion_entity() { simplify_entity_collection(&fills); }
    void    simplify_wall_extrusion_entity() { simplify_entity_collection(&perimeters); }
private:
    void    simplify_entity_collection(ExtrusionEntityCollection* entity_collection);
    void    simplify_path(ExtrusionPath* path);
    void    simplify_multi_path(ExtrusionMultiPath* multipath);
    void    simplify_loop(ExtrusionLoop* loop);

protected:
    friend class Layer;
    friend class PrintObject;

    LayerRegion(Layer *layer, const PrintRegion *region) : m_layer(layer), m_region(region) {}
    ~LayerRegion() {}

private:
    Layer             *m_layer;
    const PrintRegion *m_region;
};

class Layer
{
public:
    size_t              id() const          { return m_id; }
    void                set_id(size_t id)   { m_id = id; }
    PrintObject*        object()            { return m_object; }
    const PrintObject*  object() const      { return m_object; }

    Layer              *upper_layer;
    Layer              *lower_layer;
    bool                slicing_errors;
    coordf_t            slice_z;
    coordf_t            print_z;
    coordf_t            height;
    coordf_t            bottom_z() const { return this->print_z - this->height; }

    CurledLines         curled_lines;

    mutable ExPolygons          sharp_tails;
    mutable ExPolygons          cantilevers;
    mutable std::vector<float>  sharp_tails_height;

    ExPolygons 				 lslices;
    ExPolygons 				 lslices_extrudable;
    std::vector<BoundingBox> lslices_bboxes;
    std::vector<size_t>      lslices_separated_component_ids;

    ExPolygons              loverhangs;
    BoundingBox             loverhangs_bbox;

    std::vector<int>        conformal_patches_before;
    std::vector<int>        conformal_patches_after;
    bool                    conformal_covered { false };
    size_t                  region_count() const { return m_regions.size(); }
    const LayerRegion*      get_region(int idx) const { return m_regions[idx]; }
    LayerRegion*            get_region(int idx) { return m_regions[idx]; }
    LayerRegion*            add_region(const PrintRegion *print_region);
    const LayerRegionPtrs&  regions() const { return m_regions; }
    bool                    empty() const;
    void                    make_slices();
    void                    backup_untyped_slices();
    void                    restore_untyped_slices();
    void                    restore_untyped_slices_no_extra_perimeters();
    ExPolygons              merged(float offset) const;
    template <class T> bool any_internal_region_slice_contains(const T &item) const {
        for (const LayerRegion *layerm : m_regions) if (layerm->slices.any_internal_contains(item)) return true;
        return false;
    }
    template <class T> bool any_bottom_region_slice_contains(const T &item) const {
        for (const LayerRegion *layerm : m_regions) if (layerm->slices.any_bottom_contains(item)) return true;
        return false;
    }

    static bool             is_perimeter_compatible(const Print& print, const PrintRegion& a, const PrintRegion& b);
    void                    make_perimeters();
    void                    make_fills() { this->make_fills(nullptr); }
    void                    make_fills(const FillAdaptive::RegionOctrees* fill_octrees, FillLightning::Generator* lightning_generator = nullptr);
    Polylines               generate_sparse_infill_polylines_for_anchoring(const FillAdaptive::RegionOctrees *fill_octrees,
                                                                           FillLightning::Generator* lightning_generator) const;
    void 					make_ironing();
    static int              choose_ironing_extruder(const PrintRegionConfig &cfg,
                                                    bool spiral_mode,
                                                    bool is_topmost_layer);
    void                    make_contour_z(const sla::IndexedMesh &mesh);

    void                    export_region_slices_to_svg(const char *path) const;
    void                    export_region_fill_surfaces_to_svg(const char *path) const;
    void                    export_region_slices_to_svg_debug(const char *name) const;
    void                    export_region_fill_surfaces_to_svg_debug(const char *name) const;

    virtual bool            has_extrusions() const {
        if (!conformal_patches_before.empty() || !conformal_patches_after.empty()) return true;
        for (auto layerm : m_regions) if (layerm->has_extrusions()) return true;
        return false;
    }

    void simplify_wall_extrusion_path() { for (auto layerm : m_regions) layerm->simplify_wall_extrusion_entity();}
    void simplify_infill_extrusion_path() { for (auto layerm : m_regions) layerm->simplify_infill_extrusion_entity(); }
    coordf_t get_sparse_infill_max_void_area();

    template<typename IteratorType, typename IndexType, typename FN_HIGHER_EQUAL>
    static IndexType idx_higher_or_equal(IteratorType begin, IteratorType end, IndexType idx, FN_HIGHER_EQUAL fn_higher_equal)
    {
        auto size = int(end - begin);
        if (size == 0) {
            idx = 0;
            }
        else if (idx == IndexType(-1)) {
            int idx_low = 0;
            int idx_high = std::max(0, size - 1);
            while (idx_low + 1 < idx_high) {
                int idx_mid = (idx_low + idx_high) / 2;
                if (fn_higher_equal(begin[idx_mid]))
                    idx_high = idx_mid;
                else
                    idx_low = idx_mid;
                }
            idx = fn_higher_equal(begin[idx_low]) ? idx_low :
                (fn_higher_equal(begin[idx_high]) ? idx_high : size);
            }
        else {
            while (int(idx) < size && !fn_higher_equal(begin[idx]))
                ++idx;
            }
        return idx;
    }

    size_t get_extruder_id(unsigned int filament_id) const;

protected:
    friend class PrintObject;
    friend std::vector<Layer*> new_layers(PrintObject*, const std::vector<coordf_t>&);
    friend std::string fix_slicing_errors(PrintObject* object, LayerPtrs&, const std::function<void()>&, int &);

    Layer(size_t id, PrintObject *object, coordf_t height, coordf_t print_z, coordf_t slice_z) :
        upper_layer(nullptr), lower_layer(nullptr), slicing_errors(false),
        slice_z(slice_z), print_z(print_z), height(height),
        m_id(id), m_object(object) {}
    virtual ~Layer();

    void    simplify_support_entity_collection(ExtrusionEntityCollection* entity_collection);
    void    simplify_support_path(ExtrusionPath* path);
    void    simplify_support_multi_path(ExtrusionMultiPath* multipath);
    void    simplify_support_loop(ExtrusionLoop* loop);

private:
    size_t              m_id;
    PrintObject        *m_object;
    LayerRegionPtrs     m_regions;
};

enum SupportInnerType {
    stInnerNormal,
    stInnerTree
};

class SupportLayer : public Layer
{
public:
    ExPolygons                  support_islands;
    ExtrusionEntityCollection   support_fills;
    SupportInnerType            support_type = stInnerNormal;

    ExPolygons base_areas;


    virtual bool                has_extrusions() const { return ! support_fills.empty(); }

    size_t                      interface_id() const { return m_interface_id; }

    void simplify_support_extrusion_path() { this->simplify_support_entity_collection(&support_fills); }

protected:
    friend class PrintObject;
    friend class TreeSupport;

    SupportLayer(size_t id, size_t interface_id, PrintObject *object, coordf_t height, coordf_t print_z, coordf_t slice_z) :
        Layer(id, object, height, print_z, slice_z), m_interface_id(interface_id), support_type(stInnerNormal) {}
    virtual ~SupportLayer() = default;

    size_t m_interface_id;

    ExPolygons                                roof_areas;
    ExPolygons                                roof_1st_layer;
    ExPolygons                                floor_areas;
    ExPolygons                                roof_gap_areas;
    enum AreaType { BaseType = 0, RoofType = 1, FloorType = 2, Roof1stLayer = 3 };
    struct AreaGroup
    {
        ExPolygon *area;
        int        type;
        int interface_id = 0;
        bool interface_as_base = false;
        coordf_t   dist_to_top;
        bool need_infill = false;
        bool need_extra_wall = false;
        AreaGroup(ExPolygon *a, int t, coordf_t d) : area(a), type(t), dist_to_top(d) {}
    };
    std::vector<AreaGroup>                    area_groups;
};

template<typename LayerContainer>
inline std::vector<float> zs_from_layers(const LayerContainer &layers)
{
    std::vector<float> zs;
    zs.reserve(layers.size());
    for (const Layer *l : layers)
        zs.emplace_back((float)l->slice_z);
    return zs;
}

extern BoundingBox get_extents(const LayerRegion &layer_region);
extern BoundingBox get_extents(const LayerRegionPtrs &layer_regions);

}

#endif
