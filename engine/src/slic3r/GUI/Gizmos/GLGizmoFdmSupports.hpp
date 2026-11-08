#ifndef slic3r_GLGizmoFdmSupports_hpp_
#define slic3r_GLGizmoFdmSupports_hpp_

#include "GLGizmoPainterBase.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/ObjectID.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <boost/thread.hpp>
#include "slic3r/GUI/Gizmos/GLGizmoBase.hpp"
#include <string>
#include <wx/string.h>
#include <cstddef>
#include <vector>
#include <mutex>
#include <map>
#include <utility>

namespace Slic3r::GUI {

class GLGizmoFdmSupports : public GLGizmoPainterBase
{
public:
    GLGizmoFdmSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    void render_painter_gizmo() override;

    enum EditState {
        state_idle = 0,
        state_generating = 1,
        state_ready
    };

    bool on_tool_shortcut(Shortcut shortcut) override;

protected:
    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;

    void on_set_state() override;
    void render_tooltip_button(float x, float y);
    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    std::string get_gizmo_entering_text() const override { return _u8L("Entering Paint-on supports"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Paint-on supports"); }
    std::string get_action_snapshot_name() const override { return _u8L("Paint-on supports editing"); }

    wchar_t                           m_current_tool = 0;

private:
    bool on_init() override;

    void update_model_object() override;
    void update_from_model_object(bool first_update) override;
    void tool_changed(wchar_t old_tool, wchar_t new_tool);

    void on_opening() override;
    void on_shutdown() override;
    PainterGizmoType get_painter_type() const override;

    void select_facets_by_angle(float threshold, bool block);
    int get_selection_support_threshold_angle();

    int m_support_threshold_angle = -1;

    void init_print_instance();
    void update_support_volumes();
    void invalid_support_volumes(bool invalid_step = false);
    bool need_regenerate_support_volumes();
    void generate_support_volume();
    void run_thread();
    float m_angle_threshold_deg = 40.f;
    bool m_volume_valid = false;


    GLVolume *m_support_volume = NULL;
    mutable bool m_volume_ready = false;
    bool m_is_tree_support = false;
    bool m_cancel = false;
    size_t m_object_id;
    std::vector<ObjectBase::Timestamp> m_volume_timestamps;
    PrintInstance m_print_instance;
    mutable EditState m_edit_state;
    boost::thread   m_thread;
    std::mutex      m_mutex;

    std::map<std::string, wxString> m_desc;

    std::vector<std::pair<wxString, wxString>> m_shortcuts_brush;
    std::vector<std::pair<wxString, wxString>> m_shortcuts_bucket_fill;
    std::vector<std::pair<wxString, wxString>> m_shortcuts_gap_fill;
};



}


#endif
