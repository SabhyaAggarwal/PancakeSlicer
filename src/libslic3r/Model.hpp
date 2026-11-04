#ifndef slic3r_Model_hpp_
#define slic3r_Model_hpp_

#include "BoundingBox.hpp"
#include "Polygon.hpp"
#include "Semver.hpp"
#include "libslic3r.h"
#include "enum_bitmask.hpp"
#include "Geometry.hpp"
#include "ObjectID.hpp"
#include "Point.hpp"
#include "AppConfig.hpp"
#include "PrintConfig.hpp"
#include "Slicing.hpp"
#include "SLA/SupportPoint.hpp"
#include "SLA/Hollowing.hpp"
#include "BrimEarsPoint.hpp"
#include "TriangleMesh.hpp"
#include "CustomGCode.hpp"
#include "calib.hpp"
#include "TextConfiguration.hpp"
#include "EmbossShape.hpp"
#include "TriangleSelector.hpp"
#include "TextureDisplacement.hpp"

#include "Format/bbs_3mf.hpp"
#include "Format/STEP.hpp"
#include "Format/STL.hpp"
#include "Format/OBJ.hpp"

#include <array>
#include <cereal/access.hpp>
#include <cereal/types/base_class.hpp>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <initializer_list>
#include <cereal/specialize.hpp>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>
#include <functional>
#include <optional>

namespace cereal {
	class BinaryInputArchive;
	class BinaryOutputArchive;
	template <class T> void load_optional(BinaryInputArchive &ar, std::shared_ptr<const T> &ptr);
	template <class T> void save_optional(BinaryOutputArchive &ar, const std::shared_ptr<const T> &ptr);
	template <class T> void load_by_value(BinaryInputArchive &ar, T &obj);
	template <class T> void save_by_value(BinaryOutputArchive &ar, const T &obj);
}

namespace Slic3r {

struct TexturedMesh;
enum class ConversionType;

class BuildVolume;
class Model;
class ModelInstance;
class ModelMaterial;
class ModelObject;
class ModelVolume;
class ModelWipeTower;
class Print;
class SLAPrint;
class TriangleSelector;
class Preset;
class BBLProject;

class KeyStore;

namespace UndoRedo {
	class StackImpl;
}

class ModelConfigObject : public ObjectBase, public ModelConfig
{
private:
	friend class cereal::access;
	friend class UndoRedo::StackImpl;
	friend class ModelObject;
	friend class ModelVolume;
	friend class ModelMaterial;

    explicit ModelConfigObject() = default;
    explicit ModelConfigObject(int) : ObjectBase(-1) {}
	explicit ModelConfigObject(const ModelConfigObject &cfg) = default;
	explicit ModelConfigObject(ModelConfigObject &&cfg) = default;

    Timestamp          timestamp() const throw() override { return this->ModelConfig::timestamp(); }
    bool               object_id_and_timestamp_match(const ModelConfigObject &rhs) const throw() { return this->id() == rhs.id() && this->timestamp() == rhs.timestamp(); }

	ModelConfigObject& operator=(const ModelConfigObject &rhs) = default;
    ModelConfigObject& operator=(ModelConfigObject &&rhs) = default;

    template<class Archive> void serialize(Archive &ar) {
        ar(cereal::base_class<ModelConfig>(this));
    }
};

namespace Internal {
	template<typename T>
	class StaticSerializationWrapper
	{
	public:
		StaticSerializationWrapper(T &wrap) : wrapped(wrap) {}
	private:
		friend class cereal::access;
		friend class UndoRedo::StackImpl;
		template<class Archive> void load(Archive &ar) { cereal::load_by_value(ar, wrapped); }
		template<class Archive> void save(Archive &ar) const { cereal::save_by_value(ar, wrapped); }
		T&	wrapped;
	};
}

typedef std::string t_model_material_id;
typedef std::string t_model_material_attribute;
typedef std::map<t_model_material_attribute, std::string> t_model_material_attributes;

typedef std::map<t_model_material_id, ModelMaterial*> ModelMaterialMap;
typedef std::vector<ModelObject*> ModelObjectPtrs;
typedef std::vector<ModelVolume*> ModelVolumePtrs;
typedef std::vector<ModelInstance*> ModelInstancePtrs;

#define OBJECTBASE_DERIVED_COPY_MOVE_CLONE(TYPE) \
     \
     \
    static TYPE* new_copy(const TYPE &rhs)  { auto *ret = new TYPE(rhs); assert(ret->id() == rhs.id()); return ret; } \
    static TYPE* new_copy(TYPE &&rhs)       { auto *ret = new TYPE(std::move(rhs)); assert(ret->id() == rhs.id()); return ret; } \
    static TYPE  make_copy(const TYPE &rhs) { TYPE ret(rhs); assert(ret.id() == rhs.id()); return ret; } \
    static TYPE  make_copy(TYPE &&rhs)      { TYPE ret(std::move(rhs)); assert(ret.id() == rhs.id()); return ret; } \
    TYPE&        assign_copy(const TYPE &rhs); \
    TYPE&        assign_copy(TYPE &&rhs); \
     \
    static TYPE* new_clone(const TYPE &rhs) { \
         \
        auto obj = new TYPE(-1); \
        obj->assign_clone(rhs); \
        assert(obj->id().valid() && obj->id() != rhs.id()); \
        return obj; \
	} \
    TYPE         make_clone(const TYPE &rhs) { \
         \
        TYPE obj(-1); \
        obj.assign_clone(rhs); \
        assert(obj.id().valid() && obj.id() != rhs.id()); \
        return obj; \
    } \
    TYPE&        assign_clone(const TYPE &rhs) { \
        this->assign_copy(rhs); \
        assert(this->id().valid() && this->id() == rhs.id()); \
        this->assign_new_unique_ids_recursive(); \
        assert(this->id().valid() && this->id() != rhs.id()); \
		return *this; \
    }

class ModelMaterial final : public ObjectBase
{
public:
    t_model_material_attributes attributes;
    ModelConfigObject config;

    Model* get_model() const { return m_model; }
    void apply(const t_model_material_attributes &attributes)
        { this->attributes.insert(attributes.begin(), attributes.end()); }

private:
    Model *m_model;

    friend class Model;
	ModelMaterial(Model *model) : m_model(model) { assert(this->id().valid()); }
	ModelMaterial(const ModelMaterial &rhs) = default;
	void set_model(Model *model) { m_model = model; }
	void set_new_unique_id() { ObjectBase::set_new_unique_id(); this->config.set_new_unique_id(); }

	friend class cereal::access;
	friend class UndoRedo::StackImpl;
	ModelMaterial() : ObjectBase(-1), config(-1), m_model(nullptr) { assert(this->id().invalid()); assert(this->config.id().invalid()); }
	template<class Archive> void serialize(Archive &ar) {
		assert(this->id().invalid()); assert(this->config.id().invalid());
		Internal::StaticSerializationWrapper<ModelConfigObject> config_wrapper(config);
		ar(attributes, config_wrapper);
	}

	ModelMaterial(ModelMaterial &&rhs) = delete;
	ModelMaterial& operator=(const ModelMaterial &rhs) = delete;
    ModelMaterial& operator=(ModelMaterial &&rhs) = delete;
};

class LayerHeightProfile final : public ObjectWithTimestamp {
public:
    void assign(const LayerHeightProfile &rhs) { if (! this->timestamp_matches(rhs)) { m_data = rhs.m_data; this->copy_timestamp(rhs); } }
    void assign(LayerHeightProfile &&rhs) { if (! this->timestamp_matches(rhs)) { m_data = std::move(rhs.m_data); this->copy_timestamp(rhs); } }

    std::vector<coordf_t> get() const throw() { return m_data; }
    bool                  empty() const throw() { return m_data.empty(); }
    void                  set(const std::vector<coordf_t> &data) { if (m_data != data) { m_data = data; this->touch(); } }
    void                  set(std::vector<coordf_t> &&data) { if (m_data != data) { m_data = std::move(data); this->touch(); } }
    void                  clear() { m_data.clear(); this->touch(); }

    template<class Archive> void serialize(Archive &ar)
    {
        ar(cereal::base_class<ObjectWithTimestamp>(this), m_data);
    }

private:
    explicit LayerHeightProfile() = default;
    explicit LayerHeightProfile(int) : ObjectWithTimestamp(-1) {}
    explicit LayerHeightProfile(const LayerHeightProfile &rhs) = default;
    explicit LayerHeightProfile(LayerHeightProfile &&rhs) = default;

    LayerHeightProfile& operator=(const LayerHeightProfile &rhs) = default;
    LayerHeightProfile& operator=(LayerHeightProfile &&rhs) = default;

    std::vector<coordf_t> m_data;

    friend class ModelObject;
};

enum class CutMode : int {
    cutPlanar,
    cutTongueAndGroove
};

enum class CutConnectorType : int {
    Plug
    , Dowel
    , Snap
    , Undef
};

enum class CutConnectorStyle : int {
    Prism
    , Frustum
    , Undef
};

enum class CutConnectorShape : int {
    Triangle
    , Square
    , Hexagon
    , Circle
    , Undef
};
struct CutConnectorParas
{
    float snap_space_proportion{0.3};
    float snap_bulge_proportion{0.15};
};

struct CutConnectorAttributes
{
    CutConnectorType    type{ CutConnectorType::Plug };
    CutConnectorStyle   style{ CutConnectorStyle::Prism };
    CutConnectorShape   shape{ CutConnectorShape::Circle };

    CutConnectorAttributes() {}

    CutConnectorAttributes(CutConnectorType t, CutConnectorStyle st, CutConnectorShape sh)
        : type(t), style(st), shape(sh)
    {}

    CutConnectorAttributes(const CutConnectorAttributes& rhs) :
        CutConnectorAttributes(rhs.type, rhs.style, rhs.shape) {}

    bool operator==(const CutConnectorAttributes& other) const;

    bool operator!=(const CutConnectorAttributes& other) const { return !(other == (*this)); }

    bool operator<(const CutConnectorAttributes& other) const {
        return   this->type <  other.type ||
                (this->type == other.type && this->style <  other.style) ||
                (this->type == other.type && this->style == other.style && this->shape < other.shape);
    }

    template<class Archive> inline void serialize(Archive& ar) {
        ar(type, style, shape);
    }
};

struct CutConnector
{
    Vec3d pos;
    Transform3d rotation_m;
    float radius;
    float height;
    float radius_tolerance;
    float height_tolerance;
    float z_angle {0.f};
    CutConnectorAttributes attribs;

    CutConnector()
        : pos(Vec3d::Zero()), rotation_m(Transform3d::Identity()), radius(5.f), height(10.f), radius_tolerance(0.f), height_tolerance(0.1f), z_angle(0.f)
    {}

    CutConnector(Vec3d p, Transform3d rot, float r, float h, float rt, float ht, float za, CutConnectorAttributes attributes)
        : pos(p), rotation_m(rot), radius(r), height(h), radius_tolerance(rt), height_tolerance(ht), z_angle(za), attribs(attributes)
    {}

    CutConnector(const CutConnector& rhs) :
        CutConnector(rhs.pos, rhs.rotation_m, rhs.radius, rhs.height, rhs.radius_tolerance, rhs.height_tolerance, rhs.z_angle, rhs.attribs) {}

    bool operator==(const CutConnector& other) const;

    bool operator!=(const CutConnector& other) const { return !(other == (*this)); }

    template<class Archive> inline void serialize(Archive& ar) {
        ar(pos, rotation_m, radius, height, radius_tolerance, height_tolerance, z_angle, attribs);
    }
};

using CutConnectors = std::vector<CutConnector>;


enum class ModelVolumeType : int {
    INVALID = -1,
    MODEL_PART = 0,
    NEGATIVE_VOLUME,
    PARAMETER_MODIFIER,
    SUPPORT_BLOCKER,
    SUPPORT_ENFORCER,
    PRECISE_SEAM_CENTER,
    PRECISE_SEAM_LEFT,
    PRECISE_SEAM_RIGHT,
    PRECISE_SEAM_ENFORCED,
    PRECISE_SEAM_BLOCKED,
    PRECISE_SEAM_NEUTRAL,
};

inline bool is_precise_seam(ModelVolumeType t)       { return t >= ModelVolumeType::PRECISE_SEAM_CENTER && t <= ModelVolumeType::PRECISE_SEAM_NEUTRAL; }
inline bool is_precise_seam_strong(ModelVolumeType t) { return t >= ModelVolumeType::PRECISE_SEAM_CENTER && t <= ModelVolumeType::PRECISE_SEAM_RIGHT; }
inline bool is_precise_seam_weak(ModelVolumeType t)   { return t >= ModelVolumeType::PRECISE_SEAM_ENFORCED && t <= ModelVolumeType::PRECISE_SEAM_NEUTRAL; }

class ModelObject final : public ObjectBase
{
public:
    std::string             name;
    std::string             module_name;
    std::string             input_file;
    ModelInstancePtrs       instances;
    ModelVolumePtrs         volumes;
    ModelConfigObject 		config;
    t_layer_config_ranges   layer_config_ranges;
    LayerHeightProfile      layer_height_profile;
    bool                    printable { true };

    sla::SupportPoints      sla_support_points;
    sla::PointsStatus       sla_points_status = sla::PointsStatus::NoPoints;

    sla::DrainHoles         sla_drain_holes;

    BrimPoints              brim_points;

    Vec3d                   origin_translation;

    std::vector<ObjectID>   volume_ids;

    CutConnectors cut_connectors;
    CutObjectBase cut_id;

    Model*                  get_model() { return m_model; }
    const Model*            get_model() const { return m_model; }
    int                     get_backup_id() const;
    template<typename T> const T* get_config_value(const DynamicPrintConfig& global_config, const std::string& config_option) {
        if (config.has(config_option))
            return static_cast<const T*>(config.option(config_option));
        else
            return global_config.option<T>(config_option);
    }

    ModelVolume*            add_volume(const TriangleMesh &mesh, bool modify_to_center_geometry = true);
    ModelVolume*            add_volume(TriangleMesh &&mesh, ModelVolumeType type = ModelVolumeType::MODEL_PART, bool modify_to_center_geometry = true);
    ModelVolume*            add_volume(const ModelVolume &volume, ModelVolumeType type = ModelVolumeType::INVALID);
    ModelVolume*            add_volume(const ModelVolume &volume, TriangleMesh &&mesh);
    ModelVolume*            add_volume_with_shared_mesh(const ModelVolume &other, ModelVolumeType type = ModelVolumeType::MODEL_PART);
    void                    delete_volume(size_t idx);
    void                    clear_volumes();
    void                    sort_volumes(bool full_sort);
    bool                    is_multiparts() const { return volumes.size() > 1; }
    bool                    is_fdm_support_painted() const;
    bool                    is_seam_painted() const;
    bool                    is_mm_painted() const;
    bool                    is_fuzzy_skin_painted() const;
    bool                    has_custom_layering() const
        { return ! this->layer_config_ranges.empty() || ! this->layer_height_profile.empty(); }

    ModelInstance*          add_instance();
    ModelInstance*          add_instance(const ModelInstance &instance);
    ModelInstance*          add_instance(const Vec3d &offset, const Vec3d &scaling_factor, const Vec3d &rotation, const Vec3d &mirror);
    void                    delete_instance(size_t idx);
    void                    delete_last_instance();
    void                    clear_instances();

    const BoundingBoxf3&    bounding_box_approx() const;
    const BoundingBoxf3&    bounding_box_exact() const;
    double                  min_z() const;
    double                  max_z() const;

    void invalidate_bounding_box() {
        m_bounding_box_approx_valid     = false;
        m_bounding_box_exact_valid      = false;
        m_min_max_z_valid               = false;
        m_raw_bounding_box_valid        = false;
        m_raw_mesh_bounding_box_valid   = false;
    }

    TriangleMesh mesh() const;
    TriangleMesh raw_mesh() const;
    indexed_triangle_set raw_indexed_triangle_set() const;
    const BoundingBoxf3& raw_bounding_box() const;
    BoundingBoxf3 instance_bounding_box(size_t instance_idx, bool dont_translate = false) const;
    BoundingBoxf3 instance_bounding_box(const ModelInstance& instance, bool dont_translate = false) const;

	const BoundingBoxf3& raw_mesh_bounding_box() const;
    BoundingBoxf3 full_raw_mesh_bounding_box() const;

    BoundingBoxf3 instance_convex_hull_bounding_box(size_t instance_idx, bool dont_translate = false) const;
    BoundingBoxf3 instance_convex_hull_bounding_box(const ModelInstance* instance, bool dont_translate = false) const;

    Polygon       convex_hull_2d(const Transform3d &trafo_instance) const;

    void center_around_origin(bool include_modifiers = true);
    void ensure_on_bed(bool allow_negative_z = false);

    void translate_instances(const Vec3d& vector);
    void translate_instance(size_t instance_idx, const Vec3d& vector);
    void translate(const Vec3d &vector) { this->translate(vector(0), vector(1), vector(2)); }
    void translate(double x, double y, double z);
    void scale(const Vec3d &versor);
    void scale(const double s) { this->scale(Vec3d(s, s, s)); }
    void scale(double x, double y, double z) { this->scale(Vec3d(x, y, z)); }
    void scale_to_fit(const Vec3d &size);
    void rotate(double angle, Axis axis);
    void rotate(double angle, const Vec3d& axis);
    void mirror(Axis axis);

    void scale_mesh_after_creation(const float scale);
    void convert_units(ModelObjectPtrs&new_objects, ConversionType conv_type, std::vector<int> volume_idxs);

    size_t materials_count() const;
    size_t facets_count() const;
    size_t parts_count() const;
    void invalidate_cut();
    void delete_connectors();
    void clone_for_cut(ModelObject **obj);

    void split(ModelObjectPtrs*new_objects, bool remap_paint);
    void merge();

    bool make_boolean(ModelObject *cut_object, const std::string &boolean_opts);

    ModelObjectPtrs merge_volumes(std::vector<int>& vol_indeces);
    void bake_xy_rotation_into_meshes(size_t instance_idx);

    double get_instance_min_z(size_t instance_idx) const;
    double get_instance_max_z(size_t instance_idx) const;

    void print_info() const;

    std::string get_export_filename() const;

    TriangleMeshStats get_object_stl_stats() const;
    int         get_repaired_errors_count(const int vol_idx = -1) const;

    bool has_solid_mesh() const;
    bool is_cut() const { return cut_id.id().valid(); }
    bool has_connectors() const;
private:
    friend class Model;
    explicit ModelObject(Model* model) : m_model(model), origin_translation(Vec3d::Zero())
    {
        assert(this->id().valid());
        assert(this->config.id().valid());
        assert(this->layer_height_profile.id().valid());
    }
    explicit ModelObject(int) : ObjectBase(-1), config(-1), layer_height_profile(-1), origin_translation(Vec3d::Zero())
    {
        assert(this->id().invalid());
        assert(this->config.id().invalid());
        assert(this->layer_height_profile.id().invalid());
    }
	~ModelObject();
	void assign_new_unique_ids_recursive() override;

    ModelObject(const ModelObject &rhs) : ObjectBase(-1), config(-1), layer_height_profile(-1), m_model(rhs.m_model) {
    	assert(this->id().invalid());
        assert(this->config.id().invalid());
        assert(this->layer_height_profile.id().invalid());
        assert(rhs.id() != rhs.config.id());
        assert(rhs.id() != rhs.layer_height_profile.id());
    	this->assign_copy(rhs);
    	assert(this->id().valid());
        assert(this->config.id().valid());
        assert(this->layer_height_profile.id().valid());
        assert(this->id() != this->config.id());
        assert(this->id() != this->layer_height_profile.id());
    	assert(this->id() == rhs.id());
        assert(this->config.id() == rhs.config.id());
        assert(this->layer_height_profile.id() == rhs.layer_height_profile.id());
    }
    explicit ModelObject(ModelObject &&rhs) : ObjectBase(-1), config(-1), layer_height_profile(-1) {
    	assert(this->id().invalid());
        assert(this->config.id().invalid());
        assert(this->layer_height_profile.id().invalid());
        assert(rhs.id() != rhs.config.id());
        assert(rhs.id() != rhs.layer_height_profile.id());
    	this->assign_copy(std::move(rhs));
    	assert(this->id().valid());
        assert(this->config.id().valid());
        assert(this->layer_height_profile.id().valid());
        assert(this->id() != this->config.id());
        assert(this->id() != this->layer_height_profile.id());
    	assert(this->id() == rhs.id());
        assert(this->config.id() == rhs.config.id());
        assert(this->layer_height_profile.id() == rhs.layer_height_profile.id());
    }
    ModelObject& operator=(const ModelObject &rhs) {
    	this->assign_copy(rhs);
    	m_model = rhs.m_model;
    	assert(this->id().valid());
        assert(this->config.id().valid());
        assert(this->layer_height_profile.id().valid());
        assert(this->id() != this->config.id());
        assert(this->id() != this->layer_height_profile.id());
    	assert(this->id() == rhs.id());
        assert(this->config.id() == rhs.config.id());
        assert(this->layer_height_profile.id() == rhs.layer_height_profile.id());
    	return *this;
    }
    ModelObject& operator=(ModelObject &&rhs) {
    	this->assign_copy(std::move(rhs));
    	m_model = rhs.m_model;
    	assert(this->id().valid());
        assert(this->config.id().valid());
        assert(this->layer_height_profile.id().valid());
        assert(this->id() != this->config.id());
        assert(this->id() != this->layer_height_profile.id());
    	assert(this->id() == rhs.id());
        assert(this->config.id() == rhs.config.id());
        assert(this->layer_height_profile.id() == rhs.layer_height_profile.id());
    	return *this;
    }
	void set_new_unique_id() {
        ObjectBase::set_new_unique_id();
        this->config.set_new_unique_id();
        this->layer_height_profile.set_new_unique_id();
    }

    OBJECTBASE_DERIVED_COPY_MOVE_CLONE(ModelObject)

    Model                *m_model { nullptr };

    mutable BoundingBoxf3 m_bounding_box_approx;
    mutable bool          m_bounding_box_approx_valid { false };
    mutable BoundingBoxf3 m_bounding_box_exact;
    mutable bool          m_bounding_box_exact_valid { false };
    mutable bool          m_min_max_z_valid { false };
    mutable BoundingBoxf3 m_raw_bounding_box;
    mutable bool          m_raw_bounding_box_valid { false };
    mutable BoundingBoxf3 m_raw_mesh_bounding_box;
    mutable bool          m_raw_mesh_bounding_box_valid { false };

    void copy_transformation_caches(const ModelObject &src) {
        m_bounding_box_approx             = src.m_bounding_box_approx;
        m_bounding_box_approx_valid       = src.m_bounding_box_approx_valid;
        m_bounding_box_exact              = src.m_bounding_box_exact;
        m_bounding_box_exact_valid        = src.m_bounding_box_exact_valid;
        m_min_max_z_valid                 = src.m_min_max_z_valid;
        m_raw_bounding_box                = src.m_raw_bounding_box;
        m_raw_bounding_box_valid          = src.m_raw_bounding_box_valid;
        m_raw_mesh_bounding_box           = src.m_raw_mesh_bounding_box;
        m_raw_mesh_bounding_box_valid     = src.m_raw_mesh_bounding_box_valid;
    }

    friend class Print;
    friend class SLAPrint;
    void        set_model(Model *model) { m_model = model; }

	friend class cereal::access;
	friend class UndoRedo::StackImpl;
	ModelObject() :
        ObjectBase(-1), config(-1), layer_height_profile(-1) {
		assert(this->id().invalid());
        assert(this->config.id().invalid());
        assert(this->layer_height_profile.id().invalid());
	}
    template<class Archive> void save(Archive& ar) const {
        ar(cereal::base_class<ObjectBase>(this));
        Internal::StaticSerializationWrapper<ModelConfigObject const> config_wrapper(config);
        Internal::StaticSerializationWrapper<LayerHeightProfile const> layer_heigth_profile_wrapper(layer_height_profile);
        ar(name, module_name, input_file, instances, volumes, config_wrapper, layer_config_ranges, layer_heigth_profile_wrapper,
            sla_support_points, sla_points_status, sla_drain_holes, printable, origin_translation, brim_points,
            m_bounding_box_approx, m_bounding_box_approx_valid, 
            m_bounding_box_exact, m_bounding_box_exact_valid, m_min_max_z_valid,
            m_raw_bounding_box, m_raw_bounding_box_valid, m_raw_mesh_bounding_box, m_raw_mesh_bounding_box_valid,
            cut_connectors, cut_id);
    }
    template<class Archive> void load(Archive& ar) {
        ar(cereal::base_class<ObjectBase>(this));
        Internal::StaticSerializationWrapper<ModelConfigObject> config_wrapper(config);
        Internal::StaticSerializationWrapper<LayerHeightProfile> layer_heigth_profile_wrapper(layer_height_profile);
        SaveObjectGaurd gaurd(*this);
        ar(name, module_name, input_file, instances, volumes, config_wrapper, layer_config_ranges, layer_heigth_profile_wrapper,
            sla_support_points, sla_points_status, sla_drain_holes, printable, origin_translation, brim_points,
            m_bounding_box_approx, m_bounding_box_approx_valid, 
            m_bounding_box_exact, m_bounding_box_exact_valid, m_min_max_z_valid,
            m_raw_bounding_box, m_raw_bounding_box_valid, m_raw_mesh_bounding_box, m_raw_mesh_bounding_box_valid,
            cut_connectors, cut_id);
        std::vector<ObjectID> volume_ids2;
        std::transform(volumes.begin(), volumes.end(), std::back_inserter(volume_ids2), std::mem_fn(&ObjectBase::id));
        if (volume_ids != volume_ids2)
            Slic3r::save_object_mesh(*this);
        volume_ids.clear();
    }

    unsigned int update_instances_print_volume_state(const BuildVolume &build_volume);

    void update_min_max_z();
};

enum class ConversionType : int {
    CONV_TO_INCH,
    CONV_FROM_INCH,
    CONV_TO_METER,
    CONV_FROM_METER,
};

enum class En3mfType : int {
    From_Orca,
    From_BBS,
    From_Prusa,
    From_Other
};

class FacetsAnnotation final : public ObjectWithTimestamp {
public:
    void assign(const FacetsAnnotation &rhs) { if (! this->timestamp_matches(rhs)) { m_data = rhs.m_data; this->copy_timestamp(rhs); } }
    void assign(FacetsAnnotation &&rhs) { if (! this->timestamp_matches(rhs)) { m_data = std::move(rhs.m_data); this->copy_timestamp(rhs); } }
    const TriangleSelector::TriangleSplittingData &get_data() const noexcept { return m_data; }
    void set_data(TriangleSelector::TriangleSplittingData &&data) { m_data = std::move(data); this->touch(); }
    bool set(const TriangleSelector& selector);
    indexed_triangle_set get_facets(const ModelVolume& mv, EnforcerBlockerType type) const;
    void get_facets(const ModelVolume& mv, std::vector<indexed_triangle_set>& facets_per_type) const;
    void                 set_enforcer_block_type_limit(const ModelVolume  &mv,
                                                       EnforcerBlockerType max_type,
                                                       EnforcerBlockerType to_delete_filament = EnforcerBlockerType::NONE,
                                                       EnforcerBlockerType replace_filament = EnforcerBlockerType::NONE);
    void                 shift_states_above(const ModelVolume &mv, EnforcerBlockerType threshold, int delta);
    void                 remap_states(const ModelVolume &mv, const EnforcerBlockerStateMap &state_map);
    indexed_triangle_set get_facets_strict(const ModelVolume& mv, EnforcerBlockerType type) const;
    bool has_facets(const ModelVolume& mv, EnforcerBlockerType type) const;
    bool empty() const { return m_data.triangles_to_split.empty(); }

    void reset();

    std::string get_triangle_as_string(int i) const;

    void reserve(int n_triangles) { m_data.triangles_to_split.reserve(n_triangles); }
    void set_triangle_from_string(int triangle_id, const std::string& str);
    void shrink_to_fit() { m_data.triangles_to_split.shrink_to_fit(); m_data.bitstream.shrink_to_fit(); }
    bool equals(const FacetsAnnotation &other) const;

private:
    explicit FacetsAnnotation() = default;
    explicit FacetsAnnotation(int) : ObjectWithTimestamp(-1) {}
    explicit FacetsAnnotation(const FacetsAnnotation &rhs) = default;
    explicit FacetsAnnotation(FacetsAnnotation &&rhs) = default;

    FacetsAnnotation& operator=(const FacetsAnnotation &rhs) = default;
    FacetsAnnotation& operator=(FacetsAnnotation &&rhs) = default;

    friend class cereal::access;
    friend class UndoRedo::StackImpl;

    template<class Archive> void serialize(Archive &ar)
    {
        ar(cereal::base_class<ObjectWithTimestamp>(this), m_data);
    }

    TriangleSelector::TriangleSplittingData m_data;

    friend class ModelVolume;
};

class ModelVolume final : public ObjectBase
{
public:
    std::string         name;
    struct Source
    {
        std::string input_file;
        int object_idx{ -1 };
        int volume_idx{ -1 };
        Vec3d mesh_offset{ Vec3d::Zero() };
        Geometry::Transformation transform;
        bool is_converted_from_inches{ false };
        bool is_converted_from_meters{ false };
        bool is_from_builtin_objects{ false };

        template<class Archive> void serialize(Archive& ar) { 
            ar(input_file, object_idx, volume_idx, mesh_offset, transform, is_converted_from_inches, is_converted_from_meters, is_from_builtin_objects);
        }
    };
    Source              source;

    struct CutInfo
    {
        bool                is_from_upper{ true };
        bool                is_connector{ false };
        bool                is_processed{ true };
        CutConnectorType    connector_type{ CutConnectorType::Plug };
        float               radius_tolerance{ 0.f };
        float               height_tolerance{ 0.f };

        CutInfo() = default;
        CutInfo(CutConnectorType type, float rad_tolerance, float h_tolerance, bool processed = false) :
        is_connector(true),
        is_processed(processed),
        connector_type(type),
        radius_tolerance(rad_tolerance),
        height_tolerance(h_tolerance)
        {}

        void set_processed() { is_processed = true; }
        void invalidate()    { is_connector = false; }
        void reset_from_upper() { is_from_upper = true; }

        template<class Archive> inline void serialize(Archive& ar) {
            ar(is_connector, is_processed, connector_type, radius_tolerance, height_tolerance);
        }
    };
    CutInfo             cut_info;

    bool                is_from_upper() const    { return cut_info.is_from_upper; }
    void                reset_from_upper()       { cut_info.reset_from_upper(); }

    bool                is_cut_connector() const { return cut_info.is_processed && cut_info.is_connector; }
    void                invalidate_cut_info()    { cut_info.invalidate(); }

    const TriangleMesh& mesh() const { return *m_mesh.get(); }
    std::shared_ptr<const TriangleMesh> mesh_ptr() const { return m_mesh; }
    void                set_mesh(const TriangleMesh &mesh) { m_mesh = std::make_shared<const TriangleMesh>(mesh); }
    void                set_mesh(TriangleMesh &&mesh) { m_mesh = std::make_shared<const TriangleMesh>(std::move(mesh)); }
    void                set_mesh(const indexed_triangle_set &mesh) { m_mesh = std::make_shared<const TriangleMesh>(mesh); }
    void                set_mesh(indexed_triangle_set &&mesh) { m_mesh = std::make_shared<const TriangleMesh>(std::move(mesh)); }
    void                set_mesh(std::shared_ptr<const TriangleMesh> &mesh) { m_mesh = mesh; }
    void                set_mesh(std::unique_ptr<const TriangleMesh> &&mesh) { m_mesh = std::move(mesh); }
	void				reset_mesh() { m_mesh = std::make_shared<const TriangleMesh>(); }
    const std::shared_ptr<const TriangleMesh>& get_mesh_shared_ptr() const { return m_mesh; }
    ModelConfigObject	config;

    FacetsAnnotation    supported_facets;

    FacetsAnnotation    seam_facets;

    FacetsAnnotation    mmu_segmentation_facets;

    FacetsAnnotation    fuzzy_skin_facets;

    FacetsAnnotation    nonplanar_facets{-1};

    FacetsAnnotation texture_displacement_facets_0;
    FacetsAnnotation texture_displacement_facets_1;
    FacetsAnnotation texture_displacement_facets_2;
    FacetsAnnotation texture_displacement_facets_3;
    FacetsAnnotation texture_displacement_facets_4;
    FacetsAnnotation texture_displacement_facets_5;
    FacetsAnnotation texture_displacement_facets_6;
    FacetsAnnotation texture_displacement_facets_7;

    FacetsAnnotation& texture_displacement_facet(int slot) {
        switch (slot) {
        case 0: return texture_displacement_facets_0;
        case 1: return texture_displacement_facets_1;
        case 2: return texture_displacement_facets_2;
        case 3: return texture_displacement_facets_3;
        case 4: return texture_displacement_facets_4;
        case 5: return texture_displacement_facets_5;
        case 6: return texture_displacement_facets_6;
        default: assert(slot == 7); return texture_displacement_facets_7;
        }
    }
    const FacetsAnnotation& texture_displacement_facet(int slot) const { return const_cast<ModelVolume*>(this)->texture_displacement_facet(slot); }

    bool texture_displacement_facets_ids_valid() const {
        for (int i = 0; i < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++i)
            if (!texture_displacement_facet(i).id().valid() || texture_displacement_facet(i).id() == this->id())
                return false;
        return true;
    }
    bool texture_displacement_facets_ids_invalid() const {
        for (int i = 0; i < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++i)
            if (texture_displacement_facet(i).id().valid())
                return false;
        return true;
    }
    bool texture_displacement_facets_all_empty() const {
        for (int i = 0; i < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++i)
            if (!texture_displacement_facet(i).empty())
                return false;
        return true;
    }

    std::vector<TextureDisplacementLayer> texture_displacement_layers;

    TextureDisplacementOptions texture_displacement_options;

    std::optional<TriangleSelector::SavedPainting> save_painting() const;
    
    void restore_painting(const std::optional<TriangleSelector::SavedPainting>& saved, bool keep_existing_paint = false);

    mutable std::vector<int> mmuseg_extruders;
    mutable Timestamp        mmuseg_ts;

    std::optional<TextConfiguration> text_configuration;

    std::optional<EmbossShape> emboss_shape; 

    ModelObject*        get_object() const { return this->object; }
    ModelVolumeType     type() const { return m_type; }
    void                set_type(const ModelVolumeType t) { m_type = t; }
	bool                is_model_part()         const { return m_type == ModelVolumeType::MODEL_PART; }
    bool                is_negative_volume()    const { return m_type == ModelVolumeType::NEGATIVE_VOLUME; }
	bool                is_modifier()           const { return m_type == ModelVolumeType::PARAMETER_MODIFIER; }
	bool                is_support_enforcer()   const { return m_type == ModelVolumeType::SUPPORT_ENFORCER; }
	bool                is_support_blocker()    const { return m_type == ModelVolumeType::SUPPORT_BLOCKER; }
	bool                is_support_modifier()   const { return m_type == ModelVolumeType::SUPPORT_BLOCKER || m_type == ModelVolumeType::SUPPORT_ENFORCER; }
	bool                is_precise_seam()       const { return Slic3r::is_precise_seam(m_type); }
	bool                is_precise_seam_strong() const { return Slic3r::is_precise_seam_strong(m_type); }
	bool                is_precise_seam_weak()   const { return Slic3r::is_precise_seam_weak(m_type); }
    bool                is_text()               const { return text_configuration.has_value(); }
    bool                is_svg() const { return emboss_shape.has_value()  && !text_configuration.has_value(); }
    bool                is_the_only_one_part() const;
    t_model_material_id material_id() const { return m_material_id; }
    void                set_material_id(t_model_material_id material_id);
    void                reset_extra_facets();
    ModelMaterial*      material() const;
    void                set_material(t_model_material_id material_id, const ModelMaterial &material);
    int                 extruder_id() const;

    void clear_cache() {
        m_cached_trans_matrix = Transform3d::Identity().inverse();
        m_convex_hull_2d.clear();
        m_cached_2d_polygon.clear();
    };

    bool                is_splittable() const;

    std::vector<int>    get_extruders() const;
    void                update_extruder_count(size_t extruder_count);
    void                update_extruder_count_when_delete_filament(size_t extruder_count, size_t filament_id, int replace_filament_id = -1,
                                                                   const std::vector<unsigned char> &filament_is_mixed = {});

    size_t              split(unsigned int max_extruders, bool remap_paint);
    void                translate(double x, double y, double z) { translate(Vec3d(x, y, z)); }
    void                translate(const Vec3d& displacement);
    void                scale(const Vec3d& scaling_factors);
    void                scale(double x, double y, double z) { scale(Vec3d(x, y, z)); }
    void                scale(double s) { scale(Vec3d(s, s, s)); }
    void                rotate(double angle, Axis axis);
    void                rotate(double angle, const Vec3d& axis);
    void                mirror(Axis axis);

    void                scale_geometry_after_creation(const Vec3f &versor);
    void                scale_geometry_after_creation(const float scale) { this->scale_geometry_after_creation(Vec3f(scale, scale, scale)); }

    void                center_geometry_after_creation(bool update_source_offset = true);

    void                calculate_convex_hull();
    const TriangleMesh& get_convex_hull() const;
    const std::shared_ptr<const TriangleMesh>& get_convex_hull_shared_ptr() const { return m_convex_hull; }
    const Polygon& get_convex_hull_2d(const Transform3d &trafo_instance) const;
    void invalidate_convex_hull_2d()
    {
        m_convex_hull_2d.clear();
    }

    int                 get_repaired_errors_count() const;

    static ModelVolumeType type_from_string(const std::string &s);
    static std::string  type_to_string(const ModelVolumeType t);

    const Geometry::Transformation& get_transformation() const { return m_transformation; }
    void set_transformation(const Geometry::Transformation& transformation) { clear_cache(); m_transformation = transformation; }
    void set_transformation(const Transform3d& trafo) { clear_cache(); m_transformation.set_matrix(trafo); }

    Vec3d get_offset() const { return m_transformation.get_offset(); }

    double get_offset(Axis axis) const { return m_transformation.get_offset(axis); }

    void set_offset(const Vec3d& offset) { clear_cache(); m_transformation.set_offset(offset); }
    void set_offset(Axis axis, double offset) { clear_cache(); m_transformation.set_offset(axis, offset); }

    Vec3d get_rotation() const { return m_transformation.get_rotation(); }
    double get_rotation(Axis axis) const { return m_transformation.get_rotation(axis); }

    void set_rotation(const Vec3d& rotation) { clear_cache(); m_transformation.set_rotation(rotation); }
    void set_rotation(Axis axis, double rotation) { clear_cache(); m_transformation.set_rotation(axis, rotation); }

    Vec3d get_scaling_factor() const { return m_transformation.get_scaling_factor(); }
    double get_scaling_factor(Axis axis) const { return m_transformation.get_scaling_factor(axis); }

    void set_scaling_factor(const Vec3d& scaling_factor) { clear_cache(); m_transformation.set_scaling_factor(scaling_factor); }
    void set_scaling_factor(Axis axis, double scaling_factor) {clear_cache(); m_transformation.set_scaling_factor(axis, scaling_factor); }

    Vec3d get_mirror() const { return m_transformation.get_mirror(); }
    double get_mirror(Axis axis) const { return m_transformation.get_mirror(axis); }
    bool is_left_handed() const { return m_transformation.is_left_handed(); }

    void set_mirror(const Vec3d& mirror) { clear_cache(); m_transformation.set_mirror(mirror); }
    void set_mirror(Axis axis, double mirror) { clear_cache(); m_transformation.set_mirror(axis, mirror); }
    void convert_from_imperial_units();
    void convert_from_meters();

    const Transform3d& get_matrix() const { return m_transformation.get_matrix(); }
    Transform3d get_matrix_no_offset() const { return m_transformation.get_matrix_no_offset(); }

	void set_new_unique_id() {
        ObjectBase::set_new_unique_id();
        this->config.set_new_unique_id();
        this->supported_facets.set_new_unique_id();
        this->seam_facets.set_new_unique_id();
        this->mmu_segmentation_facets.set_new_unique_id();
        this->fuzzy_skin_facets.set_new_unique_id();
        for (int i = 0; i < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++i)
            this->texture_displacement_facet(i).set_new_unique_id();
    }

    bool is_fdm_support_painted() const { return !this->supported_facets.empty(); }
    bool is_seam_painted() const { return !this->seam_facets.empty(); }
    bool is_mm_painted() const { return !this->mmu_segmentation_facets.empty(); }
    bool is_fuzzy_skin_painted() const { return !this->fuzzy_skin_facets.empty(); }
    bool is_nonplanar_painted() const { return !this->nonplanar_facets.empty(); }
    bool is_texture_displacement_painted() const { return !this->texture_displacement_facets_all_empty(); }
    bool is_any_painted() const {
        return is_fdm_support_painted() || is_seam_painted() || is_mm_painted() || is_fuzzy_skin_painted() || is_nonplanar_painted() || is_texture_displacement_painted();
    }
    
     std::vector<size_t> get_extruders_from_multi_material_painting() const;

protected:
	friend class Print;
    friend class SLAPrint;
    friend class Model;
	friend class ModelObject;
    friend void model_volume_list_update_supports_and_seams(ModelObject& model_object_dst, const ModelObject& model_object_new);

	explicit ModelVolume(const ModelVolume &rhs) = default;
    void     set_model_object(ModelObject *model_object) { object = model_object; }
	void 	 assign_new_unique_ids_recursive() override;
    void     transform_this_mesh(const Transform3d& t, bool fix_left_handed);
    void     transform_this_mesh(const Matrix3d& m, bool fix_left_handed);

private:
    ModelObject*                    	object;
    std::shared_ptr<const TriangleMesh> m_mesh;
    ModelVolumeType                 	m_type;
    t_model_material_id             	m_material_id;
    std::shared_ptr<const TriangleMesh> m_convex_hull;
    mutable Polygon                     m_convex_hull_2d;
    mutable Transform3d                 m_cached_trans_matrix;
    mutable Polygon                     m_cached_2d_polygon;
    Geometry::Transformation        	m_transformation;

    void  calculate_convex_hull_2d(const Geometry::Transformation &transformation) const;

    mutable int               		m_is_splittable{ -1 };

	ModelVolume(ModelObject *object, const TriangleMesh &mesh, ModelVolumeType type = ModelVolumeType::MODEL_PART) : m_mesh(new TriangleMesh(mesh)), m_type(type), object(object)
    {
		assert(this->id().valid());
        assert(this->config.id().valid());
        assert(this->supported_facets.id().valid());
        assert(this->seam_facets.id().valid());
        assert(this->mmu_segmentation_facets.id().valid());
        assert(this->fuzzy_skin_facets.id().valid());
        assert(this->texture_displacement_facets_ids_valid());
        assert(this->id() != this->config.id());
        assert(this->id() != this->supported_facets.id());
        assert(this->id() != this->seam_facets.id());
        assert(this->id() != this->mmu_segmentation_facets.id());
        assert(this->id() != this->fuzzy_skin_facets.id());
        if (mesh.facets_count() > 1)
            calculate_convex_hull();
    }
    ModelVolume(ModelObject *object, const std::shared_ptr<const TriangleMesh> &mesh, ModelVolumeType type = ModelVolumeType::MODEL_PART) : m_mesh(mesh), m_type(type), object(object)
    {
		assert(this->id().valid());
        assert(this->config.id().valid());
        assert(this->supported_facets.id().valid());
        assert(this->seam_facets.id().valid());
        assert(this->mmu_segmentation_facets.id().valid());
        assert(this->fuzzy_skin_facets.id().valid());
        assert(this->texture_displacement_facets_ids_valid());
        assert(this->id() != this->config.id());
        assert(this->id() != this->supported_facets.id());
        assert(this->id() != this->seam_facets.id());
        assert(this->id() != this->mmu_segmentation_facets.id());
        assert(this->id() != this->fuzzy_skin_facets.id());
    }
    ModelVolume(ModelObject *object, TriangleMesh &&mesh, TriangleMesh &&convex_hull, ModelVolumeType type = ModelVolumeType::MODEL_PART) :
		m_mesh(new TriangleMesh(std::move(mesh))), m_convex_hull(new TriangleMesh(std::move(convex_hull))), m_type(type), object(object) {
		assert(this->id().valid());
        assert(this->config.id().valid());
        assert(this->supported_facets.id().valid());
        assert(this->seam_facets.id().valid());
        assert(this->mmu_segmentation_facets.id().valid());
        assert(this->fuzzy_skin_facets.id().valid());
        assert(this->texture_displacement_facets_ids_valid());
        assert(this->id() != this->config.id());
        assert(this->id() != this->supported_facets.id());
        assert(this->id() != this->seam_facets.id());
        assert(this->id() != this->mmu_segmentation_facets.id());
        assert(this->id() != this->fuzzy_skin_facets.id());
	}

    ModelVolume(ModelObject *object, const ModelVolume &other) :
        ObjectBase(other),
        name(other.name), source(other.source), m_mesh(other.m_mesh), m_convex_hull(other.m_convex_hull),
        config(other.config), m_type(other.m_type), object(object), m_transformation(other.m_transformation),
        supported_facets(other.supported_facets), seam_facets(other.seam_facets), mmu_segmentation_facets(other.mmu_segmentation_facets),
        fuzzy_skin_facets(other.fuzzy_skin_facets), nonplanar_facets(other.nonplanar_facets),
        texture_displacement_facets_0(other.texture_displacement_facets_0), texture_displacement_facets_1(other.texture_displacement_facets_1),
        texture_displacement_facets_2(other.texture_displacement_facets_2), texture_displacement_facets_3(other.texture_displacement_facets_3),
        texture_displacement_facets_4(other.texture_displacement_facets_4), texture_displacement_facets_5(other.texture_displacement_facets_5),
        texture_displacement_facets_6(other.texture_displacement_facets_6), texture_displacement_facets_7(other.texture_displacement_facets_7),
        texture_displacement_layers(other.texture_displacement_layers),
        texture_displacement_options(other.texture_displacement_options),
        cut_info(other.cut_info), text_configuration(other.text_configuration), emboss_shape(other.emboss_shape)
    {
		assert(this->id().valid());
        assert(this->config.id().valid());
        assert(this->supported_facets.id().valid());
        assert(this->seam_facets.id().valid());
        assert(this->mmu_segmentation_facets.id().valid());
        assert(this->fuzzy_skin_facets.id().valid());
        assert(this->id() != this->config.id());
        assert(this->id() != this->supported_facets.id());
        assert(this->id() != this->seam_facets.id());
        assert(this->id() != this->mmu_segmentation_facets.id());
		assert(this->id() == other.id());
        assert(this->config.id() == other.config.id());
        assert(this->supported_facets.id() == other.supported_facets.id());
        assert(this->seam_facets.id() == other.seam_facets.id());
        assert(this->mmu_segmentation_facets.id() == other.mmu_segmentation_facets.id());
        assert(this->fuzzy_skin_facets.id() == other.fuzzy_skin_facets.id());
        this->set_material_id(other.material_id());
    }
    ModelVolume(ModelObject *object, const ModelVolume &other, TriangleMesh &&mesh) :
        name(other.name), source(other.source), config(other.config), object(object), m_mesh(new TriangleMesh(std::move(mesh))), m_type(other.m_type), m_transformation(other.m_transformation),
        cut_info(other.cut_info), text_configuration(other.text_configuration), emboss_shape(other.emboss_shape)
    {
		assert(this->id().valid()); 
        assert(this->config.id().valid()); 
        assert(this->supported_facets.id().valid());
        assert(this->seam_facets.id().valid());
        assert(this->mmu_segmentation_facets.id().valid());
        assert(this->fuzzy_skin_facets.id().valid());
        assert(this->id() != this->config.id());
        assert(this->id() != this->supported_facets.id());
        assert(this->id() != this->seam_facets.id());
        assert(this->id() != this->mmu_segmentation_facets.id());
        assert(this->id() != this->fuzzy_skin_facets.id());
		assert(this->id() != other.id());
        assert(this->config.id() == other.config.id());
        this->set_material_id(other.material_id());
        this->config.set_new_unique_id();
        if (m_mesh->facets_count() > 1)
            calculate_convex_hull();
		assert(this->config.id().valid()); 
        assert(this->config.id() != other.config.id()); 
        assert(this->supported_facets.id() != other.supported_facets.id());
        assert(this->seam_facets.id() != other.seam_facets.id());
        assert(this->mmu_segmentation_facets.id() != other.mmu_segmentation_facets.id());
        assert(this->fuzzy_skin_facets.id() != other.fuzzy_skin_facets.id());
        assert(this->id() != this->config.id());
        assert(this->supported_facets.empty());
        assert(this->seam_facets.empty());
        assert(this->mmu_segmentation_facets.empty());
        assert(this->fuzzy_skin_facets.empty());
        assert(this->texture_displacement_facets_all_empty());
        assert(this->texture_displacement_layers.empty());
    }

    ModelVolume& operator=(ModelVolume &rhs) = delete;

	friend class cereal::access;
	friend class UndoRedo::StackImpl;
	ModelVolume() : ObjectBase(-1), config(-1), supported_facets(-1), seam_facets(-1), mmu_segmentation_facets(-1), fuzzy_skin_facets(-1), nonplanar_facets(-1),
		texture_displacement_facets_0(-1), texture_displacement_facets_1(-1), texture_displacement_facets_2(-1), texture_displacement_facets_3(-1),
		texture_displacement_facets_4(-1), texture_displacement_facets_5(-1), texture_displacement_facets_6(-1), texture_displacement_facets_7(-1),
		object(nullptr) {
		assert(this->id().invalid());
        assert(this->config.id().invalid());
        assert(this->supported_facets.id().invalid());
        assert(this->seam_facets.id().invalid());
        assert(this->mmu_segmentation_facets.id().invalid());
        assert(this->fuzzy_skin_facets.id().invalid());
        assert(this->texture_displacement_facets_ids_invalid());
	}
	template<class Archive> void load(Archive &ar) {
		bool has_convex_hull;
        bool mesh_changed = false;
        auto tr = m_transformation;
        ar(name, source, m_mesh, m_type, m_material_id, m_transformation, m_is_splittable, has_convex_hull, cut_info);
        mesh_changed |= !(tr == m_transformation);
        auto t = supported_facets.timestamp();
        cereal::load_by_value(ar, supported_facets);
        mesh_changed |= t != supported_facets.timestamp();
        t = seam_facets.timestamp();
        cereal::load_by_value(ar, seam_facets);
        mesh_changed |= t != seam_facets.timestamp();
        t = mmu_segmentation_facets.timestamp();
        cereal::load_by_value(ar, mmu_segmentation_facets);
        mesh_changed |= t != mmu_segmentation_facets.timestamp();
        t = fuzzy_skin_facets.timestamp();
        cereal::load_by_value(ar, fuzzy_skin_facets);
        mesh_changed |= t != fuzzy_skin_facets.timestamp();
        t = nonplanar_facets.timestamp();
        ar(nonplanar_facets);
        mesh_changed |= t != nonplanar_facets.timestamp();
        for (int i = 0; i < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++i) {
            FacetsAnnotation &f  = texture_displacement_facet(i);
            Timestamp         tf = f.timestamp();
            cereal::load_by_value(ar, f);
            mesh_changed |= tf != f.timestamp();
        }
        ar(texture_displacement_layers, texture_displacement_options);
        cereal::load_by_value(ar, config);
        cereal::load(ar, text_configuration);
        cereal::load(ar, emboss_shape);
		assert(m_mesh);
		if (has_convex_hull) {
			cereal::load_optional(ar, m_convex_hull);
			if (! m_convex_hull && ! m_mesh->empty())
				this->calculate_convex_hull();
		} else
			m_convex_hull.reset();
        if (mesh_changed && object)
            Slic3r::save_object_mesh(*object);
	}
	template<class Archive> void save(Archive &ar) const {
		bool has_convex_hull = m_convex_hull.get() != nullptr;
        ar(name, source, m_mesh, m_type, m_material_id, m_transformation, m_is_splittable, has_convex_hull, cut_info);
        cereal::save_by_value(ar, supported_facets);
        cereal::save_by_value(ar, seam_facets);
        cereal::save_by_value(ar, mmu_segmentation_facets);
        cereal::save_by_value(ar, fuzzy_skin_facets);
        ar(nonplanar_facets);
        for (int i = 0; i < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++i)
            cereal::save_by_value(ar, texture_displacement_facet(i));
        ar(texture_displacement_layers, texture_displacement_options);
        cereal::save_by_value(ar, config);
        cereal::save(ar, text_configuration);
        cereal::save(ar, emboss_shape);
		if (has_convex_hull)
			cereal::save_optional(ar, m_convex_hull);
	}
};

inline void model_volumes_sort_by_id(ModelVolumePtrs &model_volumes)
{
    std::sort(model_volumes.begin(), model_volumes.end(), [](const ModelVolume *l, const ModelVolume *r) { return l->id() < r->id(); });
}

inline const ModelVolume* model_volume_find_by_id(const ModelVolumePtrs &model_volumes, const ObjectID id)
{
    auto it = lower_bound_by_predicate(model_volumes.begin(), model_volumes.end(), [id](const ModelVolume *mv) { return mv->id() < id; });
    return it != model_volumes.end() && (*it)->id() == id ? *it : nullptr;
}

enum ModelInstanceEPrintVolumeState : unsigned char
{
    ModelInstancePVS_Inside,
    ModelInstancePVS_Limited,
    ModelInstancePVS_Partly_Outside,
    ModelInstancePVS_Fully_Outside,
    ModelInstanceNum_BedStates
};

class ModelInstance final : public ObjectBase
{
private:
    Geometry::Transformation m_transformation;
    Geometry::Transformation m_assemble_transformation;
    Vec3d m_offset_to_assembly{ 0.0, 0.0, 0.0 };
    bool m_assemble_initialized;

public:
    ModelInstanceEPrintVolumeState print_volume_state;
    bool printable;
    bool auto_drop;
    bool use_loaded_id_for_label {false};
    int arrange_order = 0;
    size_t loaded_id = 0;

    size_t get_labeled_id() const
    {
        if (use_loaded_id_for_label && (loaded_id > 0))
            return loaded_id;
        else
            return id().id;
    }

    ModelObject* get_object() const { return this->object; }

    const Geometry::Transformation& get_transformation() const { return m_transformation; }
    void set_transformation(const Geometry::Transformation& transformation) { m_transformation = transformation; }

    const Geometry::Transformation& get_assemble_transformation() const { return m_assemble_transformation; }
    void set_assemble_transformation(const Geometry::Transformation& transformation) {
        m_assemble_initialized = true;
        m_assemble_transformation = transformation;
    }
    void set_assemble_from_transform(const Transform3d& transform) {
        m_assemble_initialized = true;
        m_assemble_transformation.set_matrix(transform);
    }
    Vec3d get_assemble_offset() const {return m_assemble_transformation.get_offset(); }
    void set_assemble_offset(const Vec3d& offset) { m_assemble_transformation.set_offset(offset); }
    void set_assemble_rotation(const Vec3d &rotation) { m_assemble_transformation.set_rotation(rotation); }
    void rotate_assemble(double angle, const Vec3d& axis) {
        m_assemble_transformation.set_rotation(m_assemble_transformation.get_rotation() + Geometry::extract_euler_angles(Eigen::Quaterniond(Eigen::AngleAxisd(angle, axis)).toRotationMatrix()));
    }

    void set_offset_to_assembly(const Vec3d& offset) { m_offset_to_assembly = offset; }
    Vec3d get_offset_to_assembly() const { return m_offset_to_assembly; }

    Vec3d get_offset() const { return m_transformation.get_offset(); }
    double get_offset(Axis axis) const { return m_transformation.get_offset(axis); }

    void set_offset(const Vec3d& offset) { m_transformation.set_offset(offset); }
    void set_offset(Axis axis, double offset) { m_transformation.set_offset(axis, offset); }

    Vec3d get_rotation() const { return m_transformation.get_rotation(); }
    double get_rotation(Axis axis) const { return m_transformation.get_rotation(axis); }

    void set_rotation(const Vec3d& rotation) { m_transformation.set_rotation(rotation); }
    void set_rotation(Axis axis, double rotation) { m_transformation.set_rotation(axis, rotation); }

    void rotate(Matrix3d rotation_matrix) {
        auto rotation = m_transformation.get_rotation_matrix();
        rotation      = rotation_matrix * rotation;
        set_rotation(Geometry::Transformation(rotation).get_rotation());
    }

    Vec3d get_scaling_factor() const { return m_transformation.get_scaling_factor(); }
    double get_scaling_factor(Axis axis) const { return m_transformation.get_scaling_factor(axis); }

    void set_scaling_factor(const Vec3d& scaling_factor) { m_transformation.set_scaling_factor(scaling_factor); }
    void set_scaling_factor(Axis axis, double scaling_factor) { m_transformation.set_scaling_factor(axis, scaling_factor); }

    Vec3d get_mirror() const { return m_transformation.get_mirror(); }
    double get_mirror(Axis axis) const { return m_transformation.get_mirror(axis); }
    bool is_left_handed() const { return m_transformation.is_left_handed(); }

    void set_mirror(const Vec3d& mirror) { m_transformation.set_mirror(mirror); }
    void set_mirror(Axis axis, double mirror) { m_transformation.set_mirror(axis, mirror); }

    void transform_mesh(TriangleMesh* mesh, bool dont_translate = false) const;
    BoundingBoxf3 transform_bounding_box(const BoundingBoxf3 &bbox, bool dont_translate = false) const;
    Vec3d transform_vector(const Vec3d& v, bool dont_translate = false) const;
    void transform_polygon(Polygon* polygon) const;

    const Transform3d& get_matrix() const { return m_transformation.get_matrix(); }
    Transform3d get_matrix_no_offset() const { return m_transformation.get_matrix_no_offset(); }

    bool is_printable() const { return object->printable && printable && (print_volume_state == ModelInstancePVS_Inside); }
    bool is_assemble_initialized() { return m_assemble_initialized; }

    double get_auto_brim_width(double deltaT, double adhesion) const;
    double get_auto_brim_width() const;
    Polygon convex_hull_2d();
    void invalidate_convex_hull_2d();

    void get_arrange_polygon(void *arrange_polygon, const Slic3r::DynamicPrintConfig &config = Slic3r::DynamicPrintConfig()) const;

    void apply_arrange_result(const Vec2d& offs, double rotation)
    {
        set_rotation(Z, rotation);
        set_offset(X, unscale<double>(offs(X)));
        set_offset(Y, unscale<double>(offs(Y)));
        this->object->invalidate_bounding_box();
    }

    ModelInstanceEPrintVolumeState calc_print_volume_state(const BuildVolume& build_volume) const;

protected:
    friend class Print;
    friend class SLAPrint;
    friend class Model;
    friend class ModelObject;

    explicit ModelInstance(const ModelInstance &rhs) = default;
    void     set_model_object(ModelObject *model_object) { object = model_object; }

    ModelInstanceEPrintVolumeState update_print_volume_state(const BuildVolume& build_volume)
    {
        print_volume_state = calc_print_volume_state(build_volume);
        return print_volume_state;
    }

private:
    ModelObject* object;
    Polygon convex_hull;
    explicit ModelInstance(ModelObject* object)
        : print_volume_state(ModelInstancePVS_Inside), printable(true), auto_drop(true), object(object), m_assemble_initialized(false)
    {
        assert(this->id().valid());
    }
    explicit ModelInstance(ModelObject *object, const ModelInstance &other) :
        m_transformation(other.m_transformation)
        , m_assemble_transformation(other.m_assemble_transformation)
        , m_offset_to_assembly(other.m_offset_to_assembly)
        , print_volume_state(ModelInstancePVS_Inside)
        , printable(other.printable)
        , auto_drop(other.auto_drop)
        , object(object)
        , m_assemble_initialized(false) { assert(this->id().valid() && this->id() != other.id()); }

    explicit ModelInstance(ModelInstance &&rhs) = delete;
    ModelInstance& operator=(const ModelInstance &rhs) = delete;
    ModelInstance& operator=(ModelInstance &&rhs) = delete;

	friend class cereal::access;
	friend class UndoRedo::StackImpl;
	ModelInstance() : ObjectBase(-1), object(nullptr) { assert(this->id().invalid()); }
    template<class Archive> void serialize(Archive& ar) {
        ar(m_transformation, print_volume_state, printable, auto_drop, m_assemble_transformation, m_offset_to_assembly, m_assemble_initialized);
    }
};


class ModelWipeTower final : public ObjectBase
{
public:
	std::vector<Vec2d>      positions;
	double 	                rotation;

private:
	friend class cereal::access;
	friend class UndoRedo::StackImpl;
	friend class Model;

    explicit ModelWipeTower() {}
    explicit ModelWipeTower(int) : ObjectBase(-1) {}
	explicit ModelWipeTower(const ModelWipeTower &cfg) = default;

	ModelWipeTower(ModelWipeTower &&rhs) = delete;
	ModelWipeTower& operator=(const ModelWipeTower &rhs) = delete;
    ModelWipeTower& operator=(ModelWipeTower &&rhs) = delete;

    template<typename Archive> void serialize(Archive &ar) { ar(positions, rotation); }
};

struct ExtruderParams
{
    std::string materialName;
    int bedTemp;
    double heatEndTemp;
};

struct GlobalSpeedMap
{
    double perimeterSpeed;
    double externalPerimeterSpeed;
    double infillSpeed;
    double solidInfillSpeed;
    double topSolidInfillSpeed;
    double supportSpeed;
    double smallPerimeterSpeed;
    double smallSupportPerimeterSpeed;
    double maxSpeed;
    Polygon bed_poly;
};

class ModelProfileInfo
{
public:
    std::string ProfileTile;
    std::string ProfileCover;
    std::string ProfileDescription;
    std::string ProfileUserId;
    std::string ProfileUserName;
};

class ModelDesignInfo
{
public:
    std::string DesignId;
    std::string Designer;
    std::string DesignerUserId;
};

class ModelInfo
{
public:
    std::string cover_file;
    std::string license;
    std::string description;
    std::string copyright;
    std::string model_name;
    std::string origin;

    std::map<std::string, std::string> metadata_items;

    void load(ModelInfo &info) {
        this->cover_file    = info.cover_file;
        this->license       = info.license;
        this->description   = info.description;
        this->copyright     = info.copyright;
        this->model_name    = info.model_name;
        this->origin        = info.origin;
        this->metadata_items = info.metadata_items;
    }
};

class Model final : public ObjectBase
{
public:
    ModelMaterialMap    materials;
    ModelObjectPtrs     objects;
    ModelWipeTower	wipe_tower;
    static std::map<size_t, ExtruderParams> extruderParamsMap;
    static GlobalSpeedMap printSpeedMap;

    std::string stl_design_id;
    std::string stl_design_country;
    std::shared_ptr<ModelDesignInfo> design_info = nullptr;
    std::shared_ptr<ModelInfo> model_info = nullptr;
    std::shared_ptr<ModelProfileInfo> profile_info = nullptr;

    std::shared_ptr<TexturedMesh> texture_mesh;

    std::string mk_name;
    std::string mk_version;
    std::vector<std::string> md_name;
    std::vector<std::string> md_value;

    std::string cad_recipe;

    void SetDesigner(std::string designer, std::string designer_user_id) {
        if (design_info == nullptr) {
            design_info = std::make_shared<ModelDesignInfo>();
        }
        design_info->Designer = designer;
        design_info->DesignerUserId = designer_user_id;
    }

    int curr_plate_index{ 0 };
    std::map<int, CustomGCode::Info> plates_custom_gcodes;

    const CustomGCode::Info get_curr_plate_custom_gcodes() const {
        if (plates_custom_gcodes.find(curr_plate_index) != plates_custom_gcodes.end()) {
            return plates_custom_gcodes.at(curr_plate_index);
        }
        return CustomGCode::Info();
    }

    Model() { assert(this->id().valid()); }
    ~Model();

    Model(const Model &rhs) : ObjectBase(-1) { assert(this->id().invalid()); this->assign_copy(rhs); assert(this->id().valid()); assert(this->id() == rhs.id()); }
    Model(Model &&rhs) : ObjectBase(-1) { assert(this->id().invalid()); this->assign_copy(std::move(rhs)); assert(this->id().valid()); assert(this->id() == rhs.id()); }
    Model& operator=(const Model &rhs) { this->assign_copy(rhs); assert(this->id().valid()); assert(this->id() == rhs.id()); return *this; }
    Model& operator=(Model &&rhs) { this->assign_copy(std::move(rhs)); assert(this->id().valid()); assert(this->id() == rhs.id()); return *this; }

    OBJECTBASE_DERIVED_COPY_MOVE_CLONE(Model)

    static Model read_from_step(const std::string&                                      input_file,
                                LoadStrategy                                            options,
                                ImportStepProgressFn                                    stepFn,
                                StepIsUtf8Fn                                            stepIsUtf8Fn,
                                std::function<int(Slic3r::Step&, double&, double&, bool&)>     step_mesh_fn,
                                double                                                  linear_deflection,
                                double                                                  angle_deflection,
                                bool                                                    is_split_compound);

    static Model read_from_file(
        const std::string& input_file,
        DynamicPrintConfig* config = nullptr, ConfigSubstitutionContext* config_substitutions = nullptr,
        LoadStrategy options = LoadStrategy::AddDefaultInstances, PlateDataPtrs* plate_data = nullptr,
        std::vector<Preset*>* project_presets = nullptr, bool* is_xxx = nullptr, Semver* file_version = nullptr, Import3mfProgressFn proFn = nullptr,
                                ImportstlProgressFn        stlFn                = nullptr,
                                BBLProject *               project              = nullptr,
                                int                        plate_id             = 0
                                );
    static bool    obj_import_vertex_color_deal(const std::vector<unsigned char> &vertex_filament_ids, const unsigned char &first_extruder_id, Model *model);
    static bool    obj_import_face_color_deal(const std::vector<unsigned char> &face_filament_ids, const unsigned char &first_extruder_id, Model *model);
    static double findMaxSpeed(const ModelObject* object);
    static double getThermalLength(const ModelVolume* modelVolumePtr);
    static double getThermalLength(const std::vector<ModelVolume*> modelVolumePtrs);
    static Polygon getBedPolygon() { return Model::printSpeedMap.bed_poly; }
    static void setPrintSpeedTable(const DynamicPrintConfig& config, const PrintConfig& print_config);
    static void setBedPolygon(const DynamicPrintConfig& config, const PrintConfig& print_config);
    static void setExtruderParams(const DynamicPrintConfig& config, int extruders_count);

    static Model read_from_archive(
        const std::string& input_file,
        DynamicPrintConfig* config, ConfigSubstitutionContext* config_substitutions, En3mfType& out_file_type,
        LoadStrategy options = LoadStrategy::AddDefaultInstances, PlateDataPtrs* plate_data = nullptr, std::vector<Preset*>* project_presets = nullptr, Semver* file_version = nullptr, Import3mfProgressFn proFn = nullptr, BBLProject* project = nullptr);

    ModelObject* add_object();
    ModelObject* add_object(const char *name, const char *path, const TriangleMesh &mesh);
    ModelObject* add_object(const char *name, const char *path, TriangleMesh &&mesh);
    ModelObject* add_object(const ModelObject &other);
    void         delete_object(size_t idx);
    bool         delete_object(ObjectID id);
    bool         delete_object(ModelObject* object);
    void         clear_objects();
    void         collect_reusable_objects(std::vector<ObjectBase *> & objects);
    void         set_object_backup_id(ModelObject const & object, int uuid);
    int          get_object_backup_id(ModelObject const & object);
    int          get_object_backup_id(ModelObject const & object) const;

    ModelMaterial* add_material(t_model_material_id material_id);
    ModelMaterial* add_material(t_model_material_id material_id, const ModelMaterial &other);
    ModelMaterial* get_material(t_model_material_id material_id) {
        ModelMaterialMap::iterator i = this->materials.find(material_id);
        return (i == this->materials.end()) ? nullptr : i->second;
    }

    void          delete_material(t_model_material_id material_id);
    void          clear_materials();
    bool          add_default_instances();
    BoundingBoxf3 bounding_box_approx() const;
    BoundingBoxf3 bounding_box_exact() const;
    double        max_z() const;
    unsigned int  update_print_volume_state(const BuildVolume &build_volume);
    bool 		  center_instances_around_point(const Vec2d &point);
    void 		  translate(coordf_t x, coordf_t y, coordf_t z) { for (ModelObject *o : this->objects) o->translate(x, y, z); }
    TriangleMesh  mesh() const;

    void duplicate_objects_grid(size_t x, size_t y, coordf_t dist);

    bool 		  looks_like_multipart_object() const;
    void 		  convert_multipart_object(unsigned int max_extruders);
    bool          looks_like_imperial_units() const;
    void          convert_from_imperial_units(bool only_small_volumes);
    bool          looks_like_saved_in_meters() const;
    void          convert_from_meters(bool only_small_volumes);
    int           removed_objects_with_zero_volume();

    void 		  adjust_min_z();

    void 		  print_info() const { for (const ModelObject *o : this->objects) o->print_info(); }

    std::string   propose_export_file_name_and_path() const;
    std::string   propose_export_file_name_and_path(const std::string &new_extension) const;
    std::string   get_auxiliary_file_temp_path();

    std::string   get_backup_path();
    std::string   get_backup_path(const std::string &sub_path);
    void          set_backup_path(const std::string &path);
    void          load_from(Model & model);
    bool          is_need_backup() { return need_backup;  }
    void          set_need_backup();
    void          remove_backup_path_if_exist();

    bool          is_fdm_support_painted() const;
    bool          is_seam_painted() const;
    bool          is_mm_painted() const;
    bool          is_fuzzy_skin_painted() const;

    std::unique_ptr<CalibPressureAdvancePattern> calib_pa_pattern;

private:
    explicit Model(int) : ObjectBase(-1)
        {
        assert(this->id().invalid());
    }
	void assign_new_unique_ids_recursive();
	void update_links_bottom_up_recursive();

	friend class cereal::access;
	friend class UndoRedo::StackImpl;
    template<class Archive> void load(Archive& ar) {
        Internal::StaticSerializationWrapper<ModelWipeTower> wipe_tower_wrapper(wipe_tower);
        ar(materials, objects, wipe_tower_wrapper);
    }
    template<class Archive> void save(Archive& ar) const {
        Internal::StaticSerializationWrapper<ModelWipeTower const> wipe_tower_wrapper(wipe_tower);
        ar(materials, objects, wipe_tower_wrapper);
    }

    std::string backup_path;
    bool need_backup = false;
    std::map<int, int> object_backup_id_map;
    int next_object_backup_id = 1;
};

#undef OBJECTBASE_DERIVED_COPY_MOVE_CLONE
#undef OBJECTBASE_DERIVED_PRIVATE_COPY_MOVE

bool model_object_list_equal(const Model &model_old, const Model &model_new);

bool model_object_list_extended(const Model &model_old, const Model &model_new);

bool model_volume_list_changed(const ModelObject &model_object_old, const ModelObject &model_object_new, const ModelVolumeType type);
bool model_volume_list_changed(const ModelObject &model_object_old, const ModelObject &model_object_new, const std::initializer_list<ModelVolumeType> &types);

bool model_custom_supports_data_changed(const ModelObject& mo, const ModelObject& mo_new);

bool model_custom_seam_data_changed(const ModelObject& mo, const ModelObject& mo_new);

extern bool model_mmu_segmentation_data_changed(const ModelObject& mo, const ModelObject& mo_new);

extern bool model_nonplanar_data_changed(const ModelObject &model_object1, const ModelObject &model_object2);
bool model_fuzzy_skin_data_changed(const ModelObject &mo, const ModelObject &mo_new);

bool model_brim_points_data_changed(const ModelObject& mo, const ModelObject& mo_new);

bool model_has_multi_part_objects(const Model &model);
bool model_has_advanced_features(const Model &model);

void remap_model_filament_slots(Model &model, const std::map<int, int> &slot_relocations);

#ifndef NDEBUG
void check_model_ids_validity(const Model &model);
void check_model_ids_equal(const Model &model1, const Model &model2);
#endif

static const float SINKING_Z_THRESHOLD = -0.001f;
static const double SINKING_MIN_Z_THRESHOLD = 0.05;

}

namespace cereal
{
    template <class Archive> struct specialize<Archive, Slic3r::ModelVolume, cereal::specialization::member_load_save> {};
    template <class Archive> struct specialize<Archive, Slic3r::Model, cereal::specialization::member_load_save> {};
    template <class Archive> struct specialize<Archive, Slic3r::ModelObject, cereal::specialization::member_load_save> {};
    template <class Archive> struct specialize<Archive, Slic3r::ModelConfigObject, cereal::specialization::member_serialize> {};
}

#endif
