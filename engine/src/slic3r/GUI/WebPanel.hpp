#pragma once

#include <nlohmann/json.hpp>

#include <wx/event.h>
#include <wx/panel.h>
#include <wx/webview.h>

#include <optional>
#include <string>

class wxCommandEvent;
class wxWebView;
class wxWebViewEvent;
class wxWindow;

namespace Slic3r { namespace GUI {

class WebPanel : public wxPanel
{
public:
    WebPanel(wxWindow* parent, const char* bridge_script);

protected:
    wxWebView* browser() const { return m_browser; }

    void post_to_page(const std::string& json);

    virtual std::optional<std::string> page_html() = 0;
    virtual bool on_page_message(const std::string& kind, const nlohmann::json& data) = 0;

private:
    void on_load_event(wxWebViewEvent& event);
    void on_navigated(wxWebViewEvent& event);
    void on_script_message(wxWebViewEvent& event);
    void on_webview_recreated(wxCommandEvent& event);
    void apply_theme();
    void load_page_html();

    wxWebView* m_browser{nullptr};
    bool       m_content_loaded{false};
    bool       m_own_page_load{false};
    bool       m_content_navigated{false};
};

}}
