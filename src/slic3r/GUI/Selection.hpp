#ifndef slic3r_GUI_Selection_hpp_
#define slic3r_GUI_Selection_hpp_

#include "libslic3r/Geometry.hpp"
#include "GUI_Geometry.hpp"
#include "GLModel.hpp"

#include "libslic3r/Technologies.hpp"
#include "libslic3r/Point.hpp"
#include <map>
#include <memory>
#include "libslic3r/BoundingBox.hpp"
#include <array>
#include <cstddef>
#include "libslic3r/libslic3r.h"
#include "libslic3r/Color.hpp"
#include <set>
#include <optional>
#include <vector>
#include <utility>
#include <string>

namespace Slic3r {

class Shader;
class Model;
class ModelObject;
class ModelVolume;
class ObjectID;
class ModelInstance;
class GLVolume;
class GLArrow;
class GLCurvedArrow;
class DynamicPrintConfig;
class GLShaderProgram;
#if ENABLE_ENHANCED_PRINT_VOLUME_FIT
class BuildVolume;
#endif

using GLVolumePtrs = std::vector<GLVolume*>;
using ModelObjectPtrs = std::vector<ModelObject*>;


namespace GUI {

class Selection
{
public:
    typedef std::set<unsigned int> IndicesList;

    enum EMode : unsigned char
    {
        Volume,
        Instance
    };

    enum EType : unsigned char
    {
        Invalid,
        Empty,
        WipeTower,
        SingleModifier,
        MultipleModifier,
        SingleVolume,
        MultipleVolume,
        SingleFullObject,
        MultipleFullObject,
        SingleFullInstance,
        MultipleFullInstance,
        Mixed
    };

private:
    struct VolumeCache
    {
    private:
        struct TransformCache
        {
            Vec3d position;
            Transform3d rotation_matrix;
            Transform3d scale_matrix;
            Transform3d mirror_matrix;

            Geometry::Transformation transform;

            TransformCache();
            explicit TransformCache(const Geometry::Transformation& transform);
        };

        TransformCache m_volume;
        TransformCache m_instance;

    public:
        VolumeCache() = default;
        VolumeCache(const Geometry::Transformation& volume_transform, const Geometry::Transformation& instance_transform);

        const Vec3d& get_volume_position() const { return m_volume.position; }
        const Transform3d& get_volume_scale_matrix() const { return m_volume.scale_matrix; }
        const Geometry::Transformation& get_volume_transform() const { return m_volume.transform; }

        const Vec3d& get_instance_position() const { return m_instance.position; }
        const Transform3d& get_instance_rotation_matrix() const { return m_instance.rotation_matrix; }
        const Transform3d& get_instance_scale_matrix() const { return m_instance.scale_matrix; }
        const Transform3d& get_instance_mirror_matrix() const { return m_instance.mirror_matrix; }
        const Geometry::Transformation &get_instance_transform() const { return m_instance.transform; }
    };

public:
    typedef std::map<unsigned int, VolumeCache> VolumesCache;
    typedef std::set<int> InstanceIdxsList;
    typedef std::map<int, InstanceIdxsList> ObjectIdxsToInstanceIdxsMap;

    class Clipboard
    {
        std::unique_ptr<Model> m_model;

        Selection::EMode m_mode;

    public:
        Clipboard();

        void reset();
        bool is_empty() const;

        bool is_sla_compliant() const;

        ModelObject* add_object();
        ModelObject* get_object(unsigned int id);
        const ModelObjectPtrs& get_objects() const;

        Selection::EMode get_mode() const { return m_mode; }
        void set_mode(Selection::EMode mode) { m_mode = mode; }
    };

private:
    struct Cache
    {
        VolumesCache volumes_data;
        Vec3d dragging_center;
        ObjectIdxsToInstanceIdxsMap content;
        std::vector<unsigned int> sinking_volumes;
        Vec3d rotation_pivot;
    };

    GLVolumePtrs* m_volumes;
    Model* m_model;

    bool m_enabled;
    bool m_valid;
    EMode m_mode;
    EType m_type;
    IndicesList m_list;
    Cache m_cache;
    Clipboard m_clipboard;
    std::optional<BoundingBoxf3> m_bounding_box;
    std::optional<BoundingBoxf3> m_unscaled_instance_bounding_box;
    std::optional<BoundingBoxf3> m_scaled_instance_bounding_box;
    std::optional<BoundingBoxf3> m_full_unscaled_instance_bounding_box;
    std::optional<BoundingBoxf3> m_full_scaled_instance_bounding_box;
    std::optional<BoundingBoxf3> m_full_unscaled_instance_local_bounding_box;
    std::optional<std::pair<BoundingBoxf3, Transform3d>> m_bounding_box_in_current_reference_system;

    std::optional<std::pair<Vec3d, double>> m_bounding_sphere;

#if ENABLE_RENDER_SELECTION_CENTER
    GLModel m_vbo_sphere;
#endif

    GLModel m_arrow;
    GLModel m_curved_arrow;
    GLModel m_box;
    bool m_auto_drop = true;
    struct Planes
    {
        std::array<Vec3f, 2> check_points{ Vec3f::Zero(), Vec3f::Zero() };
        std::array<GLModel, 2> models;
    };
    Planes m_planes;

    float m_scale_factor;

    EMode m_volume_selection_mode{ Instance };
    bool m_volume_selection_locked { false };

public:
    Selection();

    void set_volumes(GLVolumePtrs* volumes);
    bool init();

    bool is_enabled() const { return m_enabled; }
    void set_enabled(bool enable) { m_enabled = enable; }

    Model* get_model() const { return m_model; }
    void set_model(Model* model);

    EMode get_mode() const { return m_mode; }
    void set_mode(EMode mode) { m_mode = mode; }

    int query_real_volume_idx_from_other_view(unsigned int object_idx, unsigned int instance_idx, unsigned int model_volume_idx);
    void add(unsigned int volume_idx, bool as_single_selection = true, bool check_for_already_contained = false);
    void remove(unsigned int volume_idx);

    void add_object(unsigned int object_idx, bool as_single_selection = true);
    void remove_object(unsigned int object_idx);

    void add_instance(unsigned int object_idx, unsigned int instance_idx, bool as_single_selection = true);
    void remove_instance(unsigned int object_idx, unsigned int instance_idx);

    void add_volume(unsigned int object_idx, unsigned int volume_idx, int instance_idx, bool as_single_selection = true);
    void remove_volume(unsigned int object_idx, unsigned int volume_idx);

    void add_volumes(EMode mode, const std::vector<unsigned int>& volume_idxs, bool as_single_selection = true);
    void remove_volumes(EMode mode, const std::vector<unsigned int>& volume_idxs);

    ModelVolume *                   get_selected_single_volume(int &out_object_idx, int &out_volume_idx) const;
    ModelObject *                   get_selected_single_object(int &out_object_idx) const;
    const ModelInstance *           get_selected_single_intance() const;
    void add_curr_plate();
    void add_object_from_idx(std::vector<int>& object_idxs);
    void remove_curr_plate();
    void clone(int numbers = 1);
    void center();
    void drop();
    void center_plate(const int plate_idx);
    void set_printable(bool printable);
    bool get_auto_drop() const;
    void set_auto_drop(bool enabled);

    void add_all();
    void remove_all();

    void set_deserialized(EMode mode, const std::vector<std::pair<size_t, size_t>> &volumes_and_instances);

	void instances_changed(const std::vector<size_t> &instance_ids_selected);
    void volumes_changed(const std::vector<size_t> &map_volume_old_to_new);
    void clear();

    bool is_empty() const { return m_type == Empty; }
    bool is_wipe_tower() const { return m_type == WipeTower; }
    bool is_any_modifier() const { return is_single_modifier() || is_multiple_modifier(); }
    bool is_single_modifier() const { return m_type == SingleModifier; }
    bool is_multiple_modifier() const { return m_type == MultipleModifier; }
    bool is_single_full_instance() const;
    bool is_multiple_full_instance() const { return m_type == MultipleFullInstance; }
    bool is_single_full_object() const { return m_type == SingleFullObject; }
    bool is_multiple_full_object() const { return m_type == MultipleFullObject; }
    bool is_single_volume() const { return m_type == SingleVolume; }
    bool is_multiple_volume() const { return m_type == MultipleVolume; }
    bool is_any_volume() const { return is_single_volume() || is_multiple_volume(); }
    bool is_any_connector() const;
    bool is_any_cut_volume() const;
    bool is_mixed() const { return m_type == Mixed; }
    bool is_from_single_instance() const { return get_instance_idx() != -1; }
    bool is_from_single_object() const;
    bool is_sla_compliant() const;
    bool is_instance_mode() const { return m_mode == Instance; }
    bool is_single_volume_or_modifier() const { return is_single_volume() || is_single_modifier(); }
    bool is_single_volume_instance() const { return is_single_full_instance() && m_list.size() == 1; }
    bool is_single_text() const;

    bool contains_volume(unsigned int volume_idx) const { return m_list.find(volume_idx) != m_list.end(); }
    bool contains_all_volumes(const std::vector<unsigned int>& volume_idxs) const;
    bool contains_any_volume(const std::vector<unsigned int>& volume_idxs) const;
    bool contains_sinking_volumes(bool ignore_modifiers = true) const;
    bool matches(const std::vector<unsigned int>& volume_idxs) const;

    bool requires_uniform_scale() const;

    int get_object_idx() const;
    int get_instance_idx() const;
    const InstanceIdxsList& get_instance_idxs() const;

    const IndicesList& get_volume_idxs() const { return m_list; }
    const GLVolume* get_volume(unsigned int volume_idx) const;
    const GLVolume* get_first_volume() const { return get_volume(*m_list.begin()); }
    GLVolume* get_volume(unsigned int volume_idx);

    const ObjectIdxsToInstanceIdxsMap& get_content() const { return m_cache.content; }

    unsigned int volumes_count() const { return (unsigned int)m_list.size(); }
    const BoundingBoxf3& get_bounding_box() const;
    const BoundingBoxf3& get_unscaled_instance_bounding_box() const;
    const BoundingBoxf3& get_scaled_instance_bounding_box() const;
    const BoundingBoxf3& get_full_unscaled_instance_bounding_box() const;
    const BoundingBoxf3& get_full_scaled_instance_bounding_box() const;
    const BoundingBoxf3& get_full_unscaled_instance_local_bounding_box() const;
    const std::pair<BoundingBoxf3, Transform3d>& get_bounding_box_in_current_reference_system() const;
    std::pair<BoundingBoxf3, Transform3d> get_bounding_box_in_reference_system(ECoordinatesType type) const;

    const std::pair<Vec3d, double> get_bounding_sphere() const;

    void setup_cache();
    void translate(const Vec3d& displacement, TransformationType transformation_type);
    void move_to_center(const Vec3d& displacement, bool local = false);
    void rotate(const Vec3d& rotation, TransformationType transformation_type);
    void flattening_rotate(const Vec3d& normal);
    void scale(const Vec3d& scale, TransformationType transformation_type);
#if ENABLE_ENHANCED_PRINT_VOLUME_FIT
    void scale_to_fit_print_volume(const BuildVolume& volume);
#else
    void scale_to_fit_print_volume(const DynamicPrintConfig& config);
#endif
    void scale_and_translate(const Vec3d &scale, const Vec3d &world_translation, TransformationType transformation_type);
    void mirror(Axis axis, TransformationType transformation_type);

    void translate(unsigned int object_idx, const Vec3d& displacement);
    void translate(unsigned int object_idx, unsigned int instance_idx, const Vec3d& displacement);
    void translate(unsigned int object_idx, unsigned int instance_idx, unsigned int volume_idx, const Vec3d &displacement);

    void rotate(unsigned int object_idx, unsigned int instance_idx, const Transform3d &overwrite_tran);
    void rotate(unsigned int object_idx, unsigned int instance_idx, unsigned int volume_idx, const Transform3d &overwrite_tran);
    void notify_instance_update(int object_idx, int instance_idx);
    EMode get_volume_selection_mode(){ return m_volume_selection_mode;}
    void set_volume_selection_mode(EMode mode) { if (!m_volume_selection_locked) m_volume_selection_mode = mode; }
    void lock_volume_selection_mode() { m_volume_selection_locked = true; }
    void unlock_volume_selection_mode() { m_volume_selection_locked = false; }

    void erase();

    void render(float scale_factor = 1.0);
#if ENABLE_RENDER_SELECTION_CENTER
    void render_center(bool gizmo_is_dragging);
#endif
    void render_sidebar_hints(const std::string& sidebar_field, bool uniform_scale);

    bool requires_local_axes() const;

    void render_bounding_box(const BoundingBoxf3& box, const ColorRGB& color, float scale) {
        m_scale_factor = scale;
        render_bounding_box(box, Transform3d::Identity(), get_auto_drop(), color);
    }

    void cut_to_clipboard();
    void copy_to_clipboard();
    void paste_from_clipboard();
    std::set<std::pair<int, int>> get_selected_object_instances();

    const Clipboard& get_clipboard() const { return m_clipboard; }

    void fill_color(int  extruder_id);

    std::vector<unsigned int> get_volume_idxs_from_object(unsigned int object_idx) const;
    std::vector<unsigned int> get_volume_idxs_from_instance(unsigned int object_idx, unsigned int instance_idx) const;
    std::vector<unsigned int> get_volume_idxs_from_volume(unsigned int object_idx, unsigned int instance_idx, unsigned int volume_idx) const;
    std::vector<unsigned int> get_missing_volume_idxs_from(const std::vector<unsigned int>& volume_idxs) const;
    std::vector<unsigned int> get_unselected_volume_idxs_from(const std::vector<unsigned int>& volume_idxs) const;

private:
    void update_valid();
    void update_type();
    void set_caches();
    void do_add_volume(unsigned int volume_idx);
    void do_add_volumes(const std::vector<unsigned int>& volume_idxs);
    void do_remove_volume(unsigned int volume_idx);
    void do_remove_instance(unsigned int object_idx, unsigned int instance_idx);
    void do_remove_object(unsigned int object_idx);
    void set_bounding_boxes_dirty() {
        m_bounding_box.reset();
        m_unscaled_instance_bounding_box.reset(); m_scaled_instance_bounding_box.reset();
        m_full_unscaled_instance_bounding_box.reset();
        m_full_scaled_instance_bounding_box.reset();
        m_full_unscaled_instance_local_bounding_box.reset();
        m_bounding_box_in_current_reference_system.reset();
        m_bounding_sphere.reset();
    }
    void render_synchronized_volumes();
    void render_bounding_box(const BoundingBoxf3& box, const Transform3d& trafo, const bool auto_drop, const ColorRGB& color);
    void render_sidebar_position_hints(const std::string& sidebar_field, GLShaderProgram& shader, const Transform3d& matrix);
    void render_sidebar_rotation_hints(const std::string& sidebar_field, GLShaderProgram& shader, const Transform3d& matrix);
    void render_sidebar_scale_hints(const std::string& sidebar_field, bool gizmo_uniform_scale, GLShaderProgram& shader, const Transform3d& matrix);
    void render_sidebar_layers_hints(const std::string& sidebar_field, GLShaderProgram& shader);

public:
    enum class SyncRotationType {
        NONE = 0,
        GENERAL = 1,
        RESET = 2
    };
    void synchronize_unselected_instances(SyncRotationType sync_rotation_type);
    void synchronize_unselected_volumes();

private:
    void ensure_on_bed();
    void ensure_not_below_bed();
    bool is_from_fully_selected_instance(unsigned int volume_idx) const;

    void paste_volumes_from_clipboard();
    void paste_objects_from_clipboard();

    void transform_instance_relative(GLVolume& volume, const VolumeCache& volume_data, TransformationType transformation_type,
        const Transform3d& transform, const Vec3d& world_pivot);
    void transform_volume_relative(GLVolume& volume, const VolumeCache& volume_data, TransformationType transformation_type,
        const Transform3d& transform, const Vec3d& world_pivot);
};

ModelVolume    *get_selected_volume   (const Selection &selection);
const GLVolume *get_selected_gl_volume(const Selection &selection);

ModelVolume    *get_selected_volume   (const ObjectID &volume_id, const Selection &selection);
ModelVolume    *get_volume            (const ObjectID &volume_id, const Selection &selection);

}
}

#endif
