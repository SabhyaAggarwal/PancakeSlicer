#include "WebDialog.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Widgets/WebHosting.hpp"

#include <string>
#include "slic3r/GUI/Widgets/WebViewHostDialog.hpp"
#include <wx/chartype.h>
#include <wx/event.h>

#include <utility>
#include <wx/string.h>
#include <wx/gdicmn.h>
#include <wx/webview.h>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

namespace Slic3r { namespace GUI {

WebDialog::WebDialog(wxWindow*          parent,
                     const wxString&    title,
                     const std::string& html,
                     const wxSize&      size,
                     MessageHandler     on_message,
                     SubmitHandler      on_submit,
                     CloseHandler       on_close,
                     CloseHandler       on_destroyed,
                     long               wx_style)
    : WebViewHostDialog(parent, wxID_ANY, title, wxDefaultPosition, size, wx_style)
    , m_html(html)
    , m_on_message(std::move(on_message))
    , m_on_submit(std::move(on_submit))
    , m_on_close(std::move(on_close))
    , m_on_destroyed(std::move(on_destroyed))
{
    create_webview(web_hosting::BOOTSTRAP_PAGE, title, size, wxSize(320, 240));

    SetBackgroundColour(wxGetApp().get_window_default_clr());

    if (wxWebView* wv = browser()) {
        wv->SetBackgroundColour(wxGetApp().get_window_default_clr());
        Bind(wxEVT_WEBVIEW_LOADED, &WebDialog::on_bootstrap_event, this, wv->GetId());
        Bind(wxEVT_WEBVIEW_ERROR, &WebDialog::on_bootstrap_event, this, wv->GetId());
        Bind(wxEVT_WEBVIEW_NAVIGATED, &WebDialog::on_navigated, this, wv->GetId());
    }
    Bind(wxEVT_CLOSE_WINDOW, &WebDialog::on_close_window, this);
}

void WebDialog::use_normal_window_type()
{
#ifdef __WXGTK__
    gtk_window_set_type_hint(GTK_WINDOW(m_widget), GDK_WINDOW_TYPE_HINT_NORMAL);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(m_widget), TRUE);
#endif
}

void WebDialog::add_user_scripts()
{
    if (wxWebView* wv = browser()) {
        wv->AddUserScript(wxString::FromUTF8(WebViewHostDialog::element_defaults_user_script()));
        wv->AddUserScript(wxString::FromUTF8(web_hosting::orca_bridge_script()));
    }
}

WebDialog::~WebDialog()
{
    if (m_on_destroyed)
        m_on_destroyed();
}

void WebDialog::post_message(WebDialog* dialog, const nlohmann::json& data)
{
    if (dialog != nullptr && dialog->is_open())
        dialog->push_message(data);
}

void WebDialog::request_close(WebDialog* dialog)
{
    if (dialog != nullptr)
        dialog->Close();
}

void WebDialog::destroy_silently(WebDialog* dialog)
{
    if (dialog == nullptr)
        return;

    if (dialog->IsModal()) {
        dialog->m_open = false;
        dialog->EndModal(wxID_CANCEL);
    }
    dialog->Destroy();
}

void WebDialog::on_bootstrap_event(wxWebViewEvent& event)
{
    const bool loaded = event.GetEventType() == wxEVT_WEBVIEW_LOADED;
    if (!m_content_loaded)
        load_page_html();
    else if (web_hosting::is_content_url(event.GetURL())) {
        if (m_own_page_load)
            m_own_page_load = false;
        else if (loaded && m_content_navigated)
            load_page_html();
    }
    if (loaded)
        m_content_navigated = false;
    event.Skip();
}

void WebDialog::on_navigated(wxWebViewEvent& event)
{
    m_content_navigated = web_hosting::is_content_url(event.GetURL());
    event.Skip();
}

void WebDialog::load_page_html()
{
    m_content_loaded = true;
    if (wxWebView* wv = browser()) {
        m_own_page_load = true;
        wv->SetPage(wxString::FromUTF8(m_html), web_hosting::content_base_url());
    }
}

void WebDialog::on_script_message(const nlohmann::json& payload)
{
    if (payload.value("channel", std::string()) == "orca") {
        const std::string    kind = payload.value("kind", std::string());
        const nlohmann::json data = payload.contains("data") ? payload["data"] : nlohmann::json();
        if (kind == "message") {
            if (m_on_message)
                m_on_message(data);
        } else if (kind == "submit") {
            finish(true, data);
        } else if (kind == "close") {
            finish(false, nlohmann::json());
        }
        return;
    }

    handle_common_script_command(payload);
}

void WebDialog::push_message(const nlohmann::json& data)
{
    if (!m_open)
        return;
    nlohmann::json envelope;
    envelope["data"] = data;
    call_web_handler(envelope, wxT("__orcaDispatch"));
}

void WebDialog::finish(bool submitted, const nlohmann::json& data)
{
    if (!m_open)
        return;
    m_open = false;
    if (submitted) {
        m_result = data;
        fire_submit(data);
    } else {
        m_result.reset();
        fire_close();
    }

    if (IsModal())
        EndModal(submitted ? wxID_OK : wxID_CANCEL);
    else
        Close();
}

void WebDialog::on_close_window(wxCloseEvent&)
{
    if (!m_open) {
        if (!IsModal())
            Destroy();
        return;
    }

    m_open = false;
    m_result.reset();
    fire_close();
    if (IsModal()) {
        EndModal(wxID_CANCEL);
        return;
    }
    Destroy();
}

void WebDialog::fire_submit(const nlohmann::json& data)
{
    if (m_on_submit) {
        SubmitHandler cb = std::move(m_on_submit);
        cb(data);
    }
}

void WebDialog::fire_close()
{
    if (m_close_fired)
        return;
    m_close_fired = true;
    if (m_on_close) {
        CloseHandler cb = m_on_close;
        m_on_close      = nullptr;
        cb();
    }
}

}}
