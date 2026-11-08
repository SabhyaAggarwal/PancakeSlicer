#ifndef slic3r_GLGizmoBrimEars_hpp_
#define slic3r_GLGizmoBrimEars_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLSelectionRectangle.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "libslic3r/BrimEarsPoint.hpp"
#include "libslic3r/ObjectID.hpp"
#include "slic3r/GUI/SceneRaycaster.hpp"
#include "libslic3r/Point.hpp"
#include <utility>
#include <string>
#include <wx/event.h>
#include <vector>
#include <map>
#include "slic3r/GUI/3DScene.hpp"
#include <memory>
#include <optional>
#include "slic3r/GUI/MeshUtils.hpp"
#include <wx/string.h>
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Polygon.hpp"
#include <cereal/archives/binary.hpp>
#include "libslic3r/Geometry.hpp"


namespace Slic3r {

class ConfigOption;

namespace GUI {

enum class SLAGizmoEventType : unsigned char;

class GLGizmoBrimEars : public GLGizmoBase
{
private:
    using PickRaycaster = SceneRaycasterItem;

    bool unproject_on_mesh(const Vec2d& mouse_pos, std::pair<Vec3f, Vec3f>& pos_and_normal);
    bool unproject_on_mesh2(const Vec2d& mouse_pos, std::pair<Vec3f, Vec3f>& pos_and_normal);

    const float RenderPointScale = 1.f;

    class CacheEntry {
    public:
        CacheEntry() :
            brim_point(BrimPoint()),
            selected(false),
            normal(Vec3f(0, 0, 1)),
            is_hover(false),
            is_error(false)
        {}

        CacheEntry(const BrimPoint &point, bool sel = false, const Vec3f &norm = Vec3f(0, 0, 1), bool hover = false, bool error = false)
            : brim_point(point), selected(sel), normal(norm), is_hover(hover), is_error(error)
        {}

        bool operator==(const CacheEntry& rhs) const {
            return (brim_point == rhs.brim_point);
        }

        bool operator!=(const CacheEntry& rhs) const {
            return ! ((*this) == rhs);
        }

        inline bool pos_is_zero() {
            return brim_point.pos.isZero();
        }

        void set_empty() {
            brim_point = BrimPoint();
            selected = false;
            normal.setZero();
            is_hover = false;
            is_error = false;
        }

        BrimPoint brim_point;
        bool selected;
        bool is_hover;
        bool is_error;
        Vec3f normal;

        template<class Archive>
        void serialize(Archive & ar)
        {
            ar(brim_point, selected, normal);
        }
    };

public:
    GLGizmoBrimEars(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    virtual ~GLGizmoBrimEars() = default;
    void data_changed(bool is_serializing) override;
    void set_brim_data();
    bool on_mouse(const wxMouseEvent& mouse_event) override;
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);
    void delete_selected_points();
    bool has_selected_points() const;
    void update_model_object();

    bool is_selection_rectangle_dragging() const override { return m_selection_rectangle.is_dragging(); }

    bool wants_enter_leave_snapshots() const override { return true; }
    std::string get_gizmo_entering_text() const override { return _u8L("Entering Brim Ears"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Brim Ears"); }

private:
    bool on_init() override;
    void on_dragging(const UpdateData& data) override;
    void on_render() override;

    void render_points(const Selection& selection);

    float m_new_point_head_radius;
    float m_max_angle = 125.f;
    float m_detection_radius = 1.f;
    double m_detection_radius_max = .0f;
    CacheEntry m_point_before_drag;
    float m_old_point_head_radius = 0.;
    mutable std::vector<CacheEntry> m_editing_cache;
    std::map<int, CacheEntry> m_single_brim;
    ObjectID m_old_mo_id;
    const Vec3d m_world_normal = {0, 0, 1};
    std::map<GLVolume*, std::shared_ptr<PickRaycaster>>   m_mesh_raycaster_map;
    GLVolume* m_last_hit_volume;
    std::optional<CacheEntry> render_hover_point;

    bool m_link_text_hover = false;
    
    PickingModel m_cylinder;

    std::map<std::string, wxString> m_desc;
    std::vector<std::pair<wxString, wxString>> m_shortcuts;


    GLSelectionRectangle m_selection_rectangle;

    ExPolygons m_first_layer;

    bool m_wait_for_up_event = false;
    bool m_selection_empty = true;
    EState m_old_state = Off;

    std::vector<const ConfigOption*> get_config_options(const std::vector<std::string>& keys) const;
    bool is_mesh_point_clipped(const Vec3d& point) const;

    enum {
        AllPoints = -2,
        NoPoints,
    };
    void select_point(int i);
    void unselect_point(int i);
    void reload_cache();
    Points generate_points(Polygon &obj_polygon, float ear_detection_length, float brim_ears_max_angle, bool is_outer);
    void auto_generate();
    void first_layer_slicer();
    void get_detection_radius_max();
    void update_raycasters();

    void begin_radius_change(float initial_value);
    void update_cache_radius();
    void apply_radius_change();

protected:
    void on_set_state() override;
    void on_set_hover_id() override

    {
        if ((int)m_editing_cache.size() <= m_hover_id)
            m_hover_id = -1;
    }
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_render_input_window(float x, float y, float bottom_limit) override;

    std::string on_get_name() const override;
    bool on_is_activable() const override;
    bool render_follows_cursor() const override { return render_hover_point.has_value(); }
    virtual CommonGizmosDataID on_get_requirements() const override;
    void on_load(cereal::BinaryInputArchive& ar) override;
    void on_save(cereal::BinaryOutputArchive& ar) const override;
    virtual void on_register_raycasters_for_picking() override;
    virtual void on_unregister_raycasters_for_picking() override;
    void register_single_mesh_pick();
    void reset_all_pick();
    bool add_point_to_cache(Vec3f pos, float head_radius, bool selected, Vec3f normal);
    float get_brim_default_radius() const;
    ExPolygon make_polygon(BrimPoint point, const Geometry::Transformation &trsf);
    void find_single();
};

wxDECLARE_EVENT(wxEVT_THREAD_DONE, wxCommandEvent);

}
}

#endif
