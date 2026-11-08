#ifndef slic3r_SketchInlineEditor_hpp_
#define slic3r_SketchInlineEditor_hpp_

#include <functional>
#include <string>

#include <wx/gdicmn.h>

namespace Slic3r {
namespace GUI {

class ImGuiWrapper;

class SketchInlineEditor
{
public:
    SketchInlineEditor() = default;

    void open(const wxPoint& canvas_px, double value, const std::string& title,
              std::function<void(double)> on_commit,
              std::function<void()> on_cancel);
    void close();
    void cancel();
    void commit();
    bool refuse(const std::string& why);
    bool is_open() const { return m_open; }

    bool render(ImGuiWrapper& imgui, float scale);
    void bring_to_front();

    std::function<void()> request_frame;

    bool is_mapped() const { return m_open; }
    bool has_focus() const { return m_open && (m_active || m_focus_pending); }
    void dismiss() { close(); }

private:
    void do_commit();
    void do_cancel();

    std::function<void(double)> m_commit;
    std::function<void()>       m_cancel;
    bool        m_open{false};
    bool        m_focus_pending{false};
    wxPoint     m_anchor{0, 0};
    std::string m_title;
    std::string m_err;
    char        m_buf[64]{};
    double      m_prefill_value{0.0};
    std::string m_prefill_text;
    bool        m_active{false};
    struct Closed {
        wxPoint anchor; std::string title; std::string buf;
        std::function<void(double)> commit; std::function<void()> cancel;
        double prefill_value{0.0}; std::string prefill_text;
    };
    Closed      m_last;
    bool        m_in_commit{false};
};

}}

#endif
