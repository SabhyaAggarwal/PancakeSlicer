#pragma once

#include "GLGizmoPainterBase.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <string>
#include <wx/string.h>

namespace Slic3r::GUI {
class GLGizmoNonplanar : public GLGizmoPainterBase
{
public:
    GLGizmoNonplanar(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id);
    void render_painter_gizmo() override;
    bool on_tool_shortcut(Shortcut shortcut) override;

protected:
    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;
    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;
    std::string get_gizmo_entering_text() const override { return _u8L("Entering non-planar painting"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving non-planar painting"); }
    std::string get_action_snapshot_name() const override { return _u8L("Non-planar painting"); }
    EnforcerBlockerType get_left_button_state_type() const override { return EnforcerBlockerType::ENFORCER; }
    EnforcerBlockerType get_right_button_state_type() const override { return EnforcerBlockerType::BLOCKER; }

private:
    bool on_init() override;
    void update_model_object() override;
    void update_from_model_object(bool first_update) override;
    void on_opening() override { m_detected_regions = -1; }
    void on_shutdown() override;
    PainterGizmoType get_painter_type() const override { return PainterGizmoType::NONPLANAR; }
    void detect();
    int m_tool = 0;
    int m_detected_regions = -1;
};
}
