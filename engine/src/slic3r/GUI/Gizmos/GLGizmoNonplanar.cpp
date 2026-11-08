#include "GLGizmoNonplanar.hpp"

#include "GLGizmoUtils.hpp"
#include "GLGizmosCommon.hpp"
#include "GLGizmosManager.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Nonplanar/RegionDetect.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/Event.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/GUI/Shortcuts.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include <glad/gl.h>
#include <imgui.h>
#include <wx/busycursor.h>
#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace Slic3r::GUI {
namespace {
class RegionSelector : public TriangleSelectorPatch {
public:
    RegionSelector(const TriangleMesh &mesh, const std::vector<ColorRGBA> &colors) : TriangleSelectorPatch(mesh, colors) {}
    void apply_detection(const std::vector<bool> &facets) {
        for (auto &triangle : m_triangles)
            if (triangle.valid() && !triangle.is_split() && triangle.get_state() != EnforcerBlockerType::BLOCKER)
                triangle.set_state(facets[triangle.source_triangle] ? EnforcerBlockerType::ENFORCER : EnforcerBlockerType::NONE);
        seed_fill_unselect_all_triangles();
        request_update_render_data(true);
    }
};
}

GLGizmoNonplanar::GLGizmoNonplanar(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id)
    : GLGizmoPainterBase(parent, icon_filename, sprite_id) {}

bool GLGizmoNonplanar::on_init()
{
    m_shortcut = Shortcut::GizmoNonplanar;
    return true;
}

std::string GLGizmoNonplanar::on_get_name() const { return _u8L("Non-planar painting"); }

void GLGizmoNonplanar::on_shutdown()
{
    m_parent.use_slope(false);
    m_parent.toggle_model_objects_visibility(true);
}

bool GLGizmoNonplanar::on_tool_shortcut(Shortcut shortcut)
{
    switch (shortcut) {
    case Shortcut::PaintToolCircle: m_tool = 0; break;
    case Shortcut::PaintToolSphere: m_tool = 1; break;
    case Shortcut::PaintToolTriangle: m_tool = 2; break;
    case Shortcut::PaintToolFill: m_tool = 3; break;
    default: return false;
    }
    for (auto &selector : m_triangle_selectors) {
        selector->seed_fill_unselect_all_triangles();
        selector->request_update_render_data();
    }
    return true;
}

void GLGizmoNonplanar::render_painter_gizmo()
{
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));
    render_triangles(m_parent.get_selection());
    m_c->object_clipper()->render_cut();
    m_c->instances_hider()->render_cut();
    render_cursor();
    glsafe(::glDisable(GL_BLEND));
}

void GLGizmoNonplanar::on_render_input_window(float x, float y, float bottom_limit)
{
    ModelObject *object = m_c->selection_info()->model_object();
    if (!object) return;
    const float scale = m_parent.get_scale();
    y = std::min(y, bottom_limit - m_imgui->scaled(18.f));
#if BBS_TOOLBAR_ON_TOP
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.f, 0.f);
#else
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 1.f, 0.f);
#endif
    ImGuiWrapper::push_toolbar_style(scale);
    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    m_imgui->bold_text(_u8L("Non-planar"));
    ImGui::SameLine();
    if (m_imgui->button(_L("Detect"))) detect();
    if (ImGui::IsItemHovered()) m_imgui->tooltip(_L("Find curved tops for this print head. Paint to refine."), ImGui::GetFontSize() * 20.f);
    if (m_detected_regions == 0) m_imgui->text(_L("No curved tops found"));
    ImGui::Separator();

    const std::array<wchar_t, 4> light = {ImGui::CircleButtonIcon, ImGui::SphereButtonIcon, ImGui::TriangleButtonIcon, ImGui::FillButtonIcon};
    const std::array<wchar_t, 4> dark = {ImGui::CircleButtonDarkIcon, ImGui::SphereButtonDarkIcon, ImGui::TriangleButtonDarkIcon, ImGui::FillButtonDarkIcon};
    const std::array<wxString, 4> tips = {_L("Circle"), _L("Sphere"), _L("Triangles"), _L("Smart fill")};
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine();
        const bool active = m_tool == i;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.f * scale, 4.f * scale));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,1,1));
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.f,.59f,.53f,.25f) : ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.6f,.6f,.6f,.2f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.f,.59f,.53f,.30f));
        ImGui::PushStyleColor(ImGuiCol_Border, active ? ImGuiWrapper::COL_ORCA : ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_BorderActive, active ? ImGuiWrapper::COL_ORCA : ImVec4(0,0,0,0));
        const bool clicked = m_imgui->glyph_button((m_is_dark_mode ? dark : light)[i], ImVec2(16.f*scale, 16.f*scale));
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(3);
        if (clicked && m_tool != i) {
            m_tool = i;
            for (auto &selector : m_triangle_selectors) {
                selector->seed_fill_unselect_all_triangles();
                selector->request_update_render_data();
            }
        }
        if (ImGui::IsItemHovered()) m_imgui->tooltip(tips[i], ImGui::GetFontSize() * 20.f);
    }
    m_cursor_type = m_tool == 0 ? TriangleSelector::CIRCLE : m_tool == 1 ? TriangleSelector::SPHERE : TriangleSelector::POINTER;
    m_tool_type = m_tool == 3 ? ToolType::SMART_FILL : ToolType::BRUSH;
    if (m_tool < 2) {
        m_imgui->text(_L("Brush size"));
        ImGui::SameLine();
        ImGui::PushItemWidth(m_imgui->scaled(9.f));
        m_imgui->bbl_slider_float_style("##radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f", 1.f, true);
        ImGui::PopItemWidth();
    } else if (m_tool == 3) {
        m_imgui->text(_L("Smart fill angle"));
        ImGui::SameLine();
        ImGui::PushItemWidth(m_imgui->scaled(9.f));
        if (m_imgui->bbl_slider_float_style("##fill_angle", &m_smart_fill_angle, SmartFillAngleMin, SmartFillAngleMax, "%.0f°", 1.f, true))
            for (auto &selector : m_triangle_selectors) selector->seed_fill_unselect_all_triangles();
        ImGui::PopItemWidth();
    }
    ImGui::Separator();
    ImGui::TextColored(ImVec4(TriangleSelectorGUI::enforcers_color.r(), TriangleSelectorGUI::enforcers_color.g(), TriangleSelectorGUI::enforcers_color.b(), 1.f), "%s", _u8L("Enforce").c_str());
    ImGui::SameLine(); m_imgui->text(_L("Left mouse button"));
    ImGui::TextColored(ImVec4(TriangleSelectorGUI::blockers_color.r(), TriangleSelectorGUI::blockers_color.g(), TriangleSelectorGUI::blockers_color.b(), 1.f), "%s", _u8L("Block").c_str());
    ImGui::SameLine(); m_imgui->text(_L("Right mouse button"));
    m_imgui->text(GUI::shortkey_shift_prefix() + _L("Left mouse button"));
    ImGui::SameLine(); m_imgui->text(_L("Erase"));
    ImGui::Separator();
    const bool painted = std::any_of(object->volumes.begin(), object->volumes.end(), [](const ModelVolume *v) { return v->is_nonplanar_painted(); });
    m_imgui->disabled_begin(!painted);
    if (m_imgui->button(_L("Erase all"))) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Erase non-planar painting"), UndoRedo::SnapshotType::GizmoAction);
        for (auto &selector : m_triangle_selectors) {
            selector->reset();
            selector->request_update_render_data(true);
        }
        update_model_object();
        m_parent.set_as_dirty();
    }
    m_imgui->disabled_end();
    ImGui::SameLine();
    GLGizmoUtils::begin_right_aligned_buttons({_L("Done")});
    if (m_imgui->button(_L("Done"))) m_parent.reset_all_gizmos();
    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoNonplanar::detect()
{
    ModelObject *object = m_c->selection_info()->model_object();
    if (!object || object->instances.empty()) return;
    wxBusyCursor wait;
    PrintConfig config;
    config.apply(wxGetApp().preset_bundle->printers.get_edited_preset().config, true);
    const auto head = Nonplanar::resolve_head_envelope(config);
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Detect non-planar regions"), UndoRedo::SnapshotType::GizmoAction);
    const int instance_id = m_parent.get_selection().get_instance_idx();
    const Transform3d instance = object->instances[instance_id >= 0 ? instance_id : 0]->get_matrix();
    size_t i = 0;
    m_detected_regions = 0;
    for (const ModelVolume *volume : object->volumes) {
        if (!volume->is_model_part()) continue;
        std::vector<bool> facets(volume->mesh().its.indices.size(), false);
        for (const auto &region : Nonplanar::detect_nonplanar_regions(*volume, instance * volume->get_matrix(), head)) {
            ++m_detected_regions;
            for (int facet : region.facets) facets[facet] = true;
        }
        static_cast<RegionSelector &>(*m_triangle_selectors[i++]).apply_detection(facets);
    }
    update_model_object();
    m_parent.set_as_dirty();
}

void GLGizmoNonplanar::update_model_object()
{
    ModelObject *object = m_c->selection_info()->model_object();
    if (!object) return;
    bool updated = false;
    size_t i = 0;
    for (ModelVolume *volume : object->volumes)
        if (volume->is_model_part()) updated |= volume->nonplanar_facets.set(*m_triangle_selectors[i++]);
    if (updated) {
        const auto &objects = wxGetApp().model().objects;
        wxGetApp().obj_list()->update_info_items(std::find(objects.begin(), objects.end(), object) - objects.begin());
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    }
}

void GLGizmoNonplanar::update_from_model_object(bool first_update)
{
    const ModelObject *object = m_c->selection_info()->model_object();
    m_triangle_selectors.clear();
    const std::vector<ColorRGBA> colors = {GLVolume::NEUTRAL_COLOR, TriangleSelectorGUI::enforcers_color, TriangleSelectorGUI::blockers_color};
    for (const ModelVolume *volume : object->volumes) {
        if (!volume->is_model_part()) continue;
        auto selector = std::make_unique<RegionSelector>(volume->mesh(), colors);
        selector->deserialize(volume->nonplanar_facets.get_data(), false);
        selector->request_update_render_data();
        m_triangle_selectors.push_back(std::move(selector));
    }
}

wxString GLGizmoNonplanar::handle_snapshot_action_name(bool shift_down, Button button_down) const
{
    return shift_down ? _L("Erase non-planar painting") : button_down == Button::Right ? _L("Block non-planar printing") : _L("Enforce non-planar printing");
}
}
