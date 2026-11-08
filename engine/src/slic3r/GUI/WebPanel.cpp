#include "WebPanel.hpp"

#include "GUI_App.hpp"
#include "Widgets/WebHosting.hpp"
#include "Widgets/WebView.hpp"
#include "Widgets/WebViewHostDialog.hpp"

#include <boost/log/trivial.hpp>

#include <wx/panel.h>
#include <wx/event.h>
#include <optional>
#include <string>
#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/webview.h>

namespace Slic3r { namespace GUI {

WebPanel::WebPanel(wxWindow* parent, const char* bridge_script)
    : wxPanel(parent, wxID_ANY)
{
    SetBackgroundColour(wxGetApp().get_window_default_clr());
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(sizer);

    m_browser = WebView::CreateWebView(this, web_hosting::bootstrap_url());
    m_browser->SetBackgroundColour(GetBackgroundColour());
    m_browser->AddUserScript(wxString::FromUTF8(WebViewHostDialog::theme_user_script()));
    m_browser->AddUserScript(wxString::FromUTF8(WebViewHostDialog::element_defaults_user_script()));
    m_browser->AddUserScript(wxString::FromUTF8(bridge_script));
    m_browser->Bind(wxEVT_WEBVIEW_LOADED, &WebPanel::on_load_event, this);
    m_browser->Bind(wxEVT_WEBVIEW_ERROR, &WebPanel::on_load_event, this);
    m_browser->Bind(wxEVT_WEBVIEW_NAVIGATED, &WebPanel::on_navigated, this);
    m_browser->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &WebPanel::on_script_message, this);
    m_browser->Bind(EVT_WEBVIEW_RECREATED, &WebPanel::on_webview_recreated, this);
    sizer->Add(m_browser, 1, wxEXPAND);
}

void WebPanel::on_load_event(wxWebViewEvent& event)
{
    const bool loaded = event.GetEventType() == wxEVT_WEBVIEW_LOADED;
    if (!m_content_loaded) {
        m_content_loaded = true;
        load_page_html();
    } else if (!web_hosting::is_content_url(event.GetURL())) {
        if (loaded)
            apply_theme();
    } else if (m_own_page_load) {
        m_own_page_load = false;
        if (loaded)
            apply_theme();
    } else if (loaded && m_content_navigated) {
        load_page_html();
    }
    if (loaded)
        m_content_navigated = false;
    event.Skip();
}

void WebPanel::on_navigated(wxWebViewEvent& event)
{
    m_content_navigated = web_hosting::is_content_url(event.GetURL());
    event.Skip();
}

void WebPanel::load_page_html()
{
    if (const std::optional<std::string> html = page_html()) {
        m_own_page_load = true;
        m_browser->SetPage(wxString::FromUTF8(*html), web_hosting::content_base_url());
    }
}

void WebPanel::on_script_message(wxWebViewEvent& event)
{
    const nlohmann::json payload = nlohmann::json::parse(event.GetString().utf8_string(), nullptr, false);
    if (!payload.is_object() || payload.value("channel", std::string()) != "orca")
        return;

    const std::string kind = payload.value("kind", std::string());
    if (!on_page_message(kind, payload.contains("data") ? payload["data"] : nlohmann::json()))
        BOOST_LOG_TRIVIAL(warning) << "WebPanel ignored a window.orca '" << kind << "' call; this host does not support it";
}

void WebPanel::on_webview_recreated(wxCommandEvent&)
{
    SetBackgroundColour(wxGetApp().get_window_default_clr());
    m_browser->SetBackgroundColour(GetBackgroundColour());
    Refresh();
    apply_theme();
}

void WebPanel::apply_theme()
{
    WebView::RunScript(m_browser, wxString::FromUTF8(WebViewHostDialog::theme_apply_script()));
}

void WebPanel::post_to_page(const std::string& json)
{
    WebView::RunScript(m_browser, wxString::Format(
        "(function dispatch(payload, attempts) {\n"
        "  if (typeof window.__orcaDispatch === 'function') { window.__orcaDispatch(payload); return; }\n"
        "  if (attempts < 100) window.setTimeout(function() { dispatch(payload, attempts + 1); }, 25);\n"
        "})({data: %s}, 0);",
        wxString::FromUTF8(json)));
}

}}
