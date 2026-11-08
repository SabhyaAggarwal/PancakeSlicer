#ifndef slic3r_GLGizmoSeam_hpp_
#define slic3r_GLGizmoSeam_hpp_

#include "GLGizmoPainterBase.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoBase.hpp"
#include <string>
#include <wx/string.h>
#include <map>
#include <vector>
#include <utility>

namespace Slic3r::GUI {

class GLGizmoSeam : public GLGizmoPainterBase
{
public:
    GLGizmoSeam(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    void render_painter_gizmo() override;

    bool on_tool_shortcut(Shortcut shortcut) override;

protected:
    void on_set_state() override;

    wchar_t  m_current_tool = 0;
    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;
    PainterGizmoType get_painter_type() const override;

    void tool_changed(wchar_t old_tool, wchar_t new_tool);

    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    std::string get_gizmo_entering_text() const override { return _u8L("Entering seam painting"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Seam painting"); }
    std::string get_action_snapshot_name() const override { return _u8L("Paint-on seam editing"); }
    static const constexpr float      CursorRadiusMin = 0.05f;

    const float get_cursor_radius_min() const override { return CursorRadiusMin; }

private:
    bool on_init() override;

    void update_model_object() override;
    void update_from_model_object(bool first_update = false) override;

    void on_opening() override {}
    void on_shutdown() override;

    std::map<std::string, wxString> m_desc;

    std::vector<std::pair<wxString, wxString>> m_shortcuts;
};



}


#endif
