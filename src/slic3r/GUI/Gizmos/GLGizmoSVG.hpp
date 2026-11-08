#ifndef slic3r_GLGizmoSVG_hpp_
#define slic3r_GLGizmoSVG_hpp_

#include "GLGizmoBase.hpp"
#include "GLGizmoRotate.hpp"
#include "slic3r/GUI/SurfaceDrag.hpp"
#include "slic3r/GUI/GLTexture.hpp"
#include "slic3r/Utils/RaycastManager.hpp"
#include "slic3r/GUI/IconManager.hpp"

#include "libslic3r/EmbossShape.hpp"
#include "libslic3r/ObjectID.hpp"
#include <optional>
#include <memory>
#include <atomic>

#include "libslic3r/Emboss.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Model.hpp"

#include <imgui/imgui.h>
#include <glad/gl.h>
#include <string_view>
#include <string>
#include <wx/event.h>
#include <vector>

namespace Slic3r{
class ModelVolume;
enum class ModelVolumeType : int;
}

namespace Slic3r::GUI {

struct Texture{
    unsigned id{0};
    unsigned width{0};
    unsigned height{0};
};

class GLGizmoSVG : public GLGizmoBase
{
public:
    explicit GLGizmoSVG(GLCanvas3D &parent);

    bool create_volume(ModelVolumeType volume_type, const Vec2d &mouse_pos);
    bool create_volume(ModelVolumeType volume_type);
    bool create_volume(std::string_view svg_file, const Vec2d &mouse_pos, ModelVolumeType volume_type = ModelVolumeType::MODEL_PART);
    bool create_volume(std::string_view svg_file, ModelVolumeType volume_type = ModelVolumeType::MODEL_PART);

    static bool is_svg_object(const ModelVolume &volume);

    static bool is_svg(const ModelVolume &volume);

protected:
    bool on_init() override;
    std::string on_get_name() const override;
    void on_render() override;
    void on_register_raycasters_for_picking() override;
    void on_unregister_raycasters_for_picking() override;
    void on_render_input_window(float x, float y, float bottom_limit) override;
    bool on_is_activable() const override { return true; }
    bool on_is_selectable() const override { return false; }
    void on_set_state() override;    
    void data_changed(bool is_serializing) override;
    void on_set_hover_id() override{ m_rotate_gizmo.set_hover_id(m_hover_id); }
    void on_enable_grabber(unsigned int id) override { m_rotate_gizmo.enable_grabber(); }
    void on_disable_grabber(unsigned int id) override { m_rotate_gizmo.disable_grabber(); }
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_dragging(const UpdateData &data) override;    

    bool on_mouse(const wxMouseEvent &mouse_event) override;

    bool wants_enter_leave_snapshots() const override;
    std::string get_gizmo_entering_text() const override;
    std::string get_gizmo_leaving_text() const override;
    std::string get_action_snapshot_name() const override;
private:
    void set_volume_by_selection();
    void reset_volume();

    bool process(bool make_snapshot = true);
    void close();
    void draw_window();
    void draw_preview();
    void draw_filename();
    void draw_depth();
    void draw_size();
    void draw_use_surface();
    void draw_distance();
    void draw_rotation();
    void draw_mirroring();
    void draw_face_the_camera();
    void draw_model_type();

    bool on_mouse_for_rotation(const wxMouseEvent &mouse_event);
    bool on_mouse_for_translate(const wxMouseEvent &mouse_event);

    void volume_transformation_changed();
    
    struct GuiCfg;
    std::unique_ptr<const GuiCfg> m_gui_cfg;

    ModelVolume *m_volume = nullptr;

    EmbossShape m_volume_shape;
    std::vector<std::string> m_shape_warnings;

    ObjectID m_volume_id;

    std::shared_ptr<std::atomic<bool>> m_job_cancel = nullptr;

    GLGizmoRotate m_rotate_gizmo;
    std::optional<float> m_angle;
    std::optional<float> m_distance;

    std::optional<float> m_rotate_start_angle;

    RaycastManager m_raycast_manager;
    
    bool m_keep_up = true;

    bool m_keep_ratio = true;

    std::optional<SurfaceDrag> m_surface_drag;

    std::optional<float> m_scale_width;
    std::optional<float> m_scale_height;
    std::optional<float> m_scale_depth;
    void calculate_scale();
    float get_scale_for_tolerance();

    Texture m_texture;

    BoundingBox m_shape_bb; 

    std::string m_filename_preview;

    IconManager m_icon_manager;
    IconManager::VIcons m_icons;

    static const std::string M_ICON_FILENAME;
};
}

#endif
