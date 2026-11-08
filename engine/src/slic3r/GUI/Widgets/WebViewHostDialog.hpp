#ifndef slic3r_GUI_Widgets_WebViewHostDialog_hpp_
#define slic3r_GUI_Widgets_WebViewHostDialog_hpp_

#include <nlohmann/json.hpp>
#include <slic3r/GUI/GUI_Utils.hpp>

#include <exception>
#include <string>

#include <wx/event.h>
#include <wx/chartype.h>
#include <wx/gdicmn.h>
#include <wx/string.h>
#include <wx/toplevel.h>
#include <wx/webview.h>

class wxCommandEvent;
class wxWebView;
class wxWebViewEvent;
class wxWindow;

namespace Slic3r { namespace GUI {

class WebViewHostDialog : public Slic3r::GUI::DPIDialog
{
public:
    WebViewHostDialog(wxWindow* parent,
                      wxWindowID id         = wxID_ANY,
                      const wxString& title = wxT(""),
                      const wxPoint& pos    = wxDefaultPosition,
                      const wxSize& size    = wxDefaultSize,
                      long style            = wxSYSTEM_MENU | wxCAPTION | wxCLOSE_BOX | wxMAXIMIZE_BOX | wxRESIZE_BORDER);
    ~WebViewHostDialog() override = default;

    bool create_webview(const std::string& resource_path,
                        const wxString& title,
                        const wxSize& dialog_size = wxSize(820, 660),
                        const wxSize& min_size    = wxSize(640, 640));

    void load_url(const wxString& url);
    bool run_script(const wxString& script);
    void call_web_handler(const nlohmann::json& payload, const wxString& handler = wxT("HandleStudio"));

    static std::string document_start_injector(const std::string& markup,
                                               const char*        dom_id,
                                               const char*        position,
                                               const std::string& prelude   = {},
                                               const std::string& on_inject = {});

    static std::string theme_user_script();
    static std::string element_defaults_user_script();
    static std::string theme_apply_script();

protected:
    wxWebView* browser() const { return m_browser; }

    wxString build_resource_url(const std::string& resource_path) const;
    bool handle_common_script_command(const nlohmann::json& payload, int close_return_code = wxID_CANCEL);

    void on_dpi_changed(const wxRect& suggested_rect) override;

    virtual void on_script_message(const nlohmann::json& payload) = 0;
    virtual void on_script_message_parse_error(const wxString& payload, const std::exception& error);
    virtual bool append_language_to_url() const { return true; }

    void register_theme_user_scripts();

    virtual void add_user_scripts() {}

    void apply_theme_live();

private:
    void on_script_message_event(wxWebViewEvent& event);
    void on_webview_recreated(wxCommandEvent& event);

    wxWebView* m_browser{nullptr};
};

}}

#endif
