#include "SpeedDialDialog.hpp"

#include "ActionRegistry.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "NotificationManager.hpp"
#include "Plater.hpp"
#include "Widgets/WebViewHostDialog.hpp"

#include "slic3r/Utils/MacDarkMode.hpp"

#include <algorithm>

#include "libslic3r/Config.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoBase.hpp"
#include <string>
#include <wx/chartype.h>
#include <atomic>
#include <vector>
#include <utility>
#include <wx/dcmemory.h>
#include <wx/display.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/frame.h>
#include <wx/nonownedwnd.h>
#include <wx/region.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/utils.h>
#include <wx/webview.h>

#ifdef __linux__
#include <gtk/gtk.h>
#endif

namespace Slic3r { namespace GUI {

namespace {

constexpr int kPopupWidth     = 540;
constexpr int kPopupMinHeight = 60;
constexpr int kPopupMaxHeight = 282;

int json_int_or(const nlohmann::json& j, const char* key, int fallback)
{
    auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<int>() : fallback;
}

wxString mode_label(ConfigOptionMode mode)
{
    switch (mode) {
    case comAdvanced: return _L_CONTEXT("Advanced", "Settings Mode");
    case comExpert: return _L_CONTEXT("Expert", "Settings Mode");
    case comDevelop: return _L_CONTEXT("Developer", "Settings Mode");
    default: return _L_CONTEXT("Simple", "Settings Mode");
    }
}

wxColour bg_color() { return wxGetApp().get_window_default_clr(); }

void focus_webview(wxWebView* browser, bool page_ready)
{
    if (!browser)
        return;
#ifdef __linux__
    if (void* nb = browser->GetNativeBackend())
        gtk_widget_grab_focus((GtkWidget*) nb);
#else
    browser->SetFocus();
#endif
    if (page_ready)
        browser->RunScript("focusInput();");
}

nlohmann::json speed_dial_ui_strings()
{
    const std::string alt  = GUI::shortkey_alt_prefix();
    const std::string ctrl = GUI::shortkey_ctrl_prefix();
    return {
        {"shortcut_alt", alt},
        {"shortcut_ctrl", ctrl},

        {"sd_search", _u8L("Search actions")},
        {"sd_clear", _u8L("Clear")},
        {"sd_recent", _u8L("Recent")},
        {"sd_plugins", _u8L("Plugins")},
        {"sd_other", _u8L("Other")},
        {"sd_no_match_total", _u8L("No actions match (Total: %s)")},
        {"sd_no_actions", _u8L("No actions yet")},
        {"sd_no_tabs_match", _u8L("No tabs match")},
        {"sd_no_tabs", _u8L("No tabs")},
        {"sd_result_count", _u8L("Matching actions: %s")},
        {"sd_result_count_all", _u8L("Actions: %s")},
        {"sd_tab_count", _u8L("Tabs: %s")},
        {"sd_tab_match_count", _u8L("Matching tabs: %s")},
        {"sd_favs_full", _u8L("Favorites are full (%s max)")},
        {"sd_go_to_pct", _u8L("Go to %s%% of the layer range")},
        {"sd_enter_pct", _u8L("Enter a layer percentage (0-100)")},
        {"sd_go_layer_ph", _u8L("Go to layer %% (0-100)")},
        {"sd_go_tab_ph", _u8L("Go to tab")},
        {"sd_fav_slot", _u8L("Favorite %s (%s)")},
        {"sd_pin_fav", _u8L("Pin to favorites (%s)")},
        {"sd_unpin_fav", _u8L("Unpin from favorites (%s)")},
        {"sd_remove_fav", _u8L("Remove from favorites")},
        {"sd_move_left", _u8L("Move left")},
        {"sd_move_right", _u8L("Move right")},
        {"sd_unpin", _u8L("Unpin")},
        {"sd_mode_advanced", _u8L_CONTEXT("Advanced", "Settings Mode")},
        {"sd_mode_expert", _u8L_CONTEXT("Expert", "Settings Mode")},
        {"sd_mode_develop", _u8L_CONTEXT("Developer", "Settings Mode")},
        {"sd_wiki_f1", _u8L("Wiki (F1)")},
        {"sd_no_wiki", _u8L("No wiki page for this action")},
        {"sd_show_details", _u8L("Show details")},
        {"sd_hide_details", _u8L("Hide details")},
    };
}

}

SpeedDialWebDialog::SpeedDialWebDialog(wxWindow* parent)
    : WebViewHostDialog(parent,
                        wxID_ANY,
                        wxEmptyString,
                        wxDefaultPosition,
                        wxDefaultSize,
                        wxBORDER_NONE | wxFRAME_NO_TASKBAR | wxFRAME_FLOAT_ON_PARENT | wxFRAME_SHAPED)
{
    SetBackgroundColour(bg_color());
    Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
        if (event.GetActive() && IsShown())
            focus_webview(browser(), m_page_ready);
        else if (!event.GetActive() && IsShown())
            Hide();
        event.Skip();
    });
    if (!create_webview("web/dialog/SpeedDial/index.html", wxEmptyString, wxSize(kPopupWidth, kPopupMaxHeight),
                        wxSize(kPopupWidth, kPopupMinHeight))) {
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(new wxStaticText(this, wxID_ANY, wxS("wxWebView unavailable")), wxSizerFlags().Border(wxALL, 20));
        SetSizer(sizer);
        SetClientSize(FromDIP(wxSize(kPopupWidth, kPopupMinHeight)));
    }
    if (wxWebView* wv = browser())
        wv->EnableBrowserAcceleratorKeys(false);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        apply_rounded_shape();
    });
    apply_rounded_shape();
}

SpeedDialWebDialog::~SpeedDialWebDialog() { m_alive->store(false, std::memory_order_release); }

void SpeedDialWebDialog::add_user_scripts()
{
    if (wxWebView* wv = browser()) {
        const std::string js = "window.ORCA_UI_STRINGS = " +
                               speed_dial_ui_strings().dump(-1, ' ', false, nlohmann::json::error_handler_t::ignore) + ";";
        wv->AddUserScript(wxString::FromUTF8(js));
    }
}

void SpeedDialWebDialog::request_show()
{
    if (IsShown()) {
        Raise();
        focus_webview(browser(), m_page_ready);
        repaint_webview();
        return;
    }

    Show();
    Raise();
    apply_rounded_shape();
    if (m_page_ready)
        send_actions();
    focus_webview(browser(), m_page_ready);
    repaint_webview();
}

void SpeedDialWebDialog::on_script_message(const nlohmann::json& payload)
{
    if (handle_common_script_command(payload))
        return;

    wxGetApp().CallAfter([this, alive = m_alive, payload]() {
        if (alive->load(std::memory_order_acquire))
            handle_web_command(payload);
    });
}

void SpeedDialWebDialog::handle_web_command(const nlohmann::json& payload)
{
    const std::string command = payload.value("command", "");
    if (command == "request_actions") {
        m_page_ready = true;
        send_actions();
    } else if (command == "toggle_favourite") {
        const std::string fav_id = payload.value("id", "");
        const bool ok            = wxGetApp().action_registry().set_favourite(fav_id, payload.value("fav", false));
        if (!ok)
            call_web_handler({{"command", "favourite_full"}, {"limit", (int) ActionRegistry::kFavLimit}, {"id", fav_id}});
    } else if (command == "reorder_favourites") {
        std::vector<std::string> ids;
        if (payload.contains("ids") && payload["ids"].is_array())
            for (const auto& id : payload["ids"])
                if (id.is_string())
                    ids.push_back(id.get<std::string>());
        wxGetApp().action_registry().reorder_favourites(ids);
    } else if (command == "set_tooltip_expanded")
        wxGetApp().action_registry().set_tooltip_expanded(payload.value("expanded", true));
    else if (command == "run_action")
        run_action(payload.value("id", ""), payload.value("title", ""), payload.value("param", ""));
    else if (command == "open_wiki")
        open_wiki(payload.value("id", ""));
    else if (command == "search_tabs")
        search_tabs();
    else if (command == "resize")
        resize_to_content(json_int_or(payload, "height", 0));
}

void SpeedDialWebDialog::search_tabs()
{
    wxGetApp().CallAfter([this, alive = m_alive]() {
        if (!alive->load(std::memory_order_acquire))
            return;
        auto tabs = wxGetApp().action_registry().tab_options();
        call_web_handler({{"command", "tab_results"}, {"tabs", std::move(tabs)}});
    });
}

void SpeedDialWebDialog::resize_to_content(int height)
{
    if (height <= 0)
        return;

    int display_index = wxDisplay::GetFromWindow(this);
    if (display_index == wxNOT_FOUND)
        display_index = 0;
    const int screen_dip = ToDIP(wxDisplay(display_index).GetClientArea().GetHeight());
    const int max_dip    = std::max(kPopupMinHeight, screen_dip * 85 / 100);
    const int height_dip = std::max(kPopupMinHeight, std::min(height, max_dip));
    SetClientSize(FromDIP(wxSize(kPopupWidth, height_dip)));
    Layout();
#ifdef __WXOSX__
    if (wxWebView* wv = browser())
        wv->SetSize(GetClientSize());
#endif
    apply_rounded_shape();
    repaint_webview();
}

void SpeedDialWebDialog::repaint_webview()
{
    wxWebView* wv = browser();
    if (!wv)
        return;
    wv->Refresh();
#ifdef __WXOSX__
    if (void* nb = wv->GetNativeBackend())
        WKWebView_force_display(nb);
    wv->Update();
#elif defined(__linux__)
    if (void* nb = wv->GetNativeBackend())
        gtk_widget_queue_draw((GtkWidget*) nb);
#else
    wv->Update();
#endif
}

void SpeedDialWebDialog::apply_rounded_shape()
{
    if (m_applying_shape)
        return;

    const wxSize size = GetClientSize();
    if (size.GetWidth() <= 0 || size.GetHeight() <= 0)
        return;

    m_applying_shape = true;

#ifdef __WXOSX__
    set_window_corner_radius(this, FromDIP(m_corner_radius));
#else
    m_shape_bmp.Create(size.GetWidth(), size.GetHeight(), 32);
    if (m_shape_bmp.IsOk()) {
        wxMemoryDC dc;
        dc.SelectObject(m_shape_bmp);
        dc.SetBackground(wxBrush(wxColour(0, 0, 0)));
        dc.Clear();
        dc.SetBrush(wxBrush(wxColour(255, 255, 255, 255)));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRoundedRectangle(0, 0, size.GetWidth(), size.GetHeight(), FromDIP(m_corner_radius));
        dc.SelectObject(wxNullBitmap);

        wxRegion region(m_shape_bmp, wxColour(0, 0, 0));
        if (region.IsOk())
            SetShape(region);
    }
#endif

    m_applying_shape = false;
}

void SpeedDialWebDialog::on_dpi_changed(const wxRect&)
{
    apply_rounded_shape();
    Refresh();
}

void SpeedDialWebDialog::run_action(const std::string& id, const std::string& title, const std::string& param)
{
    ActionRegistry& reg = wxGetApp().action_registry();
    const AppAction* a  = reg.by_id(id);
    if (!a)
        return;

    const bool ask                  = a->kind == AppActionKind::Plugin && reg.should_ask(id);
    const std::string atitle        = a->title();
    const ConfigOptionMode required = a->required_mode;

    if (requires_mode_switch(required, wxGetApp().get_mode())) {
        const wxString setting = title.empty() ? from_u8(atitle) : from_u8(title);
        if (required == comDevelop) {
            RichMessageDialog dlg(wxGetApp().mainframe,
                                  wxString::Format(_L("\"%s\" is a Developer setting. Enable Developer mode to edit it?"), setting),
                                  _L("Developer setting"), wxOK | wxCANCEL);
            if (dlg.ShowModal() != wxID_OK)
                return;
            wxGetApp().enable_developer_mode();
        } else {
            RichMessageDialog dlg(wxGetApp().mainframe,
                                  wxString::Format(_L("\"%s\" is a %s setting. Switch from %s mode to %s mode to edit it?"), setting,
                                                   mode_label(required), mode_label(wxGetApp().get_mode()), mode_label(required)),
                                  _L("Switch settings mode"), wxOK | wxCANCEL);
            if (dlg.ShowModal() != wxID_OK)
                return;
            wxGetApp().save_mode(required);
        }
    }

    if (IsModal())
        EndModal(wxID_CANCEL);
    else
        Hide();

    if (ask) {
        const wxString label = title.empty() ? from_u8(atitle) : from_u8(title);
        RichMessageDialog dlg(wxGetApp().mainframe, wxString::Format(_L("Run \"%s\"?"), label), _L("Run plugin"), wxOK | wxCANCEL);
        dlg.ShowCheckBox(_L("Don't ask again for this action"));
        if (dlg.ShowModal() != wxID_OK)
            return;
        if (dlg.IsCheckBoxChecked())
            wxGetApp().action_registry().suppress_ask(id);
    }

    wxGetApp().CallAfter([id, param] {
        if (wxGetApp().is_closing())
            return;
        AppActionRunResult result = wxGetApp().action_registry().run(id, param);
        if (result.level == AppActionRunResult::Level::Busy)
            return;
        if (!result.message.IsEmpty() && wxGetApp().plater())
            wxGetApp()
                .plater()
                ->get_notification_manager()
                ->push_notification(NotificationType::CustomNotification,
                                    result.level == AppActionRunResult::Level::Error ?
                                        NotificationManager::NotificationLevel::ErrorNotificationLevel :
                                        NotificationManager::NotificationLevel::RegularNotificationLevel,
                                    into_u8(result.message));
    });
}

void SpeedDialWebDialog::open_wiki(const std::string& id)
{
    const AppAction* a = wxGetApp().action_registry().by_id(id);
    if (!a || a->help_url.empty())
        return;
    Hide();
    wxLaunchDefaultBrowser(from_u8(a->help_url));
}

void SpeedDialWebDialog::send_actions()
{
    nlohmann::json snap = wxGetApp().action_registry().snapshot();
    call_web_handler({{"command", "list_actions"},
                      {"actions", std::move(snap["actions"])},
                      {"favourites", std::move(snap["favourites"])},
                      {"recent", std::move(snap["recent"])},
                      {"user_mode", std::move(snap["user_mode"])},
                      {"tooltip_expanded", std::move(snap["tooltip_expanded"])}});
}

}}
