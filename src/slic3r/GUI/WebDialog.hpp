#ifndef slic3r_GUI_WebDialog_hpp_
#define slic3r_GUI_WebDialog_hpp_

#include "Widgets/WebViewHostDialog.hpp"

#include <functional>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>
#include <wx/string.h>
#include <wx/gdicmn.h>
#include <wx/toplevel.h>
#include <wx/event.h>
#include <wx/webview.h>

class wxCloseEvent;
class wxSize;
class wxWebViewEvent;
class wxWindow;

namespace Slic3r { namespace GUI {

class WebDialog : public Slic3r::GUI::WebViewHostDialog
{
public:
    using MessageHandler = std::function<void(const nlohmann::json& data)>;
    using SubmitHandler  = std::function<void(const nlohmann::json& data)>;
    using CloseHandler   = std::function<void()>;

    WebDialog(wxWindow*          parent,
              const wxString&    title,
              const std::string& html,
              const wxSize&      size,
              MessageHandler     on_message,
              SubmitHandler      on_submit,
              CloseHandler       on_close,
              CloseHandler       on_destroyed,
              long               wx_style = wxSYSTEM_MENU | wxCAPTION | wxCLOSE_BOX | wxMAXIMIZE_BOX | wxRESIZE_BORDER);
    ~WebDialog() override;

    static void post_message(WebDialog* dialog, const nlohmann::json& data);
    static void request_close(WebDialog* dialog);
    static void destroy_silently(WebDialog* dialog);

    void push_message(const nlohmann::json& data);

    bool is_open() const { return m_open; }

    void use_normal_window_type();

    const std::optional<nlohmann::json>& result() const { return m_result; }

protected:
    void on_script_message(const nlohmann::json& payload) override;
    bool append_language_to_url() const override { return false; }
    void add_user_scripts() override;

private:
    void on_bootstrap_event(wxWebViewEvent& event);
    void on_navigated(wxWebViewEvent& event);
    void load_page_html();
    void on_close_window(wxCloseEvent& event);
    void fire_submit(const nlohmann::json& data);
    void fire_close();
    void finish(bool submitted, const nlohmann::json& data);

    std::string                   m_html;
    bool                          m_content_loaded{false};
    bool                          m_own_page_load{false};
    bool                          m_content_navigated{false};
    bool                          m_open{true};
    bool                          m_close_fired{false};
    std::optional<nlohmann::json> m_result;
    MessageHandler                m_on_message;
    SubmitHandler                 m_on_submit;
    CloseHandler                  m_on_close;
    CloseHandler                  m_on_destroyed;
};

}}

#endif
