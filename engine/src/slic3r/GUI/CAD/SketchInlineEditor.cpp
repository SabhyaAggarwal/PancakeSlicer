#include "slic3r/GUI/CAD/SketchInlineEditor.hpp"

#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "libslic3r/Color.hpp"

#include <functional>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <wx/gdicmn.h>
#include <utility>

namespace Slic3r {
namespace GUI {

namespace {

const char* const kWindowName = "##sketchvalue";

std::string fmt_value(double v, int digits = 3)
{
    char fmt[16];
    std::snprintf(fmt, sizeof(fmt), "%%.%df", digits);
    char buf[64];
    std::snprintf(buf, sizeof(buf), fmt, v);
    for (char* c = buf; *c; ++c)
        if (*c == ',') *c = '.';
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

bool parse_value(const char* text, double& out)
{
    if (text == nullptr) return false;
    std::string t(text);
    for (char& c : t)
        if (c == ',') c = '.';
    const char* b = t.c_str();
    char* end = nullptr;
    const double v = std::strtod(b, &end);
    if (end == b) return false;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end != '\0') return false;
    out = v;
    return true;
}

void ux_trace(const char* event, const std::string& title, const std::string& detail)
{
    if (!std::getenv("ORCA_CAD_UXTRACE")) return;
    std::fprintf(stderr, "[UX] %s title=%s %s\n", event, title.c_str(), detail.c_str());
    std::fflush(stderr);
}

}

void SketchInlineEditor::open(const wxPoint& canvas_px, double value, const std::string& title,
                              std::function<void(double)> on_commit,
                              std::function<void()> on_cancel)
{
    m_anchor = canvas_px;
    m_title  = title;
    m_err.clear();
    m_commit = std::move(on_commit);
    m_cancel = std::move(on_cancel);
    const std::string v = fmt_value(value);
    std::snprintf(m_buf, sizeof(m_buf), "%s", v.c_str());
    m_prefill_value = value;
    m_prefill_text  = v;
    m_active        = false;
    m_open          = true;
    m_focus_pending = true;
    ux_trace("open", m_title, "prefill=" + v);
}

void SketchInlineEditor::close()
{
    m_open          = false;
    m_focus_pending = false;
    m_active        = false;
    m_commit        = nullptr;
    m_cancel        = nullptr;
    m_err.clear();
}

void SketchInlineEditor::cancel()
{
    if (m_open) do_cancel();
}

void SketchInlineEditor::commit()
{
    if (m_open) do_commit();
}

void SketchInlineEditor::do_cancel()
{
    ux_trace("cancel", m_title, "");
    auto cb = m_cancel;
    close();
    if (cb) cb();
}

bool SketchInlineEditor::refuse(const std::string& why)
{
    if (!m_in_commit || m_open) return false;
    m_anchor        = m_last.anchor;
    m_title         = m_last.title;
    m_commit        = m_last.commit;
    m_cancel        = m_last.cancel;
    m_prefill_value = m_last.prefill_value;
    m_prefill_text  = m_last.prefill_text;
    std::snprintf(m_buf, sizeof(m_buf), "%s", m_last.buf.c_str());
    m_err           = why;
    m_open          = true;
    m_focus_pending = true;
    ux_trace("refused", m_title, "reason=" + why);
    return true;
}

void SketchInlineEditor::do_commit()
{
    double v = 0.0;
    if (m_prefill_text == m_buf)
        v = m_prefill_value;
    else if (!parse_value(m_buf, v)) {
        ux_trace("refused", m_title, std::string("typed=") + m_buf);
        m_err = (m_buf[0] == '\0') ? _u8L("Enter a number") : _u8L("Not a number");
        m_focus_pending = true;
        return;
    }
    ux_trace("commit", m_title, std::string("typed=") + m_buf + " value=" + fmt_value(v, 4));
    auto cb = m_commit;
    m_last = Closed{ m_anchor, m_title, std::string(m_buf), m_commit, m_cancel, m_prefill_value, m_prefill_text };
    close();
    m_in_commit = true;
    if (cb) cb(v);
    m_in_commit = false;
    m_last = Closed{};
}

bool SketchInlineEditor::render(ImGuiWrapper& imgui, float scale)
{
    if (!m_open) return false;

    ImGuiWrapper::push_common_window_style(scale);
    imgui.set_next_window_pos((float) m_anchor.x, (float) m_anchor.y, ImGuiCond_Always, 0.5f, 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
    imgui.begin(std::string(kWindowName),
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration
                    | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    if (!m_title.empty() || !m_err.empty()) {
        if (m_err.empty()) {
            imgui.text(m_title);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::to_ImVec4(ColorRGBA(0.91f, 0.42f, 0.42f, 1.0f)));
            imgui.text(m_err);
            ImGui::PopStyleColor();
        }
    }

    if (m_focus_pending) {
        ImGui::SetKeyboardFocusHere();
        m_focus_pending = false;
    }
    ImGui::PushItemWidth(90.0f * scale);
    const bool entered = ImGui::InputText("##sketchvalue_in", m_buf, sizeof(m_buf),
                                          ImGuiInputTextFlags_EnterReturnsTrue
                                              | ImGuiInputTextFlags_AutoSelectAll);
    m_active = ImGui::IsItemActive();
    const bool tabbed = m_active && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Tab));
    if (std::getenv("ORCA_CAD_UXTRACE")) {
        const ImGuiIO& io = ImGui::GetIO();
        std::fprintf(stderr, "[UX] frame title=%s want_text=%d want_kb=%d active=%d buf=%s\n",
                     m_title.c_str(), (int) io.WantTextInput, (int) io.WantCaptureKeyboard,
                     (int) ImGui::IsItemActive(), m_buf);
        std::fflush(stderr);
    }
    ImGui::PopItemWidth();
    imgui.end();
    ImGui::PopStyleVar();
    ImGuiWrapper::pop_common_window_style();

    if (m_open && request_frame)
        request_frame();

    if (entered || tabbed)
        do_commit();
    else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)))
        do_cancel();
    return true;
}

void SketchInlineEditor::bring_to_front()
{
    if (ImGuiWindow* w = ImGui::FindWindowByName(kWindowName))
        ImGui::BringWindowToDisplayFront(w);
}

}}
