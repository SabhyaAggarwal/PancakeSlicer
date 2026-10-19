#ifndef slic3r_Print_hpp_
#define slic3r_Print_hpp_

#include "Config.hpp"
#include "Model.hpp"
#include "Polygon.hpp"
#include "Fill/FillBase.hpp"
#include "Polyline.hpp"
#include "ExtrusionEntity.hpp"
#include "Geometry.hpp"
#include "CommonDefs.hpp"
#include "PrintBase.hpp"
#include "Fill/FillAdaptive.hpp"
#include "Fill/FillLightning.hpp"
#include "Fill/FillTpmsAdaptive.hpp"

#include "BoundingBox.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Flow.hpp"
#include "Point.hpp"
#include "PrintConfig.hpp"
#include "Slicing.hpp"
#include "TriangleMesh.hpp"
#include "TriangleMeshSlicer.hpp"
#include "GCode/ToolOrdering.hpp"
#include "GCode/WipeTower.hpp"
#include "GCode/WipeTower2.hpp"
#include "GCode/ThumbnailData.hpp"
#include "GCode/GCodeProcessor.hpp"
#include "MultiMaterialSegmentation.hpp"
#include "BeltBrim.hpp"
#include "BeltTransform.hpp"
#include "ObjectID.hpp"
#include "TriangleSelector.hpp"
#include "libslic3r.h"

#include <Eigen/Geometry>

#include <cstddef>
#include <cmath>
#include <algorithm>
#include <functional>
#include <memory>
#include <map>
#include <math.h>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <utility>

#include "calib.hpp"

namespace Slic3r {

namespace Nonplanar { struct ConformalResult; struct RepairHints; class ClearanceRepair; struct MultiplanarPlan; class OrientationField; }

class SlicingErrors;

class GCode;
class Layer;
class ModelObject;
class Print;
class PrintObject;
class SupportLayer;
class TreeSupportData;
class TreeSupport;
class ExtrusionLayers;
namespace MultiNozzleUtils { class NozzleGroupResultBase; class LayeredNozzleGroupResult; }

#define MAX_OUTER_NOZZLE_DIAMETER   4
struct VolumeSlices
{
    ObjectID                volume_id;
    std::vector<ExPolygons> slices;
};

struct groupedVolumeSlices
{
    int                     groupId = -1;
    std::vector<ObjectID>   volume_ids;
    ExPolygons              slices;
};

enum SupportNecessaryType {
    NoNeedSupp=0,
    SharpTail,
    Cantilever,
    LargeOverhang,
};

namespace FillAdaptive {
    struct Octree;
    struct OctreeDeleter;
    using OctreePtr = std::unique_ptr<Octree, OctreeDeleter>;
};

namespace FillLightning {
    class Generator;
    struct GeneratorDeleter;
    using GeneratorPtr = std::unique_ptr<Generator, GeneratorDeleter>;
};
enum PrintStep {
    psWipeTower,
    psToolOrdering = psWipeTower,
    psSkirtBrim,
    psSlicingFinished = psSkirtBrim,
    psGCodeExport,
    psConflictCheck,
    psCount
};

enum PrintObjectStep {
    posSlice, posPerimeters,posEstimateCurledExtrusions, posPrepareInfill,
    posInfill, posIroning,
    posNonplanar,
    posContouring, posSupportMaterial, posSimplifyPath, posSimplifySupportPath,
    posDetectOverhangsForLift,
    posSimplifyWall, posSimplifyInfill,
    posCount,
};

enum class SlicingPipelineStepPlugin {
    posSlice, posPerimeters, posEstimateCurledExtrusions, posPrepareInfill, posInfill, posIroning, posContouring,
    posSupportMaterial, posDetectOverhangsForLift, posSimplifyPath, psWipeTower, psSkirtBrim,
    psGCodePostProcess
};

class PrintRegion
{
public:
    PrintRegion() = default;
    PrintRegion(const PrintRegionConfig &config);
    PrintRegion(const PrintRegionConfig &config, const size_t config_hash, int print_object_region_id = -1, ObjectID gradient_volume_id = ObjectID()) : m_config(config), m_config_hash(config_hash), m_print_object_region_id(print_object_region_id), m_gradient_volume_id(gradient_volume_id) {}
    PrintRegion(PrintRegionConfig &&config);
    PrintRegion(PrintRegionConfig &&config, const size_t config_hash, int print_object_region_id = -1, ObjectID gradient_volume_id = ObjectID()) : m_config(std::move(config)), m_config_hash(config_hash), m_print_object_region_id(print_object_region_id), m_gradient_volume_id(gradient_volume_id) {}
    ~PrintRegion() = default;

public:
    const PrintRegionConfig&    config() const throw() { return m_config; }
    size_t                      config_hash() const throw() { return m_config_hash; }
    int                         print_region_id() const throw() { return m_print_region_id; }
    int                         print_object_region_id() const throw() { return m_print_object_region_id; }
    ObjectID                    gradient_volume_id() const throw() { return m_gradient_volume_id; }
	unsigned int 				extruder(FlowRole role) const;
    Flow                        flow(const PrintObject &object, FlowRole role, double layer_height, bool first_layer = false) const;
    coordf_t                    nozzle_dmr_avg(const PrintConfig &print_config) const;
    coordf_t                    bridging_height_avg(const PrintConfig &print_config) const;

	void                        collect_object_printing_extruders(const Print &print, std::vector<unsigned int> &object_extruders) const;
	static void                 collect_object_printing_extruders(const PrintConfig &print_config, const PrintRegionConfig &region_config, const bool has_brim, std::vector<unsigned int> &object_extruders);

public:
    void                        set_config(const PrintRegionConfig &config) { m_config = config; m_config_hash = m_config.hash(); }
    void                        set_config(PrintRegionConfig &&config) { m_config = std::move(config); m_config_hash = m_config.hash(); }
    void                        config_apply_only(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent = false)
                                        { m_config.apply_only(other, keys, ignore_nonexistent); m_config_hash = m_config.hash(); }
private:
    friend Print;
    friend void print_region_ref_inc(PrintRegion&);
    friend void print_region_ref_reset(PrintRegion&);
    friend int  print_region_ref_cnt(const PrintRegion&);

    PrintRegionConfig  m_config;
    size_t             m_config_hash;
    int                m_print_region_id { -1 };
    int                m_print_object_region_id { -1 };
    int                m_ref_cnt { 0 };
    ObjectID           m_gradient_volume_id;
};

inline bool operator==(const PrintRegion &lhs, const PrintRegion &rhs) { return lhs.config_hash() == rhs.config_hash() && lhs.config() == rhs.config(); }
inline bool operator!=(const PrintRegion &lhs, const PrintRegion &rhs) { return ! (lhs == rhs); }

template<typename T>
class ConstVectorOfPtrsAdaptor {
public:
    T * const *             begin() const { return m_data->data(); }
    T * const *             end()   const { return m_data->data() + m_data->size(); }
    const T*                front() const { return m_data->front(); }
    const T*                back()  const { return m_data->back(); }
    size_t                  size()  const { return m_data->size(); }
    bool                    empty() const { return m_data->empty(); }
    const T*                operator[](size_t i) const { return (*m_data)[i]; }
    const T*                at(size_t i) const { return m_data->at(i); }
    std::vector<const T*>   vector() const { return std::vector<const T*>(this->begin(), this->end()); }
protected:
    ConstVectorOfPtrsAdaptor(const std::vector<T*> *data) : m_data(data) {}
private:
    const std::vector<T*> *m_data;
};

typedef std::vector<Layer*>       LayerPtrs;
typedef std::vector<const Layer*> ConstLayerPtrs;
class ConstLayerPtrsAdaptor : public ConstVectorOfPtrsAdaptor<Layer> {
    friend PrintObject;
    ConstLayerPtrsAdaptor(const LayerPtrs *data) : ConstVectorOfPtrsAdaptor<Layer>(data) {}
};

typedef std::vector<SupportLayer*>        SupportLayerPtrs;
typedef std::vector<const SupportLayer*>  ConstSupportLayerPtrs;
class ConstSupportLayerPtrsAdaptor : public ConstVectorOfPtrsAdaptor<SupportLayer> {
    friend PrintObject;
    ConstSupportLayerPtrsAdaptor(const SupportLayerPtrs *data) : ConstVectorOfPtrsAdaptor<SupportLayer>(data) {}
};

struct PrintInstance
{
    PrintObject 		*print_object;
	const ModelInstance *model_instance;
	Point 				 shift;
    
    BoundingBoxf3   get_bounding_box() const;
    Polygon get_convex_hull_2d();
    size_t               id;
    size_t               unique_id;

    Point shift_without_plate_offset() const;
};

typedef std::vector<PrintInstance> PrintInstances;

class PrintObjectRegions
{
public:
    static_assert(sizeof(Eigen::AlignedBox<float, 3>) == 24, "Eigen::AlignedBox<float, 3> is not being vectorized, thus it does not need to be aligned");
    using BoundingBox = Eigen::AlignedBox<float, 3>;
    struct VolumeExtents {
        ObjectID             volume_id;
        BoundingBox          bbox;
    };

    struct VolumeRegion
    {
        const ModelVolume   *model_volume { nullptr };
        int                  parent { -1 };
        PrintRegion         *region { nullptr };
        const BoundingBox   *bbox { nullptr };
        const VolumeRegion  *prev_same_region { nullptr };
    };

    struct PaintedRegion
    {
        unsigned int     extruder_id;
        int              parent { -1 };
        PrintRegion     *region { nullptr };
    };

    struct LayerRangeRegions;

    struct FuzzySkinPaintedRegion
    {
        enum class ParentType { VolumeRegion, PaintedRegion };

        ParentType   parent_type { ParentType::VolumeRegion };
        int          parent { -1 };
        PrintRegion *region { nullptr };

        PrintRegion *parent_print_object_region(const LayerRangeRegions &layer_range) const;
        int          parent_print_object_region_id(const LayerRangeRegions &layer_range) const;
    };

    struct LayerRangeRegions
    {
        t_layer_height_range        layer_height_range;
        const DynamicPrintConfig*   config { nullptr };
        std::vector<VolumeExtents>  volumes;

        std::vector<VolumeRegion>           volume_regions;
        std::vector<PaintedRegion>          painted_regions;
        std::vector<FuzzySkinPaintedRegion> fuzzy_skin_painted_regions;

        bool has_volume(const ObjectID id) const {
            auto it = lower_bound_by_predicate(this->volumes.begin(), this->volumes.end(), [id](const VolumeExtents &l) { return l.volume_id < id; });
            return it != this->volumes.end() && it->volume_id == id;
        }
    };

    std::vector<std::unique_ptr<PrintRegion>>   all_regions;
    std::vector<LayerRangeRegions>              layer_ranges;
    Transform3d                                 trafo_bboxes;
    std::vector<ObjectID>                       cached_volume_ids;

    std::vector<bool>                           last_slot_per_part_enabled;

    void ref_cnt_inc() { ++ m_ref_cnt; }
    void ref_cnt_dec() { if (-- m_ref_cnt == 0) delete this; }
    void clear() {
        all_regions.clear();
        layer_ranges.clear();
        cached_volume_ids.clear();
    }

private:
    friend class PrintObject;
    size_t                                      m_ref_cnt{ 0 };
};

class PrintObject : public PrintObjectBaseWithState<Print, PrintObjectStep, posCount>
{
private:
    typedef PrintObjectBaseWithState<Print, PrintObjectStep, posCount> Inherited;

public:
    const Vec3crd&               size() const			{ return m_size; }
    const PrintObjectConfig&     config() const         { return m_config; }
    void                         configBrimWidth(double m)      {m_config.brim_width.value = m; }
    ConstLayerPtrsAdaptor        layers() const         { return ConstLayerPtrsAdaptor(&m_layers); }
    ConstSupportLayerPtrsAdaptor support_layers() const { return ConstSupportLayerPtrsAdaptor(&m_support_layers); }
    const Transform3d&           trafo() const          { return m_trafo; }
    Transform3d                  trafo_centered() const
        { Transform3d t = this->trafo(); t.pretranslate(Vec3d(- unscale<double>(m_center_offset.x()), - unscale<double>(m_center_offset.y()), 0)); return t; }
    Transform3d                  trafo_sliced() const;
    const PrintInstances&        instances() const      { return m_instances; }
    const std::vector<BoundingBox>& separated_body_bboxes() const { return m_separated_body_bboxes; }
    PrintInstances &instances() { return m_instances; }

    LayerPtrs&                   layers()               { return m_layers; }
    SupportLayerPtrs&            support_layers()       { return m_support_layers; }

    template<typename PolysType>
    static void remove_bridges_from_contacts(
        const Layer* lower_layer,
        const Layer* current_layer,
        float extrusion_width,
        PolysType* overhang_regions,
        float max_bridge_length = scale_(10),
        bool break_bridge=false);

    BoundingBox                  bounding_box() const   { return BoundingBox(Point(- m_size.x() / 2, - m_size.y() / 2), Point(m_size.x() / 2, m_size.y() / 2)); }
    coord_t 				     height() const         { return m_size.z(); }
    double                      max_z() const         { return m_max_z; }
    const Point& 			     center_offset() const  { return m_center_offset; }
    const TpmsRadialField*       tpms_radial_field(TpmsAdaptiveMode mode) const { return m_tpms_radial_fields[size_t(mode)].get(); }
    TriangleMesh                 object_frame_mesh(double &ground_offset) const;
    const Nonplanar::ConformalResult& conformal() const;
    bool auto_z_contouring() const { return m_shared_object ? m_shared_object->auto_z_contouring() : m_auto_z_contouring; }
    const Nonplanar::RepairHints*     nonplanar_repair() const { return m_nonplanar_repair.get(); }
    bool has_conformal_support() const;

    void generate_support_preview();
    const std::vector<VolumeSlices>& firstLayerObjSlice() const { return firstLayerObjSliceByVolume; }
    std::vector<VolumeSlices>& firstLayerObjSliceMod() { return firstLayerObjSliceByVolume; }
    const std::vector<groupedVolumeSlices>& firstLayerObjGroups() const { return firstLayerObjSliceByGroups; }
    std::vector<groupedVolumeSlices>& firstLayerObjGroupsMod() { return firstLayerObjSliceByGroups; }

    bool                         has_brim() const       {
        return ((this->config().brim_type != btNoBrim && this->config().brim_width.value > 0.) || this->config().brim_type == btAutoBrim
            || (this->config().brim_type == btPainted && !this->model_object()->brim_points.empty()))
            && ! this->has_raft();
    }

    bool                         has_belt_brim() const;
    unsigned int                 belt_brim_filament() const;
    const std::vector<ExtrusionEntityCollection>& belt_brim_by_layer()       const { return m_belt_brim_by_layer; }
    const std::vector<BeltBrimBand>&              belt_brim_prologue()       const { return m_belt_brim_prologue; }
    void                         clear_belt_brim();
    void                         set_belt_brim(std::vector<ExtrusionEntityCollection> &&by_layer,
                                               std::vector<ExPolygons>                &&areas,
                                               std::vector<BeltBrimBand>              &&prologue);

    const ExtrusionEntityCollection& object_skirt() const {
        return m_skirt;
    }

    size_t 			total_layer_count() const { return this->layer_count() + this->support_layer_count(); }
    size_t 			layer_count() const { return m_layers.size(); }
    void 			clear_layers();
    const Layer* 	get_layer(int idx) const { return m_layers[idx]; }
    Layer* 			get_layer(int idx) 		 { return m_layers[idx]; }
    const Layer*	get_layer_at_printz(coordf_t print_z) const;
    Layer*			get_layer_at_printz(coordf_t print_z);
    const Layer*	get_layer_at_printz(coordf_t print_z, coordf_t epsilon) const;
    Layer*			get_layer_at_printz(coordf_t print_z, coordf_t epsilon);
    int             get_layer_idx_get_printz(coordf_t print_z, coordf_t epsilon);
    const Layer*    get_layer_at_bottomz(coordf_t bottom_z, coordf_t epsilon) const;
    Layer*          get_layer_at_bottomz(coordf_t bottom_z, coordf_t epsilon);

    const Layer*	get_first_layer_bellow_printz(coordf_t print_z, coordf_t epsilon) const;

    Layer*          add_layer(int id, coordf_t height, coordf_t print_z, coordf_t slice_z);

    SupportLayer* add_tree_support_layer(int id, coordf_t height, coordf_t print_z, coordf_t slice_z);
    std::shared_ptr<TreeSupportData> alloc_tree_support_preview_cache();
    void clear_tree_support_preview_cache() { m_tree_support_preview_cache.reset(); }

    size_t          support_layer_count() const { return m_support_layers.size(); }
    void            clear_support_layers();
    SupportLayer*   get_support_layer(int idx) { return idx<m_support_layers.size()? m_support_layers[idx]:nullptr; }
    const SupportLayer* get_support_layer_at_printz(coordf_t print_z, coordf_t epsilon) const;
    SupportLayer*   get_support_layer_at_printz(coordf_t print_z, coordf_t epsilon);
    SupportLayer*   add_support_layer(int id, int interface_id, coordf_t height, coordf_t print_z);
    SupportLayerPtrs::iterator insert_support_layer(SupportLayerPtrs::iterator pos, size_t id, size_t interface_id, coordf_t height, coordf_t print_z, coordf_t slice_z);

    static bool     update_layer_height_profile(const ModelObject &model_object, const SlicingParameters &slicing_parameters, std::vector<coordf_t> &layer_height_profile);

    const SlicingParameters&    slicing_parameters() const { return m_slicing_params; }
    static SlicingParameters    slicing_parameters(const DynamicPrintConfig &full_config, const ModelObject &model_object, float object_max_z, const Vec3d &object_shrinkage_compensation, std::vector<int> variant_index = std::vector<int>());

    size_t                      num_printing_regions() const throw() { return m_shared_regions->all_regions.size(); }
    const PrintRegion&          printing_region(size_t idx) const throw() { return *m_shared_regions->all_regions[idx].get(); }
    std::vector<std::reference_wrapper<const PrintRegion>> all_regions() const;
    const PrintObjectRegions*   shared_regions() const throw() { return m_shared_regions; }

    bool                        has_support()           const { return m_config.enable_support || m_config.enforce_support_layers > 0; }
    bool                        has_raft()              const { return m_config.raft_layers > 0; }
    bool                        has_support_material()  const { return this->has_support() || this->has_raft(); }
    bool                        is_mm_painted()         const { return this->model_object()->is_mm_painted(); }
    bool                        is_fuzzy_skin_painted() const { return this->model_object()->is_fuzzy_skin_painted(); }

    std::vector<unsigned int>   object_extruders() const;

    void slice();

    std::vector<Polygons>       slice_support_volumes(const ModelVolumeType model_volume_type) const;
    std::vector<Polygons>       slice_support_blockers() const { return this->slice_support_volumes(ModelVolumeType::SUPPORT_BLOCKER); }
    std::vector<Polygons>       slice_support_enforcers() const { return this->slice_support_volumes(ModelVolumeType::SUPPORT_ENFORCER); }
    std::vector<ExPolygons>     slice_single_volume_regions(const ModelVolume* volume) const;

    void project_and_append_custom_facets(bool seam, EnforcerBlockerType type, std::vector<Polygons>& expolys, std::vector<std::pair<Vec3f,Vec3f>>* vertical_points=nullptr) const;

    BoundingBox get_first_layer_bbox(float& area, float& layer_height, std::string& name);
    void         get_certain_layers(float start, float end, std::vector<LayerPtrs> &out, std::vector<BoundingBox> &boundingbox_objects);
    Points       get_instances_shift_without_plate_offset() const;
    PrintObject* get_shared_object() const { return m_shared_object; }
    void         set_shared_object(PrintObject *object);
    void         clear_shared_object();
    void         copy_layers_from_shared_object();
    void         copy_layers_overhang_from_shared_object();

    BoundingBox                 firstLayerObjectBrimBoundingBox;

    std::vector<int>            object_first_layer_wall_extruders;

    size_t get_id() const { return m_id; }
    void set_id(size_t id) { m_id = id; }

  private:
    friend class Print;
    friend class Nonplanar::ClearanceRepair;

	PrintObject(Print* print, ModelObject* model_object, const Transform3d& trafo, PrintInstances&& instances);
	~PrintObject();

    void                    config_apply(const ConfigBase &other, bool ignore_nonexistent = false) { m_config.apply(other, ignore_nonexistent); }
    void                    config_apply_only(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent = false) { m_config.apply_only(other, keys, ignore_nonexistent); }
    PrintBase::ApplyStatus  set_instances(PrintInstances &&instances);
    bool                    invalidate_step(PrintObjectStep step);
    bool                    invalidate_all_steps();
    bool                    invalidate_state_by_config_options(
        const ConfigOptionResolver &old_config, const ConfigOptionResolver &new_config, const std::vector<t_config_option_key> &opt_keys);
    void                    update_slicing_parameters();

    static PrintObjectConfig object_config_from_model_object(const PrintObjectConfig &default_object_config, const ModelObject &object, size_t num_extruders, std::vector<int>& variant_index);

private:
    void make_perimeters();
    void prepare_infill();
    void infill();
    void ironing();
    void nonplanar_shells();
    bool need_z_contouring() const;
    void contour_z();
    void generate_support_material(bool with_belt_brim = true);
    void generate_belt_brim();
    void estimate_curled_extrusions();
    void simplify_extrusion_path();

    std::vector<std::set<int>> detect_extruder_geometric_unprintables() const;

    void slice_volumes();
    void belt_shift_layer_grid(double delta);
    size_t belt_truncate_layers_above(coordf_t z);
    void belt_restore_truncated_layers();
    size_t belt_drop_unclaimed_fills(Layer *layer, const std::function<bool(const ExtrusionEntity*)> &claimed);
    void   belt_restore_dropped_fills();
    void   belt_undo_purge_plan() { belt_restore_dropped_fills(); belt_restore_truncated_layers(); }
    ExPolygons _shrink_contour_holes(double contour_delta, double hole_delta, const ExPolygons& polys) const;
    void detect_overhangs_for_lift();
    void clear_overhangs_for_lift();

   void _transform_hole_to_polyholes();

    void detect_surfaces_type();
    void process_external_surfaces();
    void discover_vertical_shells();
    void bridge_over_infill();
    void clip_fill_surfaces();
    void discover_horizontal_shells();
    void combine_infill();
    void _generate_support_material();
    FillAdaptive::RegionOctrees prepare_adaptive_infill_data(
        const std::vector<std::pair<const Surface*, const Layer*>>& surfaces_w_layer) const;
    FillLightning::GeneratorPtr prepare_lightning_infill_data();
    TpmsRadialFields prepare_tpms_radial_fields() const;

    SupportNecessaryType is_support_necessary();

    Vec3crd									m_size;
    double                                  m_max_z;
    PrintObjectConfig                       m_config;
    Transform3d                             m_trafo = Transform3d::Identity();
    std::vector<PrintInstance>              m_instances;
    Point                                   m_center_offset;

    PrintObjectRegions                     *m_shared_regions { nullptr };

    SlicingParameters                       m_slicing_params;
    LayerPtrs                               m_layers;
    LayerPtrs                               m_belt_truncated_layers;
    struct BeltDroppedFill {
        Layer           *layer      { nullptr };
        size_t           region_idx { 0 };
        size_t           index      { 0 };
        ExtrusionEntity *entity     { nullptr };
    };
    std::vector<BeltDroppedFill>            m_belt_dropped_fills;
    SupportLayerPtrs                        m_support_layers;
    std::vector<ExtrusionEntityCollection>  m_belt_brim_by_layer;
    std::vector<ExPolygons>                 m_belt_brim_areas_by_layer;
    std::vector<BeltBrimBand>               m_belt_brim_prologue;
    std::shared_ptr<TreeSupportData>        m_tree_support_preview_cache;
    bool m_auto_z_contouring = false;
    std::unique_ptr<Nonplanar::ConformalResult> m_conformal;
    std::shared_ptr<Nonplanar::RepairHints>     m_nonplanar_repair;
    std::shared_ptr<const Nonplanar::MultiplanarPlan> m_multiplanar;
    std::shared_ptr<const Nonplanar::OrientationField> m_field;

    bool                    				m_typed_slices = false;

    FillAdaptive::RegionOctrees             m_adaptive_fill_octrees;
    std::vector<BoundingBox>                m_separated_body_bboxes;
    FillLightning::GeneratorPtr m_lightning_generator;
    TpmsRadialFields m_tpms_radial_fields;

    std::vector < VolumeSlices >            firstLayerObjSliceByVolume;
    std::vector<groupedVolumeSlices>        firstLayerObjSliceByGroups;

    ExtrusionEntityCollection               m_skirt;

    PrintObject*                            m_shared_object{ nullptr };

    double                                  m_belt_global_z_offset { 0.0 };
    bool                                    m_belt_brim_pending { false };
    double                                  m_belt_min_z { 0.0 };
    double                                  m_belt_floor_z_shift_cached { 0.0 };
    bool                                    m_belt_floor_z_shift_cache_valid { false };
public:
    double belt_global_z_offset() const { return m_belt_global_z_offset; }
private:


    size_t               m_id;
    void apply_conical_overhang();

 public:
    static bool clip_multipart_objects;
    static bool infill_only_where_needed;
};

struct FakeWipeTower
{
    Vec2f pos;
    float width;
    float height;
    float layer_height;
    float depth;
    std::vector<std::pair<float, float>> z_and_depth_pairs;
    float brim_width;
    float rotation_angle;
    float cone_angle;
    Vec2d plate_origin;
    Vec2f rib_offset{0.f,0.f};
    std::map<float , Polylines> outer_wall;

    void set_fake_extrusion_data(Vec2f p, float w, float h, float lh, float d, float bd, Vec2d o)
    {
        pos          = p;
        width        = w;
        height       = h;
        layer_height = lh;
        depth        = d;
        brim_width   = bd;
        plate_origin = o;
    }
    void set_fake_extrusion_data(const Vec2f& p, float w, float h, float lh, float d, const std::vector<std::pair<float, float>>& zad, float bd, float ra, float ca, const Vec2d& o)
    {
        pos = p;
        width = w;
        height = h;
        layer_height = lh;
        depth = d;
        z_and_depth_pairs = zad;
        brim_width = bd;
        rotation_angle = ra;
        cone_angle = ca;
        plate_origin = o;
    }

    void set_pos(Vec2f p) { pos = p+rib_offset; }
    void set_pos_and_rotation(const Vec2f& p, float rotation) { pos = p; rotation_angle = rotation; }

    std::vector<ExtrusionPaths> getFakeExtrusionPathsFromWipeTower() const
    {
        int   d         = scale_(depth);
        int   w         = scale_(width);
        int   bd        = scale_(brim_width);
        Point minCorner = {scale_(pos.x()), scale_(pos.y())};
        Point maxCorner = {minCorner.x() + w, minCorner.y() + d};

        std::vector<ExtrusionPaths> paths;
        for (float h = 0.f; h < height; h += layer_height) {
            ExtrusionPath path(ExtrusionRole::erWipeTower, 0.0, 0.0, layer_height);
            path.polyline = Polyline3(Polyline{{minCorner, {maxCorner.x(), minCorner.y()}, maxCorner, {minCorner.x(), maxCorner.y()}, minCorner}});
            paths.push_back({path});

            if (h == 0.f) {
                ExtrusionPath fakeBrim(ExtrusionRole::erBrim, 0.0, 0.0, layer_height);
                Point         wtbminCorner = {minCorner - Point{bd, bd}};
                Point         wtbmaxCorner = {maxCorner + Point{bd, bd}};
                fakeBrim.polyline          = Polyline3(Polyline{{wtbminCorner, {wtbmaxCorner.x(), wtbminCorner.y()}, wtbmaxCorner, {wtbminCorner.x(), wtbmaxCorner.y()}, wtbminCorner}});
                paths.back().push_back(fakeBrim);
            }
        }
        return paths;
    }

    ExtrusionLayers getTrueExtrusionLayersFromWipeTower() const;

    std::vector<ExtrusionPaths> getFakeExtrusionPathsFromWipeTower2() const
    {
        float h = height;
        float lh = layer_height;
        int   d = scale_(depth);
        int   w = scale_(width);
        int   bd = scale_(brim_width);
        Point minCorner = { -bd, -bd };
        Point maxCorner = { minCorner.x() + w + bd, minCorner.y() + d + bd };

        const auto [cone_base_R, cone_scale_x] = WipeTower2::get_wipe_tower_cone_base(width, height, depth, cone_angle);

        std::vector<ExtrusionPaths> paths;
        for (float hh = 0.f; hh < h; hh += lh) {
            
            if (hh != 0.f) {
                size_t i = 0;
                for (i=0; i<z_and_depth_pairs.size()-1; ++i)
                    if (hh >= z_and_depth_pairs[i].first && hh < z_and_depth_pairs[i+1].first)
                        break;
                d = scale_(z_and_depth_pairs[i].second);
                minCorner = {0.f, -d/2 + scale_(z_and_depth_pairs.front().second/2.f)};
                maxCorner = { minCorner.x() + w, minCorner.y() + d };
            }


            ExtrusionPath path(ExtrusionRole::erWipeTower, 0.0, 0.0, lh);
            path.polyline = Polyline3(Polyline{{ minCorner, {maxCorner.x(), minCorner.y()}, maxCorner, {minCorner.x(), maxCorner.y()}, minCorner }});
            paths.push_back({ path });

            for (coord_t y=minCorner.y()+scale_(3.); y<maxCorner.y(); y+=scale_(3.)) {
                path.polyline = Polyline3(Polyline{{ {minCorner.x(), y}, {maxCorner.x(), y} }});
                paths.back().emplace_back(path);
            }

            if (cone_base_R > 0.) {
                path.polyline.clear();
                double r = cone_base_R * (1 - hh/height);
                for (double alpha=0; alpha<2.01*M_PI; alpha+=2*M_PI/20.)
                    path.polyline.points.emplace_back(Point::new_scale(width/2. + r * std::cos(alpha)/cone_scale_x, depth/2. + r * std::sin(alpha)));
                paths.back().emplace_back(path);
                if (hh == 0.f) {
                    for (float bw=brim_width; bw>0.f; bw-=3.f) {
                        path.polyline.clear();
                        for (double alpha=0; alpha<2.01*M_PI; alpha+=2*M_PI/20.)
                            path.polyline.points.emplace_back(Point::new_scale(
                                width/2. + cone_base_R * std::cos(alpha)/cone_scale_x * (1. + cone_scale_x*bw/cone_base_R),
                                depth/2. + cone_base_R * std::sin(alpha) * (1. + bw/cone_base_R))
                            );
                        paths.back().emplace_back(path);
                    }
                }
            }

            if (hh == 0.f) {
                minCorner = minCorner + Point(bd, bd);
                maxCorner = maxCorner - Point(bd, bd);
            }
        }

        for (ExtrusionPaths& ps : paths) {
            for (ExtrusionPath& p : ps) {
                p.polyline.rotate(Geometry::deg2rad(rotation_angle));
                p.polyline.translate(scale_(pos.x()), scale_(pos.y()));
            }
        }

        return paths;
    }
};

struct WipeTowerData
{
    struct WipeTowerMeshData
    {
        Polygon      bottom;
        TriangleMesh real_wipe_tower_mesh;
        TriangleMesh real_brim_mesh;
    };
    ToolOrdering                                         &tool_ordering;
    std::unique_ptr<std::vector<WipeTower::ToolChangeResult>> priming;
    std::vector<std::vector<WipeTower::ToolChangeResult>> tool_changes;
    std::unique_ptr<WipeTower::ToolChangeResult>          final_purge;
    std::vector<float>                                    used_filament;
    int                                                   number_of_toolchanges;

    float                                                 depth;
    float                                                 width;
    std::vector<std::pair<float, float>>                  z_and_depth_pairs;
    float                                                 brim_width;
    float                                                 height;
    BoundingBoxf                                          bbx;
    Vec2f                                                 rib_offset;
    std::optional<WipeTowerMeshData>                      wipe_tower_mesh_data;
    void clear() {
        priming.reset(nullptr);
        tool_changes.clear();
        final_purge.reset(nullptr);
        used_filament.clear();
        number_of_toolchanges = -1;
        depth = 0.f;
        width = 0.f;
        brim_width = 0.f;
        height = 0.f;
        rib_offset = Vec2f::Zero();
        wipe_tower_mesh_data  = std::nullopt;
    }
    void construct_mesh(float width, float depth, float height, float brim_width, bool is_rib_wipe_tower, float rib_width, float rib_length, bool fillet_wall, float cone_angle = 0.f);

private:
	friend class Print;
	WipeTowerData(ToolOrdering &tool_ordering) : tool_ordering(tool_ordering) { clear(); }
	WipeTowerData(const WipeTowerData & ) = delete;
	WipeTowerData &operator=(const WipeTowerData & ) = delete;
};

struct PrintStatistics
{
    PrintStatistics() { clear(); }
    std::string                     estimated_normal_print_time;
    std::string                     estimated_silent_print_time;
    double                          total_used_filament;
    double                          total_extruded_volume;
    double                          total_cost;
    int                             total_toolchanges;
    double                          total_weight;
    double                          total_wipe_tower_cost;
    double                          total_wipe_tower_filament;
    unsigned int                    initial_tool;
    unsigned int                    initial_no_support_tool;
    std::map<size_t, double>        filament_stats;

    DynamicConfig           config() const;
    static DynamicConfig    placeholders();
    std::string             finalize_output_path(const std::string &path_in) const;

    void clear() {
        total_used_filament    = 0.;
        total_extruded_volume  = 0.;
        total_cost             = 0.;
        total_toolchanges      = 0;
        total_weight           = 0.;
        total_wipe_tower_cost  = 0.;
        total_wipe_tower_filament = 0.;
        initial_tool           = 0;
        initial_no_support_tool = 0;
        filament_stats.clear();
    }
    static const std::string FilamentUsedG;
    static const std::string FilamentUsedGMask;
    static const std::string TotalFilamentUsedG;
    static const std::string TotalFilamentUsedGMask;
    static const std::string TotalFilamentUsedGValueMask;
    static const std::string FilamentUsedCm3;
    static const std::string FilamentUsedCm3Mask;
    static const std::string FilamentUsedMm;
    static const std::string FilamentUsedMmMask;
    static const std::string FilamentCost;
    static const std::string FilamentCostMask;
    static const std::string TotalFilamentCost;
    static const std::string TotalFilamentCostMask;
    static const std::string TotalFilamentCostValueMask;
    static const std::string TotalFilamentUsedWipeTower;
    static const std::string TotalFilamentUsedWipeTowerValueMask;
    
};

typedef std::vector<PrintObject*>       PrintObjectPtrs;
typedef std::vector<const PrintObject*> ConstPrintObjectPtrs;
class ConstPrintObjectPtrsAdaptor : public ConstVectorOfPtrsAdaptor<PrintObject> {
    friend Print;
    ConstPrintObjectPtrsAdaptor(const PrintObjectPtrs *data) : ConstVectorOfPtrsAdaptor<PrintObject>(data) {}
};

typedef std::vector<PrintRegion*>       PrintRegionPtrs;

struct StatisticsByExtruderCount
{
    FilamentChangeStats stats_by_single_extruder;
    FilamentChangeStats stats_by_multi_extruder_best;
    FilamentChangeStats stats_by_multi_extruder_curr;
    void clear() {
        stats_by_single_extruder.clear();
        stats_by_multi_extruder_best.clear();
        stats_by_multi_extruder_curr.clear();
    }
};

enum FilamentTempType {
    HighTemp=0,
    LowTemp,
    HighLowCompatible,
    Undefine
};

enum FilamentCompatibilityType {
    Compatible,
    HighLowMixed,
    InvalidTemperatureRange
};

class Print : public PrintBaseWithState<PrintStep, psCount>
{
private:
    typedef PrintBaseWithState<PrintStep, psCount> Inherited;
    typedef std::pair<PrintObject *, bool>         PrintObjectInfo;

public:
    using SlicingPipelineHookFn = std::function<void(Print&, const PrintObject*, SlicingPipelineStepPlugin)>;
    static void set_slicing_pipeline_hook_fn(SlicingPipelineHookFn fn) { s_slicing_pipeline_hook_fn = std::move(fn); }

    Print() = default;
	virtual ~Print() { this->clear(); }

	PrinterTechnology	technology() const noexcept override { return ptFFF; }

    void                clear() override;
    bool                empty() const override { return m_objects.empty(); }
    std::vector<ObjectID> print_object_ids() const override;

    ApplyStatus         apply(const Model &model, DynamicPrintConfig config, bool extruder_applied = false) override;

    void                process(long long *time_cost_with_cache = nullptr, bool use_cache = false) override;
    std::string         export_gcode(const std::string& path_template, GCodeProcessorResult* result, ThumbnailsGeneratorCallback thumbnail_cb = nullptr);
    int                 export_cached_data(const std::string& dir_path, bool with_space=false) override;
    int                 load_cached_data(const std::string& directory) override;

    bool                is_step_done(PrintStep step) const { return Inherited::is_step_done(step); }
    bool                is_step_done(PrintObjectStep step) const;
    bool                finished() const override { return this->is_step_done(psGCodeExport); }

    bool                has_infinite_skirt() const;
    bool                has_skirt() const;
    bool                has_brim() const;
    bool                has_tilted_belt() const;
    bool                has_belt_brim() const;
    bool                has_auto_brim() const    {
        return std::any_of(m_objects.begin(), m_objects.end(), [](PrintObject* object) { return object->config().brim_type == btAutoBrim; });
    }

    StringObjectException validate(std::vector<StringObjectException> *warnings = nullptr, Polygons* collison_polygons = nullptr, std::vector<std::pair<Polygon, float>>* height_polygons = nullptr) const override;
    std::string slicing_errors_message(const SlicingErrors &errors) const;
    double              skirt_first_layer_height() const;
    Flow                brim_flow() const;
    Flow                skirt_flow() const;

    std::vector<unsigned int> object_extruders() const;
    std::vector<unsigned int> support_material_extruders() const;
    std::vector<unsigned int> extruders(bool conside_custom_gcode = false) const;
    double              max_allowed_layer_height() const;
    bool                has_support_material() const;
    void                auto_assign_extruders(ModelObject* model_object) const;

    const PrintConfig&          config() const { return m_config; }
    const PrintObjectConfig&    default_object_config() const { return m_default_object_config; }
    const PrintRegionConfig& default_region_config() const { return m_default_region_config; }
    ConstPrintObjectPtrsAdaptor objects() const { return ConstPrintObjectPtrsAdaptor(&m_objects); }
    PrintObject*                get_object(size_t idx) { return const_cast<PrintObject*>(m_objects[idx]); }
    const PrintObject*          get_object(size_t idx) const { return m_objects[idx]; }
    const PrintObject*          get_object(ObjectID object_id) const {
        auto it = std::find_if(m_objects.begin(), m_objects.end(),
            [object_id](const PrintObject *obj) { return obj->id() == object_id; });
        return (it == m_objects.end()) ? nullptr : *it;
    }
    std::map<ObjectID, ExtrusionEntityCollection>&
        get_brimMap() { return m_brimMap; }

    unsigned int                num_object_instances() const;

    PrintObjectPtrs&            objects_mutable() { return m_objects; }
    PrintRegionPtrs&            print_regions_mutable() { return m_print_regions; }
    std::vector<size_t>         layers_sorted_for_object(float start, float end, std::vector<LayerPtrs> &layers_of_objects, std::vector<BoundingBox> &boundingBox_for_objects, VecOfPoints& objects_instances_shift);
    struct SkirtBrimGroup {
        struct Brim {
            ExtrusionEntityCollection brim;
            std::vector<ObjectInstanceID> instances;
        };

        ExtrusionEntityCollection skirt;
        std::vector<ObjectInstanceID> instances;
        std::vector<Brim>         brims;
    };

    const ExtrusionEntityCollection& skirt() const { return m_skirt; }
    const std::vector<SkirtBrimGroup>& skirt_brim_groups() const { return m_skirt_brim_groups; }
    bool has_shared_per_object_skirt() const { return m_has_shared_per_object_skirt; }
    const Polygon&                   first_layer_convex_hull() const { return m_first_layer_convex_hull; }

    const PrintStatistics&      print_statistics() const { return m_print_statistics; }
    PrintStatistics&            print_statistics() { return m_print_statistics; }

    const StatisticsByExtruderCount statistics_by_extruder() const { return m_statistics_by_extruder_count; }
    StatisticsByExtruderCount& statistics_by_extruder() { return m_statistics_by_extruder_count; }

    bool                        has_wipe_tower() const;
    bool                        has_belt_purge_tower() const;
    const WipeTowerData&        wipe_tower_data(size_t filaments_cnt = 0) const;
    const ToolOrdering& 		tool_ordering() const { return m_tool_ordering; }

    void update_filament_maps_to_config(std::vector<int> f_maps, std::vector<int> f_volume_maps = std::vector<int>{}, std::vector<int> f_nozzle_maps = std::vector<int>{});
    void update_to_config_by_nozzle_group_result(const MultiNozzleUtils::LayeredNozzleGroupResult& group_result);
    void apply_config_for_render(const DynamicConfig &config);

    std::vector<int> get_filament_maps() const;
    FilamentMapMode  get_filament_map_mode() const;
    std::vector<int> get_filament_volume_maps() const;
    std::vector<int> get_filament_nozzle_maps() const;
    size_t get_extruder_id(unsigned int filament_id) const;

    Polygons get_extruder_shared_printable_polygon() const;

    void set_nozzle_group_result(std::shared_ptr<MultiNozzleUtils::NozzleGroupResultBase> result);
    std::shared_ptr<MultiNozzleUtils::NozzleGroupResultBase> get_nozzle_group_result() const { return m_nozzle_group_result; }
    std::shared_ptr<MultiNozzleUtils::LayeredNozzleGroupResult> get_layered_nozzle_group_result() const { return m_layered_nozzle_group_result; }

    bool is_dynamic_group_reorder() const;

    const std::map<const PrintObject*, ToolOrdering>& sequential_dynamic_orderings() const { return m_sequential_dynamic_orderings; }

    const std::vector<std::vector<DynamicPrintConfig>>& get_extruder_filament_info() const { return m_extruder_filament_info; }
    void set_extruder_filament_info(const std::vector<std::vector<DynamicPrintConfig>>& filament_info) { m_extruder_filament_info = filament_info; }

    void set_geometric_unprintable_filaments(const std::vector<std::set<int>> &unprintables_filament_ids) { m_geometric_unprintable_filaments = unprintables_filament_ids; }
    std::vector<std::set<int>> get_geometric_unprintable_filaments() const { return m_geometric_unprintable_filaments;}

    void set_slice_used_filaments(const std::vector<unsigned int> &first_layer_used_filaments, const std::vector<unsigned int> &used_filaments){
        m_slice_used_filaments_first_layer = first_layer_used_filaments;
        m_slice_used_filaments = used_filaments;
    }
    std::vector<unsigned int> get_slice_used_filaments(bool first_layer) const { return first_layer ? m_slice_used_filaments_first_layer : m_slice_used_filaments;}
    void set_slice_used_mixed_filaments(const std::vector<unsigned int> &used_mixed_filaments) {
        m_slice_used_mixed_filaments = used_mixed_filaments;
    }
    const std::vector<unsigned int>& get_slice_used_mixed_filaments() const { return m_slice_used_mixed_filaments; }

    std::vector<std::set<int>> get_physical_unprintable_filaments(const std::vector<unsigned int>& used_filaments) const;

    std::map<int, std::set<NozzleVolumeType>> get_filament_unprintable_flow(const std::vector<unsigned int> &used_filaments) const;

    std::vector<double> get_extruder_printable_height() const;
    std::vector<Polygons> get_extruder_printable_polygons() const;
    std::vector<Polygons> get_extruder_unprintable_polygons() const;

    bool                        enable_timelapse_print() const;

	std::string                 output_filename(const std::string &filename_base = std::string()) const override;

	std::string                 get_model_name() const;
	std::string                 get_plate_number_formatted() const;

    size_t                      num_print_regions() const throw() { return m_print_regions.size(); }
    const PrintRegion&          get_print_region(size_t idx) const  { return *m_print_regions[idx]; }
    const ToolOrdering&         get_tool_ordering() const { return m_wipe_tower_data.tool_ordering; }

    void set_plate_origin(Vec3d origin) { m_origin = origin; }
    const Vec3d get_plate_origin() const { return m_origin; }
    void update_imex_slice_offset();
    Vec2d get_imex_slice_offset() const { return m_imex_slice_offset; }
    void set_gcode_file_ready();
    void set_gcode_file_invalidated();
    void export_gcode_from_previous_file(const std::string& file, GCodeProcessorResult* result, ThumbnailsGeneratorCallback thumbnail_cb = nullptr);
    void reload_gcode_moves(GCodeProcessorResult* result) const;
    int get_modified_count() const {return m_modified_count;}
    bool is_support_used() const {return m_support_used;}
    std::string get_conflict_string() const
    {
        std::string result;
        if (m_conflict_result) {
            result = "Found gcode path conflicts between object " + m_conflict_result.value()._objName1 + " and " + m_conflict_result.value()._objName2;
        }

        return result;
    }

    static StringObjectException sequential_print_clearance_valid(const Print &print, Polygons *polygons = nullptr, std::vector<std::pair<Polygon, float>>* height_polygons = nullptr);
    static StringObjectException compacted_wipe_tower_clearance_valid(const Print &print, Polygons *polygons = nullptr, std::vector<std::pair<Polygon, float>>* height_polygons = nullptr);
    ConflictResultOpt            get_conflict_result() const { return m_conflict_result; }

    Points first_layer_wipe_tower_corners(bool check_wipe_tower_existance=true) const;

    bool &is_BBL_printer() { return m_isBBLPrinter; }
    const bool is_BBL_printer() const { return m_isBBLPrinter; }
    WipeTowerType wipe_tower_type() const { return is_BBL_printer() ? WipeTowerType::Type1 : m_config.wipe_tower_type.value; }
    CalibMode& calib_mode() { return m_calib_params.mode; }
    const CalibMode calib_mode() const { return m_calib_params.mode; }
    void set_calib_params(const Calib_Params& params);
    const Calib_Params& calib_params() const { return m_calib_params; }
    Vec2d translate_to_print_space(const Vec2d &point) const;
    void                validate_compacted_wipe_tower_clearance() const;
    float               get_wipe_tower_depth() const { return m_wipe_tower_data.depth; }
    BoundingBoxf        get_wipe_tower_bbx() const { return m_wipe_tower_data.bbx; }
    Vec2f               get_rib_offset() const { return m_wipe_tower_data.rib_offset; }
    const FakeWipeTower& get_fake_wipe_tower() const { return m_fake_wipe_tower; }

    void set_check_multi_filaments_compatibility(bool check) { m_need_check_multi_filaments_compatibility = check; }
    bool need_check_multi_filaments_compatibility() const { return m_need_check_multi_filaments_compatibility; }

    Vec2d translate_to_print_space(const Point &point) const;
    static FilamentTempType get_filament_temp_type(const std::string& filament_type);
    static int get_hrc_by_nozzle_type(const NozzleType& type);
    static std::vector<std::string> get_incompatible_filaments_by_nozzle(const float nozzle_diameter, const std::optional<NozzleVolumeType> nozzle_volume_type = std::nullopt);
    static FilamentCompatibilityType check_multi_filaments_compatibility(
        const std::vector<std::string>& filament_types,
        const std::vector<int>& nozzle_temperatures,
        const std::vector<int>& nozzle_temperature_range_lows,
        const std::vector<int>& nozzle_temperature_range_highs);
    static bool is_filaments_compatible(const std::vector<int>& types);
    static int get_compatible_filament_type(const std::set<int>& types);

    bool is_all_objects_are_short() const {
        return std::all_of(this->objects().begin(), this->objects().end(), [&](PrintObject* obj) { return obj->height() < scale_(this->config().nozzle_height.value); });
    }

    int get_filament_config_indx(int filament_id, int layer_id, bool use_cache = true);
    int get_nozzle_config_index(int filament_id, int layer_id);
    size_t config_index_generation() const { return m_config_index_generation; }

     bool has_same_shrinkage_compensations() const;
     Vec3d shrinkage_compensation() const;

    std::tuple<float, float> object_skirt_offset(double margin_height = 0) const;

protected:
    struct FilamentIndexKey
    {
        int              filament_id;
        ExtruderType     extruder;
        NozzleVolumeType nozzle_volume_type;

        bool operator==(const FilamentIndexKey &other) const
        {
            return filament_id == other.filament_id && extruder == other.extruder && nozzle_volume_type == other.nozzle_volume_type;
        }
    };

    struct PrintIndexKey
    {
        int              filament_id;
        int              extruder_id;
        ExtruderType     extruder;
        NozzleVolumeType nozzle_volume_type;

        bool operator==(const PrintIndexKey &other) const
        {
            return filament_id == other.filament_id && extruder_id == other.extruder_id && extruder == other.extruder && nozzle_volume_type == other.nozzle_volume_type;
        }
    };

    struct FilamentIndexKeyHash
    {
        std::size_t operator()(const FilamentIndexKey &k) const
        {
            size_t h1 = std::hash<int>{}(k.filament_id);
            size_t h2 = std::hash<int>{}(static_cast<int>(k.extruder));
            size_t h3 = std::hash<int>{}(static_cast<int>(k.nozzle_volume_type));
            return h1 ^ (h2 << 8) ^ (h3 << 12);
        }
    };
    struct PrintIndexKeyHash
    {
        std::size_t operator()(const PrintIndexKey &k) const
        {
            size_t h1 = std::hash<int>{}(k.filament_id);
            size_t h2 = std::hash<int>{}(k.extruder_id);
            size_t h3 = std::hash<int>{}(static_cast<int>(k.extruder));
            size_t h4 = std::hash<int>{}(static_cast<int>(k.nozzle_volume_type));
            return h1 ^ (h2 << 8) ^ (h3 << 12) ^ (h4 << 16);
        }
    };
    using FilamentIndexMap = std::unordered_map<FilamentIndexKey, int, FilamentIndexKeyHash>;
    using PrintIndexMap = std::unordered_map<PrintIndexKey, int, PrintIndexKeyHash>;
    int get_config_index(int filament_id, int layer_id, const std::vector<std::string> &variant_list, const std::vector<int>& self_index_list, FilamentIndexMap *index_map);
    int get_config_index(int filament_id, int layer_id, const std::vector<std::string> &variant_list, const std::vector<int>& self_index_list, PrintIndexMap &index_map);

    bool                invalidate_step(PrintStep step);

private:
    static StringObjectException check_multi_filament_valid(const Print &print);

    bool                has_tpu_filament() const;
    bool                invalidate_state_by_config_options(const ConfigOptionResolver &new_config, const std::vector<t_config_option_key> &opt_keys);

    void                _make_skirt();
    void                _make_wipe_tower();
    void                _plan_belt_purge();
    void                _align_belt_purge_layers();
    void                finalize_first_layer_convex_hull();
    void                update_filament_self_index_cache();
    bool                collect_filament_variant_uses(const MultiNozzleUtils::LayeredNozzleGroupResult& group_result,
                                                      const DynamicPrintConfig& config,
                                                      std::unordered_map<int, std::vector<FilamentVariantUse>>& uses) const;

    Polygons            first_layer_islands() const;

    static SlicingPipelineHookFn s_slicing_pipeline_hook_fn;
    bool m_pipeline_plugin_active { false };
    void run_pipeline_hook(SlicingPipelineStepPlugin step, const PrintObject* object) {
        if (m_pipeline_plugin_active && s_slicing_pipeline_hook_fn)
            s_slicing_pipeline_hook_fn(*this, object, step);
    }

    PrintConfig                             m_config;
    PrintObjectConfig                       m_default_object_config;
    PrintRegionConfig                       m_default_region_config;
    PrintObjectPtrs                         m_objects;
    PrintRegionPtrs                         m_print_regions;
    
    bool m_isBBLPrinter = false;

    ExtrusionEntityCollection               m_skirt;
    std::vector<SkirtBrimGroup>             m_skirt_brim_groups;
    bool                                    m_has_shared_per_object_skirt { false };
    std::map<ObjectID, ExtrusionEntityCollection>         m_brimMap;
    std::map<ObjectInstanceID, ExtrusionEntityCollection> m_brimMapByInstance;
    std::map<ObjectInstanceID, ExPolygons>                m_objectBrimAreasByInstance;
    Polygon                                 m_first_layer_convex_hull;
    Points                                  m_skirt_convex_hull;

    std::vector<std::vector<DynamicPrintConfig>> m_extruder_filament_info;

    std::shared_ptr<MultiNozzleUtils::NozzleGroupResultBase> m_nozzle_group_result;
    std::shared_ptr<MultiNozzleUtils::LayeredNozzleGroupResult> m_layered_nozzle_group_result;

    std::map<const PrintObject*, ToolOrdering> m_sequential_dynamic_orderings;

    FilamentIndexMap m_filament_index_map;
    PrintIndexMap m_nozzle_index_map;
    size_t        m_config_index_generation{0};
    std::set<int> m_missing_nozzle_group_logged;
    std::vector<int> m_filament_self_index;

    ToolOrdering 							m_tool_ordering;
    WipeTowerData                           m_wipe_tower_data {m_tool_ordering};

    PrintStatistics                         m_print_statistics;
    bool                                    m_support_used {false};
    StatisticsByExtruderCount               m_statistics_by_extruder_count;

    std::vector<unsigned int> m_slice_used_filaments;
    std::vector<unsigned int> m_slice_used_filaments_first_layer;
    std::vector<unsigned int> m_slice_used_mixed_filaments;

    Vec3d   m_origin {0, 0, 0};
    Vec2d   m_imex_slice_offset { Vec2d::Zero() };
    int     m_modified_count {0};
    ConflictResultOpt m_conflict_result;
    FakeWipeTower     m_fake_wipe_tower;
    bool              m_has_auto_filament_map_result{false};
    
    std::vector<std::set<int>> m_geometric_unprintable_filaments;

    Calib_Params m_calib_params;

    bool m_need_check_multi_filaments_compatibility{true};

    Nonplanar::ClearanceRepair *m_clearance_repair{ nullptr };

    friend class GCode;
    friend class Nonplanar::ClearanceRepair;
    friend class PrintObject;

public:
};


inline double compacted_tower_half_clearance(double clearance) { return 0.5 * (clearance - 0.2); }

struct CompactedTowerZone
{
    Polygon     hull;
    Polygons    grown_body;
    Polygons    grown_nozzle;
    BoundingBox bbox_rod;
    double      body_radius { 0. };

    bool empty() const { return hull.points.empty(); }
};

double compacted_tower_footprint_padding(const PrintConfig &config, double brim_width);

CompactedTowerZone compacted_wipe_tower_zone(const PrintConfig &config, const Polygon &tower_footprint);

struct CompactedTowerClearance
{
    double allowed_rise;
    double far_clearance;
    bool   near_body;
    double body_clearance;
};

CompactedTowerClearance compacted_wipe_tower_clearance(const PrintConfig &config, const CompactedTowerZone &zone,
                                                      const Polygon &inst_hull, double object_rise);

inline bool compacted_tower_body_tier(const CompactedTowerClearance &clearance)
{
    return clearance.body_clearance > double(MAX_OUTER_NOZZLE_DIAMETER);
}

Polygons compacted_wipe_tower_rings(const CompactedTowerZone &zone, bool any_body_tier);

Polygon compacted_wipe_tower_offender_outline(const Polygon &inst_hull, double body_clearance);

}

#endif
