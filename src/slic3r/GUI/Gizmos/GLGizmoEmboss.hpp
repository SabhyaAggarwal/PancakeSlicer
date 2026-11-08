#ifndef slic3r_GLGizmoEmboss_hpp_
#define slic3r_GLGizmoEmboss_hpp_

#include "GLGizmoBase.hpp"
#include "GLGizmoRotate.hpp"
#include "slic3r/GUI/IconManager.hpp"
#include "slic3r/GUI/SurfaceDrag.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/TextLines.hpp"
#include "slic3r/Utils/RaycastManager.hpp"
#include "slic3r/Utils/EmbossStyleManager.hpp"

#include <cstddef>
#include "libslic3r/ObjectID.hpp"
#include <optional>
#include <memory>
#include <atomic>

#include "libslic3r/Emboss.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/TextConfiguration.hpp"

#include <imgui/imgui.h>
#include <glad/gl.h>
#include <string>
#include <wx/event.h>
#include <wx/string.h>
#include <vector>
#include <utility>

class wxFont;
namespace Slic3r{
    class AppConfig;
    class GLVolume;
    enum class ModelVolumeType : int;
}

namespace Slic3r::GUI {
class GLGizmoEmboss : public GLGizmoBase
{
public:
    explicit GLGizmoEmboss(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id);

    bool create_volume(ModelVolumeType volume_type, const Vec2d &mouse_pos);

    bool create_volume(ModelVolumeType volume_type);

    void on_shortcut_key();

    bool do_mirror(size_t axis);


    static bool re_emboss(const ModelVolume &text, std::shared_ptr<std::atomic<bool>> job_cancel = nullptr);

protected:
    bool on_init() override;
    std::string on_get_name() const override;
    void on_render() override;
    void on_register_raycasters_for_picking() override;
    void on_unregister_raycasters_for_picking() override;
    void on_render_input_window(float x, float y, float bottom_limit) override;
    void on_set_state() override;
    void data_changed(bool is_serializing) override;
    void on_set_hover_id() override{ m_rotate_gizmo.set_hover_id(m_hover_id); }
    void on_enable_grabber(unsigned int id) override { m_rotate_gizmo.enable_grabber(); }
    void on_disable_grabber(unsigned int id) override { m_rotate_gizmo.disable_grabber(); }
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_dragging(const UpdateData &data) override;    
    void push_button_style(bool pressed);
    void pop_button_style();

    bool on_mouse(const wxMouseEvent &mouse_event) override;

    bool wants_enter_leave_snapshots() const override;
    std::string get_gizmo_entering_text() const override;
    std::string get_gizmo_leaving_text() const override;
    std::string get_action_snapshot_name() const override;

private:
    void volume_transformation_changing();
    void volume_transformation_changed();

    static EmbossStyles create_default_styles();
    bool init_create(ModelVolumeType volume_type);

    void set_volume_by_selection();
    void reset_volume();

    bool is_changed_from_default_style();
    void reset_to_default_style();

    bool process(bool make_snapshot = true);
    void close();
    void draw_window(float x, float y);
    void draw_text_input();
    void draw_model_type();
    void draw_style_list();
    void draw_delete_style_button();
    void draw_style_rename_popup();
    void draw_style_rename_button();
    void draw_style_save_button(bool is_modified);
    void draw_style_save_as_popup();
    void draw_style_add_button();
    void init_font_name_texture();
    void draw_font_list_line();
    void draw_font_list();
    void draw_height(bool use_inch);
    void draw_depth(bool use_inch);

    bool set_height();

    bool draw_italic_button();
    bool draw_bold_button();
    void draw_advanced();

    bool select_facename(const wxString& facename);

    template<typename T> bool rev_input_mm(const std::string &name, T &value, const T *default_value,
        const std::string &undo_tooltip, T step, T step_fast, const char *format, bool use_inch, const std::optional<float>& scale) const;

    template<typename T> bool rev_input(const std::string &name, T &value, const T *default_value, 
        const std::string &undo_tooltip, T step, T step_fast, const char *format, ImGuiInputTextFlags flags = 0) const;
    bool rev_checkbox(const std::string &name, bool &value, const bool* default_value, const std::string  &undo_tooltip) const;
    bool rev_slider(const std::string &name, std::optional<int>& value, const std::optional<int> *default_value,
        const std::string &undo_tooltip, int v_min, int v_max, const std::string &format, const wxString &tooltip) const;
    bool rev_slider(const std::string &name, std::optional<float>& value, const std::optional<float> *default_value,
        const std::string &undo_tooltip, float v_min, float v_max, const std::string &format, const wxString &tooltip) const;
    bool rev_slider(const std::string &name, float &value, const float *default_value, 
        const std::string &undo_tooltip, float v_min, float v_max, const std::string &format, const wxString &tooltip) const;
    template<typename T, typename Draw> bool revertible(const std::string &name, T &value, const T *default_value,
        const std::string &undo_tooltip, float undo_offset, Draw draw) const;

    bool on_mouse_for_rotation(const wxMouseEvent &mouse_event);
    bool on_mouse_for_translate(const wxMouseEvent &mouse_event);
    void on_mouse_change_selection(const wxMouseEvent &mouse_event);

    bool m_is_unknown_font = false;
    void create_notification_not_valid_font(const TextConfiguration& tc);
    void create_notification_not_valid_font(const std::string& text);
    void remove_notification_not_valid_font();

    struct GuiCfg;
    std::unique_ptr<const GuiCfg> m_gui_cfg;

    bool m_is_advanced_edit_style = false;

    Emboss::StyleManager m_style_manager;

    struct Facenames;
    std::unique_ptr<Facenames> m_face_names;

    std::string m_text;
    bool m_keep_up = true;

    ModelVolume *m_volume = nullptr;

    ObjectID m_volume_id;

    bool m_text_contain_unknown_glyph = false;

    std::shared_ptr<std::atomic<bool>> m_job_cancel = nullptr;

    TextLinesModel m_text_lines;
    void reinit_text_lines(unsigned count_lines=0);

    GLGizmoRotate m_rotate_gizmo;
    std::optional<float> m_rotate_start_angle;

    std::optional<SurfaceDrag> m_surface_drag;

    RaycastManager m_raycast_manager;

    std::optional<float> m_scale_height;
    std::optional<float> m_scale_depth;
    void calculate_scale();

    IconManager m_icon_manager;
    IconManager::VIcons m_icons;
    void init_icons();

    static const std::string M_ICON_FILENAME;

    std::vector<std::pair<wxString, wxString>> m_shortcuts;
};

}

#endif
