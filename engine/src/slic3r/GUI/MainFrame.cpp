#include "MainFrame.hpp"

#include <wx/event.h>
#include "slic3r/GUI/Event.hpp"
#include <wx/gdicmn.h>
#include <wx/dcclient.h>
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include <wx/filehistory.h>
#include "slic3r/GUI/BBLTopbar.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include <boost/lexical_cast.hpp>
#include <optional>
#include "slic3r/GUI/GLToolbar.hpp"
#include "libslic3r/Preset.hpp"
#include <functional>
#include <wx/busycursor.h>
#include <cstddef>
#include "slic3r/GUI/Project.hpp"
#include <wx/bookctrl.h>
#include "slic3r/GUI/LazyPage.hpp"
#include "slic3r/GUI/Monitor.hpp"
#include "slic3r/GUI/PrinterWebView.hpp"
#include "slic3r/GUI/MultiMachinePage.hpp"
#include "slic3r/GUI/CalibrationPanel.hpp"
#include "slic3r/GUI/ReleaseNote.hpp"
#include "slic3r/GUI/GUI.hpp"
#include <vector>
#include "libslic3r/PublishSettings.hpp"
#include "libslic3r/Config.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include <wx/colour.h>
#include "slic3r/GUI/FilamentGroupPopup.hpp"
#include "slic3r/GUI/Widgets/SideMenuPopup.hpp"
#include <utility>
#include "libslic3r/libslic3r.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <wx/dirdlg.h>
#include <exception>
#include <wx/filedlg.h>
#include "slic3r/GUI/Lazy.hpp"
#include <wx/filefn.h>
#include <string>
#include <sstream>
#include <wx/base64.h>
#include <ctime>
#include <boost/filesystem/operations.hpp>
#include "slic3r/GUI/calib_dlg.hpp"
#include "slic3r/GUI/NonplanarCalibDialog.hpp"
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/textentry.h>
#include <wx/notebook.h>
#include <wx/listbook.h>
#include <wx/simplebook.h>
#include <wx/icon.h>
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/progdlg.h>
#include <wx/tooltip.h>
#include <wx/filename.h>
#include <wx/debug.h>
#include <wx/toplevel.h>
#include <wx/utils.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <wx/webview.h>

#include "libslic3r/Print.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "Tab.hpp"
#include "ProgressStatusBar.hpp"
#include "3DScene.hpp"
#include "ParamsDialog.hpp"
#include "PrintHostDialogs.hpp"
#include "libslic3r_version.h"
#include "wxExtensions.hpp"
#include "GUI_ObjectList.hpp"
#include "Mouse3DController.hpp"
#include "InstanceCheck.hpp"
#include "I18N.hpp"
#include "GLCanvas3D.hpp"
#include "Plater.hpp"
#ifdef SLIC3R_CAD
#include "slic3r/GUI/CAD/DesignPanel.hpp"
#include "slic3r/GUI/CAD/McpControl.hpp"
#endif
#include "WebViewDialog.hpp"
#include "../Utils/Process.hpp"
#include "PartPlate.hpp"
#include "Preferences.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "Widgets/StaticBox.hpp"
#include "BindDialog.hpp"
#include "../Utils/MacDarkMode.hpp"
#include "../Utils/NetworkAgentFactory.hpp"
#include "../Utils/PrintHost.hpp"

#include "GUI_App.hpp"
#include "Shortcuts.hpp"
#include "UnsavedChangesDialog.hpp"
#include "PublishSettingsDialog.hpp"
#include "MsgDialog.hpp"
#include "Notebook.hpp"
#include "GUI_Factories.hpp"
#include "NotificationManager.hpp"
#include "MarkdownTip.hpp"
#include "NetworkTestDialog.hpp"
#include "SceneBenchmark.hpp"
#include "ConfigWizard.hpp"
#include "Widgets/WebView.hpp"
#include "DailyTips.hpp"
#include "FilamentMapDialog.hpp"

#include "DeviceCore/DevManager.h"

#ifdef _WIN32
#include <dbt.h>
#include <shlobj.h>
#include <shellapi.h>
#endif

#ifdef __WXGTK__
#include <gtk/gtk.h>
#include <wx/glcanvas.h>
#endif
#include <slic3r/GUI/CreatePresetsDialog.hpp>
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosManager.hpp"
#include "slic3r/GUI/IdleScheduler.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Jobs/Worker.hpp"
#include "slic3r/GUI/KeyChord.hpp"
#include "slic3r/GUI/ParamsPanel.hpp"
#include "slic3r/GUI/Tabbook.hpp"
#include "slic3r/GUI/Widgets/SideButton.hpp"
#include "slic3r/plugin/host/PluginPages.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"
#include <wx/defs.h>
#include <cassert>
#include <wx/display.h>
#include <wx/window.h>


using json = nlohmann::json;

namespace Slic3r {
namespace GUI {

wxDEFINE_EVENT(EVT_SELECT_TAB, wxCommandEvent);
wxDEFINE_EVENT(EVT_HTTP_ERROR, wxCommandEvent);
wxDEFINE_EVENT(EVT_USER_LOGIN, wxCommandEvent);
wxDEFINE_EVENT(EVT_USER_LOGIN_HANDLE, wxCommandEvent);
wxDEFINE_EVENT(EVT_CHECK_PRIVACY_VER, wxCommandEvent);
wxDEFINE_EVENT(EVT_CHECK_PRIVACY_SHOW, wxCommandEvent);
wxDEFINE_EVENT(EVT_SHOW_IP_DIALOG, wxCommandEvent);
wxDEFINE_EVENT(EVT_UPDATE_MACHINE_LIST, wxCommandEvent);
wxDEFINE_EVENT(EVT_UPDATE_PRESET_CB, SimpleEvent);



wxDEFINE_EVENT(EVT_BACKUP_POST, wxCommandEvent);
wxDEFINE_EVENT(EVT_LOAD_URL, wxCommandEvent);
wxDEFINE_EVENT(EVT_LOAD_PRINTER_URL, LoadPrinterViewEvent);

enum class ERescaleTarget
{
    Mainframe,
    SettingsDialog
};

namespace {

// fall through to "open"; the Notebook itself does too, so Space still opens the dial on any page.
bool focus_keeps_space(wxWindow* focus)
{
    if (!focus)
        return false;
    if (dynamic_cast<wxTextEntryBase*>(focus))
        return true;
    if (dynamic_cast<wxWebView*>(focus))
        return true;
    if (dynamic_cast<::Button*>(focus))
        return true;
    if (dynamic_cast<StaticBox*>(focus))
        return true;
    if (dynamic_cast<wxControl*>(focus) && !dynamic_cast<Notebook*>(focus))
        return true;
    return false;
}

}

#ifdef __WXGTK__
class ResizeEdgePanel : public wxPanel
{
public:
    enum Edge { Bottom, Left, Right };

    static constexpr int BORDER_PX = 5;

    ResizeEdgePanel(MainFrame* frame, Edge edge)
        : wxPanel(frame, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                  wxBORDER_NONE)
        , m_frame(frame)
        , m_edge(edge)
    {
        SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
        Bind(wxEVT_MOTION,       &ResizeEdgePanel::OnCursorUpdate, this);
        Bind(wxEVT_ENTER_WINDOW, &ResizeEdgePanel::OnCursorUpdate, this);
        Bind(wxEVT_LEFT_DOWN,    &ResizeEdgePanel::OnLeftDown,     this);
        Bind(wxEVT_LEAVE_WINDOW, &ResizeEdgePanel::OnLeave,        this);
        Bind(wxEVT_PAINT,        &ResizeEdgePanel::OnPaint,        this);
    }

private:
    void OnPaint(wxPaintEvent&)
    {
        wxPaintDC dc(this);
    }

    GdkWindowEdge get_gdk_edge(const wxPoint& pos) const
    {
        wxSize size = GetSize();
        switch (m_edge) {
        case Bottom:
            if (pos.x < BORDER_PX)           return GDK_WINDOW_EDGE_SOUTH_WEST;
            if (pos.x > size.x - BORDER_PX)  return GDK_WINDOW_EDGE_SOUTH_EAST;
            return GDK_WINDOW_EDGE_SOUTH;
        case Left:
            if (pos.y < BORDER_PX)            return GDK_WINDOW_EDGE_NORTH_WEST;
            if (pos.y > size.y - BORDER_PX)   return GDK_WINDOW_EDGE_SOUTH_WEST;
            return GDK_WINDOW_EDGE_WEST;
        case Right:
            if (pos.y < BORDER_PX)            return GDK_WINDOW_EDGE_NORTH_EAST;
            if (pos.y > size.y - BORDER_PX)   return GDK_WINDOW_EDGE_SOUTH_EAST;
            return GDK_WINDOW_EDGE_EAST;
        }
        return GDK_WINDOW_EDGE_SOUTH;
    }

    void OnCursorUpdate(wxMouseEvent& evt)
    {
        GdkWindowEdge edge = get_gdk_edge(evt.GetPosition());
        const char* name;
        switch (edge) {
        case GDK_WINDOW_EDGE_NORTH:       name = "n-resize";  break;
        case GDK_WINDOW_EDGE_SOUTH:       name = "s-resize";  break;
        case GDK_WINDOW_EDGE_WEST:        name = "w-resize";  break;
        case GDK_WINDOW_EDGE_EAST:        name = "e-resize";  break;
        case GDK_WINDOW_EDGE_NORTH_WEST:  name = "nw-resize"; break;
        case GDK_WINDOW_EDGE_NORTH_EAST:  name = "ne-resize"; break;
        case GDK_WINDOW_EDGE_SOUTH_WEST:  name = "sw-resize"; break;
        case GDK_WINDOW_EDGE_SOUTH_EAST:  name = "se-resize"; break;
        default:                          name = "s-resize";  break;
        }
        if (name == m_last_cursor_name) return;
        m_last_cursor_name = name;

        GdkDisplay* display = gtk_widget_get_display(m_widget);
        GdkCursor*  cursor  = gdk_cursor_new_from_name(display, name);
        if (cursor) {
            gdk_window_set_cursor(gtk_widget_get_window(m_widget), cursor);
            g_object_unref(cursor);
        }
    }

    void OnLeave(wxMouseEvent&)
    {
        m_last_cursor_name = nullptr;
        gdk_window_set_cursor(gtk_widget_get_window(m_widget), nullptr);
    }

    void OnLeftDown(wxMouseEvent& evt)
    {
        if (m_frame->IsMaximized() || m_frame->IsFullScreen())
            return;

        GdkWindowEdge edge = get_gdk_edge(evt.GetPosition());
        wxPoint mouse = ClientToScreen(evt.GetPosition());

        gtk_window_begin_resize_drag(
            GTK_WINDOW(m_frame->m_widget),
            edge,
            1,
            mouse.x, mouse.y,
            gtk_get_current_event_time());
    }

    MainFrame*    m_frame;
    Edge          m_edge;
    const char*   m_last_cursor_name{nullptr};
};
#endif

#ifdef __APPLE__
class OrcaSlicerTaskBarIcon : public wxTaskBarIcon
{
public:
    OrcaSlicerTaskBarIcon(wxTaskBarIconType iconType = wxTBI_DEFAULT_TYPE) : wxTaskBarIcon(iconType) {}
    wxMenu *CreatePopupMenu() override {
        wxMenu *menu = new wxMenu;
        if (wxGetApp().app_config->get("single_instance") == "false") {
            append_menu_item(menu, wxID_ANY, _L("New Window"), _L("Open a new window"),
            [](wxCommandEvent&) { start_new_slicer(); }, "", nullptr);
        }
        return menu;
    }
};
#endif
static wxIcon main_frame_icon(GUI_App::EAppMode app_mode)
{
#if _WIN32
    std::wstring path(size_t(MAX_PATH), wchar_t(0));
    int len = int(::GetModuleFileName(nullptr, path.data(), MAX_PATH));
    if (len > 0 && len < MAX_PATH) {
        path.erase(path.begin() + len, path.end());
    }
    return wxIcon(path, wxBITMAP_TYPE_ICO);
#else
    return wxIcon(Slic3r::var("OrcaSlicer_128px.png"), wxBITMAP_TYPE_PNG);
#endif
}

#ifndef __APPLE__
#define BORDERLESS_FRAME_STYLE (wxRESIZE_BORDER | wxMINIMIZE_BOX | wxMAXIMIZE_BOX | wxCLOSE_BOX)
#else
#define BORDERLESS_FRAME_STYLE (wxMINIMIZE_BOX | wxMAXIMIZE_BOX | wxCLOSE_BOX)
#endif

wxDEFINE_EVENT(EVT_SYNC_CLOUD_PRESET,     SimpleEvent);

MainFrame::MainFrame() :
DPIFrame(NULL, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, BORDERLESS_FRAME_STYLE, "mainframe")
    , m_printhost_queue_dlg(new PrintHostQueueDialog(this))
    , m_recent_projects(18)
    , m_settings_dialog(this)
    , m_idle([] { return wxGetApp().input_idle_ms(); })
    , m_diff_dialog("compare_presets", 100, [this] { return make_diff_dialog(); })
{
#ifdef __WXOSX__
    set_miniaturizable(GetHandle());
#endif

#ifdef __WXGTK__
    m_gdkDecor = 0;

    m_edge_bottom = new ResizeEdgePanel(this, ResizeEdgePanel::Bottom);
    m_edge_left   = new ResizeEdgePanel(this, ResizeEdgePanel::Left);
    m_edge_right  = new ResizeEdgePanel(this, ResizeEdgePanel::Right);
#endif

#ifdef __WXMSW__
    if (HWND hWnd = GetHandle(); hWnd != nullptr) {
        LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
        if ((style & WS_CAPTION) != 0) {
            SetWindowLongPtr(hWnd, GWL_STYLE, style & ~WS_CAPTION);
            SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
                         SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
    }
#endif

    if (!wxGetApp().app_config->has("user_mode")) {
        wxGetApp().app_config->set("user_mode", "simple");
        wxGetApp().app_config->set_bool("developer_mode", false);
        wxGetApp().app_config->save();
    }

    wxGetApp().app_config->set_bool("internal_developer_mode", false);

    wxString max_recent_count_str = wxGetApp().app_config->get("max_recent_count");
    long max_recent_count = 18;
    if (max_recent_count_str.ToLong(&max_recent_count))
        set_max_recent_count((int)max_recent_count);

    auto loglevel = wxGetApp().app_config->get("log_severity_level");
    Slic3r::set_logging_level(Slic3r::level_string_to_boost(loglevel));

    m_recent_projects.SetMenuPathStyle(wxFH_PATH_SHOW_ALWAYS);
    MarkdownTip::Recreate(this);

    wxGetApp().update_fonts(this);

#ifndef __APPLE__
    m_topbar         = new BBLTopbar(this);
#else
    auto panel_topbar = new wxPanel(this, wxID_ANY);
    panel_topbar->SetBackgroundColour(wxColour(38, 46, 48));
    auto sizer_tobar = new wxBoxSizer(wxVERTICAL);
    panel_topbar->SetSizer(sizer_tobar);
    panel_topbar->Layout();
#endif

#ifdef __APPLE__
	m_reset_title_text_colour_timer = new wxTimer();
	m_reset_title_text_colour_timer->SetOwner(this);
	Bind(wxEVT_TIMER, [this](auto& e) {
		set_title_colour_after_set_title(GetHandle());
		m_reset_title_text_colour_timer->Stop();
	});
	this->Bind(wxEVT_FULLSCREEN, [this](wxFullScreenEvent& e) {
		set_tag_when_enter_full_screen(e.IsFullScreen());
		if (!e.IsFullScreen()) {
            if (m_reset_title_text_colour_timer) {
                m_reset_title_text_colour_timer->Stop();
                m_reset_title_text_colour_timer->Start(500);
            }
            m_mac_fullscreen = false;
        } else {
            m_mac_fullscreen = true;
        }
        auto int_event = new IntEvent(EVT_NOTICE_FULL_SCREEN_CHANGED, e.IsFullScreen() ? 1 : 0);
        wxQueueEvent(wxGetApp().plater(), int_event);
		e.Skip();
	});
#endif

#ifdef __APPLE__
    switch (wxGetApp().get_app_mode()) {
    default:
    case GUI_App::EAppMode::Editor:
        m_taskbar_icon = std::make_unique<OrcaSlicerTaskBarIcon>(wxTBI_DOCK);
        m_taskbar_icon->SetIcon(wxIcon(Slic3r::var("OrcaSlicer-mac_256px.ico"), wxBITMAP_TYPE_ICO), "OrcaSlicer");
        break;
    case GUI_App::EAppMode::GCodeViewer:
        break;
    }
#endif
    SetIcon(main_frame_icon(wxGetApp().get_app_mode()));

    init_tabpanel();
    if (wxGetApp().is_gcode_viewer())
        init_menubar_as_gcodeviewer();
    else
        init_menubar_as_editor();

#if 0
    wxAcceleratorEntry entries[6];
    entries[0].Set(wxACCEL_CTRL, WXK_NUMPAD1, wxID_HIGHEST + 1);
    entries[1].Set(wxACCEL_CTRL, WXK_NUMPAD2, wxID_HIGHEST + 2);
    entries[2].Set(wxACCEL_CTRL, WXK_NUMPAD3, wxID_HIGHEST + 3);
    entries[3].Set(wxACCEL_CTRL, WXK_NUMPAD4, wxID_HIGHEST + 4);
    entries[4].Set(wxACCEL_CTRL, WXK_NUMPAD5, wxID_HIGHEST + 5);
    entries[5].Set(wxACCEL_CTRL, WXK_NUMPAD6, wxID_HIGHEST + 6);
    wxAcceleratorTable accel(6, entries);
    SetAcceleratorTable(accel);
#endif
    Bind(wxEVT_SIZE, [this](wxSizeEvent&) {
            BOOST_LOG_TRIVIAL(trace) << "mainframe: size changed, is maximized = " << this->IsMaximized();
#ifndef __APPLE__
            if (this->IsMaximized()) {
                m_topbar->SetWindowSize();
            } else {
                m_topbar->SetMaximizedSize();
            }
#endif
        Refresh();
        Layout();
#ifdef __WXGTK__
        update_edge_panels();
#endif
        wxQueueEvent(wxGetApp().plater(), new SimpleEvent(EVT_NOTICE_CHILDE_SIZE_CHANGED));

        fit_tab_labels();
        if (m_prebuild_started)
            m_idle.start();
    });

    Bind(EVT_SELECT_TAB, [this](wxCommandEvent& evt) {
        m_tabpanel->SelectPageByName(evt.GetString());
    });

    Bind(EVT_SYNC_CLOUD_PRESET, &MainFrame::on_select_default_preset, this);

    wxToolTip::SetAutoPop(32767);

    m_loaded = true;

    m_main_sizer = new wxBoxSizer(wxVERTICAL);
    wxSizer* sizer = new wxBoxSizer(wxVERTICAL);
#ifndef __APPLE__
     sizer->Add(m_topbar, 0, wxEXPAND);
#else
     sizer->Add(panel_topbar, 0, wxEXPAND);
#endif


    sizer->Add(m_main_sizer, 1, wxEXPAND);
    SetSizerAndFit(sizer);
    update_layout();
    sizer->SetSizeHints(this);

    #ifdef __WXMSW__
    this->Bind(wxEVT_MAXIMIZE, [this](auto &e) {
        wxDisplay display(this);
        auto      size = display.GetClientArea().GetSize();
        auto      pos  = display.GetClientArea().GetPosition();
        HWND      hWnd = GetHandle();
        RECT      borderThickness;
        SetRectEmpty(&borderThickness);
        AdjustWindowRectEx(&borderThickness, GetWindowLongPtr(hWnd, GWL_STYLE), FALSE, 0);
        const auto max_size = size + wxSize{-borderThickness.left + borderThickness.right, -borderThickness.top + borderThickness.bottom};
        const auto current_size = GetSize();
        SetSize({std::min(max_size.x, current_size.x), std::min(max_size.y, current_size.y)});
        Move(pos + wxPoint{borderThickness.left, borderThickness.top});
        e.Skip();
    });

#endif
    Fit();

    const wxSize min_size = wxGetApp().get_min_size();

    SetMinSize(min_size );
    SetSize(wxSize(FromDIP(1200), FromDIP(800)));

    Layout();

    update_title();

    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< ": mainframe received close_widow event";
        if (event.CanVeto() && m_plater->get_view3D_canvas3D()->get_gizmos_manager().is_in_editing_mode(true)) {
            event.Veto();
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< "cancelled by gizmo in editing";
            return;
        }

        auto check = [](bool yes_or_no) {
            if (yes_or_no)
                return true;
            return wxGetApp().check_and_save_current_preset_changes(_L("Closing application"), _L("Closing Application while some presets are modified."));
        };

        int result;
        if (event.CanVeto() && ((result = m_plater->close_with_confirm(check)) == wxID_CANCEL)) {
            event.Veto();
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< "cancelled by close_with_confirm selection";
            return;
        }
        if (event.CanVeto() && !wxGetApp().check_print_host_queue()) {
            event.Veto();
            return;
        }

    #if 0
    #endif

        MarkdownTip::ExitTip();
        wxGetApp().set_closing(true);

        m_plater->reset();
        this->shutdown();

        wxGetApp().remove_mall_system_dialog();
        event.Skip();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< ": mainframe finished process close_widow event";
    });

    Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
        if (m_plater != nullptr && event.GetActive())
            m_plater->on_activate();
        event.Skip();
    });

#if __APPLE__
    Bind(wxEVT_MOVE, [](wxMoveEvent& event) {
        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
        wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
        event.Skip();
    });
#endif

    update_ui_from_settings();

    if (m_plater != nullptr) {
        update_slice_print_status(eEventSliceUpdate, true, true);

        if (wxGetApp().app_config->get("backup_switch") == "true") {
            std::string backup_interval;
            if (!wxGetApp().app_config->get("app", "backup_interval", backup_interval))
                backup_interval = "10";
            Slic3r::set_backup_interval(boost::lexical_cast<long>(backup_interval));
        } else {
            Slic3r::set_backup_interval(0);
        }
        Slic3r::set_backup_callback([this](int action) {
            if (action == 0) {
                wxPostEvent(this, wxCommandEvent(EVT_BACKUP_POST));
            }
            else if (action == 1) {
                if (!m_plater->up_to_date(false, true)) {
                    m_plater->export_3mf(m_plater->model().get_backup_path() + "/.3mf", SaveStrategy::Backup);
                    m_plater->up_to_date(true, true);
                }
            }
         });
        Bind(EVT_BACKUP_POST, [](wxCommandEvent& e) {
            Slic3r::run_backup_ui_tasks();
            });
;    }
    this->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &evt) {
#ifdef __APPLE__
        if (evt.CmdDown() && (evt.GetKeyCode() == 'H')) {
            return;}
        if (evt.CmdDown() && (!evt.ShiftDown()) && (evt.GetKeyCode() == 'M')) {
            this->Iconize();
            return;
        }
        if (evt.CmdDown() && evt.GetKeyCode() == 'Q') { wxPostEvent(this, wxCloseEvent(wxEVT_CLOSE_WINDOW)); return;}
        if (evt.CmdDown() && evt.RawControlDown() && evt.GetKeyCode() == 'F') {
            EnableFullScreenView(true);
            if (IsFullScreen()) {
                ShowFullScreen(false);
            } else {
                ShowFullScreen(true);
            }
            return;}
#endif
        if (!handle_global_shortcut(KeyChord::from_event(evt)))
            evt.Skip();
    });

    Bind(wxEVT_SHOW, [](wxShowEvent &evt) {
        DeviceManager *manger = wxGetApp().getDeviceManager();
        if (manger) {
            evt.IsShown() ? manger->start_refresher() : manger->stop_refresher();
        }
    });

#ifdef _MSW_DARK_MODE
    wxGetApp().UpdateDarkUIWin(this);
#endif

    wxGetApp().persist_window_geometry(this, true);
    wxGetApp().persist_window_geometry(&m_settings_dialog, true);
}

bool MainFrame::handle_global_shortcut(const KeyChord& chord)
{
    const std::optional<Shortcut> shortcut = wxGetApp().shortcuts().lookup(ShortcutContext::Global, chord);
    if (!shortcut.has_value())
        return false;

    switch (*shortcut) {
    case Shortcut::SlicePlate:
        if (m_slice_enable) {
            wxGetApp().plater()->update(true, true);
            wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SLICE_PLATE));
            m_tabpanel->SelectPageByName(TAB_ID_PREVIEW);
        }
        break;
    case Shortcut::PrintPlate:
        m_plater->apply_background_progress();
        m_print_enable = get_enable_print_status();
        m_print_btn->Enable(m_print_enable);
        if (m_print_enable) {
            if (wxGetApp().preset_bundle->use_bbl_network() || wxGetApp().app_config->get_bool("use_printer_agents"))
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_PRINT_PLATE));
            else
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SEND_GCODE));
        }
        return false;
    case Shortcut::ExportSlicedFile:
        if (can_export_gcode())
            wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_SLICED_FILE));
        return false;
    case Shortcut::PrintHostQueue: m_printhost_queue_dlg->Show(); break;
    case Shortcut::SpeedDial:
        if (!wxGetApp().app_config->get_bool("enable_speed_dial") || (chord == KeyChord{ WXK_SPACE } && focus_keeps_space(wxWindow::FindFocus())))
            return false;
        CallAfter([] { wxGetApp().open_speed_dial(); });
        break;
    case Shortcut::NewProject: m_plater->new_project(); break;
    case Shortcut::OpenProject: m_plater->load_project(); break;
    case Shortcut::SaveProjectAs:
        if (can_save_as())
            m_plater->save_project(true);
        break;
    case Shortcut::SaveProject:
        if (can_save())
            m_plater->save_project();
        break;
    case Shortcut::Search:
        if (m_plater && is_prepare_or_preview_tab())
            m_plater->sidebar().can_search();
        return false;
    case Shortcut::Preferences:
        wxGetApp().open_preferences();
        plater()->get_current_canvas3D()->force_set_focus();
        break;
    case Shortcut::ImportModel:
        if (can_add_models() && m_plater)
            m_plater->add_file();
        break;
    case Shortcut::Publish3mf:
        if (can_export_model())
            publish_project();
        break;
    case Shortcut::ShowLabels:
        if (m_plater && m_plater->is_view3D_shown()) {
            m_plater->show_view3D_labels(!m_plater->are_view3D_labels_shown());
            m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT));
        }
        break;
    case Shortcut::ViewDefault:
        if (m_plater) {
            select_view("plate");
            m_plater->get_current_canvas3D()->zoom_to_bed();
        }
        break;
    case Shortcut::ViewTop:    select_view("top"); break;
    case Shortcut::ViewBottom: select_view("bottom"); break;
    case Shortcut::ViewFront:  select_view("front"); break;
    case Shortcut::ViewRear:   select_view("rear"); break;
    case Shortcut::ViewLeft:   select_view("left"); break;
    case Shortcut::ViewRight:  select_view("right"); break;
    case Shortcut::ViewPlate:
        if (m_plater)
            m_plater->get_current_canvas3D()->select_plate();
        break;
    default: return false;
    }
    return true;
}

DiffPresetDialog* MainFrame::make_diff_dialog()
{
    auto* dialog = new DiffPresetDialog(this);

    auto get_tab = [](Preset::Type type) {
        Tab* null_tab = nullptr;
        for (Tab* tab : wxGetApp().tabs_list)
            if (tab->type() == type)
                return tab;
        return null_tab;
    };

    auto transfer = [dialog, get_tab](Preset::Type type) {
        get_tab(type)->transfer_options(dialog->get_left_preset_name(type),
                                        dialog->get_right_preset_name(type),
                                        dialog->get_selected_options(type));
    };

    auto process_options = [dialog](std::function<void(Preset::Type)> process) {
        const Preset::Type diff_dlg_type = dialog->view_type();
        if (diff_dlg_type == Preset::TYPE_INVALID) {
            for (const Preset::Type& type : dialog->types_list() )
                process(type);
        }
        else
            process(diff_dlg_type);
    };

    dialog->Bind(EVT_DIFF_DIALOG_TRANSFER, [process_options, transfer](SimpleEvent&) { process_options(transfer); });
    return dialog;
}


#ifdef __WXMSW__

static void AdjustWorkingAreaForAutoHide(const HWND hWnd, MINMAXINFO* mmi)
{
    const auto taskbarHwnd = FindWindowA("Shell_TrayWnd", nullptr);
    if (!taskbarHwnd) {
        return;
    }
    const auto monitorContainingApplication = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONULL);
    const auto monitorWithTaskbarOnIt = MonitorFromWindow(taskbarHwnd, MONITOR_DEFAULTTONULL);
    if (monitorContainingApplication != monitorWithTaskbarOnIt) {
        return;
    }
    APPBARDATA abd;
    abd.cbSize = sizeof(APPBARDATA);
    abd.hWnd   = taskbarHwnd;

    const auto uState = (UINT) SHAppBarMessage(ABM_GETSTATE, &abd);
    if ((uState & ABS_AUTOHIDE) != ABS_AUTOHIDE) {
        return;
    }

    RECT borderThickness;
    SetRectEmpty(&borderThickness);
    AdjustWindowRectEx(&borderThickness, GetWindowLongPtr(hWnd, GWL_STYLE) & ~WS_CAPTION, FALSE, 0);

    SHAppBarMessage(ABM_GETTASKBARPOS, &abd);
    const auto& rc = abd.rc;
    if (rc.top == rc.left && rc.bottom > rc.right) {
        const auto offset = borderThickness.left + 2;
        mmi->ptMaxPosition.x += offset;
        mmi->ptMaxTrackSize.x -= offset;
        mmi->ptMaxSize.x -= offset;
    } else if (rc.top == rc.left && rc.bottom < rc.right) {
        const auto offset = borderThickness.top + 2;
        mmi->ptMaxPosition.y += offset;
        mmi->ptMaxTrackSize.y -= offset;
        mmi->ptMaxSize.y -= offset;
    } else if (rc.top > rc.left) {
        const auto offset = borderThickness.bottom + 2;
        mmi->ptMaxSize.y -= offset;
        mmi->ptMaxTrackSize.y -= offset;
    } else {
        const auto offset = borderThickness.right + 2;
        mmi->ptMaxSize.x -= offset;
        mmi->ptMaxTrackSize.x -= offset;
    }
}

WXLRESULT MainFrame::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    HWND hWnd = GetHandle();
    switch (nMsg) {
    case WM_NCACTIVATE: {
        lParam = -1;
        break;
    }
    case WM_NCCALCSIZE:
        if (wParam) {
            WINDOWPLACEMENT wPos;
            wPos.length = sizeof(wPos);
            GetWindowPlacement(hWnd, &wPos);
            NCCALCSIZE_PARAMS *sz = reinterpret_cast<NCCALCSIZE_PARAMS *>(lParam);
            RECT borderThickness;
            SetRectEmpty(&borderThickness);
            AdjustWindowRectEx(&borderThickness, GetWindowLongPtr(hWnd, GWL_STYLE) & ~WS_CAPTION, FALSE, NULL);
            borderThickness.left *= -1;
            borderThickness.top *= -1;
            if (wPos.showCmd != SW_SHOWMAXIMIZED) {
                sz->rgrc[0].top += 1;
            } else {
                sz->rgrc[0].top += borderThickness.top;
            }
            sz->rgrc[0].left += borderThickness.left;
            sz->rgrc[0].right -= borderThickness.right;
            sz->rgrc[0].bottom -= borderThickness.bottom;
            return 0;
        }
        break;

    case WM_NCHITTEST: {
        if (IsMaximized()) {
            return HTCAPTION;
        }

        wxPoint mouse_pos = ::wxGetMousePosition();
        if (m_topbar->GetScreenRect().GetBottom() >= mouse_pos.y) {
            RECT borderThickness;
            SetRectEmpty(&borderThickness);
            AdjustWindowRectEx(&borderThickness, GetWindowLongPtr(hWnd, GWL_STYLE) & ~WS_CAPTION, FALSE, NULL);
            borderThickness.left *= -1;
            borderThickness.top *= -1;
            wxPoint client_pos = this->ScreenToClient(mouse_pos);

            bool on_top_border = client_pos.y <= borderThickness.top;

            if (client_pos.x <= borderThickness.left) {
                return on_top_border ? HTTOPLEFT : HTLEFT;
            } else if (client_pos.x >= GetClientSize().x - borderThickness.right) {
                return on_top_border ? HTTOPRIGHT : HTRIGHT;
            }

            return on_top_border ? HTTOP : HTCAPTION;
        }
        break;
    }

    case WM_GETMINMAXINFO: {
        auto mmi = (MINMAXINFO*) lParam;
        HandleGetMinMaxInfo(mmi);
        AdjustWorkingAreaForAutoHide(hWnd, mmi);
        return 0;
    }
    }
    return wxFrame::MSWWindowProc(nMsg, wParam, lParam);
}

#endif

void  MainFrame::show_log_window()
{
    m_log_window = new wxLogWindow(this, _L("Logging"), true, false);
    m_log_window->Show();
}

void MainFrame::update_layout()
{
    auto restore_to_creation = [this]() {
        auto clean_sizer = [](wxSizer* sizer) {
            while (!sizer->GetChildren().IsEmpty()) {
                sizer->Detach(0);
            }
        };

        int plater_page_id = m_tabpanel->FindPage(m_plater);
        if (plater_page_id != wxNOT_FOUND)
            m_tabpanel->RemovePage(plater_page_id);

        if (m_plater->GetParent() != this)
            m_plater->Reparent(this);

        if (m_tabpanel->GetParent() != this)
            m_tabpanel->Reparent(this);

        plater_page_id = (m_plater_page != nullptr) ? m_tabpanel->FindPage(m_plater_page) : wxNOT_FOUND;
        if (plater_page_id != wxNOT_FOUND) {
            m_tabpanel->DeletePage(plater_page_id);
            m_plater_page = nullptr;
        }

        clean_sizer(m_main_sizer);
        clean_sizer(m_settings_dialog.GetSizer());

        if (m_settings_dialog.IsShown())
            m_settings_dialog.Close();

        m_tabpanel->Hide();
        m_plater->Hide();

        Layout();
    };

    ESettingsLayout layout =  ESettingsLayout::Old;

    if (m_layout == layout)
        return;

    wxBusyCursor busy;

    Freeze();

    if (m_layout != ESettingsLayout::Unknown)
        restore_to_creation();

    ESettingsLayout old_layout = m_layout;
    m_layout = layout;

    m_last_selected_tab = TAB_ID_PREPARE;

    switch (m_layout)
    {
    case ESettingsLayout::Old:
    {
        m_plater->Reparent(m_tabpanel);
        const int home_idx = m_tabpanel->FindPageByName(TAB_ID_HOME);
        size_t prepare_pos = (home_idx == wxNOT_FOUND) ? 0 : static_cast<size_t>(home_idx) + 1;
#ifdef SLIC3R_CAD
        if (m_design_page != nullptr) {
            m_design_page->Reparent(m_tabpanel);
            m_tabpanel->InsertPage(prepare_pos++, TAB_ID_DESIGN, m_design_page, _L("Design"), "tab_design_active");
        }
#endif
        m_tabpanel->InsertPage(prepare_pos, TAB_ID_PREPARE, m_plater, _L("Prepare"), "tab_3d_active");
        m_tabpanel->InsertPage(prepare_pos + 1, TAB_ID_PREVIEW, m_plater, _L("Preview"), "tab_preview_active");
        m_main_sizer->Add(m_tabpanel, 1, wxEXPAND | wxTOP, 0);

        m_tabpanel->Bind(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED, [this](wxCommandEvent& evt)
        {
            if (evt.GetId() == m_tabpanel->FindPageByName(TAB_ID_PREPARE)) {
                Sidebar& sidebar = GUI::wxGetApp().sidebar();
                if (sidebar.need_auto_sync_after_connect_printer()) {
                    sidebar.set_need_auto_sync_after_connect_printer(false);
                    sidebar.sync_extruder_list();
                }

                m_plater->update(true);

                if (!preview_only_hint())
                    return;
            }
            evt.Skip();
        });

        m_plater->Show();
        m_tabpanel->Show();

        break;
    }
    case ESettingsLayout::GCodeViewer:
    {
        m_main_sizer->Add(m_plater, 1, wxEXPAND);
        m_plater->set_bed_shape({{0.0, 0.0}, {200.0, 0.0}, {200.0, 200.0}, {0.0, 200.0}}, {}, {}, 0.0, {}, {}, {}, {}, true);
        m_plater->get_collapse_toolbar().set_enabled(false);
        m_plater->enable_sidebar(false);
        m_plater->Show();
        break;
    }
    default:
        break;
    }

#ifdef __APPLE__
    m_plater->sidebar().change_top_border_for_mode_sizer(m_layout != ESettingsLayout::Old);
#endif

    Layout();
    Thaw();
}

#ifdef __WXGTK__
void MainFrame::update_edge_panels()
{
    if (!m_edge_bottom) return;

    bool hide = IsMaximized() || IsFullScreen();
    m_edge_bottom->Show(!hide);
    m_edge_left->Show(!hide);
    m_edge_right->Show(!hide);
    if (hide) return;

    constexpr int B = ResizeEdgePanel::BORDER_PX;
    wxSize cs = GetClientSize();
    m_edge_bottom->SetSize(0, cs.y - B, cs.x, B);
    m_edge_left->SetSize(0, 0, B, cs.y);
    m_edge_right->SetSize(cs.x - B, 0, B, cs.y);

    m_edge_bottom->Raise();
    m_edge_left->Raise();
    m_edge_right->Raise();
}
#endif

void MainFrame::shutdown()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "MainFrame::shutdown enter";
    m_idle.stop();
    if (ProjectPanel* project = ProjectPanel::if_built())
        project->shutdown();
    m_plugin_pages.shutdown();
    if (m_plater != nullptr)
        m_plater->remove_dock_panes();
#ifdef SLIC3R_CAD
    if (DesignPanel* design = DesignPanel::if_built())
        design->shutdown();
#endif
#ifdef __WXGTK__
    m_edge_bottom = nullptr;
    m_edge_left   = nullptr;
    m_edge_right  = nullptr;
#endif
    Slic3r::set_backup_callback(nullptr);
#ifdef _WIN32
	if (m_hDeviceNotify) {
		::UnregisterDeviceNotification(HDEVNOTIFY(m_hDeviceNotify));
		m_hDeviceNotify = nullptr;
	}
 	if (m_ulSHChangeNotifyRegister) {
        SHChangeNotifyDeregister(m_ulSHChangeNotifyRegister);
        m_ulSHChangeNotifyRegister = 0;
 	}
#endif

    if (m_plater != nullptr) {
        m_plater->get_ui_job_worker().cancel_all();

        m_plater->unbind_canvas_event_handlers();

        m_plater->reset_canvas_volumes();
    }

    this->Show(false);

    if (m_settings_dialog.IsShown())
        m_settings_dialog.Close();

    if (m_plater != nullptr) {
        m_plater->get_mouse3d_controller().shutdown();
        m_plater->get_mouse3d_controller().save_config(*wxGetApp().app_config);
    }

    NetworkAgent* agent = wxGetApp().getAgent();
    if (agent)
        agent->track_enable(false);

	wxGetApp().other_instance_message_handler()->shutdown(this);
    if(wxGetApp().app_config->dirty())
        wxGetApp().app_config->save();
    wxGetApp().tabs_list.clear();
    wxGetApp().model_tabs_list.clear();
    wxGetApp().shutdown();

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "MainFrame::shutdown exit";
}

void MainFrame::update_filament_tab_ui()
{
    wxGetApp().get_tab(Preset::Type::TYPE_FILAMENT)->reload_config();
    wxGetApp().get_tab(Preset::Type::TYPE_FILAMENT)->update_dirty();
    wxGetApp().get_tab(Preset::Type::TYPE_FILAMENT)->update_tab_ui();
}

void MainFrame::update_title()
{
    return;
}

void MainFrame::show_publish_button(bool show)
{
}

void MainFrame::update_title_colour_after_set_title()
{
#ifdef __APPLE__
    set_title_colour_after_set_title(GetHandle());
#endif
}

void MainFrame::show_option(bool show)
{
    if (!show) {
        if (m_slice_btn->IsShown()) {
            m_slice_btn->Hide();
            m_print_btn->Hide();
            m_slice_option_btn->Hide();
            m_print_option_btn->Hide();
            Layout();
        }
    } else {
        if (!m_slice_btn->IsShown()) {
            m_slice_btn->Show();
            m_print_btn->Show();
            m_slice_option_btn->Show();
            m_print_option_btn->Show();
            Layout();
        }
    }
}

void MainFrame::set_undo_redo_enabled(bool undo, bool redo)
{
#ifndef __APPLE__
    m_topbar->EnableUndoRedo(undo, redo);
#else
    (void) undo; (void) redo;
#endif
}

#ifdef SLIC3R_CAD
DesignPanel* MainFrame::shown_design_panel() const
{
    DesignPanel* design = DesignPanel::if_built();
    return (design != nullptr && m_design_page != nullptr && m_design_page->IsShownOnScreen()) ? design : nullptr;
}
#endif

void MainFrame::init_tabpanel() {
    wxBoxSizer *side_tools = create_side_tools();
    m_tabpanel = new Notebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, side_tools,
                              wxNB_TOP | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME);
    m_tabpanel->SetBackgroundColour(*wxWHITE);

#ifndef __WXOSX__
    m_tabpanel->SetFont(Slic3r::GUI::wxGetApp().normal_font());
#endif
    m_tabpanel->Hide();
    m_settings_dialog.set_tabpanel(m_tabpanel);

#ifdef __WXMSW__
    m_tabpanel->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
#else
    m_tabpanel->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
#endif
        wxWindow* panel = m_tabpanel->GetCurrentPage();
        m_last_selected_tab = m_tabpanel->GetSelectedPageName();
        if (panel == m_plater) {
            if (m_last_selected_tab == TAB_ID_PREPARE) {
                wxPostEvent(m_plater, SimpleEvent(EVT_GLVIEWTOOLBAR_3D));
                m_param_panel->OnActivate();
            }
            else if (m_last_selected_tab == TAB_ID_PREVIEW) {
                m_plater->reset_check_status();
                if (!m_plater->check_ams_status(m_slice_select == eSliceAll))
                    return;
                wxPostEvent(m_plater, SimpleEvent(EVT_GLVIEWTOOLBAR_PREVIEW));
                m_param_panel->OnActivate();
            }
            fit_tab_labels();
        }
#ifdef SLIC3R_CAD
        else if (m_design_page != nullptr && panel == m_design_page) {
            DesignPanel::ensure()->on_tab_shown();
        }
#endif
        else if (panel == m_monitor_page) {
        }
#ifdef SLIC3R_CAD
        if (DesignPanel* design = DesignPanel::if_built(); design != nullptr && panel != m_design_page)
            design->on_tab_hidden();
#endif
#ifndef __APPLE__
        if (m_last_selected_tab == TAB_ID_PREPARE) {
            m_topbar->EnableUndoRedoItems();
        }
        else {
            m_topbar->DisableUndoRedoItems();
        }
#endif
#ifdef SLIC3R_CAD
        if (m_design_page != nullptr && panel == m_design_page)
            DesignPanel::ensure()->update_undo_redo_buttons();
#endif

        if (panel)
            panel->SetFocus();
    });

    if (wxGetApp().is_editor()) {
        m_home_page = new LazyPage<WebViewPanel>(m_tabpanel, TAB_ID_HOME, 10);
        m_lazy_pages.push_back(m_home_page);
        Bind(EVT_LOAD_URL, [this](wxCommandEvent &evt) {
            wxString url = evt.GetString();
            select_tab(TAB_ID_HOME);
            WebViewPanel::ensure()->load_url(url);
        });
        m_tabpanel->AddPage(TAB_ID_HOME, m_home_page, "", "tab_home_active");
        m_param_panel = new ParamsPanel(m_tabpanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
    }

    m_plater = new Plater(this, this);
    m_plater->SetBackgroundColour(*wxWHITE);
    m_plater->Hide();

    wxGetApp().plater_ = m_plater;

#ifdef SLIC3R_CAD
    if (wxGetApp().is_enable_cad_feature() && wxGetApp().is_editor()) {
        m_design_page = new LazyPage<DesignPanel>(this, TAB_ID_DESIGN, -1);
        m_lazy_pages.push_back(m_design_page);
        start_mcp_control_if_enabled();
    }
#endif

    create_preset_tabs();

    m_monitor_page = new LazyPage<MonitorPanel>(m_tabpanel, TAB_ID_MONITOR, 20);
    m_lazy_pages.push_back(m_monitor_page);
    m_tabpanel->AddPage(TAB_ID_MONITOR, m_monitor_page, _L("Device"), "tab_monitor_active");

    m_printer_view_page = new LazyPage<PrinterWebView>(m_tabpanel, TAB_ID_MONITOR_WEB, 50, [this](wxWindow* parent) {
        auto* view = new PrinterWebView(parent);
        if (!m_printer_url.empty())
            view->load_url(m_printer_url, m_printer_api_key);
        return view;
    });
    m_lazy_pages.push_back(m_printer_view_page);
    Bind(EVT_LOAD_PRINTER_URL, [this](LoadPrinterViewEvent &evt) {
        m_printer_url     = evt.GetString();
        m_printer_api_key = evt.GetAPIkey();
        if (PrinterWebView* view = PrinterWebView::if_built())
            view->load_url(m_printer_url, m_printer_api_key);
    });

    m_multi_machine_page = new LazyPage<MultiMachinePage>(m_tabpanel, TAB_ID_MULTI_DEVICE, 40);
    m_lazy_pages.push_back(m_multi_machine_page);
    if (wxGetApp().is_enable_multi_machine()) {
        m_tabpanel->AddPage(TAB_ID_MULTI_DEVICE, m_multi_machine_page, _L("Multi-device"), "tab_multi_active");
    }

    m_project_page = new LazyPage<ProjectPanel>(m_tabpanel, TAB_ID_PROJECT, 60);
    m_lazy_pages.push_back(m_project_page);
    m_tabpanel->AddPage(TAB_ID_PROJECT, m_project_page, _L("Project"), "tab_auxiliary_active");

    m_calibration_page = new LazyPage<CalibrationPanel>(m_tabpanel, TAB_ID_CALIBRATION, 30);
    m_lazy_pages.push_back(m_calibration_page);
    m_tabpanel->AddPage(TAB_ID_CALIBRATION, m_calibration_page, _L("Calibration"), "tab_calibration_active");

    m_plugin_pages.initialize(m_tabpanel);

    if (m_plater) {
        auto full_config = wxGetApp().preset_bundle->full_config();
        m_plater->on_config_change(full_config);

        if (full_config.has("filament_colour")) {
            m_plater->on_filament_count_change(full_config.option<ConfigOptionStrings>("filament_colour")->values.size());
        }
    }
}

void MainFrame::show_device(bool should_use_native) {
    auto idx = -1;

    const bool use_printer_agents = wxGetApp().app_config->get_bool("use_printer_agents");

    const bool want_web_device_tab = use_printer_agents && wxGetApp().preset_bundle != nullptr &&
                                     !wxGetApp().preset_bundle->use_bbl_device_tab();

    if (!want_web_device_tab) {
        if ((idx = m_tabpanel->FindPageByName(TAB_ID_MONITOR_WEB)) != wxNOT_FOUND) {
            m_printer_view_page->Show(false);
            m_tabpanel->RemovePage(idx);
        }
    }

    if (use_printer_agents) {
        if (!m_monitor_page->in_book()) {
            if ((idx = m_tabpanel->FindPage(m_printer_view_page)) != wxNOT_FOUND) {
                m_printer_view_page->Show(false);
                m_tabpanel->RemovePage(idx);
            }
            m_monitor_page->Show(false);
            m_tabpanel->InsertPage(m_tabpanel->PositionAfter({TAB_ID_PREVIEW}), TAB_ID_MONITOR, m_monitor_page,
                                   _L("Device"), "tab_monitor_active");
        }

        if (wxGetApp().is_enable_multi_machine()) {
            if (!m_multi_machine_page->in_book()) {
                m_multi_machine_page->Show(false);
                m_tabpanel->InsertPage(m_tabpanel->PositionAfter({TAB_ID_MONITOR_WEB, TAB_ID_MONITOR}),
                                       TAB_ID_MULTI_DEVICE, m_multi_machine_page, _L("Multi-device"), "tab_multi_active");
            }
        }
        if (!m_calibration_page->in_book()) {
            m_calibration_page->Show(false);
            m_tabpanel->InsertPage(m_tabpanel->PositionAfter({TAB_ID_PROJECT}), TAB_ID_CALIBRATION, m_calibration_page,
                                   _L("Calibration"), "tab_calibration_active");
        }

        if (want_web_device_tab) {
            if ((idx = m_tabpanel->FindPage(m_printer_view_page)) == wxNOT_FOUND) {
                m_printer_view_page->Show(false);
                m_tabpanel->InsertPage(m_tabpanel->PositionAfter({TAB_ID_MONITOR}), TAB_ID_MONITOR_WEB,
                                       m_printer_view_page, _L("Device (Web)"), "tab_monitor_active");
            } else {
                m_tabpanel->SetPageText(idx, _L("Device (Web)"));
            }
        }

#ifdef _MSW_DARK_MODE
        wxGetApp().UpdateDarkUIWin(this);
#endif

        fit_tab_labels();
        m_plugin_pages.relayout();
        if (m_prebuild_started)
            m_idle.start();
        return;
    }

    if (should_use_native) {
        if (m_monitor_page->in_book()) {
            fit_tab_labels();
            return;
        }
        if ((idx = m_tabpanel->FindPage(m_printer_view_page)) != wxNOT_FOUND) {
            m_printer_view_page->Show(false);
            m_tabpanel->RemovePage(idx);
        }

        m_monitor_page->Show(false);
        m_tabpanel->InsertPage(m_tabpanel->PositionAfter({TAB_ID_PREVIEW}), TAB_ID_MONITOR, m_monitor_page,
                               _L("Device"), "tab_monitor_active");

        if (wxGetApp().is_enable_multi_machine()) {
            m_multi_machine_page->Show(false);
            m_tabpanel->InsertPage(m_tabpanel->PositionAfter({TAB_ID_MONITOR}), TAB_ID_MULTI_DEVICE, m_multi_machine_page,
                                   _L("Multi-device"), "tab_multi_active");
        }
        m_calibration_page->Show(false);
        m_tabpanel->InsertPage(m_tabpanel->PositionAfter({TAB_ID_PROJECT}), TAB_ID_CALIBRATION, m_calibration_page,
                               _L("Calibration"), "tab_calibration_active");

#ifdef _MSW_DARK_MODE
        wxGetApp().UpdateDarkUIWin(this);
#endif

    } else {
        if (m_printer_view_page->in_book()) {
            fit_tab_labels();
            return;
        }
        if ((idx = m_tabpanel->FindPage(m_calibration_page)) != wxNOT_FOUND) {
            m_calibration_page->Show(false);
            m_tabpanel->RemovePage(idx);
        }
        if ((idx = m_tabpanel->FindPage(m_multi_machine_page)) != wxNOT_FOUND) {
            m_multi_machine_page->Show(false);
            m_tabpanel->RemovePage(idx);
        }
        if ((idx = m_tabpanel->FindPage(m_monitor_page)) != wxNOT_FOUND) {
            m_monitor_page->Show(false);
            m_tabpanel->RemovePage(idx);
        }
        m_printer_view_page->Show(false);
        m_tabpanel->InsertPage(m_tabpanel->PositionAfter({TAB_ID_PREVIEW}), TAB_ID_MONITOR, m_printer_view_page,
                               _L("Device"), "tab_monitor_active");
    }
    fit_tab_labels();
    m_plugin_pages.relayout();
    if (m_prebuild_started)
        m_idle.start();
}

bool MainFrame::is_prepare_or_preview_tab() const
{
    const wxString tab = m_tabpanel->GetSelectedPageName();
    return tab == TAB_ID_PREPARE || tab == TAB_ID_PREVIEW;
}

void MainFrame::fit_tab_labels()
{
    if (!m_tabpanel || !m_slice_option_btn)
        return;

    auto* ctrl  = m_tabpanel->GetBtnsListCtrl();
    auto* sizer = ctrl->GetBtnsSizer();
    int   count = sizer->GetItemCount();

    for (size_t i = 1; i < count; ++i)
        ctrl->SetCompact(i, false);
    m_tabpanel->Refresh();
    Layout();

    for (size_t i = count - 1; i >= 1; --i) {
        int right = ScreenToClient(m_slice_option_btn->ClientToScreen({})).x;
        int left  = sizer->GetSize().GetWidth();
        if (right - left - FromDIP(15) > 0) return;
        ctrl->SetCompact(i, true);
        m_tabpanel->Refresh();
        Layout();
    }
}

bool MainFrame::preview_only_hint()
{
    if (m_plater && (m_plater->only_gcode_mode() || (m_plater->using_exported_file()))) {
        BOOST_LOG_TRIVIAL(info) << boost::format("skipped tab switch from %1% to %2% in preview mode")%m_tabpanel->GetSelectedPageName() %wxString(TAB_ID_PREPARE);

        ConfirmBeforeSendDialog confirm_dlg(this, wxID_ANY, _L("Warning"));
        confirm_dlg.Bind(EVT_SECONDARY_CHECK_CONFIRM, [this](wxCommandEvent& e) {
            preview_only_to_editor = true;
        });
        confirm_dlg.update_btn_label(_L("Yes"), _L("No"));
        auto filename = m_plater->get_preview_only_filename();

        confirm_dlg.update_text(filename + " " + _L("will be closed before creating a new model. Do you want to continue?"));
        confirm_dlg.on_show();
        if (preview_only_to_editor) {
            m_plater->new_project();
            preview_only_to_editor = false;
        }

        return false;
    }

    return true;
}

#ifdef WIN32
void MainFrame::register_win32_callbacks()
{
    static GUID GUID_DEVINTERFACE_HID           = { 0x4D1E55B2, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter = { 0 };
    NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_HID;
    m_hDeviceNotify = ::RegisterDeviceNotification(this->GetHWND(), &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

    LPITEMIDLIST ppidl;
    if (SHGetSpecialFolderLocation(this->GetHWND(), CSIDL_DESKTOP, &ppidl) == NOERROR) {
        SHChangeNotifyEntry shCNE;
        shCNE.pidl       = ppidl;
        shCNE.fRecursive = TRUE;
        m_ulSHChangeNotifyRegister = SHChangeNotifyRegister(this->GetHWND(),
            SHCNE_DISKEVENTS,
            SHCNE_MEDIAINSERTED | SHCNE_MEDIAREMOVED,
            WM_USER_MEDIACHANGED,
            1,
            &shCNE);
        assert(m_ulSHChangeNotifyRegister != 0);
    } else {
        assert(false);
    }

    {
        static constexpr int device_count = 1;
        RAWINPUTDEVICE devices[device_count] = {};
        devices[0].usUsagePage = 0x01;
        devices[0].usUsage = 0x08;
        if (! RegisterRawInputDevices(devices, device_count, sizeof(RAWINPUTDEVICE)))
            BOOST_LOG_TRIVIAL(error) << "RegisterRawInputDevices failed";
    }
}
#endif

void MainFrame::create_preset_tabs()
{
    wxGetApp().update_label_colours_from_appconfig();

    m_param_dialog = new ParamsDialog(m_plater);

    add_created_tab(new TabPrint(m_param_panel), "cog");
    add_created_tab(new TabPrintPlate(m_param_panel), "cog");
    add_created_tab(new TabPrintObject(m_param_panel), "cog");
    add_created_tab(new TabPrintPart(m_param_panel), "cog");
    add_created_tab(new TabPrintLayer(m_param_panel), "cog");
    add_created_tab(new TabFilament(m_param_dialog->panel()), "spool");
    add_created_tab(new TabPrinter(m_param_dialog->panel()), "printer");

    m_param_panel->rebuild_panels();
    m_param_dialog->panel()->rebuild_panels();
}

void MainFrame::add_created_tab(Tab* panel,  const std::string& bmp_name )
{
    panel->create_preset_tab();

    if (panel->type() == Preset::TYPE_PLATE) {
        wxGetApp().tabs_list.pop_back();
        wxGetApp().plate_tab = panel;
    }
    if (panel->type() == Preset::TYPE_MODEL) {
        wxGetApp().tabs_list.pop_back();
        wxGetApp().model_tabs_list.push_back(panel);
    }
}

bool MainFrame::is_active_and_shown_tab(wxPanel* panel)
{
    if (panel == m_param_panel)
        panel = m_plater;
    else
        return m_param_dialog->IsShown();

    if (m_tabpanel->GetCurrentPage() != panel)
        return false;
    return true;
}

bool MainFrame::can_start_new_project() const
{
    return (m_plater && !m_plater->is_background_process_slicing());
}

bool MainFrame::can_open_project() const
{
    return (m_plater && !m_plater->is_background_process_slicing());
}

bool  MainFrame::can_add_models() const
{
    return (m_plater && !m_plater->is_background_process_slicing() && !m_plater->only_gcode_mode() && !m_plater->using_exported_file());
}

bool MainFrame::can_save() const
{
    return (m_plater != nullptr) &&
        !m_plater->get_view3D_canvas3D()->get_gizmos_manager().is_in_editing_mode(false) &&
        m_plater->is_project_dirty() && !m_plater->using_exported_file() && !m_plater->only_gcode_mode();
}

bool MainFrame::can_save_as() const
{
    return (m_plater != nullptr) &&
        !m_plater->get_view3D_canvas3D()->get_gizmos_manager().is_in_editing_mode(false) && !m_plater->using_exported_file() && !m_plater->only_gcode_mode();
}

void MainFrame::save_project()
{
    save_project_as(m_plater->get_project_filename(".3mf"));
}

bool MainFrame::save_project_as(const wxString& filename)
{
    bool ret = (m_plater != nullptr) ? m_plater->export_3mf(into_path(filename)) : false;
    if (ret) {
        m_plater->reset_project_dirty_after_save();
    }
    return ret;
}

void MainFrame::publish_project()
{
    if (m_plater == nullptr)
        return;
    std::vector<std::string> pending_keys;
    std::vector<Slic3r::PublishedMaterialEntry> pending_material;
    const bool has_prior = m_plater->get_pending_published(pending_keys, pending_material);
    PublishSettingsDialog dlg(this, has_prior ? &pending_keys : nullptr, has_prior ? &pending_material : nullptr);
    if (dlg.ShowModal() != wxID_OK)
        return;
    m_plater->set_pending_published(dlg.GetPublishedKeys(), dlg.GetPublishedMaterialKeys());
    m_plater->export_published_3mf(dlg.GetPublishedKeys(), dlg.GetPublishedMaterialKeys());
}

bool MainFrame::can_upload() const
{
    return true;
}

bool MainFrame::can_export_model() const
{
    return (m_plater != nullptr) && !m_plater->model().objects.empty();
}

bool MainFrame::can_export_toolpaths() const
{
    return (m_plater != nullptr) && (m_plater->printer_technology() == ptFFF) && m_plater->is_preview_shown() && m_plater->is_preview_loaded() && m_plater->has_toolpaths_to_export();
}

bool MainFrame::can_export_supports() const
{
    if ((m_plater == nullptr) || (m_plater->printer_technology() != ptSLA) || m_plater->model().objects.empty())
        return false;

    bool can_export = false;
    const PrintObjects& objects = m_plater->sla_print().objects();
    for (const SLAPrintObject* object : objects)
    {
        if (object->has_mesh(slaposPad) || object->has_mesh(slaposSupportTree))
        {
            can_export = true;
            break;
        }
    }
    return can_export;
}

bool MainFrame::can_export_gcode() const
{
    if (m_plater == nullptr)
        return false;

    if (m_plater->model().objects.empty())
        return false;

    if (m_plater->is_export_gcode_scheduled())
        return false;

    PartPlateList &part_plate_list = m_plater->get_partplate_list();
    PartPlate *current_plate = part_plate_list.get_curr_plate();
    if (!current_plate->is_slice_result_ready_for_print())
        return false;

    return true;
}

bool MainFrame::can_export_all_gcode() const
{
    if (m_plater == nullptr)
        return false;

    if (m_plater->model().objects.empty())
        return false;

    if (m_plater->is_export_gcode_scheduled())
        return false;

    PartPlateList& part_plate_list = m_plater->get_partplate_list();
    return part_plate_list.is_all_slice_results_ready_for_print();
}

bool MainFrame::can_print_3mf() const
{
    if (m_plater && !m_plater->model().objects.empty()) {
    }
    return true;
}

bool MainFrame::can_send_gcode() const
{
    if (m_plater && !m_plater->model().objects.empty())
    {
        auto cfg = wxGetApp().preset_bundle->printers.get_edited_preset().config;

        const auto *print_host_opt = cfg.option<ConfigOptionString>("print_host");
        if (! print_host_opt) return false;
        else return !print_host_opt->value.empty();
    }
    return true;
}

bool MainFrame::can_slice() const
{
#ifdef SUPPORT_BACKGROUND_PROCESSING
    bool bg_proc = wxGetApp().app_config->get("background_processing") == "1";
    return (m_plater != nullptr) ? !m_plater->model().objects.empty() && !bg_proc : false;
#else
    return (m_plater != nullptr) ? !m_plater->model().objects.empty() : false;
#endif
}

bool MainFrame::can_change_view() const
{
    switch (m_layout)
    {
    default:                   { return false; }
    case ESettingsLayout::Old: {
        int page_id = m_tabpanel->GetSelection();
        return page_id != wxNOT_FOUND && dynamic_cast<const Slic3r::GUI::Plater*>(m_tabpanel->GetPage((size_t)page_id)) != nullptr;
    }
    case ESettingsLayout::GCodeViewer: { return true; }
    }
}

bool MainFrame::can_clone() const {
    return can_select() && !m_plater->is_selection_empty();
}

bool MainFrame::can_select() const
{
    return (m_plater != nullptr) && (m_tabpanel->GetSelectedPageName() == TAB_ID_PREPARE) && !m_plater->model().objects.empty();
}

bool MainFrame::can_deselect() const
{
    return (m_plater != nullptr) && (m_tabpanel->GetSelectedPageName() == TAB_ID_PREPARE) && !m_plater->is_selection_empty();
}

bool MainFrame::can_delete() const
{
    return (m_plater != nullptr) && (m_tabpanel->GetSelectedPageName() == TAB_ID_PREPARE) && !m_plater->is_selection_empty();
}

bool MainFrame::can_delete_all() const
{
    return (m_plater != nullptr) && (m_tabpanel->GetSelectedPageName() == TAB_ID_PREPARE) && !m_plater->model().objects.empty();
}

bool MainFrame::can_reslice() const
{
    return (m_plater != nullptr) && !m_plater->model().objects.empty();
}

namespace {
const char* print_select_type_key(MainFrame::PrintSelectType type)
{
    switch (type) {
    case MainFrame::ePrintAll:            return "print_all";
    case MainFrame::ePrintPlate:          return "print_plate";
    case MainFrame::eExportSlicedFile:    return "export_sliced_file";
    case MainFrame::eExportAllSlicedFile: return "export_all_sliced_file";
    case MainFrame::eExportGcode:         return "export_gcode";
    case MainFrame::eSendGcode:           return "send_gcode";
    case MainFrame::eSendToPrinter:       return "send_to_printer";
    case MainFrame::eSendToPrinterAll:    return "send_to_printer_all";
    case MainFrame::ePrintMultiMachine:   return "print_multi_machine";
    case MainFrame::eUploadGcode:         break;
    }
    return "";
}

wxString print_select_type_label(MainFrame::PrintSelectType type)
{
    switch (type) {
    case MainFrame::ePrintAll:            return _L("Print all");
    case MainFrame::ePrintPlate:          return _L("Print plate");
    case MainFrame::eExportSlicedFile:    return _L("Export plate sliced file");
    case MainFrame::eExportAllSlicedFile: return _L("Export all sliced file");
    case MainFrame::eExportGcode:         return _L("Export G-code file");
    case MainFrame::eSendGcode:           return _L_CONTEXT("Print", "Verb");
    case MainFrame::eSendToPrinter:       return _L("Send");
    case MainFrame::eSendToPrinterAll:    return _L("Send all");
    case MainFrame::ePrintMultiMachine:   return _L("Send to Multi-device");
    case MainFrame::eUploadGcode:         break;
    }
    return _L("Print plate");
}
}

std::vector<MainFrame::PrintSelectType> MainFrame::available_print_actions() const
{
    std::vector<PrintSelectType> actions;
    const auto preset_bundle      = wxGetApp().preset_bundle;
    const bool use_printer_agents = wxGetApp().app_config->get_bool("use_printer_agents");

    if (preset_bundle && !preset_bundle->is_bbl_vendor() && !use_printer_agents) {
        actions.push_back(eSendGcode);
        const auto* use_3mf_opt = preset_bundle->printers.get_edited_preset().config.option<ConfigOptionBool>("use_3mf");
        if (use_3mf_opt != nullptr && use_3mf_opt->value)
            actions.push_back(eExportSlicedFile);
        actions.push_back(eExportGcode);
        return actions;
    }

    bool support_send      = true;
    bool support_print_all = true;
    if (preset_bundle && !preset_bundle->use_bbl_network() && !use_printer_agents) {
        support_send = false;

        const auto& cfg           = preset_bundle->printers.get_edited_preset().config;
        const auto* host_type_opt = cfg.option<ConfigOptionEnum<PrintHostType>>("host_type");
        support_print_all = host_type_opt != nullptr && host_type_opt->value == PrintHostType::htSimplyPrint;
    }

    actions.push_back(ePrintPlate);
    if (support_print_all)
        actions.push_back(ePrintAll);
    if (support_send) {
        actions.push_back(eSendToPrinter);
        actions.push_back(eSendToPrinterAll);
    }
    if (enable_multi_machine)
        actions.push_back(ePrintMultiMachine);
    actions.push_back(eExportSlicedFile);
    actions.push_back(eExportAllSlicedFile);
    actions.push_back(eExportGcode);
    return actions;
}

void MainFrame::select_print_action(PrintSelectType select_type)
{
    m_print_btn->SetLabel(print_select_type_label(select_type));
    m_print_select = select_type;
    remember_print_select(select_type);
    m_print_enable = get_enable_print_status();
    m_print_btn->Enable(m_print_enable);
    this->Layout();
    fit_tab_labels();
}

void MainFrame::remember_print_select(PrintSelectType select_type)
{
    if (!wxGetApp().app_config->get_bool("remember_print_action"))
        return;
    wxGetApp().app_config->set("last_print_action", print_select_type_key(select_type));
}

bool MainFrame::get_remembered_print_select(PrintSelectType& out) const
{
    if (!wxGetApp().app_config->get_bool("remember_print_action"))
        return false;
    const std::string saved = wxGetApp().app_config->get("last_print_action");
    if (saved.empty())
        return false;
    for (PrintSelectType type : available_print_actions()) {
        if (saved == print_select_type_key(type)) {
            out = type;
            return true;
        }
    }
    return false;
}

wxBoxSizer* MainFrame::create_side_tools()
{
    enable_multi_machine = wxGetApp().is_enable_multi_machine();
    int em = em_unit();
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

    m_slice_select = eSlicePlate;
    m_print_select = ePrintPlate;

    auto slice_panel = new wxPanel(this,wxID_ANY,wxDefaultPosition,wxDefaultSize);
    auto print_panel = new wxPanel(this,wxID_ANY,wxDefaultPosition,wxDefaultSize);
    slice_panel->SetBackgroundColour(StateColor::darkModeColorFor(wxColour("#3B4446")));
    print_panel->SetBackgroundColour(StateColor::darkModeColorFor(wxColour("#3B4446")));

    m_slice_btn = new SideButton(slice_panel, _L("Slice plate"), "");
    m_slice_option_btn = new SideButton(slice_panel, "", "sidebutton_dropdown", 0, 14);
    m_print_btn = new SideButton(print_panel, _L("Print plate"), "");
    m_print_option_btn = new SideButton(print_panel, "", "sidebutton_dropdown", 0, 14);

    PrintSelectType remembered_print_select;
    if (get_remembered_print_select(remembered_print_select)) {
        m_print_select = remembered_print_select;
        m_print_btn->SetLabel(print_select_type_label(remembered_print_select));
        fit_tab_labels();
    }

    auto slice_sizer = new wxBoxSizer(wxHORIZONTAL);
    slice_sizer->Add(m_slice_option_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(1));
    slice_sizer->Add(m_slice_btn, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(1));
    slice_panel->SetSizer(slice_sizer);

    auto print_sizer = new wxBoxSizer(wxHORIZONTAL);
    print_sizer->Add(m_print_option_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(1));
    print_sizer->Add(m_print_btn, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(1));
    print_panel->SetSizer(print_sizer);

    update_side_button_style();
    m_slice_option_btn->Enable();
    m_print_option_btn->Enable();
    sizer->Add(slice_panel);
    sizer->Add(FromDIP(15), 0, 0, 0, 0);
    sizer->Add(print_panel);
    sizer->Add(FromDIP(19), 0, 0, 0, 0);

    sizer->Layout();

    m_filament_group_popup = new FilamentGroupPopup(m_slice_btn);

    auto try_hover_pop_up = [this]() {
#ifdef __APPLE__
        if (!IsActive()) {
            return;
        }
#endif
        wxPoint pos = m_slice_btn->ClientToScreen(wxPoint(0, 0));
        pos.y += m_slice_btn->GetRect().height * 1.25;
        pos.x -= (m_slice_option_btn->GetRect().width + FromDIP(380) * 0.6);
        auto curr_plate = this->m_plater->get_partplate_list().get_curr_plate();
        m_filament_group_popup->SetPosition(pos);
        m_filament_group_popup->tryPopup(m_plater, curr_plate, m_slice_select == eSliceAll);
        };

#ifndef __linux__
    slice_panel->Bind(wxEVT_ENTER_WINDOW, [this,try_hover_pop_up](auto& event) {
        if(!m_slice_option_pop_up || !m_slice_option_pop_up->IsShown())
            try_hover_pop_up();
        });

    slice_panel->Bind(wxEVT_LEAVE_WINDOW, [this](auto& event) {
        m_filament_group_popup->tryClose();
        });

    m_slice_btn->Bind(wxEVT_ENTER_WINDOW, [this, try_hover_pop_up](auto& event) {
        if (!m_slice_option_pop_up || !m_slice_option_pop_up->IsShown())
            try_hover_pop_up();
        });

    m_slice_btn->Bind(wxEVT_LEAVE_WINDOW, [this](auto& event) {
        m_filament_group_popup->tryClose();
        });
#endif

    m_slice_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
        {

            m_plater->exit_gizmo();
            m_plater->update(true, true);

            bool slice = true;

            if (m_filament_group_popup)
                m_filament_group_popup->Dismiss();

            auto curr_plate = m_plater->get_partplate_list().get_curr_plate();
            #ifdef __linux__
                PresetBundle* preset = wxGetApp().preset_bundle;
                bool force_show_fila_group_dlg        = (preset && preset->is_bbl_vendor() && preset->get_printer_extruder_count() == 2);
                slice = try_pop_up_before_slice(m_slice_select == eSliceAll, m_plater, curr_plate, force_show_fila_group_dlg);
            #else
                slice = try_pop_up_before_slice(m_slice_select == eSliceAll, m_plater, curr_plate, false);
            #endif

            if (slice) {
                if (m_slice_select == eSliceAll)
                    wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SLICE_ALL));
                else
                    wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SLICE_PLATE));
                this->m_tabpanel->SelectPageByName(TAB_ID_PREVIEW);
            }
        });

    m_print_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
        {
            if (m_print_select == ePrintAll || m_print_select == ePrintPlate || m_print_select == ePrintMultiMachine)
            {
                m_plater->apply_background_progress();
                m_print_enable = get_enable_print_status();
                m_print_btn->Enable(m_print_enable);
                if (m_print_enable) {
                    if (m_print_select == ePrintAll)
                        wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_PRINT_ALL));
                    if (m_print_select == ePrintPlate)
                        wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_PRINT_PLATE));
                    if(m_print_select == ePrintMultiMachine)
                         wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_PRINT_MULTI_MACHINE));
                }
            }
            else if (m_print_select == eExportGcode)
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_GCODE));
            else if (m_print_select == eSendGcode)
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SEND_GCODE));
            else if (m_print_select == eUploadGcode)
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_UPLOAD_GCODE));
            else if (m_print_select == eExportSlicedFile)
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_SLICED_FILE));
            else if (m_print_select == eExportAllSlicedFile)
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_ALL_SLICED_FILE));
            else if (m_print_select == eSendToPrinter)
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SEND_TO_PRINTER));
            else if (m_print_select == eSendToPrinterAll)
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SEND_TO_PRINTER_ALL));
        });

    m_slice_option_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
        {
            if(m_slice_option_pop_up)
                delete m_slice_option_pop_up;
            m_slice_option_pop_up = new SidePopup(this);
            SideButton* slice_all_btn = new SideButton(m_slice_option_pop_up, _L("Slice all"), "");
            slice_all_btn->SetCornerRadius(0);
            SideButton* slice_plate_btn = new SideButton(m_slice_option_pop_up, _L("Slice plate"), "");
            slice_plate_btn->SetCornerRadius(0);

            slice_all_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
                m_slice_btn->SetLabel(_L("Slice all"));
                m_slice_select = eSliceAll;
                m_slice_enable = get_enable_slice_status();
                m_slice_btn->Enable(m_slice_enable);
                this->Layout();
                fit_tab_labels();
                if(m_slice_option_pop_up)
                    m_slice_option_pop_up->Dismiss();
                });

            slice_plate_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
                m_slice_btn->SetLabel(_L("Slice plate"));
                m_slice_select = eSlicePlate;
                m_slice_enable = get_enable_slice_status();
                m_slice_btn->Enable(m_slice_enable);
                this->Layout();
                fit_tab_labels();
                if(m_slice_option_pop_up)
                    m_slice_option_pop_up->Dismiss();
                });
            m_slice_option_pop_up->append_button(slice_all_btn);
            m_slice_option_pop_up->append_button(slice_plate_btn);
            m_slice_option_pop_up->Popup(m_slice_btn);
        }
    );

    m_print_option_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
        {
            SidePopup* p = new SidePopup(this);
            for (PrintSelectType type : available_print_actions()) {
                SideButton* btn = new SideButton(p, print_select_type_label(type), "");
                btn->SetCornerRadius(0);
                btn->Bind(wxEVT_BUTTON, [this, p, type](wxCommandEvent&) {
                    select_print_action(type);
                    p->Dismiss();
                    });
                p->append_button(btn);
            }
            p->Popup(m_print_btn);
        }
    );

    sizer->Add(FromDIP(19), 0, 0, 0, 0);

    return sizer;
}

bool MainFrame::get_enable_slice_status()
{
    bool enable = true;

    bool on_slicing = m_plater->is_background_process_slicing();
    if (on_slicing) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": on slicing, return false directly!");
        return false;
    }
    else if  (m_plater->only_gcode_mode() || m_plater->using_exported_file()) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": in gcode/exported 3mf mode, return false directly!");
        return false;
    }

    PartPlateList &part_plate_list = m_plater->get_partplate_list();
    PartPlate *current_plate = part_plate_list.get_curr_plate();

    if (m_slice_select == eSliceAll)
    {
        enable = true;
    }
    else if (m_slice_select == eSlicePlate)
    {
        if (current_plate->is_slice_result_valid())
        {
            enable = false;
        }
        else if (!current_plate->can_slice())
        {
            enable = false;
        }
    }

    if (enable && m_plater->sidebar().has_broken_mixed_filament())
        enable = false;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": m_slice_select %1%, enable= %2% ")%m_slice_select %enable;
    return enable;
}

bool MainFrame::get_enable_print_status()
{
    bool enable = true;

    PartPlateList &part_plate_list = m_plater->get_partplate_list();
    PartPlate *current_plate = part_plate_list.get_curr_plate();
    bool is_all_plates = wxGetApp().plater()->get_preview_canvas3D()->is_all_plates_selected();
    if (m_print_select == ePrintAll)
    {
        if (!part_plate_list.is_all_slice_results_ready_for_print())
        {
            enable = false;
        }
    }
    else if (m_print_select == ePrintPlate)
    {
        if (!current_plate->is_slice_result_ready_for_print())
        {
            enable = false;
        }
        enable = enable && !is_all_plates;
    }
    else if (m_print_select == eExportGcode)
    {
        if (!current_plate->is_slice_result_valid())
        {
            enable = false;
        }
        enable = enable && !is_all_plates;
    }
    else if (m_print_select == eSendGcode)
    {
        if (!current_plate->is_slice_result_valid())
            enable = false;
        if (!can_send_gcode())
            enable = false;
        enable = enable && !is_all_plates;
    }
    else if (m_print_select == eUploadGcode)
    {
        if (!current_plate->is_slice_result_valid())
            enable = false;
        if (!can_send_gcode())
            enable = false;
        enable = enable && !is_all_plates;
    }
    else if (m_print_select == eExportSlicedFile)
    {
        if (!current_plate->is_slice_result_ready_for_export())
        {
            enable = false;
        }
        enable = enable && !is_all_plates;
	}
	else if (m_print_select == eSendToPrinter)
	{
		if (!current_plate->is_slice_result_ready_for_print())
		{
			enable = false;
		}
        enable = enable && !is_all_plates;
	}
    else if (m_print_select == eSendToPrinterAll)
    {
        if (!part_plate_list.is_all_slice_results_ready_for_print())
        {
            enable = false;
        }
    }
    else if (m_print_select == eExportAllSlicedFile)
    {
        if (!part_plate_list.is_all_slice_result_ready_for_export())
        {
            enable = false;
        }
    }
    else if (m_print_select == ePrintMultiMachine)
    {
        if (!current_plate->is_slice_result_ready_for_print())
        {
            enable = false;
        }
        enable = enable && !is_all_plates;
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": m_print_select %1%, enable= %2% ")%m_print_select %enable;

    return enable;
}

void MainFrame::update_side_button_style()
{
    int em = em_unit();

    StateColor m_btn_bg_enable = StateColor(
        std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(48, 221, 112), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    m_slice_btn->SetTextLayout(SideButton::EHorizontalOrientation::HO_Left, FromDIP(15));
    m_slice_btn->SetCornerRadius(FromDIP(12));
    m_slice_btn->SetExtraSize(wxSize(FromDIP(38), FromDIP(10)));
    m_slice_btn->SetMinSize(wxSize(-1, FromDIP(24)));

    m_slice_option_btn->SetTextLayout(SideButton::EHorizontalOrientation::HO_Center);
    m_slice_option_btn->SetCornerRadius(FromDIP(12));
    m_slice_option_btn->SetExtraSize(wxSize(FromDIP(10), FromDIP(10)));
    m_slice_option_btn->SetIconOffset(FromDIP(2));
    m_slice_option_btn->SetMinSize(wxSize(FromDIP(24), FromDIP(24)));

    m_print_btn->SetTextLayout(SideButton::EHorizontalOrientation::HO_Left, FromDIP(15));
    m_print_btn->SetCornerRadius(FromDIP(12));
    m_print_btn->SetExtraSize(wxSize(FromDIP(38), FromDIP(10)));
    m_print_btn->SetMinSize(wxSize(-1, FromDIP(24)));

    m_print_option_btn->SetTextLayout(SideButton::EHorizontalOrientation::HO_Center);
    m_print_option_btn->SetCornerRadius(FromDIP(12));
    m_print_option_btn->SetExtraSize(wxSize(FromDIP(10), FromDIP(10)));
    m_print_option_btn->SetIconOffset(FromDIP(2));
    m_print_option_btn->SetMinSize(wxSize(FromDIP(24), FromDIP(24)));

    auto bg = StateColor::darkModeColorFor(wxColour("#3B4446"));
    m_slice_btn->GetParent()->SetBackgroundColour(bg);
    m_print_btn->GetParent()->SetBackgroundColour(bg);
}

void MainFrame::update_slice_print_status(SlicePrintEventType event, bool can_slice, bool can_print)
{
    bool enable_print = true, enable_slice = true;

    if (!can_slice)
    {
        if (m_slice_select == eSlicePlate)
            enable_slice = false;
    }
    if (!can_print)
        enable_print = false;


    if (enable_print)
    {
        enable_print = get_enable_print_status();
    }

    if (enable_slice)
    {
        enable_slice = get_enable_slice_status();
    }

    bool old_slice_status = m_slice_btn->IsEnabled();

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" m_slice_select %1%: can_slice= %2%, can_print %3%, enable_slice %4%, enable_print %5% ")%m_slice_select % can_slice %can_print %enable_slice %enable_print;
    m_print_btn->Enable(enable_print);
    m_slice_btn->Enable(enable_slice);
    m_slice_enable = enable_slice;
    m_print_enable = enable_print;

    if (!old_slice_status && enable_slice)
        m_plater->reset_check_status();

    if (wxGetApp().mainframe)
        wxGetApp().plater()->update_title_dirty_status();
}


void MainFrame::on_dpi_changed(const wxRect& suggested_rect)
{
    wxGetApp().update_fonts(this);
    this->SetFont(this->normal_font());

#ifdef _MSW_DARK_MODE
    if (!wxGetApp().tabs_as_menu())
        dynamic_cast<Notebook*>(m_tabpanel)->Rescale();
#endif

#ifndef __APPLE__
    m_topbar->Rescale();
#endif

    m_tabpanel->Rescale();

    update_side_button_style();

    m_slice_btn->Rescale();
    m_print_btn->Rescale();
    m_slice_option_btn->Rescale();
    m_print_option_btn->Rescale();

    wxGetApp().plater()->msw_rescale();

    m_param_panel->msw_rescale();
    ProjectPanel::when_built([](ProjectPanel& project) { project.msw_rescale(); });
    MonitorPanel::when_built([](MonitorPanel& monitor) { monitor.msw_rescale(); });
    MultiMachinePage::when_built([](MultiMachinePage& multi_machine) { multi_machine.msw_rescale(); });
    CalibrationPanel::when_built([](CalibrationPanel& calibration) { calibration.msw_rescale(); });
#ifdef SLIC3R_CAD
    DesignPanel::when_built([](DesignPanel& design) { design.msw_rescale(); });
#endif

#if 0
    for (size_t id = 0; id < m_menubar->GetMenuCount(); id++)
        msw_rescale_menu(m_menubar->GetMenu(id));
#endif

    const bool is_maximized = this->IsMaximized();
    if (is_maximized)
        this->Maximize(false);

    const wxSize& sz = this->GetSize();
    this->SetSize(sz.x + 1, sz.y + 1);
    this->SetSize(sz);

    this->Maximize(is_maximized);

    fit_tab_labels();
}

void MainFrame::on_sys_color_changed()
{
    wxBusyCursor wait;

    wxGetApp().init_label_colours();

#ifndef __WINDOWS__
    wxGetApp().force_colors_update();
    wxGetApp().update_ui_from_settings();
#endif

#ifdef __WXMSW__
    wxGetApp().UpdateDarkUI(m_tabpanel);
#ifdef _MSW_DARK_MODE
    if (!wxGetApp().tabs_as_menu())
        dynamic_cast<Notebook*>(m_tabpanel)->Rescale();
#endif
#endif

    if (DiffPresetDialog* dialog = DiffPresetDialog::if_built())
        dialog->on_sys_color_changed();

    m_tabpanel->Rescale();
    m_param_panel->msw_rescale();

    wxGetApp().plater()->sys_color_changed();
    MonitorPanel::when_built([](MonitorPanel& monitor) { monitor.on_sys_color_changed(); });
    CalibrationPanel::when_built([](CalibrationPanel& calibration) { calibration.on_sys_color_changed(); });
#ifdef SLIC3R_CAD
    DesignPanel::when_built([](DesignPanel& design) { design.on_sys_color_changed(); });
#endif
    for (auto tab : wxGetApp().tabs_list)
        tab->sys_color_changed();
    for (auto tab : wxGetApp().model_tabs_list)
        tab->sys_color_changed();
    wxGetApp().plate_tab->sys_color_changed();

    MenuFactory::sys_color_changed(m_menubar);

    WebView::RecreateAll();

    this->Refresh();
}

#ifdef __APPLE__
static const wxString sep = " - ";
#else
static const wxString sep = "\t";
#endif

wxString MainFrame::shortcut_label(const wxString& label, Shortcut shortcut, bool accelerator)
{
    const ShortcutRegistry& shortcuts = wxGetApp().shortcuts();
    if (accelerator) {
        const std::string accel = shortcuts.accelerator(shortcut);
        if (!accel.empty())
            return label + "	" + from_u8(accel);
    }
    const std::string text = shortcuts.display(shortcut);
    return text.empty() ? label : label + sep + from_u8(text);
}

void MainFrame::update_shortcut_labels()
{
    for (const ShortcutMenuItem& entry : m_shortcut_menu_items)
        entry.item->SetItemLabel(shortcut_label(entry.label, entry.shortcut, entry.accelerator));
}

wxMenu* MainFrame::generate_help_menu()
{
    wxMenu* helpMenu = new wxMenu();

    append_shortcut_item(helpMenu, Shortcut::KeyboardShortcuts, false, _L("Keyboard Shortcuts"), _L("Show the list of keyboard shortcuts"),
        [](wxCommandEvent&) { wxGetApp().keyboard_shortcuts(ShortcutContext::Global); });
    append_menu_item(helpMenu, wxID_ANY, _L("Setup Wizard"), _L("Setup Wizard"), [](wxCommandEvent &) {wxGetApp().ShowUserGuide();});

    helpMenu->AppendSeparator();

    append_menu_item(helpMenu, wxID_ANY, _L("Show Configuration Folder"), _L("Show Configuration Folder"),
        [](wxCommandEvent&) { Slic3r::GUI::desktop_open_datadir_folder(); });

    helpMenu->AppendSeparator();

    append_menu_item(helpMenu, wxID_ANY, _L("Troubleshoot Center"), "",
        [](wxCommandEvent&) { wxGetApp().troubleshoot(); });

    append_menu_item(helpMenu, wxID_ANY, _L("Open Network Test"), _L("Open Network Test"), [](wxCommandEvent&) {
            NetworkTestDialog dlg(wxGetApp().mainframe);
            dlg.ShowModal();
        });

    if (wxGetApp().is_editor())
        append_menu_item(helpMenu, wxID_ANY, _L("Benchmark 3D Scene"), _L("Measure how fast the 3D scene renders in Prepare and Preview"),
            [](wxCommandEvent&) { run_scene_benchmark(); });

    helpMenu->AppendSeparator();

    append_menu_item(helpMenu, wxID_ANY, _L("Show Tip of the Day"), _L("Show Tip of the Day"), [](wxCommandEvent&) {
        wxGetApp().plater()->get_dailytips()->open();
        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
        });

    append_menu_item(helpMenu, wxID_ANY, _L("Check for Updates"), _L("Check for Updates"),
        [](wxCommandEvent&) {
            wxGetApp().check_new_version_sf(true, 1);
        }, "", nullptr, []() {
            return true;
        });

#ifndef __APPLE__
    wxString about_title = wxString::Format(_L("&About %s"), SLIC3R_APP_FULL_NAME);
    append_menu_item(helpMenu, wxID_ANY, about_title, about_title,
            [](wxCommandEvent&) { Slic3r::GUI::about(); });
#endif

    return helpMenu;
}


static void add_common_publish_menu_items(wxMenu* publish_menu, MainFrame* mainFrame)
{
#ifndef __WINDOWS__
    append_menu_item(publish_menu, wxID_ANY, _L("Upload Models"), _L("Upload Models"),
        [](wxCommandEvent&) {
            if (!wxGetApp().getAgent()) {
                BOOST_LOG_TRIVIAL(info) << "publish: no agent";
                return;
            }

            json j;
            NetworkAgent* agent = GUI::wxGetApp().getAgent();

            wxGetApp().open_publish_page_dialog();
        });

    append_menu_item(publish_menu, wxID_ANY, _L("Download Models"), _L("Download Models"),
        [](wxCommandEvent&) {
            if (!wxGetApp().getAgent()) {
                BOOST_LOG_TRIVIAL(info) << "publish: no agent";
                return;
}

            wxGetApp().open_mall_page_dialog();
        });
#endif
}

void MainFrame::add_common_view_menu_items(wxMenu* view_menu, std::function<bool(void)> can_change_view)
{
    append_shortcut_item(view_menu, Shortcut::ViewDefault, true, _L("Default View"), _L("Default View"), [this](wxCommandEvent&) {
        select_view("plate");
        plater()->get_current_canvas3D()->zoom_to_bed();
        },
        "", nullptr, [can_change_view]() { return can_change_view(); }, this);
    //TRN To be shown in the main menu View->Top
    append_shortcut_item(view_menu, Shortcut::ViewTop, true, _L_CONTEXT("Top", "Camera View"), _L("Top View"), [this](wxCommandEvent&) { select_view("top"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, this);
    //TRN To be shown in the main menu View->Bottom
    append_shortcut_item(view_menu, Shortcut::ViewBottom, true, _L_CONTEXT("Bottom", "Camera View"), _L("Bottom View"), [this](wxCommandEvent&) { select_view("bottom"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, this);
    append_shortcut_item(view_menu, Shortcut::ViewFront, true, _L_CONTEXT("Front", "Camera View"), _L("Front View"), [this](wxCommandEvent&) { select_view("front"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, this);
    append_shortcut_item(view_menu, Shortcut::ViewRear, true, _L_CONTEXT("Rear", "Camera View"), _L("Rear View"), [this](wxCommandEvent&) { select_view("rear"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, this);
    append_shortcut_item(view_menu, Shortcut::ViewLeft, true, _L_CONTEXT("Left", "Camera View"), _L("Left View"), [this](wxCommandEvent &) { select_view("left"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, this);
    append_shortcut_item(view_menu, Shortcut::ViewRight, true, _L_CONTEXT("Right", "Camera View"), _L("Right View"), [this](wxCommandEvent &) { select_view("right"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, this);
}

void MainFrame::init_menubar_as_editor()
{
#ifdef __APPLE__
    m_menubar = new wxMenuBar();
#endif

    wxMenu* fileMenu = new wxMenu;
    {
#ifdef __APPLE__
        append_menu_item(fileMenu, wxID_ANY, _L("New Window"), _L("Start a new window"),
                         [](wxCommandEvent&) { start_new_slicer(); }, "", nullptr,
                         [this] { return m_plater != nullptr && wxGetApp().app_config->get("app", "single_instance") == "false"; }, this);
#endif
        append_shortcut_item(fileMenu, Shortcut::NewProject, true, _L("New Project"), _L("Start a new project"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->new_project(); }, "", nullptr,
            [this](){return can_start_new_project(); }, this);

#ifndef __APPLE__
        append_shortcut_item(fileMenu, Shortcut::OpenProject, true, _L("Open Project") + dots, _L("Open a project file"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->load_project(); }, "menu_open", nullptr,
            [this](){return can_open_project(); }, this);
#else
        append_shortcut_item(fileMenu, Shortcut::OpenProject, true, _L("Open Project") + dots, _L("Open a project file"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->load_project(); }, "", nullptr,
            [this](){return can_open_project(); }, this);
#endif

        wxMenu* recent_projects_menu = new wxMenu();
        wxMenuItem* recent_projects_submenu = append_submenu(fileMenu, recent_projects_menu, wxID_ANY, _L("Recent files"), "");
        m_recent_projects.UseMenu(recent_projects_menu);
        Bind(wxEVT_MENU, [this](wxCommandEvent& evt) {
            size_t file_id = evt.GetId() - wxID_FILE1;
            wxString filename = m_recent_projects.GetHistoryFile(file_id);
                open_recent_project(file_id, filename);
            }, wxID_FILE1, wxID_FILE1 + 49);

        std::vector<std::string> recent_projects = wxGetApp().app_config->get_recent_projects();
        std::reverse(recent_projects.begin(), recent_projects.end());
        for (const std::string& project : recent_projects)
        {
            m_recent_projects.AddFileToHistory(from_u8(project));
        }
        m_recent_projects.LoadThumbnails();

        Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Enable(can_open_project() && (m_recent_projects.GetCount() > 0)); }, recent_projects_submenu->GetId());

#ifndef __APPLE__
        append_shortcut_item(fileMenu, Shortcut::SaveProject, true, _L("Save Project"), _L("Save current project to file"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->save_project(); }, "menu_save", nullptr,
            [this](){return m_plater != nullptr && can_save(); }, this);
#else
        append_shortcut_item(fileMenu, Shortcut::SaveProject, true, _L("Save Project"), _L("Save current project to file"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->save_project(); }, "", nullptr,
            [this](){return m_plater != nullptr && can_save(); }, this);
#endif

#ifndef __APPLE__
        append_shortcut_item(fileMenu, Shortcut::SaveProjectAs, true, _L("Save Project as") + dots, _L("Save current project as"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->save_project(true); }, "menu_save", nullptr,
            [this](){return m_plater != nullptr && can_save_as(); }, this);
#else
        append_shortcut_item(fileMenu, Shortcut::SaveProjectAs, true, _L("Save Project as") + dots, _L("Save current project as"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->save_project(true); }, "", nullptr,
            [this](){return m_plater != nullptr && can_save_as(); }, this);
#endif

        fileMenu->AppendSeparator();
        auto publish_handler = [this](wxCommandEvent&) { publish_project(); };

#ifndef __APPLE__
        append_shortcut_item(fileMenu, Shortcut::Publish3mf, true, _L("Publish 3MF") + dots, _L("Export a 3MF file with the selected settings embedded"),
            publish_handler, "menu_publish", nullptr,
            [this](){return can_export_model(); }, this);
#else
        append_shortcut_item(fileMenu, Shortcut::Publish3mf, true, _L("Publish 3MF") + dots, _L("Export a 3MF file with the selected settings embedded"),
            publish_handler, "", nullptr,
            [this](){return can_export_model(); }, this);
#endif


        fileMenu->AppendSeparator();

        wxMenu *import_menu = new wxMenu();
#ifndef __APPLE__
        append_shortcut_item(import_menu, Shortcut::ImportModel, true, _L("Import 3MF/STL/STEP/SVG/OBJ/AMF") + dots, _L("Load a model"),
            [this](wxCommandEvent&) { if (m_plater) {
            m_plater->add_file();
        } }, "menu_import", nullptr,
            [this](){return can_add_models(); }, this);
#else
        append_shortcut_item(import_menu, Shortcut::ImportModel, true, _L("Import 3MF/STL/STEP/SVG/OBJ/AMF") + dots, _L("Load a model"),
            [this](wxCommandEvent&) { if (m_plater) { m_plater->add_model(); } }, "", nullptr,
            [this](){return can_add_models(); }, this);
#endif
        append_menu_item(import_menu, wxID_ANY, _L("Import ZIP Archive") + dots, _L("Load models contained within a ZIP archive"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->import_zip_archive(); }, "menu_import", nullptr,
            [this]() { return can_add_models(); });
        append_menu_item(import_menu, wxID_ANY, _L("Import Configs") + dots , _L("Load configs"),
            [this](wxCommandEvent&) { load_config_file(); }, "menu_import", nullptr,
            [](){return true; }, this);

        append_submenu(fileMenu, import_menu, wxID_ANY, _L("Import"), "");


        wxMenu* export_menu = new wxMenu();
        append_menu_item(export_menu, wxID_ANY, _L("Export all objects as one STL") + dots, _L("Export all objects as one STL"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->export_stl(); }, "menu_export_stl", nullptr,
            [this](){return can_export_model(); }, this);
        append_menu_item(export_menu, wxID_ANY, _L("Export all objects as STLs") + dots, _L("Export all objects as STLs"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->export_stl(false, false, true); }, "menu_export_stl", nullptr,
            [this](){return can_export_model(); }, this);
        append_menu_item(export_menu, wxID_ANY, _L("Export all objects as one DRC") + dots, _L("Export all objects as one DRC"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->export_stl(false, false, false, FT_DRC); }, "menu_export_stl", nullptr,
            [this](){return can_export_model(); }, this);
        append_menu_item(export_menu, wxID_ANY, _L("Export all objects as DRCs") + dots, _L("Export all objects as DRCs"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->export_stl(false, false, true, FT_DRC); }, "menu_export_stl", nullptr,
            [this](){return can_export_model(); }, this);
        append_menu_item(export_menu, wxID_ANY, _L("Export Generic 3MF") + dots , _L("Export 3MF file without using some 3mf-extensions"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->export_core_3mf(); }, "menu_export_sliced_file", nullptr,
            [this](){return can_export_model(); }, this);
        append_shortcut_item(export_menu, Shortcut::ExportSlicedFile, true, _L("Export plate sliced file") + dots, _L("Export current sliced file"),
            [this](wxCommandEvent&) { if (m_plater) wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_SLICED_FILE)); }, "menu_export_sliced_file", nullptr,
            [this](){return can_export_gcode(); }, this);

        append_menu_item(export_menu, wxID_ANY, _L("Export all plate sliced file") + dots , _L("Export all plate sliced file"),
            [this](wxCommandEvent&) { if (m_plater) wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_ALL_SLICED_FILE)); }, "menu_export_sliced_file", nullptr,
            [this]() {return can_export_all_gcode(); }, this);

        append_menu_item(export_menu, wxID_ANY, _L("Export G-code") + dots , _L("Export current plate as G-code"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->export_gcode(false); }, "menu_export_gcode", nullptr,
            [this]() {return can_export_gcode(); }, this);

        append_menu_item(export_menu, wxID_ANY, _L("Export toolpaths as OBJ") + dots, _L("Export toolpaths as OBJ"),
            [this](wxCommandEvent&) { if (m_plater != nullptr) m_plater->export_toolpaths_to_obj(); }, "menu_export_toolpaths", nullptr,
            [this]() {return can_export_toolpaths(); }, this);

        append_menu_item(
            export_menu, wxID_ANY, _L("Export Preset Bundle") + dots , _L("Export current configuration to files"),
            [this](wxCommandEvent &) { export_config(); },
            "menu_export_config", nullptr,
            []() { return true; }, this);

        append_submenu(fileMenu, export_menu, wxID_ANY, _L("Export"), "");

        fileMenu->AppendSeparator();

#ifndef __APPLE__
        append_menu_item(fileMenu, wxID_EXIT, _L("Quit"), wxString::Format(_L("Quit")),
            [this](wxCommandEvent&) { Close(false); }, "menu_exit", nullptr);
#else
        append_menu_item(fileMenu, wxID_EXIT, _L("Quit"), wxString::Format(_L("Quit")),
            [this](wxCommandEvent&) { Close(false); }, "", nullptr);
#endif
    }

    wxMenu* editMenu = nullptr;
    if (m_plater != nullptr)
    {
        editMenu = new wxMenu();

    auto handle_key_event = [](wxKeyEvent& evt) {
        if (wxGetApp().imgui()->update_key_data(evt)) {
            wxGetApp().plater()->get_current_canvas3D()->render();
            return true;
        }
        return false;
    };
#ifndef __APPLE__
        append_shortcut_item(editMenu, Shortcut::Undo, true, _L("Undo"),
            _L("Undo"), [this](wxCommandEvent&) {
#ifdef SLIC3R_CAD
                if (DesignPanel* dp = shown_design_panel()) { dp->menu_undo_redo(false); return; }
#endif
                m_plater->undo(); },
            "menu_undo", nullptr, [this](){
#ifdef SLIC3R_CAD
                if (DesignPanel* dp = shown_design_panel()) return dp->menu_can_undo_redo(false);
#endif
                return m_plater->can_undo(); }, this);
        append_shortcut_item(editMenu, Shortcut::Redo, true, _L("Redo"),
            _L("Redo"), [this](wxCommandEvent&) {
#ifdef SLIC3R_CAD
                if (DesignPanel* dp = shown_design_panel()) { dp->menu_undo_redo(true); return; }
#endif
                m_plater->redo(); },
            "menu_redo", nullptr, [this](){
#ifdef SLIC3R_CAD
                if (DesignPanel* dp = shown_design_panel()) return dp->menu_can_undo_redo(true);
#endif
                return m_plater->can_redo(); }, this);
        editMenu->AppendSeparator();
        append_shortcut_item(editMenu, Shortcut::Cut, true, _L("Cut"),
            _L("Cut selection to clipboard"), [this](wxCommandEvent&) {m_plater->cut_selection_to_clipboard(); },
            "menu_cut", nullptr, [this]() {return m_plater->can_copy_to_clipboard(); }, this);
        append_shortcut_item(editMenu, Shortcut::Copy, true, _L("Copy"),
            _L("Copy selection to clipboard"), [this](wxCommandEvent&) { m_plater->copy_selection_to_clipboard(); },
            "menu_copy", nullptr, [this](){return m_plater->can_copy_to_clipboard(); }, this);
        append_shortcut_item(editMenu, Shortcut::Paste, true, _L("Paste"),
            _L("Paste clipboard"), [this](wxCommandEvent&) { m_plater->paste_from_clipboard(); },
            "menu_paste", nullptr, [this](){return m_plater->can_paste_from_clipboard(); }, this);
        append_shortcut_item(editMenu, Shortcut::DeleteSelected, true, _L("Delete Selected"),
            _L("Deletes the current selection"),[this](wxCommandEvent&) { m_plater->remove_selected(); },
            "menu_remove", nullptr, [this](){return can_delete(); }, this);
        append_shortcut_item(editMenu, Shortcut::DeleteAll, true, _L("Delete All"),
            _L("Deletes all objects"),[this](wxCommandEvent&) { m_plater->delete_all_objects_from_model(); },
            "menu_remove", nullptr, [this](){return can_delete_all(); }, this);
        editMenu->AppendSeparator();
        append_shortcut_item(editMenu, Shortcut::CloneSelected, true, _L("Clone Selected"),
            _L("Clone copies of selections"),[this](wxCommandEvent&) {
                m_plater->clone_selection();
            },
            "menu_remove", nullptr, [this](){return can_clone(); }, this);
        editMenu->AppendSeparator();
        append_menu_item(editMenu, wxID_ANY, _L("Duplicate Current Plate"),
            _L("Duplicate the current plate"),[this](wxCommandEvent&) {
                m_plater->duplicate_plate();
            },
            "menu_remove", nullptr, [](){return true;}, this);
        editMenu->AppendSeparator();
#else
        append_shortcut_item(editMenu, Shortcut::Undo, false, _L("Undo"),
            _L("Undo"), [this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.SetControlDown(true);
                e.m_keyCode = 'Z';
                if (handle_key_event(e)) {
                    return;
                }
#ifdef SLIC3R_CAD
                if (DesignPanel* dp = shown_design_panel()) { dp->menu_undo_redo(false); return; }
#endif
                m_plater->undo(); },
            "", nullptr, [this](){
#ifdef SLIC3R_CAD
                if (DesignPanel* dp = shown_design_panel()) return dp->menu_can_undo_redo(false);
#endif
                return m_plater->can_undo(); }, this);
        append_shortcut_item(editMenu, Shortcut::Redo, false, _L("Redo"),
            _L("Redo"), [this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.SetControlDown(true);
                e.m_keyCode = 'Y';
                if (handle_key_event(e)) {
                    return;
                }
#ifdef SLIC3R_CAD
                if (DesignPanel* dp = shown_design_panel()) { dp->menu_undo_redo(true); return; }
#endif
                m_plater->redo(); },
            "", nullptr, [this](){
#ifdef SLIC3R_CAD
                if (DesignPanel* dp = shown_design_panel()) return dp->menu_can_undo_redo(true);
#endif
                return m_plater->can_redo(); }, this);
        editMenu->AppendSeparator();
        append_shortcut_item(editMenu, Shortcut::Cut, false, _L("Cut"),
            _L("Cut selection to clipboard"), [this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.SetControlDown(true);
                e.m_keyCode = 'X';
                if (handle_key_event(e)) {
                    return;
                }
                m_plater->cut_selection_to_clipboard(); },
            "", nullptr, [this]() {return m_plater->can_copy_to_clipboard(); }, this);
        append_shortcut_item(editMenu, Shortcut::Copy, false, _L("Copy"),
            _L("Copy selection to clipboard"), [this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.SetControlDown(true);
                e.m_keyCode = 'C';
                if (handle_key_event(e)) {
                    return;
                }
                m_plater->copy_selection_to_clipboard(); },
            "", nullptr, [this](){return m_plater->can_copy_to_clipboard(); }, this);
        append_shortcut_item(editMenu, Shortcut::Paste, false, _L("Paste"),
            _L("Paste clipboard"), [this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.SetControlDown(true);
                e.m_keyCode = 'V';
                if (handle_key_event(e)) {
                    return;
                }
                m_plater->paste_from_clipboard(); },
            "", nullptr, [this](){return m_plater->can_paste_from_clipboard(); }, this);
#if 0
        append_shortcut_item(editMenu, Shortcut::DeleteSelected, true, _L("Delete Selected"),
            _L("Deletes the current selection"),[this](wxCommandEvent&) {
                m_plater->remove_selected();
            },
            "", nullptr, [this](){return can_delete(); }, this);
#endif
        append_shortcut_item(editMenu, Shortcut::DeleteAll, true, _L("Delete All"),
            _L("Deletes all objects"),[this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.SetControlDown(true);
                e.m_keyCode = 'D';
                if (handle_key_event(e)) {
                    return;
                }
                m_plater->delete_all_objects_from_model(); },
            "", nullptr, [this](){return can_delete_all(); }, this);
        editMenu->AppendSeparator();
        append_shortcut_item(editMenu, Shortcut::CloneSelected, true, _L("Clone Selected"),
            _L("Clone copies of selections"),[this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.SetControlDown(true);
                e.m_keyCode = 'M';
                if (handle_key_event(e)) {
                    return;
                }
                m_plater->clone_selection();
            },
            "", nullptr, [this](){return can_clone(); }, this);
        editMenu->AppendSeparator();
        append_menu_item(editMenu, wxID_ANY, _L("Duplicate Current Plate"),
            _L("Duplicate the current plate"),[this](wxCommandEvent&) {
                m_plater->duplicate_plate();
            },
            "", nullptr, [](){return true;}, this);
        editMenu->AppendSeparator();

#endif

        append_shortcut_item(editMenu, Shortcut::SelectAll, false, _L("Select All"),
            _L("Selects all objects"), [this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.SetControlDown(true);
                e.m_keyCode = 'A';
                if (handle_key_event(e)) {
                    return;
                }
                m_plater->select_all(); },
            "", nullptr, [this](){return can_select(); }, this);
        append_menu_item(editMenu, wxID_ANY, _L("Deselect All") + sep + _L_CONTEXT("Esc", "Keyboard Shortcut"),
            _L("Deselects all objects"), [this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.m_keyCode = WXK_ESCAPE;
                if (handle_key_event(e)) {
                    return;
                }
                m_plater->deselect_all(); },
            "", nullptr, [this](){return can_deselect(); }, this);
    }

    wxMenu* viewMenu = nullptr;
    if (m_plater) {
        viewMenu = new wxMenu();
        add_common_view_menu_items(viewMenu, std::bind(&MainFrame::can_change_view, this));
        viewMenu->AppendSeparator();

        wxWindowID camera_id_base = wxWindow::NewControlId(int(wxID_CAMERA_COUNT));
        auto perspective_item = append_menu_radio_item(viewMenu, wxID_CAMERA_PERSPECTIVE + camera_id_base, _L("Use Perspective View"), _L("Use Perspective View"),
            [](wxCommandEvent&) {
                wxGetApp().app_config->set_bool("use_perspective_camera", true);
                wxGetApp().update_ui_from_settings();
            }, nullptr);
        auto orthogonal_item = append_menu_radio_item(viewMenu, wxID_CAMERA_ORTHOGONAL + camera_id_base, _L("Use Orthogonal View"), _L("Use Orthogonal View"),
            [](wxCommandEvent&) {
                wxGetApp().app_config->set_bool("use_perspective_camera", false);
                wxGetApp().update_ui_from_settings();
            }, nullptr);
        this->Bind(wxEVT_UPDATE_UI, [viewMenu, camera_id_base](wxUpdateUIEvent& evt) {
                if (wxGetApp().app_config->get("use_perspective_camera").compare("true") == 0)
                    viewMenu->Check(wxID_CAMERA_PERSPECTIVE + camera_id_base, true);
                else
                    viewMenu->Check(wxID_CAMERA_ORTHOGONAL + camera_id_base, true);
            }, perspective_item->GetId());
        append_menu_check_item(viewMenu, wxID_ANY, _L("Auto Perspective"), _L("Automatically switch between orthographic and perspective when changing from top/bottom/side views."),
            [this](wxCommandEvent&) {
                wxGetApp().app_config->set_bool("auto_perspective", !wxGetApp().app_config->get_bool("auto_perspective"));
                m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT));
            },
            this, [this]() { return is_prepare_or_preview_tab(); },
            []() { return wxGetApp().app_config->get_bool("auto_perspective"); }, this);

        viewMenu->AppendSeparator();
        wxMenuItem* gcode_window = append_menu_check_item(viewMenu, wxID_ANY, shortcut_label(_L("Show &G-code Window"), Shortcut::ToggleGcodeWindow, true), _L("Show G-code window in Preview scene."),
            [this](wxCommandEvent &) {
                wxGetApp().toggle_show_gcode_window();
                m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT));
            },
            this, [this]() { return m_tabpanel->GetSelectedPageName() == TAB_ID_PREVIEW; },
            []() { return wxGetApp().show_gcode_window(); }, this);
        m_shortcut_menu_items.push_back({ gcode_window, Shortcut::ToggleGcodeWindow, _L("Show &G-code Window"), true });

        append_menu_check_item(
            viewMenu, wxID_ANY, _L("Show 3D Navigator"), _L("Show 3D navigator in Prepare and Preview scene."),
            [this](wxCommandEvent&) {
                wxGetApp().toggle_show_3d_navigator();
                m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT));
            },
            this, [this]() { return is_prepare_or_preview_tab(); },
            []() { return wxGetApp().show_3d_navigator(); }, this);

        append_menu_check_item(viewMenu, wxID_ANY, _L("Show Gridlines"), _L("Show Gridlines on plate"),
            [this](wxCommandEvent&) {
                wxGetApp().toggle_show_plate_gridlines();
                m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT));
            }, this,
            [this]() { return is_prepare_or_preview_tab(); },
            []() { return wxGetApp().show_plate_gridlines(); }, this);

        append_menu_check_item(viewMenu, wxID_ANY, _L("Show IDEX/IQEX Toolhead"),
            _L("Show the IDEX/IQEX toolhead representation around each active nozzle in the G-code preview."),
            [this](wxCommandEvent&) {
                const bool current = wxGetApp().app_config->get("show_imex_toolhead_boxes") != "false";
                wxGetApp().app_config->set("show_imex_toolhead_boxes", current ? "false" : "true");
                m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT));
            },
            this,
            [this]() {
                if (m_tabpanel->GetSelectedPageName() != TAB_ID_PREVIEW) return false;
                if (auto* pb = wxGetApp().preset_bundle) {
                    auto* o = pb->printers.get_edited_preset().config.option<ConfigOptionBool>("is_imex");
                    return o && o->value;
                }
                return false;
            },
            []() { return wxGetApp().app_config->get("show_imex_toolhead_boxes") != "false"; },
            this);

        append_menu_item(
            viewMenu, wxID_ANY, _L("Reset Window Layout"), _L("Reset to default window layout"),
            [this](wxCommandEvent&) { m_plater->reset_window_layout(); }, "", this,
            [this]() {
#ifdef SLIC3R_CAD
                if (shown_design_panel() != nullptr)
                    return true;
#endif
                return is_prepare_or_preview_tab() && m_plater->is_sidebar_enabled();
            },
            this);

        viewMenu->AppendSeparator();
        wxMenuItem* show_labels = append_menu_check_item(viewMenu, wxID_ANY, shortcut_label(_L("Show &Labels"), Shortcut::ShowLabels, true), _L("Show object labels in 3D scene."),
            [this](wxCommandEvent&) { m_plater->show_view3D_labels(!m_plater->are_view3D_labels_shown()); m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT)); }, this,
            [this]() { return m_plater->is_view3D_shown(); }, [this]() { return m_plater->are_view3D_labels_shown(); }, this);
        m_shortcut_menu_items.push_back({ show_labels, Shortcut::ShowLabels, _L("Show &Labels"), true });

        append_menu_check_item(viewMenu, wxID_ANY, _L("Show &Overhang"), _L("Show object overhang highlight in 3D scene."),
            [this](wxCommandEvent &) {
                m_plater->show_view3D_overhang(!m_plater->is_view3D_overhang_shown());
                m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT));
            },
            this, [this]() { return m_plater->is_view3D_shown(); }, [this]() { return m_plater->is_view3D_overhang_shown(); }, this);

        append_menu_check_item(viewMenu, wxID_ANY, _L("Show &Non-planar Surfaces"),
            _L("Color top surfaces by their slope against the print head's limit and show where curved top shells will be printed."),
            [this](wxCommandEvent &) {
                GLCanvas3D *canvas = m_plater->get_view3D_canvas3D();
                canvas->show_nonplanar_view(!canvas->is_nonplanar_view_shown());
                wxGetApp().app_config->set_bool("show_nonplanar_view", canvas->is_nonplanar_view_shown());
                m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT));
            },
            this, [this]() { return m_plater->is_view3D_shown(); },
            [this]() { return m_plater->is_view3D_shown() && m_plater->get_view3D_canvas3D()->is_nonplanar_view_shown(); }, this);

        append_menu_check_item(
            viewMenu, wxID_ANY, _L("Show Selected Outline (beta)"), _L("Show outline around selected object in 3D scene."),
            [this](wxCommandEvent&) {
                wxGetApp().toggle_show_outline();
                m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT));
            },
            this, [this]() { return m_tabpanel->GetSelectedPageName() == TAB_ID_PREPARE; },
            []() { return wxGetApp().show_outline(); }, this);

    }

    wxWindowID config_id_base = wxWindow::NewControlId(int(ConfigMenuCnt));
#ifdef __APPLE__
    wxWindowID bambu_studio_id_base = wxWindow::NewControlId(int(2));
    wxMenu* parent_menu = m_menubar->OSXGetAppleMenu();
#else
    wxMenu* parent_menu = m_topbar->GetTopMenu();
#endif

#ifdef __APPLE__
    wxString about_title = wxString::Format(_L("&About %s"), SLIC3R_APP_FULL_NAME);
    append_menu_item(
        parent_menu, wxID_ANY, _L(about_title), "",
        [](wxCommandEvent &) { Slic3r::GUI::about();},
        "", nullptr, []() { return true; }, this, 0);
    append_shortcut_item(
        parent_menu, Shortcut::Preferences, true, _L("Preferences"), "",
        [](wxCommandEvent &) {
            wxGetApp().open_preferences();
        },
        "", nullptr, []() { return true; }, this, 1);
#endif
    auto helpMenu = generate_help_menu();

#ifndef __APPLE__
    m_topbar->SetFileMenu(fileMenu);
    if (editMenu)
        m_topbar->AddDropDownSubMenu(editMenu, _L_CONTEXT("Edit", "Menu"));
    if (viewMenu)
        m_topbar->AddDropDownSubMenu(viewMenu, _L("View"));

    append_shortcut_item(
        m_topbar->GetTopMenu(), Shortcut::Preferences, true, _L("Preferences"), "",
        [](wxCommandEvent &) {
            wxGetApp().open_preferences();
        },
        "", nullptr, []() { return true; }, this);

    auto top_menu = m_topbar->GetTopMenu();
    top_menu->AppendSeparator();

    append_shortcut_item(
        top_menu, Shortcut::SpeedDial, false, _L("Open Speed Dial"), "",
        [](wxCommandEvent &) { wxGetApp().open_speed_dial(); },
        "", nullptr, []() { return true; }, this);
    top_menu->AppendSeparator();

    append_menu_item(
        top_menu, wxID_ANY, _L("Preset Bundle") + "\t", "",
        [this](wxCommandEvent &) {
            wxGetApp().open_presetbundledialog();
            plater()->get_current_canvas3D()->force_set_focus();
        },
        "", nullptr, []() { return true; }, this);

    append_menu_item(
        top_menu, wxID_ANY, _L("Sync Presets"), _L("Pull and apply the latest presets from OrcaCloud"),
        [this](wxCommandEvent&) {
            if (!wxGetApp().is_user_login()) {
                MessageDialog info_dlg(this, _L("You must be logged in to sync presets from cloud."),
                    _L("Sync Presets"), wxOK | wxICON_INFORMATION);
                info_dlg.ShowModal();
                return;
            }
            if (m_plater)
                m_plater->get_notification_manager()->push_notification(
                    into_u8(_L("Syncing presets from cloud\u2026")));
            wxGetApp().restart_sync_user_preset();
        }, "", nullptr,
        []() {
            return wxGetApp().is_user_login() && !wxGetApp().app_config->get_stealth_mode();
        }, this);

    top_menu->AppendSeparator();
    append_menu_item(
        top_menu, wxID_ANY, _L("Plugins") + "\t", "",
        [](wxCommandEvent &) {
            wxGetApp().open_plugins_dialog();
        },
        "", nullptr, []() { return true; }, this);

    top_menu->AppendSeparator();
    m_topbar->AddDropDownSubMenu(helpMenu, _L("Help"));

    append_menu_item(m_topbar->GetCalibMenu(), wxID_ANY, _L("Temperature"), _L("Temperature Calibration"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::Temperature); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    append_menu_item(m_topbar->GetCalibMenu(), wxID_ANY, _L("Max flowrate"), _L("Max flowrate"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::MaxVolumetric); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    append_menu_item(m_topbar->GetCalibMenu(), wxID_ANY, _L("Pressure advance"), _L("Pressure advance"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::PressureAdvance); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    append_menu_item(m_topbar->GetCalibMenu(), wxID_ANY, _L("Flow ratio"), _L("Flow Rate Calibration"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::FlowRatio); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    append_menu_item(m_topbar->GetCalibMenu(), wxID_ANY, _L("Retraction"), _L("Retraction"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::Retraction); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    append_menu_item(m_topbar->GetCalibMenu(), wxID_ANY, _L("Cornering"), _L("Cornering calibration"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::Cornering); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    auto input_shaping_menu = new wxMenu();
    append_menu_item(
        input_shaping_menu, wxID_ANY, _L("Input Shaping Frequency"), _L("Input Shaping Frequency"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::InputShapingFreq); },
        "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    append_menu_item(
        input_shaping_menu, wxID_ANY, _L("Input Shaping Damping/zeta factor"), _L("Input Shaping Damping/zeta factor"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::InputShapingDamp); },
        "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    m_topbar->GetCalibMenu()->AppendSubMenu(input_shaping_menu, _L("Input Shaping"));

    append_menu_item(m_topbar->GetCalibMenu(), wxID_ANY, _L("VFA"), _L("VFA"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::VFA); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    append_menu_item(m_topbar->GetCalibMenu(), wxID_ANY, _L("Non-planar"), _L("Non-planar calibration"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::Nonplanar); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    append_menu_item(m_topbar->GetCalibMenu(), wxID_ANY, _L("Calibration Guide"), _L("Calibration Guide"), [](wxCommandEvent &)
                     { wxLaunchDefaultBrowser("https://www.orcaslicer.com/wiki/calibration_guide", wxBROWSER_NEW_WINDOW); }, "", nullptr, [this]()
                     {return m_plater->is_view3D_shown();; }, this);

#else
    fileMenu->AppendSeparator();
    append_shortcut_item(
        fileMenu, Shortcut::SpeedDial, false, _L("Open Speed Dial"), "",
        [](wxCommandEvent&) { wxGetApp().open_speed_dial(); },
        "", nullptr, []() { return true; }, this);
    append_menu_item(
        fileMenu, wxID_ANY, _L("Preset Bundle"), "",
        [this](wxCommandEvent&) {
            wxGetApp().open_presetbundledialog();
            plater()->get_current_canvas3D()->force_set_focus();
        },
        "", nullptr, []() { return true; }, this);

    append_menu_item(
        fileMenu, wxID_ANY, _L("Sync Presets"), _L("Pull and apply the latest presets from OrcaCloud"),
        [this](wxCommandEvent&) {
            if (!wxGetApp().is_user_login()) {
                MessageDialog info_dlg(this, _L("You must be logged in to sync presets from cloud."),
                    _L("Sync Presets"), wxOK | wxICON_INFORMATION);
                info_dlg.ShowModal();
                return;
            }
            if (m_plater)
                m_plater->get_notification_manager()->push_notification(
                    into_u8(_L("Syncing presets from cloud\u2026")));
            wxGetApp().restart_sync_user_preset();
        }, "", nullptr,
        []() {
            return wxGetApp().is_user_login() && !wxGetApp().app_config->get_stealth_mode();
        }, this);

    fileMenu->AppendSeparator();
    append_menu_item(
        fileMenu, wxID_ANY, _L("Plugins"), "", [](wxCommandEvent&) { wxGetApp().open_plugins_dialog(); }, "", nullptr,
        []() { return true; }, this);

    fileMenu->AppendSeparator();

    m_menubar->Append(fileMenu, wxString::Format("&%s", _L("File")));
    if (editMenu)
        m_menubar->Append(editMenu, wxString::Format("&%s", _L_CONTEXT("Edit", "Menu")));
    if (viewMenu)
        m_menubar->Append(viewMenu, wxString::Format("&%s", _L("View")));
    auto calib_menu = new wxMenu();

    append_menu_item(calib_menu, wxID_ANY, _L("Temperature"), _L("Temperature"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::Temperature); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    append_menu_item(calib_menu, wxID_ANY, _L("Max flowrate"), _L("Max flowrate"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::MaxVolumetric); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    append_menu_item(calib_menu, wxID_ANY, _L("Pressure advance"), _L("Pressure advance"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::PressureAdvance); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    append_menu_item(calib_menu, wxID_ANY, _L("Flow ratio"), _L("Flow Rate Calibration"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::FlowRatio); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    append_menu_item(calib_menu, wxID_ANY, _L("Retraction"), _L("Retraction"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::Retraction); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    append_menu_item(calib_menu, wxID_ANY, _L("Cornering"), _L("Cornering calibration"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::Cornering); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    auto input_shaping_menu = new wxMenu();
    append_menu_item(
        input_shaping_menu, wxID_ANY, _L("Input Shaping Frequency"), _L("Input Shaping Frequency"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::InputShapingFreq); },
        "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    append_menu_item(
        input_shaping_menu, wxID_ANY, _L("Input Shaping Damping/zeta factor"), _L("Input Shaping Damping/zeta factor"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::InputShapingDamp); },
        "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    calib_menu->AppendSubMenu(input_shaping_menu, _L("Input Shaping"));

    append_menu_item(calib_menu, wxID_ANY, _L("VFA"), _L("VFA"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::VFA); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    append_menu_item(calib_menu, wxID_ANY, _L("Non-planar"), _L("Non-planar calibration"),
        [this](wxCommandEvent&) { run_calibration(CalibKind::Nonplanar); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    append_menu_item(calib_menu, wxID_ANY, _L("Calibration Guide"), _L("Calibration Guide"),
        [](wxCommandEvent&) { wxLaunchDefaultBrowser("https://www.orcaslicer.com/wiki/calibration_guide", wxBROWSER_NEW_WINDOW); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    m_menubar->Append(calib_menu,wxString::Format("&%s", _L("Calibration")));
    if (helpMenu)
        m_menubar->Append(helpMenu, wxString::Format("&%s", _L("Help")));
    SetMenuBar(m_menubar);

#endif

#ifdef _MSW_DARK_MODE
    if (wxGetApp().tabs_as_menu())
        m_menubar->EnableTop(6, false);
#endif

#ifdef __APPLE__
    wxMenu* apple_menu = m_menubar->OSXGetAppleMenu();
    if (apple_menu != nullptr) {
        apple_menu->Bind(wxEVT_MENU, [this](wxCommandEvent &) {
            Close();
        }, wxID_EXIT);
    }
#endif
}

void MainFrame::set_max_recent_count(int max)
{
    max = max < 0 ? 0 : max > 999 ? 999 : max;
    size_t count = m_recent_projects.GetCount();
    m_recent_projects.SetMaxFiles(max);
    if (count != m_recent_projects.GetCount()) {
        count = m_recent_projects.GetCount();
        std::vector<std::string> recent_projects;
        for (size_t i = 0; i < count; ++i) {
            recent_projects.push_back(into_u8(m_recent_projects.GetHistoryFile(i)));
        }
        wxGetApp().app_config->set_recent_projects(recent_projects);
        wxGetApp().app_config->save();
        if (WebViewPanel* home = WebViewPanel::if_built())
            home->SendRecentList(-1);
    }
}

void MainFrame::open_menubar_item(const wxString& menu_name,const wxString& item_name)
{
    if (m_menubar == nullptr)
        return;
    int     menu_index = m_menubar->FindMenu(menu_name);
    wxMenu* menu       = m_menubar->GetMenu(menu_index);
    if (menu == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "Mainframe open_menubar_item function couldn't find menu: " << menu_name;
        return;
    }
    int     item_id   = menu->FindItem(item_name);
    if (item_id == wxNOT_FOUND)
    {
        item_id = menu->FindItem(item_name + dots);
    }
    if (item_id == wxNOT_FOUND)
    {
        BOOST_LOG_TRIVIAL(error) << "Mainframe open_menubar_item function couldn't find item: " << item_name;
        return;
    }
    wxPostEvent((wxEvtHandler*)menu, wxCommandEvent(wxEVT_MENU, item_id));
}

void MainFrame::init_menubar_as_gcodeviewer()
{
#if 0
    wxMenu* fileMenu = new wxMenu;
    {
        append_menu_item(fileMenu, wxID_ANY, _L("&Open G-code") + dots + "\t" + ctrl + "O", _L("Open a G-code file"),
            [this](wxCommandEvent&) { if (m_plater != nullptr) m_plater->load_gcode(); }, "open", nullptr,
            [this]() {return m_plater != nullptr; }, this);
#ifdef __APPLE__
        append_menu_item(fileMenu, wxID_ANY, _L("Re&load from Disk") + dots + "\t" + ctrl + shift + "R",
            _L("Reload the plater from disk"), [this](wxCommandEvent&) { m_plater->reload_gcode_from_disk(); },
            "", nullptr, [this]() { return !m_plater->get_last_loaded_gcode().empty(); }, this);
#else
        append_menu_item(fileMenu, wxID_ANY, _L("Re&load from Disk") + sep + "F5",
            _L("Reload the plater from disk"), [this](wxCommandEvent&) { m_plater->reload_gcode_from_disk(); },
            "", nullptr, [this]() { return !m_plater->get_last_loaded_gcode().empty(); }, this);
#endif
        fileMenu->AppendSeparator();
        append_menu_item(fileMenu, wxID_ANY, _L("Export &Toolpaths as OBJ") + dots, _L("Export toolpaths as OBJ"),
            [this](wxCommandEvent&) { if (m_plater != nullptr) m_plater->export_toolpaths_to_obj(); }, "export_plater", nullptr,
            [this]() {return can_export_toolpaths(); }, this);
        append_menu_item(fileMenu, wxID_ANY, _L("Open &Slicer") + dots, _L("Open Slicer"),
            [](wxCommandEvent&) { start_new_slicer(); }, "", nullptr,
            []() {return true; }, this);
        fileMenu->AppendSeparator();
        append_menu_item(fileMenu, wxID_EXIT, _L("&Quit"), wxString::Format(_L("Quit %s"), SLIC3R_APP_NAME),
            [this](wxCommandEvent&) { Close(false); });
    }

    wxMenu* viewMenu = nullptr;
    if (m_plater != nullptr) {
        viewMenu = new wxMenu();
        add_common_view_menu_items(viewMenu, std::bind(&MainFrame::can_change_view, this));
    }

    auto helpMenu = generate_help_menu();

    m_menubar = new wxMenuBar();
    m_menubar->Append(fileMenu, _L("&File"));
    if (viewMenu != nullptr) m_menubar->Append(viewMenu, _L("&View"));
    wxGetApp().add_config_menu(m_menubar);
    m_menubar->Append(helpMenu, _L("&Help"));
    SetMenuBar(m_menubar);

#ifdef __APPLE__
    wxMenu* apple_menu = m_menubar->OSXGetAppleMenu();
    if (apple_menu != nullptr) {
        apple_menu->Bind(wxEVT_MENU, [this](wxCommandEvent&) {
            Close();
            }, wxID_EXIT);
    }
#endif
#endif
}

void MainFrame::update_menubar()
{
    if (wxGetApp().is_gcode_viewer())
        return;

    const bool is_fff = plater()->printer_technology() == ptFFF;
}

void MainFrame::reslice_now()
{
    if (m_plater)
        m_plater->reslice();
}

struct ConfigsOverwriteConfirmDialog : MessageDialog
{
    ConfigsOverwriteConfirmDialog(wxWindow *parent, wxString name, bool exported)
        : MessageDialog(parent,
                        wxString::Format(exported ? _L("A file exists with the same name: %s. Do you want to overwrite it\?") :
                                                  _L("A config exists with the same name: %s. Do you want to overwrite it\?"),
                                         name),
                        exported ? _L("Overwrite file") : _L("Overwrite config"),
                        wxYES_NO | wxNO_DEFAULT)
    {
        add_button(wxID_YESTOALL, false, _L("Yes to All"));
        add_button(wxID_NOTOALL, false, _L("No to All"));
    }
};

void MainFrame::export_config()
{
    ExportConfigsDialog export_configs_dlg(nullptr);
    export_configs_dlg.ShowModal();
    return;

    wxDirDialog dlg(this, _L("Choose a directory"),
        from_u8(!m_last_config.IsEmpty() ? get_dir_name(m_last_config) : wxGetApp().app_config->get_last_dir()), wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    wxString path;
    if (dlg.ShowModal() == wxID_OK)
        path = dlg.GetPath();
    if (!path.IsEmpty()) {
        wxGetApp().app_config->update_config_dir(into_u8(path));
        try {
            auto files = wxGetApp().preset_bundle->export_current_configs(into_u8(path), [this](std::string const & name) {
                    ConfigsOverwriteConfirmDialog dlg(this, from_u8(name), true);
                    int res = dlg.ShowModal();
                    int ids[]{wxID_NO, wxID_YES, wxID_NOTOALL, wxID_YESTOALL};
                    return std::find(ids, ids + 4, res) - ids;
            }, false);
            if (!files.empty())
                m_last_config = from_u8(files.back());
            MessageDialog dlg(this, wxString::Format(_L_PLURAL("There is %d config exported. (Only non-system configs)",
                "There are %d configs exported. (Only non-system configs)", files.size()), files.size()),
                              _L("Export Result"), wxOK);
            dlg.ShowModal();
        } catch (const std::exception &ex) {
            show_error(this, ex.what());
        }
    }
}

void MainFrame::load_config_file()
{
    wxFileDialog dlg(this, _L("Select profile to load:"),
        !m_last_config.IsEmpty() ? get_dir_name(m_last_config) : wxGetApp().app_config->get_last_dir(),
        "config.json", _L("Config files (*.json;*.zip;*.orca_printer;*.orca_bundle;*.orca_filament)|*.json;*.zip;*.orca_printer;*.orca_bundle;*.orca_filament"), wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
     wxArrayString files;
    if (dlg.ShowModal() != wxID_OK)
        return;
    dlg.GetPaths(files);
    std::vector<std::string> cfiles;
    for (auto file : files) {
        cfiles.push_back(into_u8(file));
        m_last_config = file;
    }
    bool update = false;
    wxGetApp().preset_bundle->import_presets(cfiles, [this](std::string const & name) {
            ConfigsOverwriteConfirmDialog dlg(this, from_u8(name), false);
            int           res = dlg.ShowModal();
            int           ids[]{wxID_NO, wxID_YES, wxID_NOTOALL, wxID_YESTOALL};
            return std::find(ids, ids + 4, res) - ids;
        },
        ForwardCompatibilitySubstitutionRule::Enable,
        *wxGetApp().app_config);
    if (!cfiles.empty()) {
        wxGetApp().app_config->update_config_dir(get_dir_name(cfiles.back()));
        wxGetApp().load_current_presets();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " presets has been import,and size is" << cfiles.size();
        NetworkAgent* agent = wxGetApp().getAgent();
        if (agent) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " user is: " << agent->get_user_id();
        }
    }
    wxGetApp().preset_bundle->update_compatible(PresetSelectCompatibleType::Always);
    update_side_preset_ui();
    auto msg = wxString::Format(_L_PLURAL("There is %d config imported. (Only non-system and compatible configs)",
        "There are %d configs imported. (Only non-system and compatible configs)", cfiles.size()), cfiles.size());
    if(cfiles.empty())
        msg += _L("\nHint: Make sure you have added the corresponding printer before importing the configs.");
    MessageDialog dlg2(this,msg ,
                        _L("Import result"), wxOK);
    dlg2.ShowModal();
}

bool MainFrame::load_config_file(const std::string &path)
{
    try {
        ConfigSubstitutions config_substitutions = wxGetApp().preset_bundle->load_config_file(path, ForwardCompatibilitySubstitutionRule::Enable);
        if (!config_substitutions.empty())
            show_substitutions_info(config_substitutions, path);
    } catch (const std::exception &ex) {
        show_error(this, ex.what());
        return false;
    }
    wxGetApp().load_current_presets();
    if (Plater *plater = wxGetApp().plater())
        plater->normalize_bed_types(false);
    return true;
}

void MainFrame::load_config(const DynamicPrintConfig& config)
{
	PrinterTechnology printer_technology = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology();
	const auto       *opt_printer_technology = config.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
	if (opt_printer_technology != nullptr && opt_printer_technology->value != printer_technology) {
		printer_technology = opt_printer_technology->value;
		this->plater()->set_printer_technology(printer_technology);
	}
#if 0
	for (auto tab : wxGetApp().tabs_list)
		if (tab->supports_printer_technology(printer_technology)) {
			if (tab->type() == Slic3r::Preset::TYPE_PRINTER)
				static_cast<TabPrinter*>(tab)->update_pages();
			tab->load_config(config);
		}
    if (m_plater)
        m_plater->on_config_change(config);
#else
    for (auto tab : wxGetApp().tabs_list)
        if (tab->supports_printer_technology(printer_technology)) {
			for (const std::string &opt_key : tab->get_config()->diff(config))
				if (! boost::algorithm::ends_with(opt_key, "_settings_id"))
					tab->get_config()->option(opt_key)->set(config.option(opt_key));
        }

    wxGetApp().load_current_presets();
#endif
}

void MainFrame::select_tab(wxPanel* panel)
{
    if (!panel)
        return;
    if (panel == m_param_panel) {
        panel = m_plater;
    } else if (dynamic_cast<ParamsPanel*>(panel)) {
        wxGetApp().params_dialog()->Popup();
        return;
    }
    int page_idx = m_tabpanel->FindPage(panel);
    wxString page_name = (page_idx == wxNOT_FOUND) ? wxString() : m_tabpanel->GetPageName(static_cast<size_t>(page_idx));
    if (page_name == TAB_ID_PREPARE && m_tabpanel->GetSelectedPageName() == TAB_ID_PREVIEW)
        return;
    select_tab(page_name);
}

void MainFrame::select_prepare_for_gl_init()
{
    m_tabpanel->ChangeSelection(m_tabpanel->FindPageByName(TAB_ID_PREPARE));
}

bool MainFrame::Show(bool show)
{
    const bool changed = DPIFrame::Show(show);
    if (show && changed && m_tabpanel != nullptr)
        if (wxWindow* page = m_tabpanel->GetCurrentPage())
            page->Show(true);
    return changed;
}

bool MainFrame::GLResourcesPrebuild::built() const
{
    return m_frame.m_plater != nullptr && m_frame.m_plater->canvas3D()->is_initialized() &&
           m_frame.m_plater->get_partplate_list().icon_textures_loaded();
}

bool MainFrame::GLResourcesPrebuild::build_step()
{
    GLCanvas3D* canvas = m_frame.m_plater->canvas3D();
#ifdef __WXGTK__
    gtk_widget_realize(canvas->get_wxglcanvas()->GetHandle());
#endif
    if (!canvas->make_current_for_postinit()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": cannot make the GL context current on the hidden canvas";
        m_failed = true;
        return false;
    }
    switch (m_step) {
    case 0:
        m_failed = !wxGetApp().init_opengl();
        break;
    case 1: {
        const Size size = canvas->get_canvas_size();
        wxGetApp().imgui()->set_display_size(float(std::max(1, size.get_width())), float(std::max(1, size.get_height())));
        canvas->set_imgui_scaling();
        wxGetApp().imgui()->new_frame();
        wxGetApp().imgui()->end_frame();
        break;
    }
    case 2:
        if (m_frame.m_plater->get_partplate_list().load_next_plate_texture())
            return true;
        break;
    case 3:
        m_failed = !canvas->init();
        break;
    default:
        m_frame.m_plater->get_partplate_list().load_icon_textures();
        return false;
    }
    ++m_step;
    return !m_failed;
}

bool MainFrame::PrepareLayoutPrebuild::built() const
{
    const wxWindow* page = m_frame.m_tabpanel != nullptr ? m_frame.m_tabpanel->GetCurrentPage() : nullptr;
    return page == nullptr || page == m_frame.m_plater || page->GetSize() == m_laid_out_size;
}

bool MainFrame::PrepareLayoutPrebuild::build_step()
{
    const wxWindow* page = m_frame.m_tabpanel->GetCurrentPage();
    m_laid_out_size      = page->GetSize();
    m_frame.m_plater->SetSize(page->GetRect());
    return false;
}

void MainFrame::prebuild_pages_when_idle()
{
    m_idle.clear();
    m_idle.add(m_gl_prebuild);
    if (m_param_panel)
        m_idle.add(m_param_panel->settings_page_prebuild());
    m_idle.add(m_prepare_layout_prebuild);
    for (LazyBase* page : m_lazy_pages)
        if (page->prebuild_order() >= 0)
            m_idle.add(*page);
    m_idle.add(m_diff_dialog);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": prebuild queue: " << m_idle.names();
    m_idle.start();
    m_prebuild_started = true;
}

void MainFrame::jump_to_monitor(std::string dev_id)
{
    m_tabpanel->SelectPageByName(TAB_ID_MONITOR);
    if (!dev_id.empty()) {
        MonitorPanel::ensure()->select_machine(dev_id);
    }
}

void MainFrame::jump_to_multipage()
{
    if (!m_multi_machine_page->in_book())
        return;
    m_tabpanel->SelectPageByName(TAB_ID_MULTI_DEVICE);
    if (MultiMachinePage* page = m_multi_machine_page->ensure())
        page->jump_to_send_page();
}


void MainFrame::select_tab(const wxString& id )
{
    auto select = [this, id](bool was_hidden) {
        wxString new_selection = id.empty() ? m_last_selected_tab : id;

        if (m_tabpanel->GetSelectedPageName() != new_selection)
            m_tabpanel->SelectPageByName(new_selection);
#ifdef _MSW_DARK_MODE
#endif
        if (id == TAB_ID_PREPARE && m_layout == ESettingsLayout::Old)
            m_plater->canvas3D()->render();
        else if (was_hidden) {
            Tab* cur_tab = dynamic_cast<Tab*>(m_tabpanel->GetPageByName(new_selection));
            if (cur_tab)
                cur_tab->OnActivate();
        }
    };

    select(false);
}

void MainFrame::request_select_tab(const wxString& id)
{
    wxCommandEvent* evt = new wxCommandEvent(EVT_SELECT_TAB);
    evt->SetString(id);
    wxQueueEvent(this, evt);
}

int MainFrame::get_calibration_curr_tab() {
    if (CalibrationPanel* calibration = CalibrationPanel::if_built())
        return calibration->get_tabpanel()->GetSelection();
    return -1;
}

void MainFrame::select_view(const std::string& direction)
{
     if (m_plater)
         m_plater->select_view(direction);
}

void MainFrame::on_presets_changed(SimpleEvent &event)
{
    auto *tab = dynamic_cast<Tab*>(event.GetEventObject());
    wxASSERT(tab != nullptr);
    if (tab == nullptr) {
        return;
    }

    auto presets = tab->get_presets();
    if (m_plater != nullptr && presets != nullptr) {

        Slic3r::Preset::Type preset_type = tab->type();
        if (preset_type == Slic3r::Preset::TYPE_INVALID) {
            wxASSERT(false);
            return;
        }

        m_plater->on_config_change(*tab->get_config());

        m_plater->sidebar().update_presets(preset_type);
    }
}

void MainFrame::on_value_changed(wxCommandEvent& event)
{
    auto *tab = dynamic_cast<Tab*>(event.GetEventObject());
    wxASSERT(tab != nullptr);
    if (tab == nullptr)
        return;

    auto opt_key = event.GetString();
    if (m_plater) {
        m_plater->on_config_change(*tab->get_config());
        if (opt_key == "extruders_count") {
            auto value = event.GetInt();
            m_plater->on_filament_count_change(value);
        }
    }
}

void MainFrame::on_config_changed(DynamicPrintConfig* config) const
{
    if (m_plater)
        m_plater->on_config_change(*config);
}

void MainFrame::set_print_button_to_default(PrintSelectType select_type)
{
    PrintSelectType remembered;
    if (get_remembered_print_select(remembered))
        select_type = remembered;

    if (select_type == eUploadGcode)
        return;

    m_print_btn->SetLabel(print_select_type_label(select_type));
    m_print_select = select_type;
    if (m_print_enable)
        m_print_enable = get_enable_print_status();
    m_print_btn->Enable(m_print_enable);
    this->Layout();
}

void MainFrame::add_to_recent_projects(const wxString& filename)
{
    if (wxFileExists(filename))
    {
        m_recent_projects.AddFileToHistory(filename);
        std::vector<std::string> recent_projects;
        size_t count = m_recent_projects.GetCount();
        for (size_t i = 0; i < count; ++i)
        {
            recent_projects.push_back(into_u8(m_recent_projects.GetHistoryFile(i)));
        }
        wxGetApp().app_config->set_recent_projects(recent_projects);
        if (WebViewPanel* home = WebViewPanel::if_built())
            home->SendRecentList(0);
    }
}

std::wstring MainFrame::FileHistory::GetThumbnailUrl(int index) const
{
    if (m_thumbnails[index].empty()) return L"";
    std::wstringstream wss;
    wss << L"data:image/png;base64,";
    wss << wxBase64Encode(m_thumbnails[index].data(), m_thumbnails[index].size());
    return wss.str();
}

bool MainFrame::FileHistory::GetPublished(int index) const
{
    return index >= 0 && index < static_cast<int>(m_published_files.size()) && m_published_files[index];
}

void MainFrame::FileHistory::AddFileToHistory(const wxString &file)
{
    if (this->m_fileMaxFiles == 0)
        return;
    wxFileHistory::AddFileToHistory(file);
    if (m_load_called) {
        m_thumbnails.push_front(bbs_3mf_get_thumbnail(into_u8(file).c_str()));
        m_published_files.push_front(bbs_3mf_is_published(into_u8(file)));
    } else {
        m_thumbnails.push_front("");
        m_published_files.push_front(false);
    }
}

void MainFrame::FileHistory::RemoveFileFromHistory(size_t i)
{
    if (i >= m_thumbnails.size())
        return;
    wxFileHistory::RemoveFileFromHistory(i);
    m_thumbnails.erase(m_thumbnails.begin() + i);
    m_published_files.erase(m_published_files.begin() + i);
}

size_t MainFrame::FileHistory::FindFileInHistory(const wxString & file)
{
    return m_fileHistory.Index(file);
}

void MainFrame::FileHistory::LoadThumbnails()
{
    tbb::parallel_for(tbb::blocked_range<size_t>(0, GetCount()), [this](tbb::blocked_range<size_t> range) {
        for (size_t i = range.begin(); i < range.end(); ++i) {
            auto thumbnail = bbs_3mf_get_thumbnail(into_u8(GetHistoryFile(i)).c_str());
            if (!thumbnail.empty()) {
                m_thumbnails[i] = thumbnail;
            }
            m_published_files[i] = bbs_3mf_is_published(into_u8(GetHistoryFile(i)));
        }
    });
    m_load_called = true;
}

inline void MainFrame::FileHistory::SetMaxFiles(int max)
{
    m_fileMaxFiles  = max;
    size_t numFiles = m_fileHistory.size();
    while (numFiles > m_fileMaxFiles)
        RemoveFileFromHistory(--numFiles);
}

void MainFrame::get_recent_projects(boost::property_tree::wptree &tree, int images)
{
    for (size_t i = 0; i < m_recent_projects.GetCount(); ++i) {
        boost::property_tree::wptree item;
        std::wstring proj = m_recent_projects.GetHistoryFile(i).ToStdWstring();
        item.put(L"project_name", proj.substr(proj.find_last_of(L"/\\") + 1));
        item.put(L"path", proj);
        item.put(L"published", m_recent_projects.GetPublished(i) ? L"1" : L"0");
        boost::system::error_code ec;
        std::time_t t = boost::filesystem::last_write_time(proj, ec);
        if (!ec) {
            std::wstring time = wxDateTime(t).FormatISOCombined(' ').ToStdWstring();
            item.put(L"time", time);
            if (i <= images) {
                auto thumbnail = m_recent_projects.GetThumbnailUrl(i);
                if (!thumbnail.empty()) item.put(L"image", thumbnail);
            }
        } else {
            item.put(L"time", _L("File is missing"));
        }
        tree.push_back({L"", item});
    }
}

void MainFrame::open_recent_project(size_t file_id, wxString const & filename)
{
    if (file_id == size_t(-1)) {
        file_id = m_recent_projects.FindFileInHistory(filename);
    }
    if (wxFileExists(filename)) {
        CallAfter([this, filename] {
            if (wxGetApp().can_load_project())
                m_plater->load_project(filename);
        });
    }
    else
    {
        MessageDialog msg(this, _L("The project is no longer available."), _L("Error"), wxOK | wxYES_DEFAULT);
        if (msg.ShowModal() == wxID_YES)
        {
            m_recent_projects.RemoveFileFromHistory(file_id);
            std::vector<std::string> recent_projects;
            size_t count = m_recent_projects.GetCount();
            for (size_t i = 0; i < count; ++i)
            {
                recent_projects.push_back(into_u8(m_recent_projects.GetHistoryFile(i)));
            }
            wxGetApp().app_config->set_recent_projects(recent_projects);
            if (WebViewPanel* home = WebViewPanel::if_built())
                home->SendRecentList(-1);
        }
    }
}

void MainFrame::remove_recent_project(size_t file_id, wxString const &filename)
{
    if (file_id == size_t(-1)) {
        if (filename.IsEmpty())
            while (m_recent_projects.GetCount() > 0)
                m_recent_projects.RemoveFileFromHistory(0);
        else
            file_id = m_recent_projects.FindFileInHistory(filename);
    }
    if (file_id != size_t(-1))
        m_recent_projects.RemoveFileFromHistory(file_id);
    std::vector<std::string> recent_projects;
    size_t count = m_recent_projects.GetCount();
    for (size_t i = 0; i < count; ++i)
    {
        recent_projects.push_back(into_u8(m_recent_projects.GetHistoryFile(i)));
    }
    wxGetApp().app_config->set_recent_projects(recent_projects);
    if (WebViewPanel* home = WebViewPanel::if_built())
        home->SendRecentList(-1);
}

void MainFrame::load_url(wxString url)
{
    BOOST_LOG_TRIVIAL(trace) << "load_url:" << url;
    auto evt = new wxCommandEvent(EVT_LOAD_URL, this->GetId());
    evt->SetString(url);
    wxQueueEvent(this, evt);
}

void MainFrame::load_printer_url(wxString url, wxString apikey)
{
    BOOST_LOG_TRIVIAL(trace) << "load_printer_url:" << url;
    auto evt = new LoadPrinterViewEvent(EVT_LOAD_PRINTER_URL, this->GetId());
    evt->SetString(url);
    evt->SetAPIkey(apikey);
    wxQueueEvent(this, evt);
}

void MainFrame::load_printer_url()
{
    PresetBundle &preset_bundle = *wxGetApp().preset_bundle;
    if (preset_bundle.use_bbl_device_tab() && !wxGetApp().app_config->get_bool("use_printer_agents"))
        return;

    auto     cfg = preset_bundle.printers.get_edited_preset().config;
    if (cfg.opt_string("print_host").empty()) {
        if (auto *device_manager = wxGetApp().getDeviceManager()) {
            auto *machine = device_manager->get_selected_machine();
            if (!machine) {
                auto machines = device_manager->get_my_machine_list(device_manager->get_current_printer_agent_id());
                if (machines.size() == 1)
                    machine = machines.begin()->second;
            }
            if (machine && !machine->get_dev_ip().empty())
                cfg.opt_string("print_host") = machine->get_dev_ip();
        }
    }
    wxString url = from_u8(PrintHost::get_print_host_webui(&cfg));
    wxString apikey;
    const auto host_type = cfg.option<ConfigOptionEnum<PrintHostType>>("host_type")->value;
    if (cfg.has("printhost_apikey") && host_type != htSimplyPrint)
        apikey = cfg.opt_string("printhost_apikey");
    if (!url.empty()) {
        load_printer_url(url, apikey);
    }
}

bool MainFrame::is_printer_view() const { return m_tabpanel->GetSelectedPageName() == TAB_ID_MONITOR; }


void MainFrame::refresh_plugin_tips()
{
    if (WebViewPanel* home = WebViewPanel::if_built())
        home->ShowNetpluginTip();
}

void MainFrame::RunScript(wxString js)
{
    if (WebViewPanel* home = WebViewPanel::if_built())
        home->RunScript(js);
}

void MainFrame::technology_changed()
{
    PrinterTechnology pt = plater()->printer_technology();
    if (int id = m_menubar->FindMenu(pt == ptFFF ? _omitL("Material Settings") : _L("Filament settings")); id != wxNOT_FOUND)
        m_menubar->SetMenuLabel(id, pt == ptSLA ? _omitL("Material Settings") : _L("Filament settings"));
}

void MainFrame::run_calibration(CalibKind calib_kind)
{
    switch (calib_kind) {
    case CalibKind::Temperature: {
        if (!m_temp_calib_dlg)
            m_temp_calib_dlg = new Temp_Calibration_Dlg((wxWindow*) this, wxID_ANY, m_plater);
        m_temp_calib_dlg->ShowModal();
        break;
    }
    case CalibKind::MaxVolumetric: {
        if (!m_vol_test_dlg)
            m_vol_test_dlg = new MaxVolumetricSpeed_Test_Dlg((wxWindow*) this, wxID_ANY, m_plater);
        m_vol_test_dlg->ShowModal();
        break;
    }
    case CalibKind::PressureAdvance: {
        if (!m_pa_calib_dlg)
            m_pa_calib_dlg = new PA_Calibration_Dlg((wxWindow*) this, wxID_ANY, m_plater);
        m_pa_calib_dlg->ShowModal();
        break;
    }
    case CalibKind::FlowRatio: {
        if (!m_plater)
            break;
        if (!m_flow_rate_calib_dlg)
            m_flow_rate_calib_dlg = new FlowRateCalibrationDialog((wxWindow*) this, wxID_ANY, m_plater);
        m_flow_rate_calib_dlg->ShowModal();
        break;
    }
    case CalibKind::Retraction: {
        if (!m_retraction_calib_dlg)
            m_retraction_calib_dlg = new Retraction_Test_Dlg((wxWindow*) this, wxID_ANY, m_plater);
        m_retraction_calib_dlg->ShowModal();
        break;
    }
    case CalibKind::Cornering: {
        auto dlg = new Cornering_Test_Dlg((wxWindow*) this, wxID_ANY, m_plater);
        dlg->ShowModal();
        dlg->Destroy();
        break;
    }
    case CalibKind::InputShapingFreq: {
        auto dlg = new Input_Shaping_Freq_Test_Dlg((wxWindow*) this, wxID_ANY, m_plater);
        dlg->ShowModal();
        dlg->Destroy();
        break;
    }
    case CalibKind::InputShapingDamp: {
        auto dlg = new Input_Shaping_Damp_Test_Dlg((wxWindow*) this, wxID_ANY, m_plater);
        dlg->ShowModal();
        dlg->Destroy();
        break;
    }
    case CalibKind::VFA: {
        if (!m_vfa_test_dlg)
            m_vfa_test_dlg = new VFA_Test_Dlg((wxWindow*) this, wxID_ANY, m_plater);
        m_vfa_test_dlg->ShowModal();
        break;
    }
    case CalibKind::Nonplanar: {
        if (!m_nonplanar_calib_dlg)
            m_nonplanar_calib_dlg = new NonplanarCalibDialog((wxWindow*) this, m_plater);
        m_nonplanar_calib_dlg->ShowModal();
        break;
    }
    }
}


void MainFrame::update_ui_from_settings()
{
    if (m_plater)
        m_plater->update_ui_from_settings();
    for (auto tab: wxGetApp().tabs_list)
        tab->update_ui_from_settings();
}


void MainFrame::show_sync_dialog()
{
    SimpleEvent* evt = new SimpleEvent(EVT_SYNC_CLOUD_PRESET);
    wxQueueEvent(this, evt);
}

void MainFrame::update_side_preset_ui()
{
    for (auto tab : wxGetApp().tabs_list) {
        tab->update_tab_ui();
    }

    m_plater->sidebar().update_presets(Preset::TYPE_PRINTER);
    m_plater->sidebar().update_presets(Preset::TYPE_FILAMENT);


    if (MultiMachinePage* multi_machine = MultiMachinePage::if_built())
        multi_machine->clear_page();
}

void MainFrame::on_select_default_preset(SimpleEvent& evt)
{
    MessageDialog dialog(this,
                    _L("Do you want to synchronize your personal data from Orca Cloud?\n"
                        "It contains the following information:\n"
                        "1. The Process presets\n"
                        "2. The Filament presets\n"
                        "3. The Printer presets"),
                    _L("Synchronization"),
                    wxCENTER |
                    wxYES_DEFAULT | wxYES_NO |
                    wxICON_INFORMATION);

    NetworkAgent* agent = wxGetApp().getAgent();
    switch ( dialog.ShowModal() )
    {
        case wxID_YES: {
            wxGetApp().app_config->set_bool("sync_user_preset", true);
            wxGetApp().start_sync_user_preset(true);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " sync_user_preset: true";
            break;
        }
        case wxID_NO:
            wxGetApp().app_config->set_bool("sync_user_preset", false);
            wxGetApp().stop_sync_user_preset();
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " sync_user_preset: false";
            break;
        default:
            break;
    }

    update_side_preset_ui();
}

std::string MainFrame::get_base_name(const wxString &full_name, const char *extension) const
{
    boost::filesystem::path filename = boost::filesystem::path(full_name.wx_str()).filename();
    if (extension != nullptr)
		filename = filename.replace_extension(extension);
    return filename.string();
}

std::string MainFrame::get_dir_name(const wxString &full_name) const
{
    return boost::filesystem::path(into_u8(full_name)).parent_path().string();
}


SettingsDialog::SettingsDialog(MainFrame* mainframe)
:DPIDialog(NULL, wxID_ANY, wxString(SLIC3R_APP_NAME) + " - " + _L("Settings"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE, "settings_dialog")
{
    if (wxGetApp().is_gcode_viewer())
        return;

#if defined(__WXMSW__)
    this->SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
#else
    this->SetFont(wxGetApp().normal_font());
    this->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif
#if _WIN32
    {
        TCHAR szExeFileName[MAX_PATH];
        GetModuleFileName(nullptr, szExeFileName, MAX_PATH);
        SetIcon(wxIcon(szExeFileName, wxBITMAP_TYPE_ICO));
    }
#else
    SetIcon(wxIcon(var("OrcaSlicer_128px.png"), wxBITMAP_TYPE_PNG));
#endif
    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& evt) { this->Hide(); });

#ifdef _MSW_DARK_MODE
    if (wxGetApp().tabs_as_menu()) {
    }
#endif

    auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->SetSizeHints(this);
    SetSizer(sizer);
    Fit();

    const wxSize min_size = wxSize(85 * em_unit(), 50 * em_unit());
#ifdef __APPLE__
    SetSize(min_size);
#else
    SetMinSize(min_size);
    SetSize(GetMinSize());
#endif
    Layout();
}

void SettingsDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    if (wxGetApp().is_gcode_viewer())
        return;

    const int& em = em_unit();
    const wxSize& size = wxSize(85 * em, 50 * em);

    m_tabpanel->Rescale();

    for (auto tab : wxGetApp().tabs_list)
        tab->msw_rescale();

    SetMinSize(size);
    Fit();
    Refresh();
}


}
}
