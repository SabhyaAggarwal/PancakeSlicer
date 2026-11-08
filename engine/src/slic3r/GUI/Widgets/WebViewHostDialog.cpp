#include "WebViewHostDialog.hpp"

#include "WebView.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "libslic3r/Utils.hpp"
#include <exception>
#include <nlohmann/json.hpp>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <string>
#include <wx/colour.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include "slic3r/GUI/GUI_Utils.hpp"
#include <wx/chartype.h>
#include <wx/log.h>
#include <wx/sizer.h>

#include <algorithm>
#include <wx/webview.h>

namespace Slic3r { namespace GUI {

namespace {

std::string css_color(const wxColour& c) { return c.GetAsString(wxC2S_HTML_SYNTAX).ToStdString(); }

std::string host_theme_name() { return wxGetApp().dark_mode() ? "dark" : "light"; }

std::string host_theme_vars_css()
{
    GUI_App&       app    = wxGetApp();
    const wxColour bg     = app.get_window_default_clr();
    const wxColour fg     = app.get_label_clr_default();
    const wxColour muted  = app.get_label_clr_sys();
    const wxColour border = app.get_highlight_default_clr();
    const wxColour accent = StateColor::darkModeColorFor(wxColour("#009688"));
    std::string    font   = app.normal_font().GetFaceName().ToStdString();
    font.erase(std::remove_if(font.begin(), font.end(), [](char c) {
                   return c == '\'' || c == '"' || c == '<' || c == '>' || c == '{' || c == '}' || c == ';';
               }),
               font.end());

    std::string s;
    s += ":root{";
    s += "--orca-bg:" + css_color(bg) + ";";
    s += "--orca-fg:" + css_color(fg) + ";";
    s += "--orca-muted:" + css_color(muted) + ";";
    s += "--orca-border:" + css_color(border) + ";";
    s += "--orca-accent:" + css_color(accent) + ";";
    s += "--orca-accent-fg:#ffffff;";
    s += "--orca-font:" + (font.empty() ? std::string() : "'" + font + "',") +
         "system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;";
    s += "color-scheme:" + host_theme_name() + ";";
    s += "}";
    return s;
}

std::string host_theme_apply_js()
{
    const std::string vars_literal = nlohmann::json(host_theme_vars_css()).dump();
    const std::string theme = host_theme_name();
    return "(function(){var css=" + vars_literal + ";var theme=\"" + theme + "\";" + R"JS(
var el=document.getElementById('orca-host-theme-vars');
if(el){el.textContent=css;}
else if(document.head){document.head.insertAdjacentHTML('afterbegin','<style id="orca-host-theme-vars"></style>');var e2=document.getElementById('orca-host-theme-vars');if(e2)e2.textContent=css;}
if(document.documentElement)
    document.documentElement.setAttribute('data-orca-theme',theme);
})();)JS";
}

}

std::string WebViewHostDialog::theme_apply_script() { return host_theme_apply_js(); }

std::string WebViewHostDialog::theme_user_script()
{
    const std::string style = "<style id=\"orca-host-theme-vars\">" + host_theme_vars_css() + "</style>";
    return document_start_injector(
        style, "orca-host-theme-vars", "afterbegin",
        "window.__orcaHostThemed=true;var theme=\"" + host_theme_name() + "\";",
        "if(document.documentElement)document.documentElement.setAttribute('data-orca-theme',theme);");
}

std::string WebViewHostDialog::element_defaults_user_script()
{
    std::string css;
    css += "<style id=\"orca-plugin-defaults\">";
    css += "html,body{background:var(--orca-bg);color:var(--orca-fg);"
           "font-family:var(--orca-font);font-size:13px;}";
    css += "body{margin:0;}";
    css += "h1,h2,h3,h4,h5,h6{color:var(--orca-fg);font-weight:600;}";
    css += "a{color:var(--orca-accent);}";
    css += "hr{border:0;border-top:1px solid var(--orca-border);}";
    css += "button{font:inherit;color:var(--orca-accent-fg);background:var(--orca-accent);"
           "border:1px solid var(--orca-accent);border-radius:4px;padding:5px 14px;cursor:pointer;}";
    css += "button:hover{filter:brightness(1.1);}";
    css += "button:disabled{opacity:.5;cursor:default;}";
    css += "input,select,textarea{font:inherit;color:var(--orca-fg);"
           "background:var(--orca-bg);border:1px solid var(--orca-border);"
           "border-radius:4px;padding:4px 8px;}";
    css += "input:focus,select:focus,textarea:focus{outline:none;border-color:var(--orca-accent);}";
    css += "table{border-collapse:collapse;}";
    css += "th,td{text-align:left;padding:6px 10px;border-bottom:1px solid var(--orca-border);}";
    css += "th{color:var(--orca-muted);font-weight:600;}";
    css += "::-webkit-scrollbar{width:12px;height:12px;}";
    css += "::-webkit-scrollbar-thumb{background:var(--orca-border);border-radius:6px;}";
    css += "::-webkit-scrollbar-track{background:transparent;}";
    css += "</style>";
    return document_start_injector(css, "orca-plugin-defaults", "beforeend");
}

std::string WebViewHostDialog::document_start_injector(const std::string& markup,
                                                       const char*        dom_id,
                                                       const char*        position,
                                                       const std::string& prelude,
                                                       const std::string& on_inject)
{
    const std::string literal = nlohmann::json(markup).dump();
    std::string       s;
    s += "(function(){";
    s += "if(window.top!==window.self)return;";
    s += prelude;
    s += "var css=" + literal + ";";
    s += "function inject(){";
    s += "var root=document.head||document.documentElement;if(!root)return false;";
    s += "if(!document.getElementById('" + std::string(dom_id) + "'))root.insertAdjacentHTML('" +
         std::string(position) + "',css);";
    s += on_inject;
    s += "return true;}";
    s += "if(inject())return;";
    s += "var obs=new MutationObserver(function(){if(inject())obs.disconnect();});";
    s += "obs.observe(document,{childList:true});})();";
    return s;
}

WebViewHostDialog::WebViewHostDialog(wxWindow* parent,
                                     wxWindowID id,
                                     const wxString& title,
                                     const wxPoint& pos,
                                     const wxSize& size,
                                     long style)
    : DPIDialog(parent, id, title, pos, size, style)
{
    SetBackgroundColour(*wxWHITE);
}

bool WebViewHostDialog::create_webview(const std::string& resource_path,
                                       const wxString& title,
                                       const wxSize& dialog_size,
                                       const wxSize& min_size)
{
    SetTitle(title);
    SetMinSize(FromDIP(min_size));

    const wxString target_url = build_resource_url(resource_path);
    wxBoxSizer*    topsizer   = new wxBoxSizer(wxVERTICAL);

    m_browser = WebView::CreateWebView(this, target_url);
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        delete topsizer;
        return false;
    }

    SetSizer(topsizer);
    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    SetSize(FromDIP(dialog_size));
    CenterOnParent();

    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &WebViewHostDialog::on_script_message_event, this, m_browser->GetId());

    register_theme_user_scripts();
    m_browser->Bind(EVT_WEBVIEW_RECREATED, &WebViewHostDialog::on_webview_recreated, this);

    load_url(target_url);
    wxGetApp().UpdateDlgDarkUI(this);
    return true;
}

wxString WebViewHostDialog::build_resource_url(const std::string& resource_path) const
{
    wxString target_url = file_url_from_path(boost::filesystem::path(resources_dir()) / resource_path);

    if (append_language_to_url()) {
        const wxString lang = wxGetApp().current_language_code_safe();
        if (!lang.empty()) {
            target_url += wxT("?lang=");
            target_url += lang;
        }
    }

    return target_url;
}

void WebViewHostDialog::load_url(const wxString& url)
{
    if (!m_browser)
        return;

    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << " enter, url=" << into_u8(url);
    WebView::LoadUrl(m_browser, url);
    m_browser->SetFocus();
}

bool WebViewHostDialog::run_script(const wxString& script)
{
    if (!m_browser)
        return false;

    return WebView::RunScript(m_browser, script);
}

void WebViewHostDialog::call_web_handler(const nlohmann::json& payload, const wxString& handler)
{
    const wxString payload_text = wxString::FromUTF8(payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::ignore));
    const wxString script       = handler + wxT("(") + payload_text + wxT(")");

    wxGetApp().CallAfter([this, script] { run_script(script); });
}

bool WebViewHostDialog::handle_common_script_command(const nlohmann::json& payload, int close_return_code)
{
    const std::string command = payload.value("command", "");
    if (command == "close_page") {
        if (IsModal())
            EndModal(close_return_code);
        else
            Close();
        return true;
    }

    return false;
}

void WebViewHostDialog::on_dpi_changed(const wxRect&)
{
    Refresh();
}

void WebViewHostDialog::on_script_message_parse_error(const wxString& payload, const std::exception& error)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "; payload=" << into_u8(payload) << "; error=" << error.what();
}

void WebViewHostDialog::on_script_message_event(wxWebViewEvent& event)
{
    const wxString payload = event.GetString();

    try {
        on_script_message(nlohmann::json::parse(payload.utf8_string()));
    } catch (const std::exception& e) {
        on_script_message_parse_error(payload, e);
    }
}

void WebViewHostDialog::register_theme_user_scripts()
{
    if (!m_browser)
        return;
    m_browser->AddUserScript(wxString::FromUTF8(theme_user_script()));
    add_user_scripts();
}

void WebViewHostDialog::apply_theme_live()
{
    if (!m_browser)
        return;
    run_script(wxString::FromUTF8(host_theme_apply_js()));
}

void WebViewHostDialog::on_webview_recreated(wxCommandEvent&)
{
    apply_theme_live();
}

}}
