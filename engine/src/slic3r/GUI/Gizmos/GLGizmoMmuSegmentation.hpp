#ifndef slic3r_GLGizmoMmuSegmentation_hpp_
#define slic3r_GLGizmoMmuSegmentation_hpp_

#include "GLGizmoPainterBase.hpp"
#include "slic3r/GUI/I18N.hpp"
#include <cstddef>
#include <cassert>
#include <vector>
#include "slic3r/GUI/Gizmos/GLGizmoBase.hpp"
#include <string>
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/Color.hpp"
#include <wx/string.h>
#include <wx/gdicmn.h>
#include <set>
#include <map>
#include <utility>

namespace Slic3r::GUI {

class GLMmSegmentationGizmo3DScene
{
public:
    GLMmSegmentationGizmo3DScene() = delete;

    explicit GLMmSegmentationGizmo3DScene(size_t triangle_indices_buffers_count)
    {
    }

    virtual ~GLMmSegmentationGizmo3DScene() { release_geometry(); }

    [[nodiscard]] inline bool has_VBOs(size_t triangle_indices_idx) const
    {
        assert(triangle_indices_idx < this->triangle_patches.size());
        return this->triangle_indices_VBO_ids[triangle_indices_idx] != 0;
    }

    void release_geometry();
    void finalize_vertices();
    void finalize_triangle_indices();

    void clear()
    {
        this->vertices.clear();
        this->triangle_indices_VBO_ids.clear();
        this->triangle_indices_sizes.clear();

        for (TrianglePatch& patch : this->triangle_patches)
            patch.triangle_indices.clear();
        this->triangle_patches.clear();
    }

    void render(size_t triangle_indices_idx) const;

    std::vector<float>            vertices;
    std::vector<TrianglePatch>    triangle_patches;

    std::vector<size_t> triangle_indices_sizes;

    unsigned int              vertices_VAO_id{ 0 };
    unsigned int              vertices_VBO_id{0};
    std::vector<unsigned int> triangle_indices_VBO_ids;
};

class GLGizmoMmuSegmentation : public GLGizmoPainterBase
{
public:
    GLGizmoMmuSegmentation(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoMmuSegmentation() override = default;

    void render_painter_gizmo() override;

    void data_changed(bool is_serializing) override;

    static const constexpr size_t EXTRUDERS_LIMIT = static_cast<size_t>(EnforcerBlockerType::ExtruderMax);

    const float get_cursor_radius_min() const override { return CursorRadiusMin; }

    bool on_number_key_down(int number);
    bool on_tool_shortcut(Shortcut shortcut) override;

protected:
    ColorRGBA get_cursor_hover_color() const override;
    void on_set_state() override;

    EnforcerBlockerType get_left_button_state_type() const override { return EnforcerBlockerType(m_selected_extruder_idx + 1); }
    EnforcerBlockerType get_right_button_state_type() const override { return EnforcerBlockerType(-1); }

    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;
    void render_tooltip_button(float x, float y);
    bool on_is_selectable() const override;
    bool on_is_activable() const override;

    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    std::string get_gizmo_entering_text() const override { return _u8L("Entering color painting"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving color painting"); }
    std::string get_action_snapshot_name() const override { return _u8L("Color painting editing"); }

    size_t                            m_selected_extruder_idx = 0;
    std::vector<ColorRGBA>            m_extruders_colors;
    std::vector<int>                  m_volumes_extruder_idxs;

    wchar_t                           m_current_tool = 0;
    bool                              m_detect_geometry_edge = true;
    
    std::vector<size_t>               m_extruder_remap;
    std::vector<std::vector<wxColour>> m_gradient_ramps;
    std::set<size_t>                  m_used_filaments;

    static const constexpr float      CursorRadiusMin = 0.1f;

private:
    bool on_init() override;

    void update_model_object() override;
    void update_from_model_object(bool first_update = false) override;
    void tool_changed(wchar_t old_tool, wchar_t new_tool);

    void on_opening() override;
    void on_shutdown() override;
    PainterGizmoType get_painter_type() const override;

    void init_model_triangle_selectors();

    bool draw_color_button(int idx, const char* id_str, const ColorRGBA& color, ColorRGBA& map_color, bool active, float scale);
    const std::vector<wxColour>* gradient_of(int idx) const
    {
        return idx >= 0 && idx < (int) m_gradient_ramps.size() && !m_gradient_ramps[idx].empty() ? &m_gradient_ramps[idx] : nullptr;
    }

    void update_triangle_selectors_colors();
    void init_extruders_data();
    
    void remap_filament_assignments();
    void render_filament_remap_ui(float window_width, float max_tooltip_width, float scale);
    void update_used_filaments();

    std::map<std::string, wxString> m_desc;

    std::vector<std::pair<wxString, wxString>> m_shortcuts_brush;
    std::vector<std::pair<wxString, wxString>> m_shortcuts_bucket_fill;
    std::vector<std::pair<wxString, wxString>> m_shortcuts_gap_fill;
};

}


#endif
