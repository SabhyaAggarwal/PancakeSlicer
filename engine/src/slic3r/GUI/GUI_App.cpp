#include "CloudProvider.hpp"
#include "ExportPresetBundleDialog.hpp"
#include "ICloudServiceAgent.hpp"
#include "IPrinterAgent.hpp"
#include "OrcaCloudServiceAgent.hpp"
#include "libslic3r/Technologies.hpp"
#include "libslic3r/Platform.hpp"
#include "GUI_App.hpp"
#include "Shortcuts.hpp"
#include "DeviceCore/DevConfigUtil.h"
#include "BindDialog.hpp"
#include "DeviceManager.hpp"
#include "HMS.hpp"
#include "PresetBundleDialog.hpp"
#include "WebUserLoginDialog.hpp"
#include "WebViewDialog.hpp"
#include "slic3r/Utils/BBLCloudServiceAgent.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"
#include "GUI_Init.hpp"
#include "GUI_ObjectList.hpp"
#include "slic3r/GUI/UserManager.hpp"
#include "slic3r/GUI/TaskManager.hpp"
#include "format.hpp"
#include "libslic3r_version.h"
#include "BuildCommit.hpp"
#include "Downloader.hpp"
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/assert/source_location.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <atomic>
#include <boost/asio/ip/basic_endpoint.hpp>
#include <boost/chrono/duration.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/directory.hpp>
#include <boost/format/exceptions.hpp>
#include <boost/filesystem/file_status.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/locale/encoding_utf.hpp>
#include <boost/log/detail/native_typeof.hpp>
#include <iomanip>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <ctime>
#include "libslic3r/AppConfig.hpp"
#include <ios>
#include <chrono>
#include <functional>
#include <boost/optional/optional.hpp>
#include <fstream>
#include <boost/none.hpp>
#include <cstring>
#include <cmath>
#include <climits>
#include <cctype>
#include <cstdio>
#include <libslic3r/Config.hpp>
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include <ostream>
#include <new>
#include "libslic3r/Exception.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include <memory>
#include "slic3r/GUI/Jobs/UpgradeNetworkJob.hpp"
#include "slic3r/GUI/Jobs/SendJob.hpp"
#include "slic3r/GUI/Printer/PrinterFileSystem.h"
#include <miniz.h>
#include "slic3r/GUI/Monitor.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include <optional>
#include "slic3r/GUI/UserNotification.hpp"
#include <map>
#include "libslic3r/Preset.hpp"
#include <set>
#include "libslic3r/libslic3r.h"
#include "slic3r/GUI/PrinterWebView.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <slic3r/plugin/PythonPluginInterface.hpp>
#include <string>
#include <vector>
#include <sstream>
#include <wx/dcclient.h>
#include <wx/chartype.h>
#include <utility>
#include <wx/app.h>
#include <wx/busycursor.h>
#include <wx/debug.h>
#include <wx/anybutton.h>
#include <wx/clntdata.h>
#include <unordered_set>
#include <stdio.h>
#include <wx/arrstr.h>
#include <wx/event.h>
#include <wx/strconv.h>
#include <wx/gdicmn.h>
#include <wx/string.h>
#include <wx/frame.h>
#include <wx/filename.h>
#include <wx/translation.h>
#include <wx/msgdlg.h>
#include <wx/setup.h>
#include <wx/timer.h>
#include <wx/snglinst.h>
#include <wx/image.h>
#include <wx/settings.h>
#include <wx/font.h>
#include <wx/platinfo.h>
#include <wx/language.h>
#include <wx/localedefs.h>
#include <wx/eventfilter.h>
#include <wx/toplevel.h>

#define SLIC3R_ALLOW_LIBSLIC3R_I18N_IN_SLIC3R
#include "libslic3r/I18N.hpp"
#include "libslic3r/Point.hpp"
#undef SLIC3R_ALLOW_LIBSLIC3R_I18N_IN_SLIC3R
#include "slic3r/GUI/I18N.hpp"

#include <algorithm>
#include <iterator>
#include <exception>
#include <cstdlib>
#include <mutex>
#include <regex>
#include <thread>
#include <string_view>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/beast/core/detail/base64.hpp>

#include <wx/stdpaths.h>
#include <wx/imagpng.h>
#include <wx/display.h>
#include <wx/menu.h>
#include <wx/menuitem.h>
#include <wx/filedlg.h>
#include <wx/progdlg.h>
#include <wx/busyinfo.h>
#include <wx/dir.h>
#include <wx/wupdlock.h>
#include <wx/filefn.h>
#include <wx/sysopt.h>
#include <wx/richmsgdlg.h>
#include <wx/log.h>
#include <wx/intl.h>

#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/splash.h>
#include <wx/weakref.h>
#include <wx/fontutil.h>
#include <wx/glcanvas.h>
#include <wx/utils.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

#include "libslic3r/Utils.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/InstanceLock.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "slic3r/plugin/PluginManager.hpp"
#include "slic3r/plugin/host/PluginHostUi.hpp"
#include "slic3r/plugin/PythonInterpreter.hpp"

#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "3DScene.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "GLCanvas3D.hpp"
#include "EncodedFilament.hpp"
#include "GeneratedConfig.hpp"

#include "DeviceCore/DevManager.h"

#include "../Utils/PresetUpdater.hpp"
#include "../Utils/PrintHost.hpp"
#include "../Utils/Process.hpp"
#include "../Utils/wxInspectorPlugins/Registration.hpp"
#include "../Utils/Http.hpp"
#include "../Utils/InstanceID.hpp"
#include "../Utils/UndoRedo.hpp"
#include "slic3r/Config/Snapshot.hpp"
#include "Preferences.hpp"
#include "Tab.hpp"
#include "UpdateDialogs.hpp"
#include "Mouse3DController.hpp"
#include "RemovableDriveManager.hpp"
#include "InstanceCheck.hpp"
#ifdef __APPLE__
#include "../Utils/MacDarkMode.hpp"
#include "DeepLinkHandlerMac.h"
#endif
#include "NotificationManager.hpp"
#include "UnsavedChangesDialog.hpp"
#include "SavePresetDialog.hpp"
#include "PrintHostDialogs.hpp"
#include "NetworkPluginDialog.hpp"
#include "DesktopIntegrationDialog.hpp"
#include "KBShortcutsDialog.hpp"
#include "DownloadProgressDialog.hpp"
#include "TroubleshootDialog.hpp"

#include "BitmapCache.hpp"
#include "Notebook.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/ProgressDialog.hpp"

#include "WebGuideDialog.hpp"
#include "ReleaseNote.hpp"
#include "PrivacyUpdateDialog.hpp"
#include "ModelMall.hpp"
#include "HintNotification.hpp"

#include "slic3r/Utils/NetworkAgentFactory.hpp"
#include "slic3r/Utils/BBLNetworkPlugin.hpp"
#include "slic3r/Utils/bambu_networking.hpp"

#include "PluginsDialog.hpp"
#include "SpeedDialDialog.hpp"
#include "TerminalDialog.hpp"
#include "libslic3r/Format/STEP.hpp"
#include "libslic3r/Semver.hpp"
#include "slic3r/GUI/ActionRegistry.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/ConfigWizard.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosManager.hpp"
#include "slic3r/GUI/HttpServer.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/ParamsDialog.hpp"
#include "slic3r/GUI/ParamsPanel.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/plugin/host/PluginPages.hpp"
#include <wx/defs.h>
#include "slic3r/GUI/Widgets/WebView.hpp"
#include <cwchar>
#include <wx/dataview.h>
#include <wx/itemattr.h>
#include <wx/version.h>

#ifdef __WXMSW__
#include <dbt.h>
#include <shlobj.h>

#ifdef __WINDOWS__
#ifdef _MSW_DARK_MODE
#include "dark_mode.hpp"
#include "wx/headerctrl.h"
#include "wx/msw/headerctrl.h"

typedef BOOL (WINAPI *LPFN_ISWOW64PROCESS2)(
    HANDLE hProcess,
    USHORT *pProcessMachine,
    USHORT *pNativeMachine
);

#endif
#endif

#endif
#ifdef _WIN32
#include <boost/dll/runtime_symbol_info.hpp>
#include <direct.h>
#endif

#ifdef WIN32
#include "dev-utils/BaseException.h"
#endif

#if ENABLE_THUMBNAIL_GENERATOR_DEBUG
#include <boost/beast/core/detail/base64.hpp>
#include <boost/nowide/fstream.hpp>
#endif

#ifdef __WXGTK__
#include "LinuxDisplayBackend.hpp"
#endif

#if defined(__WXGTK20__) || defined(__WXGTK3__)
    #include <gtk/gtk.h>
#endif

namespace fs = boost::filesystem;
using namespace std::literals;
namespace pt = boost::property_tree;
using json = nlohmann::json;

struct StaticBambuLib
{
    static void reset();
    static void release();
};

namespace Slic3r {
namespace GUI {

class MainFrame;

void start_ping_test()
{
    return;
    wxArrayString output;
    wxExecute("ping www.amazon.com", output, wxEXEC_NODISABLE);

    wxString output_i;
    std::string output_temp;

    for (int i = 0; i < output.size(); i++) {
        output_i = output[i].To8BitData();
        output_temp = output_i.ToStdString(wxConvUTF8);
        BOOST_LOG_TRIVIAL(info) << "ping amazon:" << output_temp;

    }
    wxExecute("ping www.apple.com", output, wxEXEC_NODISABLE);
    for (int i = 0; i < output.size(); i++) {
        output_i = output[i].To8BitData();
        output_temp = output_i.ToStdString(wxConvUTF8);
        BOOST_LOG_TRIVIAL(info) << "ping www.apple.com:" << output_temp;
    }
    wxExecute("ping www.bambulab.com", output, wxEXEC_NODISABLE);
    for (int i = 0; i < output.size(); i++) {
        output_i = output[i].To8BitData();
        output_temp = output_i.ToStdString(wxConvUTF8);
        BOOST_LOG_TRIVIAL(info) << "ping bambulab:" << output_temp;
    }
    wxExecute("ping 192.168.0.1", output, wxEXEC_NODISABLE);
    for (int i = 0; i < output.size(); i++) {
        output_i = output[i].To8BitData();
        output_temp = output_i.ToStdString(wxConvUTF8);
        BOOST_LOG_TRIVIAL(info) << "ping 192.168.0.1:" << output_temp;
    }
}

std::string VersionInfo::convert_full_version(std::string short_version)
{
    std::string result = "";
    std::vector<std::string> items;
    boost::split(items, short_version, boost::is_any_of("."));
    if (items.size() == VERSION_LEN) {
        for (int i = 0; i < VERSION_LEN; i++) {
            std::stringstream ss;
            ss << std::setw(2) << std::setfill('0') << items[i];
            result += ss.str();
            if (i != VERSION_LEN - 1)
                result += ".";
        }
        return result;
    }
    return result;
}

std::string VersionInfo::convert_short_version(std::string full_version)
{
    full_version.erase(std::remove(full_version.begin(), full_version.end(), '0'), full_version.end());
    return full_version;
}

#ifdef _WIN32
bool is_associate_files(std::wstring extend)
{
    wchar_t app_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, app_path, sizeof(app_path));

    std::wstring prog_id             = L" Orca.Slicer.1";
    std::wstring reg_base            = L"Software\\Classes";
    std::wstring reg_extension       = reg_base + L"\\." + extend;

    wchar_t szValueCurrent[1000];
    DWORD   dwType;
    DWORD   dwSize = sizeof(szValueCurrent);

    int iRC = ::RegGetValueW(HKEY_CURRENT_USER, reg_extension.c_str(), nullptr, RRF_RT_ANY, &dwType, szValueCurrent, &dwSize);

    bool bDidntExist = iRC == ERROR_FILE_NOT_FOUND;

    if (!bDidntExist && ::wcscmp(szValueCurrent, prog_id.c_str()) == 0)
        return true;

    return false;
}
#endif

class SplashScreen : public wxSplashScreen
{
public:
    SplashScreen(wxPoint pos = wxDefaultPosition)
        : wxSplashScreen(wxBitmap(FromDIP(wxSize(480,480),nullptr)), wxSPLASH_CENTRE_ON_SCREEN, 0, nullptr, wxID_ANY, wxDefaultPosition, wxDefaultSize,
#ifdef __APPLE__
            wxBORDER_NONE | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP
#else
            wxBORDER_NONE | wxFRAME_NO_TASKBAR
#endif
        )
    {
		#if defined(__WXGTK__)
	        if (Slic3r::GUI::is_running_on_wayland()) {
	            GtkWidget *empty = gtk_fixed_new();
	            gtk_widget_set_size_request(empty, 0, 0);
	            gtk_window_set_titlebar(GTK_WINDOW(GetHandle()), empty);
	            gtk_window_set_decorated(GTK_WINDOW(GetHandle()), false);
	        }
		#endif
		
        this->SetPosition(pos);
        this->CenterOnScreen();

        scale_font(m_font_version, 1.65f);

        m_bg_color = StateColor::darkModeColorFor(wxColour("#FFFFFF"));
        m_fg_color = StateColor::darkModeColorFor(wxColour("#6B6A6A"));
        m_progress_bg_color = StateColor::darkModeColorFor(wxColour("#DFDFDF"));
        m_progress_fg_color = StateColor::darkModeColorFor(wxColour("#009688"));
        m_progress_h = FromDIP(6);
        bool dark_mode = m_fg_color != wxColour("#6B6A6A");
        wxSize sz  = m_window->GetClientSize();
        BitmapCache bmp_cache;
        m_logo_bmp = *bmp_cache.load_svg(dark_mode ? "splash_logo_dark" : "splash_logo", sz.GetWidth(), sz.GetHeight());

        m_window->Bind(wxEVT_PAINT, &SplashScreen::OnPaint, this);
        m_window->Refresh();
        m_window->Update();
    }

    void OnPaint(wxPaintEvent& evt)
    {
        wxPaintDC dc(m_window);
        wxSize c_sz = m_window->GetClientSize();

        dc.SetBackground(wxBrush(m_bg_color));
        dc.Clear();
        if (m_logo_bmp.IsOk())
            dc.DrawBitmap(m_logo_bmp, 0, 0, true);

        wxRect rc = wxRect(0, 0, c_sz.GetWidth(), 0);
        dc.SetTextForeground(m_fg_color);

        dc.SetFont(m_font_version);
        rc.y      = c_sz.GetHeight() * 0.72;
        rc.height = dc.GetTextExtent(m_text_version).GetHeight();
        dc.DrawLabel(m_text_version, rc, wxALIGN_CENTER);

        dc.SetFont(m_font_action);
        rc.y      = c_sz.GetHeight() * 0.85;
        rc.height = dc.GetTextExtent(m_text_action).GetHeight();
        dc.DrawLabel(m_text_action, rc, wxALIGN_CENTER);

        const wxRect progress_rc(0, c_sz.GetHeight() - m_progress_h, c_sz.GetWidth(), m_progress_h);
                
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(m_progress_bg_color));
        dc.DrawRectangle(progress_rc);

        const int fill_width = progress_rc.GetWidth() * m_progress * 0.01;
        if (fill_width > 0) {
            dc.SetBrush(wxBrush(m_progress_fg_color));
            dc.DrawRectangle(0, progress_rc.GetTop(), fill_width, m_progress_h);
        }
    }

    void SetText(const wxString& text, int progress)
    {
        int calc_progress = std::max(m_progress, std::clamp(progress, 0, 100));
        if (m_text_action != text || m_progress != calc_progress){
            m_text_action = text;
            m_progress = calc_progress;
            m_window->Refresh();
            m_window->Update();
#ifdef __WXOSX__
        wxYield();
#endif
        }
    }

    int FilterEvent(wxEvent& ) override { return wxEventFilter::Event_Skip; }

    void scale_font(wxFont& font, float scale)
    {
#ifdef __WXMSW__
        wxNativeFontInfo nfi= *font.GetNativeFontInfo();
        float pointSizeNew  = scale * font.GetPointSize();
        nfi.lf.lfHeight     = nfi.GetLogFontHeightAtPPI(pointSizeNew, get_dpi_for_window(this));
        nfi.pointSize       = pointSizeNew;
        font = wxFont(nfi);
#else
        font.Scale(scale);
#endif
    }

private:
    wxBitmap m_logo_bmp;
    wxColour m_fg_color;
    wxColour m_bg_color;
    wxColour m_progress_bg_color;
    wxColour m_progress_fg_color;

    wxString m_text_version = GUI_App::format_display_version();
    wxString m_text_action  = _L("Loading configuration") + dots;
    int      m_progress     = 0;
    int      m_progress_h   = 6;

    wxFont m_font_version = Label::Body_16;
    wxFont m_font_action  = Label::Body_16;
};

#ifdef __linux__
static void migrate_flatpak_legacy_datadir(const boost::filesystem::path &data_dir_path)
{
    if(!boost::filesystem::exists("/.flatpak-info"))
        return;
    
    namespace fs = boost::filesystem;

    if (fs::exists(data_dir_path)){
        std::cerr << "New Flatpak data dir: " << data_dir_path << std::endl;
        return;
    }
    std::cerr << "Migrating Flatpak data dir: " << data_dir_path << std::endl;

    std::string legacy_data_dir_str = data_dir_path.string();
    boost::replace_first(legacy_data_dir_str, "com.orcaslicer.OrcaSlicer", "io.github.softfever.OrcaSlicer");
    const fs::path legacy_data_dir(legacy_data_dir_str);

    std::cerr << "Legacy Flatpak data dir: " << legacy_data_dir << std::endl;

    if ( ! fs::exists(legacy_data_dir) || ! fs::is_directory(legacy_data_dir))
        return;
    std::cerr << "Legacy Flatpak data dir exists: " << legacy_data_dir << std::endl;

    try {
        std::cerr << "Migrating Flatpak data dir from " << legacy_data_dir << " to " << data_dir_path << std::endl;
        copy_directory_recursively(legacy_data_dir, data_dir_path);
    } catch (const std::exception &ex) {
        std::cerr << "Failed to migrate Flatpak data dir from " << legacy_data_dir << " to " << data_dir_path << ": " << ex.what() << std::endl;
    }
}

bool static check_old_linux_datadir(const wxString& app_name) {

    namespace fs = boost::filesystem;

    std::string new_path = Slic3r::data_dir();

    wxString dir;
    if (! wxGetEnv(wxS("XDG_CONFIG_HOME"), &dir) || dir.empty() )
        dir = wxFileName::GetHomeDir() + wxS("/.config");
    std::string default_path = (dir + "/" + app_name).ToUTF8().data();

    if (new_path != default_path) {
        return true;
    }

    fs::path data_dir = fs::path(new_path);
    if (! fs::is_directory(data_dir))
        return true;

    int file_count = std::distance(fs::directory_iterator(data_dir), fs::directory_iterator());

    if (file_count <= 1) {
    } else {
    }
    return true;
}
#endif

struct FileWildcards {
    const char*                 title_id;
    std::vector<std::string_view> file_extensions;
};

static const FileWildcards file_wildcards_by_type[FT_SIZE] = {
        { L("STEP files"),      { ".stp"sv, ".step"sv } },
         { L("STL files"),       { ".stl"sv } },
         { L("OBJ files"),       { ".obj"sv } },
         { L("AMF files"),       { ".amf"sv, ".zip.amf"sv, ".xml"sv } },
         { L("3MF files"),       { ".3mf"sv } },
     {L("G-code 3MF files"), {".gcode.3mf"sv}},
       { L("G-code files"),    { ".gcode"sv} },
#ifdef __APPLE__
    {L("Supported files"), {".3mf"sv, ".stl"sv, ".oltp"sv, ".stp"sv, ".step"sv, ".svg"sv, ".amf"sv, ".obj"sv, ".gltf"sv, ".glb"sv, ".fbx"sv, ".usd"sv, ".usda"sv, ".usdc"sv, ".usdz"sv, ".abc"sv, ".ply"sv, ".drc"sv}},
#else
    {L("Supported files"), {".3mf"sv, ".stl"sv, ".oltp"sv, ".stp"sv, ".step"sv, ".svg"sv, ".amf"sv, ".obj"sv, ".gltf"sv, ".glb"sv, ".fbx"sv, ".drc"sv}},
#endif
         { L("ZIP files"),       { ".zip"sv } },
     { L("Project files"),   { ".3mf"sv} },
     { L("Known files"),     { ".stl"sv, ".obj"sv } },

         { L("INI files"),       { ".ini"sv } },
         { L("SVG files"),       { ".svg"sv } },
         { L("Texture"),         { ".png"sv, ".svg"sv } },
         { L("Masked SLA files"), { ".sl1"sv, ".sl1s"sv } },
         { L("Draco files"),     { ".drc"sv } },
};

wxString file_wildcards(FileType file_type, const std::string &custom_extension)
{
    const FileWildcards& data = file_wildcards_by_type[file_type];
    std::string title;
    std::string mask;
    std::string custom_ext_lower;

    if (! custom_extension.empty()) {
        custom_ext_lower = boost::to_lower_copy(custom_extension);
        const std::string custom_ext_upper = boost::to_upper_copy(custom_extension);
        if (custom_ext_lower == custom_extension) {
            title = std::string("*") + custom_ext_lower;
            mask = title;
            mask  += ";*";
            mask  += custom_ext_upper;
        } else if (custom_ext_upper == custom_extension) {
            title = std::string("*") + custom_ext_upper;
            mask = title;
            mask += ";*";
            mask += custom_ext_lower;
        } else {
            title = std::string("*") + custom_extension;
            mask = title;
        }
    }

    for (const std::string_view &ext : data.file_extensions)
        if (ext != custom_ext_lower) {
            if (title.empty()) {
                title = "*";
                title += ext;
                mask  = title;
            } else {
                title += ", *";
                title += ext;
                mask  += ";*";
                mask  += ext;
            }
            mask += ";*";
            mask += boost::to_upper_copy(std::string(ext));
        }
    const wxString translated_title = Slic3r::GUI::I18N::translate(data.title_id);
    return GUI::format_wxstr("%s (%s)|%s", translated_title, title, mask);
}

static std::string libslic3r_translate_callback(const char *s) { return wxGetTranslation(wxString(s, wxConvUTF8)).utf8_str().data(); }

#ifdef WIN32
static GUID GUID_DEVINTERFACE_HID = { 0x4D1E55B2, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

static void register_win32_device_notification_event()
{
    wxWindow::MSWRegisterMessageHandler(WM_DEVICECHANGE, [](wxWindow *win, WXUINT , WXWPARAM wParam, WXLPARAM lParam) {
        auto main_frame = dynamic_cast<MainFrame*>(win);
        auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
        if (plater == nullptr)
            return true;
		PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR)lParam;
        switch (wParam) {
        case DBT_DEVICEARRIVAL:
			if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
		        plater->GetEventHandler()->AddPendingEvent(VolumeAttachedEvent(EVT_VOLUME_ATTACHED));
			else if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				PDEV_BROADCAST_DEVICEINTERFACE lpdbi = (PDEV_BROADCAST_DEVICEINTERFACE)lpdb;
				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_HID)
			        plater->GetEventHandler()->AddPendingEvent(HIDDeviceAttachedEvent(EVT_HID_DEVICE_ATTACHED, boost::nowide::narrow(lpdbi->dbcc_name)));
			}
            break;
		case DBT_DEVICEREMOVECOMPLETE:
			if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
                plater->GetEventHandler()->AddPendingEvent(VolumeDetachedEvent(EVT_VOLUME_DETACHED));
			else if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				PDEV_BROADCAST_DEVICEINTERFACE lpdbi = (PDEV_BROADCAST_DEVICEINTERFACE)lpdb;
				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_HID)
        			plater->GetEventHandler()->AddPendingEvent(HIDDeviceDetachedEvent(EVT_HID_DEVICE_DETACHED, boost::nowide::narrow(lpdbi->dbcc_name)));
			}
			break;
        default:
            break;
        }
        return true;
    });

    wxWindow::MSWRegisterMessageHandler(MainFrame::WM_USER_MEDIACHANGED, [](wxWindow *win, WXUINT , WXWPARAM wParam, WXLPARAM lParam) {
        auto main_frame = dynamic_cast<MainFrame*>(win);
        auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
        if (plater == nullptr)
            return true;
        wchar_t sPath[MAX_PATH];
        if (lParam == SHCNE_MEDIAINSERTED || lParam == SHCNE_MEDIAREMOVED) {
            struct _ITEMIDLIST* pidl = *reinterpret_cast<struct _ITEMIDLIST**>(wParam);
            if (! SHGetPathFromIDList(pidl, sPath)) {
                BOOST_LOG_TRIVIAL(error) << "MediaInserted: SHGetPathFromIDList failed";
                return false;
            }
        }
        switch (lParam) {
        case SHCNE_MEDIAINSERTED:
        {
            plater->GetEventHandler()->AddPendingEvent(VolumeAttachedEvent(EVT_VOLUME_ATTACHED));
            break;
        }
        case SHCNE_MEDIAREMOVED:
        {
            plater->GetEventHandler()->AddPendingEvent(VolumeDetachedEvent(EVT_VOLUME_DETACHED));
            break;
        }
	    default:
            break;
	    }
        return true;
    });

    wxWindow::MSWRegisterMessageHandler(WM_INPUT, [](wxWindow *win, WXUINT , WXWPARAM wParam, WXLPARAM lParam) {
        auto main_frame = dynamic_cast<MainFrame*>(Slic3r::GUI::find_toplevel_parent(win));
        auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
        if (wParam == RIM_INPUT && plater != nullptr && main_frame->IsActive()) {
        RAWINPUT raw;
			UINT rawSize = sizeof(RAWINPUT);
			::GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &raw, &rawSize, sizeof(RAWINPUTHEADER));
			if (raw.header.dwType == RIM_TYPEHID && plater->get_mouse3d_controller().handle_raw_input_win32(raw.data.hid.bRawData, raw.data.hid.dwSizeHid))
				return true;
		}
        return false;
    });

	wxWindow::MSWRegisterMessageHandler(WM_COPYDATA, [](wxWindow* win, WXUINT , WXWPARAM wParam, WXLPARAM lParam) {
		COPYDATASTRUCT* copy_data_structure = { 0 };
		copy_data_structure = (COPYDATASTRUCT*)lParam;
		if (copy_data_structure->dwData == 1) {
			LPCWSTR arguments = (LPCWSTR)copy_data_structure->lpData;
			Slic3r::GUI::wxGetApp().other_instance_message_handler()->handle_message(boost::nowide::narrow(arguments));
		}
		return true;
		});
}
#endif

static void generic_exception_handle()
{
    try {
        throw;
    } catch (const std::bad_alloc& ex) {
        BOOST_LOG_TRIVIAL(error) << boost::format("std::bad_alloc exception: %1%") % ex.what();
        flush_logs();
        wxString errmsg = wxString::Format(_L("OrcaSlicer will terminate because of running out of memory. "
                                              "It may be a bug. It will be appreciated if you report the issue to our team."));
        wxMessageBox(errmsg + "\n\n" + wxString(ex.what()), _L("Fatal error"), wxOK | wxICON_ERROR);

        std::terminate();
     } catch (const boost::io::bad_format_string& ex) {
     	BOOST_LOG_TRIVIAL(error) << boost::format("Uncaught exception: %1%") % ex.what();
        	flush_logs();
        wxString errmsg = _L("OrcaSlicer will terminate because of a localization error. "
                             "It will be appreciated if you report the specific scenario this issue happened.");
        wxMessageBox(errmsg + "\n\n" + wxString(ex.what()), _L("Critical error"), wxOK | wxICON_ERROR);
        std::terminate();
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << boost::format("Uncaught exception: %1%") % ex.what();
        flush_logs();
        wxLogError(format_wxstr(_L("OrcaSlicer got an unhandled exception: %1%"), ex.what()));
        throw;
    }
}

void GUI_App::toggle_show_gcode_window()
{
    m_show_gcode_window = !m_show_gcode_window;
    app_config->set_bool("show_gcode_window", m_show_gcode_window);
}

std::vector<std::string> GUI_App::split_str(std::string src, std::string separator)
{
    std::string::size_type pos;
    std::vector<std::string> result;
    src += separator;
    int size = src.size();

    for (int i = 0; i < size; i++)
    {
        pos = src.find(separator, i);
        if (pos < size)
        {
            std::string s = src.substr(i, pos - i);
            result.push_back(s);
            i = pos + separator.size() - 1;
        }
    }
    return result;
}

void GUI_App::post_init()
{
    assert(initialized());
    if (! this->initialized())
        throw Slic3r::RuntimeError("Calling post_init() while not yet initialized");

#if wxUSE_WEBVIEW_EDGE
    init_webview_runtime();
#endif

    m_open_method = "double_click";
    bool switch_to_3d = false;

    if (!this->init_params->input_files.empty()) {

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", init with input files, size %1%, input_gcode %2%")
            %this->init_params->input_files.size() %this->init_params->input_gcode;

        switch_to_3d = true;

        const auto first_url = this->init_params->input_files.front();
        if (this->init_params->input_files.size() == 1 && is_supported_open_protocol(first_url)) {
            start_download(first_url);
            m_open_method = "url";
        } else {
            if (this->init_params->input_gcode) {
                mainframe->select_tab(TAB_ID_PREPARE);
                plater_->select_view_3D("3D");
                this->plater()->load_gcode(from_u8(this->init_params->input_files.front()));
                m_open_method = "gcode";
            } else {
                mainframe->select_tab(TAB_ID_PREPARE);
                plater_->select_view_3D("3D");
                wxArrayString input_files;
                for (auto& file : this->init_params->input_files) {
                    input_files.push_back(wxString::FromUTF8(file));
                }
                this->plater()->set_project_filename(_L("Untitled"));
                this->plater()->load_files(input_files);
                try {
                    if (!input_files.empty()) {
                        std::string           file_path = input_files.front().ToStdString();
                        std::filesystem::path path(file_path);
                        m_open_method = "file_" + path.extension().string();
                    }
                } catch (...) {
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", file path exception!";
                    m_open_method = "file";
                }
            }
        }
    }

    bool slow_bootup = false;
    if (app_config->get("slow_bootup") == "true") {
        slow_bootup = true;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", slow bootup, won't render gl here.";
    }
    const bool gl_at_idle = !starts_on_prepare() && is_editor();
    if (!switch_to_3d && gl_at_idle) {
        plater_->select_view_3D("3D");
    } else if (!switch_to_3d) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", begin load_gl_resources";
#ifndef __linux__
        mainframe->Freeze();
#endif
        plater_->canvas3D()->enable_render(false);
        mainframe->select_prepare_for_gl_init();
        plater_->select_view_3D("3D");
        if (!plater_->canvas3D()->get_wxglcanvas()->IsShownOnScreen() ||
            !plater_->canvas3D()->make_current_for_postinit()) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": glcontext not ready, postpone init";
            plater_->canvas3D()->enable_render(true);
            plater_->canvas3D()->set_as_dirty();
#ifdef __linux__
            m_post_initialized = false;
            return;
#endif
        } else {
            Size canvas_size = plater_->canvas3D()->get_canvas_size();
            wxGetApp().imgui()->set_display_size(static_cast<float>(canvas_size.get_width()), static_cast<float>(canvas_size.get_height()));
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", start to init opengl";
            wxGetApp().init_opengl();

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished init opengl";
            plater_->canvas3D()->init();

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished init canvas3D";
            wxGetApp().imgui()->new_frame();

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished init imgui frame";
            plater_->canvas3D()->enable_render(true);

            if (!slow_bootup) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", start to render a first frame for test";
                plater_->canvas3D()->render(false);
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished rendering a first frame for test";
            }
        }
        if (starts_on_prepare())
            mainframe->select_tab(TAB_ID_PREPARE);
#ifndef __linux__
        mainframe->Thaw();
#endif
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", end load_gl_resources";
    }

    plater_->trigger_restore_project(1);
    mainframe->prebuild_pages_when_idle();

#if 1
    if (app_config->get("show_hints") == "true" && !is_gcode_viewer()) {
        plater_->get_notification_manager()->push_hint_notification(false);
    }
#endif

    hms_query = new HMSQuery();

    m_show_gcode_window = app_config->get_bool("show_gcode_window");
    if (m_networking_need_update) {
        show_network_plugin_download_dialog(false);
    }

    if (app_config->get("sync_user_preset") == "true") {
        if (m_agent) {
            start_sync_user_preset();
        }
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " sync_user_preset: true";
    } else {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " sync_user_preset: false";
    }

    if (this->preset_updater) {
        CallAfter([this] {
            bool cw_showed = this->config_wizard_startup();

            if (!app_config->get_stealth_mode()) {
                std::string http_url = get_http_url(app_config->get_country_code());
                std::string language = GUI::into_u8(current_language_code());
                std::string network_ver = Slic3r::NetworkAgent::get_version();
                bool        sys_preset  = app_config->get("sync_system_preset") == "true";
                this->preset_updater->sync(http_url, language, network_ver, sys_preset ? preset_bundle : nullptr);
            }

            this->check_new_version_sf();
            const auto cloud_provider = get_printer_cloud_provider();
            if (is_user_login(cloud_provider) && !app_config->get_stealth_mode()) {
              request_user_handle(0, cloud_provider);
            }
        });
    }

    if (is_editor() && m_last_config_version && m_last_config_version->valid()
        && *m_last_config_version < Semver(2, 4, 0)) {
        CallAfter([] {
            const wxString wiki_url = "https://www.orcaslicer.com/wiki/user_profiles/user_profiles.html#profiles-missing-after-updating-from-bambu-cloud";
            MessageDialog dlg(nullptr,
                _L("Since version 2.4.0, OrcaSlicer syncs user profiles through Orca Cloud instead of Bambu Cloud.\n\n"
                   "To migrate your existing profiles, log in to Orca Cloud and they will be transferred automatically. "
                   "To learn more about how OrcaSlicer stores and syncs your profiles, or to migrate your presets manually, check out our wiki.\n\n"
                   "If you did not use Bambu Cloud to sync profiles, this change does not affect you and you can safely ignore this message."),
                _L("Profile syncing change"),
                wxOK,
                "",
                _L("Learn more"),
                [wiki_url](const wxString &) { wxLaunchDefaultBrowser(wiki_url); });
            const wxSize sz = dlg.GetSize();
            dlg.SetSize(std::max(sz.x, dlg.FromDIP(280)), std::max(sz.y, dlg.FromDIP(200)));
            dlg.CenterOnParent();
            dlg.ShowModal();
        });
    }

    if(!m_networking_need_update && m_agent) {
        m_agent->set_on_ssdp_msg_fn(
            [this](std::string json_str) {
                if (is_closing()) {
                    return;
                }
                GUI::wxGetApp().CallAfter([this, json_str] {
                    if (m_device_manager) {
                        m_device_manager->on_machine_alive(json_str);
                    }
                    });
            }
        );
        m_agent->set_on_http_error_fn([this](CloudEvent event, unsigned int status, std::string body) {
            this->handle_http_error(status, body, event.provider);
        });
        m_agent->start_discovery(true, false);
    }

    CallAfter([this] {
            mainframe->refresh_plugin_tips();
        });

    std::string log_addr = data_dir();
    if (!log_addr.empty()) {
        auto log_folder = boost::filesystem::path(log_addr) / "log";
        if (boost::filesystem::exists(log_folder)) {
           std::vector<std::pair<time_t, std::string>> files_vec;
           for (auto& it : boost::filesystem::directory_iterator(log_folder)) {
               auto temp_path = it.path();
               try {
                   if (it.status().type() == boost::filesystem::regular_file) {
                       std::time_t lw_t = boost::filesystem::last_write_time(temp_path) ;
                       files_vec.push_back({ lw_t, temp_path.filename().string() });
                   }
               } catch (const std::exception &) {
               }
           }
           std::sort(files_vec.begin(), files_vec.end(), [](
               std::pair<time_t, std::string> &a, std::pair<time_t, std::string> &b) {
               return a.first > b.first;
           });

           while (files_vec.size() > LOG_FILES_MAX_NUM) {
               auto full_path = log_folder / boost::filesystem::path(files_vec[files_vec.size() - 1].second);
               BOOST_LOG_TRIVIAL(info) << "delete log file over " << LOG_FILES_MAX_NUM << ", filename: "<< files_vec[files_vec.size() - 1].second;
               try {
                   boost::filesystem::remove(full_path);
               }
               catch (const std::exception& ex) {
                   BOOST_LOG_TRIVIAL(error) << "failed to delete log file: "<< files_vec[files_vec.size() - 1].second << ". Error: " << ex.what();
               }
               files_vec.pop_back();
           }
        }
    }
    BOOST_LOG_TRIVIAL(info) << "finished post_init";
#ifdef _WIN32
    OtherInstanceMessageHandler::init_windows_properties(mainframe, m_instance_hash_int);
#endif
}

wxDEFINE_EVENT(EVT_ENTER_FORCE_UPGRADE, wxCommandEvent);
wxDEFINE_EVENT(EVT_SHOW_NO_NEW_VERSION, wxCommandEvent);
wxDEFINE_EVENT(EVT_SHOW_DIALOG, wxCommandEvent);
wxDEFINE_EVENT(EVT_CONNECT_LAN_MODE_PRINT, wxCommandEvent);
wxDEFINE_EVENT(EVT_UPDATE_PRESET_BUNDLE, wxCommandEvent);
wxDEFINE_EVENT(EVT_UPDATE_BUNDLE_COMPLETE, wxCommandEvent);

IMPLEMENT_APP(GUI_App)

GUI_App::GUI_App()
    : wxApp()
    , m_app_mode(EAppMode::Editor)
    , m_em_unit(10)
    , m_imgui(new ImGuiWrapper())
	, m_removable_drive_manager(std::make_unique<RemovableDriveManager>())
    , m_downloader(std::make_unique<Downloader>())
	, m_other_instance_message_handler(std::make_unique<OtherInstanceMessageHandler>())
{
    this->init_app_config();
    m_shortcuts = std::make_unique<ShortcutRegistry>();
    m_shortcuts->load(*app_config);
    this->init_download_path();

    reset_to_active();
}

void GUI_App::shutdown()
{
    BOOST_LOG_TRIVIAL(info) << "GUI_App::shutdown enter";

	if (m_removable_drive_manager) {
		removable_drive_manager()->shutdown();
	}

    if (login_dlg != nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": destroy login dialog");
        delete login_dlg;
        login_dlg = nullptr;
    }

    if (m_is_recreating_gui) return;
    stop_http_server();
    set_closing(true);
    Slic3r::PluginManager::instance().set_shutting_down();

    if (m_agent)
        m_agent->set_printer_agent(nullptr);
    NetworkAgentFactory::clear_printer_agent_cache();

    BOOST_LOG_TRIVIAL(info) << "GUI_App::shutdown exit";
}


std::string GUI_App::get_http_url(std::string country_code, std::string path)
{
    std::string url;
    if (country_code == "US") {
        url = "https://api.bambulab.com/";
    }
    else if (country_code == "CN") {
        url = "https://api.bambulab.cn/";
    }
    else if (country_code == "ENV_CN_DEV") {
        url = "https://api-dev.bambu-lab.com/";
    }
    else if (country_code == "ENV_CN_QA") {
        url = "https://api-qa.bambu-lab.com/";
    }
    else if (country_code == "ENV_CN_PRE") {
        url = "https://api-pre.bambu-lab.com/";
    }
    else {
        url = "https://api.bambulab.com/";
    }

    url += path.empty() ? "v1/iot-service/api/slicer/resource" : path;
    return url;
}

std::string GUI_App::get_model_http_url(std::string country_code)
{
    std::string url;
    if (country_code == "US") {
        url = "https://makerworld.com/";
    }
    else if (country_code == "CN") {
        url = "https://makerworld.com/";
    }
    else if (country_code == "ENV_CN_DEV") {
        url = "https://makerhub-dev.bambu-lab.com/";
    }
    else if (country_code == "ENV_CN_QA") {
        url = "https://makerhub-qa.bambu-lab.com/";
    }
    else if (country_code == "ENV_CN_PRE") {
        url = "https://makerhub-pre.bambu-lab.com/";
    }
    else {
        url = "https://makerworld.com/";
    }

    return url;
}


std::string GUI_App::get_plugin_url(std::string name, std::string country_code)
{
    std::string url = get_http_url(country_code);

    std::string curr_version;
    if (use_legacy_network_plugin()) {
        curr_version = BAMBU_NETWORK_AGENT_VERSION_LEGACY;
    } else if (name == "plugins" && app_config) {
        std::string user_version = app_config->get_network_plugin_version();
        curr_version = user_version.empty() ? get_latest_network_version() : user_version;
    } else {
        curr_version = get_latest_network_version();
    }

    std::string using_version = use_legacy_network_plugin()
        ? curr_version.substr(0, 9) + "00"
        : network_plugin_series(curr_version) + ".00";
    if (name == "cameratools")
        using_version = curr_version.substr(0, 6) + "00.00";
    url += (boost::format("?slicer/%1%/cloud=%2%") % name % using_version).str();
    return url;
}

static std::string decode(std::string const& extra, std::string const& path = {}) {
    char const* p = extra.data();
    char const* e = p + extra.length();
    while (p + 4 < e) {
        boost::uint16_t len = ((boost::uint16_t)p[2]) | ((boost::uint16_t)p[3] << 8);
        if (p[0] == '\x75' && p[1] == '\x70' && len >= 5 && p + 4 + len < e && p[4] == '\x01') {
            return std::string(p + 9, p + 4 + len);
        }
        else {
            p += 4 + len;
        }
    }
    return Slic3r::decode_path(path.c_str());
}

int GUI_App::download_plugin(std::string name, std::string package_name, InstallProgressFn pro_fn, WasCancelledFn cancel_fn)
{
    int result = 0;
    json j;
    std::string err_msg;

    AppConfig* app_config = wxGetApp().app_config;
    if (!app_config) {
        j["result"] = "failed";
        j["error_msg"] = "app_config is nullptr";
        return -1;
    }

    BOOST_LOG_TRIVIAL(info) << "[download_plugin]: enter";
    m_networking_cancel_update = false;
    fs::path target_file_path = (fs::temp_directory_path() / package_name);
    fs::path tmp_path = target_file_path;
    tmp_path += format(".%1%%2%", get_current_pid(), ".tmp");

#if defined(__WINDOWS__)
    std::string os_type = (is_running_on_arm64() && !use_legacy_network_plugin()) ? "windows_arm" : "windows";
#elif defined(__APPLE__)
    std::string os_type = "macos";
#elif defined(__linux__)
    std::string os_type = "linux";
#else
    std::string os_type = "windows";
#endif

    std::string  url = get_plugin_url(name, app_config->get_country_code());
    std::string download_url;
    Slic3r::Http http_url = Slic3r::Http::get(url);
    BOOST_LOG_TRIVIAL(info) << "[download_plugin]: check the plugin from " << url;
    http_url.timeout_connect(TIMEOUT_CONNECT)
        .timeout_max(TIMEOUT_RESPONSE)
        .header("X-BBL-OS-Type", os_type)
        .on_complete(
        [&download_url](std::string body, unsigned status) {
            try {
                json j = json::parse(body);
                std::string message = j["message"].get<std::string>();

                if (message == "success") {
                    json resource = j.at("resources");
                    if (resource.is_array()) {
                        for (auto iter = resource.begin(); iter != resource.end(); iter++) {
                            Semver version;
                            std::string version_str;
                            std::string url;
                            std::string type;
                            std::string vendor;
                            std::string description;
                            for (auto sub_iter = iter.value().begin(); sub_iter != iter.value().end(); sub_iter++) {
                                if (boost::iequals(sub_iter.key(), "type")) {
                                    type = sub_iter.value();
                                    BOOST_LOG_TRIVIAL(info) << "[download_plugin]: get version of settings's type, " << sub_iter.value();
                                }
                                else if (boost::iequals(sub_iter.key(), "version")) {
                                    version_str = sub_iter.value();
                                    version = *(Semver::parse(version_str));
                                }
                                else if (boost::iequals(sub_iter.key(), "description")) {
                                    description = sub_iter.value();
                                }
                                else if (boost::iequals(sub_iter.key(), "url")) {
                                    url = sub_iter.value();
                                }
                            }
                            BOOST_LOG_TRIVIAL(info) << "[download_plugin 1]: get type " << type << ", version " << version.to_string() << ", url " << url;
                            download_url = url;
                        }
                    }
                }
                else {
                    BOOST_LOG_TRIVIAL(info) << "[download_plugin 1]: get version of plugin failed, body=" << body;
                }
            }
            catch (...) {
                BOOST_LOG_TRIVIAL(error) << "[download_plugin 1]: catch unknown exception";
                ;
            }
        }).on_error(
            [&result, &err_msg](std::string body, std::string error, unsigned int status) {
                BOOST_LOG_TRIVIAL(error) << "[download_plugin 1] on_error: " << error<<", body = " << body;
                err_msg += "[download_plugin 1] on_error: " + error + ", body = " + body;
                result = -1;
        }).perform_sync();

    bool cancel = false;
    if (result < 0) {
        j["result"] = "failed";
        j["error_msg"] = err_msg;
        if (pro_fn) pro_fn(InstallStatusDownloadFailed, 0, cancel);
        return result;
    }


    if (download_url.empty()) {
        BOOST_LOG_TRIVIAL(info) << "[download_plugin 1]: no available plugin found for this app version: " << SLIC3R_VERSION;
        if (pro_fn) pro_fn(InstallStatusDownloadFailed, 0, cancel);
        j["result"] = "failed";
        j["error_msg"] = "[download_plugin 1]: no available plugin found for this app version: " + std::string(SLIC3R_VERSION);
        return -1;
    }
    else if (pro_fn) {
        pro_fn(InstallStatusNormal, 5, cancel);
    }

    if (m_networking_cancel_update || cancel) {
        BOOST_LOG_TRIVIAL(info) << boost::format("[download_plugin 1]: %1%, cancelled by user") % __LINE__;
        j["result"] = "failed";
        j["error_msg"] = (boost::format("[download_plugin 1]: %1%, cancelled by user") % __LINE__).str();
        return -1;
    }
    BOOST_LOG_TRIVIAL(info) << "[download_plugin] get_url = " << download_url;

    Slic3r::Http http = Slic3r::Http::get(download_url);
    int reported_percent = 0;
    http.header("X-BBL-OS-Type", os_type)
        .on_progress(
        [this, &pro_fn, cancel_fn, &result, &reported_percent, &err_msg](Slic3r::Http::Progress progress, bool& cancel) {
            int percent = 0;
            if (progress.dltotal != 0)
                percent = progress.dlnow * 50 / progress.dltotal;
            bool was_cancel = false;
            if (pro_fn && ((percent - reported_percent) >= 10)) {
                pro_fn(InstallStatusNormal, percent, was_cancel);
                reported_percent = percent;
                BOOST_LOG_TRIVIAL(info) << "[download_plugin 2] progress: " << reported_percent;
            }
            cancel = m_networking_cancel_update || was_cancel;
            if (cancel_fn)
                if (cancel_fn())
                    cancel = true;

            if (cancel) {
                err_msg += "[download_plugin] cancel";
                result = -1;
            }
        })
        .on_complete([&pro_fn, tmp_path, target_file_path](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << "[download_plugin 2] completed";
            bool cancel = false;
            int percent = 0;
            fs::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
            file.write(body.c_str(), body.size());
            file.close();
            fs::rename(tmp_path, target_file_path);
            if (pro_fn) pro_fn(InstallStatusDownloadCompleted, 80, cancel);
            })
        .on_error([&pro_fn, &result, &err_msg](std::string body, std::string error, unsigned int status) {
            bool cancel = false;
            if (pro_fn) pro_fn(InstallStatusDownloadFailed, 0, cancel);
            BOOST_LOG_TRIVIAL(error) << "[download_plugin 2] on_error: " << error<<", body = " << body;
            err_msg += "[download_plugin 2] on_error: " + error + ", body = " + body;
            result = -1;
        });
    http.perform_sync();

    j["result"] = result < 0 ? "failed" : "success";
    j["error_msg"] = err_msg;
    return result;
}

int GUI_App::install_plugin(std::string name, std::string package_name, InstallProgressFn pro_fn, WasCancelledFn cancel_fn)
{
    bool cancel = false;
    std::string target_file_path = (fs::temp_directory_path() / package_name).string();

    BOOST_LOG_TRIVIAL(info) << "[install_plugin] enter";
    std::string data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    auto plugin_folder = data_dir_path / name;
    auto backup_folder = plugin_folder/"backup";
    if (!boost::filesystem::exists(plugin_folder)) {
        BOOST_LOG_TRIVIAL(info) << "[install_plugin] will create directory "<<plugin_folder.string();
        boost::filesystem::create_directory(plugin_folder);
    }
    if (!boost::filesystem::exists(backup_folder)) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", will create directory %1%")%backup_folder.string();
        boost::filesystem::create_directory(backup_folder);
    }

    if (m_networking_cancel_update) {
        BOOST_LOG_TRIVIAL(info) << boost::format("[install_plugin]: %1%, cancelled by user")%__LINE__;
        return -1;
    }
    if (pro_fn) {
        pro_fn(InstallStatusNormal, 50, cancel);
    }
    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);
    if (!open_zip_reader(&archive, target_file_path)) {
        BOOST_LOG_TRIVIAL(error) << boost::format("[install_plugin]: %1%, open zip file failed")%__LINE__;
        if (pro_fn) pro_fn(InstallStatusDownloadFailed, 0, cancel);
        return InstallStatusUnzipFailed;
    }

    boost::filesystem::path legacy_lib_path, legacy_lib_backup;
    bool had_existing_legacy = false;
    if (name == "plugins") {
#if defined(_MSC_VER) || defined(_WIN32)
        legacy_lib_path = plugin_folder / (std::string(BAMBU_NETWORK_LIBRARY) + ".dll");
#elif defined(__WXMAC__)
        legacy_lib_path = plugin_folder / (std::string("lib") + std::string(BAMBU_NETWORK_LIBRARY) + ".dylib");
#else
        legacy_lib_path = plugin_folder / (std::string("lib") + std::string(BAMBU_NETWORK_LIBRARY) + ".so");
#endif
        legacy_lib_backup = legacy_lib_path;
        legacy_lib_backup += ".backup";

        if (boost::filesystem::exists(legacy_lib_path)) {
            had_existing_legacy = true;
            boost::system::error_code ec;
            boost::filesystem::rename(legacy_lib_path, legacy_lib_backup, ec);
            if (ec) {
                BOOST_LOG_TRIVIAL(warning) << "[install_plugin] failed to backup existing legacy library: " << ec.message();
                had_existing_legacy = false;
            } else {
                BOOST_LOG_TRIVIAL(info) << "[install_plugin] backed up existing legacy library";
            }
        }
    }

    mz_uint num_entries = mz_zip_reader_get_num_files(&archive);
    mz_zip_archive_file_stat stat;
    BOOST_LOG_TRIVIAL(error) << boost::format("[install_plugin]: %1%, got %2% files")%__LINE__ %num_entries;
    for (mz_uint i = 0; i < num_entries; i++) {
        if (m_networking_cancel_update || cancel) {
            BOOST_LOG_TRIVIAL(info) << boost::format("[install_plugin]: %1%, cancelled by user")%__LINE__;
            return -1;
        }
        if (mz_zip_reader_file_stat(&archive, i, &stat)) {
            if (stat.m_uncomp_size > 0) {
                std::string dest_file;
                if (stat.m_is_utf8) {
                    dest_file = stat.m_filename;
                }
                else {
                    std::string extra(1024, 0);
                    size_t n = mz_zip_reader_get_extra(&archive, stat.m_file_index, extra.data(), extra.size());
                    dest_file = decode(extra.substr(0, n), stat.m_filename);
                }
                if (!is_path_within_root(dest_file, plugin_folder)) {
                    BOOST_LOG_TRIVIAL(error) << "[install_plugin] entry " << dest_file << " resolves outside " << plugin_folder.string();
                    close_zip_reader(&archive);
                    if (pro_fn) { pro_fn(InstallStatusUnzipFailed, 0, cancel); }
                    return InstallStatusUnzipFailed;
                }
                auto dest_path = plugin_folder / dest_file;
                std::string dest_zip_file = encode_path(dest_path.string().c_str());
#ifndef WIN32
                const bool is_link = S_ISLNK(stat.m_external_attr >> 16);
                std::string link;
                if (is_link) {
                    link.assign(stat.m_uncomp_size, 0);
                    if (!mz_zip_reader_extract_to_mem(&archive, stat.m_file_index, link.data(), stat.m_uncomp_size, 0) ||
                        !is_symlink_target_within_root(dest_file, link, plugin_folder)) {
                        BOOST_LOG_TRIVIAL(error) << "[install_plugin] link " << dest_file << " -> " << link << " is unreadable or resolves outside " << plugin_folder.string();
                        close_zip_reader(&archive);
                        if (pro_fn) { pro_fn(InstallStatusUnzipFailed, 0, cancel); }
                        return InstallStatusUnzipFailed;
                    }
                }
#endif
                try {
                    boost::filesystem::create_directories(dest_path.parent_path());
                    if (fs::exists(fs::symlink_status(dest_path))) {
                        boost::system::error_code ec;
                        fs::remove(dest_path, ec);
                        if (ec) {
                            boost::filesystem::path aside = dest_path;
                            aside += ".old";
                            boost::system::error_code ec2;
                            fs::remove(aside, ec2);
                            fs::rename(dest_path, aside, ec2);
                            if (ec2) {
                                close_zip_reader(&archive);
                                BOOST_LOG_TRIVIAL(error) << "[install_plugin] cannot replace in-use file "
                                                         << dest_path.string() << ": " << ec2.message();
                                if (pro_fn) { pro_fn(InstallStatusUnzipFailed, 0, cancel); }
                                return InstallStatusUnzipFailed;
                            }
                            BOOST_LOG_TRIVIAL(warning) << "[install_plugin] " << dest_path.filename().string()
                                                       << " was in use, renamed aside to .old";
                        }
                    }
                    mz_bool res = 0;
#ifndef WIN32
                    if (is_link) {
                        res = 1;
                        try {
                            boost::filesystem::create_symlink(link, dest_path);
                        } catch (const std::exception &e) {
                            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " create_symlink:" << e.what();
                        }
                    } else {
#endif
                        res = mz_zip_reader_extract_to_file(&archive, stat.m_file_index, dest_zip_file.c_str(), 0);
#ifndef WIN32
                    }
#endif
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", extract  %1% from plugin zip %2%\n") % dest_file % stat.m_filename;
                    if (res == 0) {
#ifdef WIN32
                        std::wstring new_dest_zip_file = boost::locale::conv::utf_to_utf<wchar_t>(dest_path.generic_string());
                        res                            = mz_zip_reader_extract_to_file_w(&archive, stat.m_file_index, new_dest_zip_file.c_str(), 0);
#endif
                        if (res == 0) {
                            mz_zip_error zip_error = mz_zip_get_last_error(&archive);
                            BOOST_LOG_TRIVIAL(error) << "[install_plugin]Archive read error:" << mz_zip_get_error_string(zip_error) << std::endl;
                            close_zip_reader(&archive);
                            if (pro_fn) { pro_fn(InstallStatusUnzipFailed, 0, cancel); }
                            return InstallStatusUnzipFailed;
                        }
                    }
                }
                catch (const std::exception& e)
                {
                    close_zip_reader(&archive);
                    BOOST_LOG_TRIVIAL(error) << "[install_plugin]Archive read exception:"<<e.what();
                    if (pro_fn) {
                        pro_fn(InstallStatusUnzipFailed, 0, cancel);
                    }
                    return InstallStatusUnzipFailed;
                }
            }
        }
        else {
            BOOST_LOG_TRIVIAL(error) << boost::format("[install_plugin]: %1%, mz_zip_reader_file_stat for file %2% failed")%__LINE__%i;
        }
    }

    close_zip_reader(&archive);

    if (name == "plugins") {
        std::string config_version = app_config->get_network_plugin_version();
        if (config_version.empty()) {
            config_version = get_latest_network_version();
            BOOST_LOG_TRIVIAL(info) << "[install_plugin] config_version was empty, using latest: " << config_version;
            app_config->set_network_plugin_version(config_version);
            GUI::wxGetApp().CallAfter([this] {
                if (app_config)
                    app_config->save();
            });
        }
        if (!config_version.empty() && boost::filesystem::exists(legacy_lib_path)) {
#if defined(_MSC_VER) || defined(_WIN32)
            auto versioned_lib = plugin_folder / (std::string(BAMBU_NETWORK_LIBRARY) + "_" + config_version + ".dll");
#elif defined(__WXMAC__)
            auto versioned_lib = plugin_folder / (std::string("lib") + std::string(BAMBU_NETWORK_LIBRARY) + "_" + config_version + ".dylib");
#else
            auto versioned_lib = plugin_folder / (std::string("lib") + std::string(BAMBU_NETWORK_LIBRARY) + "_" + config_version + ".so");
#endif
            BOOST_LOG_TRIVIAL(info) << "[install_plugin] renaming newly extracted " << legacy_lib_path.string() << " to " << versioned_lib.string();
            boost::system::error_code ec;
            if (boost::filesystem::exists(versioned_lib)) {
                boost::filesystem::remove(versioned_lib, ec);
            }
            boost::filesystem::rename(legacy_lib_path, versioned_lib, ec);
            if (ec) {
                BOOST_LOG_TRIVIAL(error) << "[install_plugin] failed to rename to versioned: " << ec.message();
            }
        }

        if (had_existing_legacy && boost::filesystem::exists(legacy_lib_backup)) {
            BOOST_LOG_TRIVIAL(info) << "[install_plugin] restoring backed up legacy library";
            boost::system::error_code ec;
            boost::filesystem::rename(legacy_lib_backup, legacy_lib_path, ec);
            if (ec) {
                BOOST_LOG_TRIVIAL(warning) << "[install_plugin] failed to restore legacy library backup: " << ec.message();
            }
        }
    }

    {
        fs::path dir_path(plugin_folder);
        if (fs::exists(dir_path) && fs::is_directory(dir_path)) {
            int file_count = 0, file_index = 0;
            for (fs::directory_iterator it(dir_path); it != fs::directory_iterator(); ++it) {
                if (fs::is_regular_file(it->status())) { ++file_count; }
            }
            for (fs::directory_iterator it(dir_path); it != fs::directory_iterator(); ++it) {
                BOOST_LOG_TRIVIAL(info) << " current path:" << it->path().string();
                if (it->path().string() == backup_folder) {
                    continue;
                }
                auto dest_path = backup_folder.string() + "/" + it->path().filename().string();
                if (fs::is_regular_file(it->status())) {
                    BOOST_LOG_TRIVIAL(info) << " copy file:" << it->path().string() << "," << it->path().filename();
                    try {
                        if (pro_fn) { pro_fn(InstallStatusNormal, 50 + file_index / file_count, cancel); }
                        file_index++;
                        if (fs::exists(dest_path)) { fs::remove(dest_path); }
                        std::string    error_message;
                        CopyFileResult cfr = copy_file(it->path().string(), dest_path, error_message, false);
                        if (cfr != CopyFileResult::SUCCESS) { BOOST_LOG_TRIVIAL(error) << "Copying to backup failed(" << cfr << "): " << error_message; }
                    } catch (const std::exception &e) {
                        BOOST_LOG_TRIVIAL(error) << "Copying to backup failed: " << e.what();
                    }
                } else {
                    BOOST_LOG_TRIVIAL(info) << " copy framework:" << it->path().string() << "," << it->path().filename();
                    copy_framework(it->path().string(), dest_path);
                }
            }
        }
    }


    if (pro_fn)
        pro_fn(InstallStatusInstallCompleted, 100, cancel);
    if (name == "plugins")
        app_config->set_bool("installed_networking", true);
    BOOST_LOG_TRIVIAL(info) << "[install_plugin] success";
    return 0;
}

void GUI_App::restart_networking()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(" enter, mainframe %1%")%mainframe;
    on_init_network(true);
    StaticBambuLib::reset();
    if(m_agent) {
        init_networking_callbacks();
        m_agent->set_on_ssdp_msg_fn(
            [this](std::string json_str) {
                if (is_closing()) {
                    return;
                }
                GUI::wxGetApp().CallAfter([this, json_str] {
                    if (m_device_manager) {
                        m_device_manager->on_machine_alive(json_str);
                    }
                    });
            }
        );
        m_agent->set_on_http_error_fn([this](CloudEvent event, unsigned int status, std::string body) {
            this->handle_http_error(status, body, event.provider);
        });
        switch_printer_agent();
        if (mainframe)
            mainframe->refresh_plugin_tips();
        if (plater_)
            plater_->get_notification_manager()->bbl_close_plugin_install_notification();

        if (m_agent->is_user_login()) {
            remove_user_presets();
            enable_user_preset_folder(true);
            preset_bundle->load_user_presets(m_agent->get_user_id(), ForwardCompatibilitySubstitutionRule::Enable);
            mainframe->update_side_preset_ui();
        }

        if (app_config->get("sync_user_preset") == "true") {
            start_sync_user_preset();
        }
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(" exit, m_agent=%1%")%m_agent;
}

namespace {
    constexpr int CALLBACK_DRAIN_TIMEOUT_MS   = 200;
    constexpr int NETWORK_IDLE_TIMEOUT_MS     = 500;
    constexpr int FINAL_DRAIN_TIMEOUT_MS      = 100;
    constexpr int POLL_INTERVAL_MS            = 50;
    constexpr int MAX_YIELD_ITERATIONS        = 20;
    void stop_orca_health_check(NetworkAgent* agent)
    {
        if (!agent)
            return;
        if (auto orca_agent = std::dynamic_pointer_cast<OrcaCloudServiceAgent>(agent->get_cloud_agent()))
            orca_agent->stop_health_check();
    }
}

void GUI_App::drain_pending_events(int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    int yield_count = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        if (wxTheApp) {
            wxTheApp->ProcessPendingEvents();
        }

        if (yield_count < MAX_YIELD_ITERATIONS) {
            wxYield();
            ++yield_count;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }
}

bool GUI_App::wait_for_network_idle(int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        if (!m_agent) {
            return true;
        }

        bool server_disconnected = !m_agent->is_server_connected();

        if (server_disconnected) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": network is idle";
            return true;
        }

        if (wxTheApp) {
            wxTheApp->ProcessPendingEvents();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }

    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": timeout after " << timeout_ms
                                << "ms, server_connected=" << (m_agent ? m_agent->is_server_connected() : false);
    return false;
}

void GUI_App::migrate_network_plugin_config()
{
    if (!app_config)
        return;

    const std::string cfg = app_config->get_network_plugin_version();
    if (!is_series_managed_version(cfg) || !is_supported_network_version(cfg))
        return;

    const std::string series = network_plugin_series(cfg);
    if (cfg != series) {
        app_config->set_network_plugin_version(series);
        app_config->save();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": network_plugin_version " << cfg << " -> " << series;
    }

    std::string newest;
    for (const auto& v : BBLNetworkPlugin::scan_plugin_versions())
        if (is_series_managed_version(v) && network_plugin_series(v) == series && (newest.empty() || v > newest))
            newest = v;
    if (newest.empty())
        return;

    boost::system::error_code ec;
    const boost::filesystem::path series_path(BBLNetworkPlugin::get_versioned_library_path(series));
    if (newest != series) {
        boost::filesystem::remove(series_path, ec);
        boost::filesystem::rename(BBLNetworkPlugin::get_versioned_library_path(newest), series_path, ec);
        if (ec)
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": rename " << newest << " -> " << series
                                       << " failed (" << ec.message() << "), loader will resolve it";
    }

    for (const auto& v : BBLNetworkPlugin::scan_plugin_versions()) {
        if (v == series || !is_series_managed_version(v) || network_plugin_series(v) != series)
            continue;
        boost::filesystem::remove(BBLNetworkPlugin::get_versioned_library_path(v), ec);
    }
}

bool GUI_App::hot_reload_network_plugin()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": starting hot reload";

    wxBusyCursor busy;
    wxBusyInfo info(_L("Reloading network plug-in..."), mainframe);
    wxYield();
    wxWindowDisabler disabler;

    if (mainframe) {
        wxString current_tab = mainframe->m_tabpanel->GetSelectedPageName();
        if (current_tab == TAB_ID_MONITOR) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": navigating away from Monitor tab before unload";
            mainframe->m_tabpanel->SelectPageByName(TAB_ID_PREPARE);
        }
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": stopping sync thread before unload";
    if (m_user_sync_token) {
        m_user_sync_token.reset();
    }
    if (m_sync_update_thread.joinable()) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": waiting for sync thread to finish";
        m_sync_update_thread.join();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": sync thread finished";
    }

    if (m_agent) {
        stop_orca_health_check(m_agent);

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Phase 1 - clearing callbacks";
        m_agent->set_on_ssdp_msg_fn(nullptr);
        m_agent->set_on_printer_connected_fn(nullptr);
        m_agent->set_on_server_connected_fn(nullptr);
        m_agent->set_on_http_error_fn(nullptr);
        m_agent->set_on_subscribe_failure_fn(nullptr);
        m_agent->set_on_message_fn(nullptr);
        m_agent->set_on_user_message_fn(nullptr);
        m_agent->set_on_local_connect_fn(nullptr);
        m_agent->set_on_local_message_fn(nullptr);
        m_agent->set_queue_on_main_fn(nullptr);

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Phase 2 - draining callbacks";
        drain_pending_events(CALLBACK_DRAIN_TIMEOUT_MS);

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Phase 3 - stopping operations";
        bool discovery_stopped = m_agent->start_discovery(false, false);
        int disconnect_result = m_agent->disconnect_printer();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": discovery_stopped=" << discovery_stopped
                                << ", disconnect_result=" << disconnect_result;

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Phase 4 - waiting for idle";
        bool became_idle = wait_for_network_idle(NETWORK_IDLE_TIMEOUT_MS);
        if (!became_idle) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": proceeding despite timeout";
        }

        drain_pending_events(FINAL_DRAIN_TIMEOUT_MS);

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Phase 6 - destroying agent";
        delete m_agent;
        m_agent = nullptr;
    }

    if (Slic3r::NetworkAgent::is_network_module_loaded()) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Phase 7 - unloading module";
        drain_pending_events(FINAL_DRAIN_TIMEOUT_MS);
        int unload_result = Slic3r::NetworkAgent::unload_network_module();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": unload_result=" << unload_result;
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": calling restart_networking";
    restart_networking();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": restart_networking returned";

    std::string loaded_version = Slic3r::NetworkAgent::get_version();
    bool success = m_agent != nullptr && !loaded_version.empty() && loaded_version != "00.00.00.00";
    bool user_logged_in = m_agent && m_agent->is_user_login();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": after restart_networking, is_user_login = " << user_logged_in
                            << ", m_agent = " << (m_agent ? "valid" : "null")
                            << ", version = " << loaded_version;

    if (success && m_agent && m_device_manager && !app_config->get_stealth_mode()) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": connecting to cloud server";
        m_agent->connect_server();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": re-subscribing to cloud printers";
        m_device_manager->add_user_subscribe();
    }

    if (MonitorPanel* monitor = MonitorPanel::if_built()) {
        monitor->update_network_version_footer();
        monitor->set_default();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": reset monitor panel";
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": hot reload " << (success ? "successful" : "failed");
    return success;
}

std::string GUI_App::get_latest_network_version() const
{
    return Slic3r::get_latest_network_version();
}

bool GUI_App::has_network_update_available() const
{
    std::string current = Slic3r::NetworkAgent::get_version();
    std::string latest = get_latest_network_version();

    if (current.empty() || current == "00.00.00.00")
        return false;

    return current.substr(0, 8) != latest.substr(0, 8);
}

void GUI_App::show_network_plugin_download_dialog(bool is_update)
{
    auto load_error = Slic3r::NetworkAgent::get_load_error();

    NetworkPluginDownloadDialog::Mode mode;
    if (load_error.has_error) {
        mode = NetworkPluginDownloadDialog::Mode::CorruptedPlugin;
    } else if (is_update) {
        mode = NetworkPluginDownloadDialog::Mode::UpdateAvailable;
    } else {
        mode = NetworkPluginDownloadDialog::Mode::MissingPlugin;
    }

    std::string current_version = Slic3r::NetworkAgent::get_version();

    NetworkPluginDownloadDialog dlg(mainframe, mode, current_version,
        load_error.message, load_error.technical_details);

    int result = dlg.ShowModal();

    switch (result) {
    case NetworkPluginDownloadDialog::RESULT_DOWNLOAD:
        {
            std::string selected = dlg.get_selected_version();
            app_config->set_network_plugin_version(selected);
            app_config->save();

            DownloadProgressDialog download_dlg(_L("Downloading Network Plug-in"));
            download_dlg.ShowModal();
        }
        break;

    case NetworkPluginDownloadDialog::RESULT_REMIND_LATER:
        app_config->set_remind_network_update_later(true);
        app_config->save();
        break;

    case NetworkPluginDownloadDialog::RESULT_SKIP_VERSION:
        {
            std::string latest = get_latest_network_version();
            app_config->add_skipped_network_version(latest);
            app_config->save();
        }
        break;

    case NetworkPluginDownloadDialog::RESULT_DONT_ASK:
        app_config->set_network_update_prompt_disabled(true);
        app_config->save();
        break;

    case NetworkPluginDownloadDialog::RESULT_SKIP:
    default:
        break;
    }
}

void GUI_App::remove_old_networking_plugins()
{
    std::string data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    auto plugin_folder = data_dir_path / "plugins";
    if (boost::filesystem::exists(plugin_folder)) {
        BOOST_LOG_TRIVIAL(info) << "[remove_old_networking_plugins] remove the directory "<<plugin_folder.string();
        try {
            fs::remove_all(plugin_folder);
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Failed  removing the plugins directory " << plugin_folder.string();
        }
    }
}

int GUI_App::updating_bambu_networking()
{
    DownloadProgressDialog dlg(_L("Downloading Bambu Network Plug-in"));
    dlg.ShowModal();
    return 0;
}

bool GUI_App::check_networking_version()
{
    std::string network_ver = Slic3r::NetworkAgent::get_version();
    if (!network_ver.empty()) {
        BOOST_LOG_TRIVIAL(info) << "get_network_agent_version=" << network_ver;
    }

    std::string studio_ver;
    if (use_legacy_network_plugin()) {
        studio_ver = BAMBU_NETWORK_AGENT_VERSION_LEGACY;
    } else if (app_config) {
        std::string user_version = app_config->get_network_plugin_version();
        studio_ver = user_version.empty() ? get_latest_network_version() : user_version;
    } else {
        studio_ver = get_latest_network_version();
    }

    BOOST_LOG_TRIVIAL(info) << "check_networking_version: network_ver=" << network_ver << ", expected=" << studio_ver;

    if (!use_legacy_network_plugin() && !is_supported_network_version(studio_ver)) {
        m_networking_compatible = false;
        return false;
    }

    if (network_ver.length() >= 8 && studio_ver.length() >= 8) {
        if (network_ver.substr(0,8) == studio_ver.substr(0,8)) {
            m_networking_compatible = true;
            return true;
        }
    }

    m_networking_compatible = false;
    return false;
}

bool GUI_App::use_legacy_network_plugin() const
{
    return app_config && BBLNetworkPlugin::is_legacy_version(app_config->get_network_plugin_version());
}

bool GUI_App::is_compatibility_version()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": m_networking_compatible=%1%")%m_networking_compatible;
    return m_networking_compatible;
}

void GUI_App::cancel_networking_install()
{
    m_networking_cancel_update = true;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": plugin install cancelled!");
}

void GUI_App::init_networking_callbacks()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": enter, m_agent=%1%")%m_agent;
    if (m_agent) {
        m_agent->set_server_callback([](std::string url, int status) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": server_callback, url=%1%, status=%2%") % url % status;
        });


        m_agent->set_on_server_connected_fn([this](CloudEvent event, int return_code, int reason_code) {
            if (is_closing()) {
                return;
            }
            if (return_code == 5) {
                GUI::wxGetApp().CallAfter([this, provider = event.provider] {
                    BOOST_LOG_TRIVIAL(info) << "logout: login expired";
                    this->request_user_logout(provider);
                    MessageDialog msg_dlg(nullptr, _L("Login information expired. Please login again."), "", wxAPPLY | wxOK);
                    if (msg_dlg.ShowModal() == wxOK) {
                        return;
                    }
                });
                return;
            }
            GUI::wxGetApp().CallAfter([this, provider = event.provider] {
                if (is_closing())
                    return;
                BOOST_LOG_TRIVIAL(trace) << "static: server connected";
                if (provider != this->get_printer_cloud_provider()) {
                    return;
                }
                m_agent->set_user_selected_machine(m_agent->get_user_selected_machine());
                if (this->is_enable_multi_machine()) {
                    auto evt = new wxCommandEvent(EVT_UPDATE_MACHINE_LIST);
                    wxQueueEvent(this, evt);
                }
                m_agent->set_user_selected_machine(m_agent->get_user_selected_machine());
                if (m_agent->is_user_login(provider)) {

                    DeviceManager* dev = this->getDeviceManager();
                    if (!dev) return;

                    MachineObject *obj = dev->get_selected_machine();
                    if (!obj) return;

                    if (this->is_enable_multi_machine()) {

                        if (!dev->subscribe_list_cache.empty()) {
                            dev->subscribe_device_list(dev->subscribe_list_cache);
                        }
                    }
                }
            });
        });

        m_agent->set_on_printer_connected_fn([this](std::string dev_id) {
            if (is_closing()) {
                return;
            }
            GUI::wxGetApp().CallAfter([this, dev_id] {
                if (is_closing())
                    return;
                bool tunnel = boost::algorithm::starts_with(dev_id, "tunnel/");
                MachineObject* obj = m_device_manager->get_my_machine(tunnel ? dev_id.substr(7) : dev_id);
                if (obj) {
                    obj->is_tunnel_mqtt = tunnel;
                    obj->command_request_push_all(true);
                    obj->command_get_version();
                    obj->command_get_access_code();
                    if (m_agent)
                        m_agent->install_device_cert(obj->get_dev_id(), obj->is_lan_mode_printer());

                    obj->set_online_state(true);
                }
                });
            });

        m_agent->set_get_country_code_fn([this]() {
            if (app_config)
                return app_config->get_country_code();
            return std::string();
            }
        );

        m_agent->set_on_subscribe_failure_fn([this](std::string dev_id) {
            CallAfter([this, dev_id] {
                on_start_subscribe_again(dev_id);
            });
        });

        m_agent->set_on_local_connect_fn(
            [this](int state, std::string dev_id, std::string msg) {
                if (is_closing()) {
                    return;
                }
                CallAfter([this, state, dev_id, msg] {
                    if (is_closing()) {
                        return;
                    }
                    MachineObject* obj = m_device_manager->get_my_machine(dev_id);
                    wxCommandEvent event(EVT_CONNECT_LAN_MODE_PRINT);

                    if (obj) {

                        if (obj->is_lan_mode_printer()) {
                            if (state == ConnectStatus::ConnectStatusOk) {
                                obj->command_request_push_all(true);
                                obj->command_get_version();
                                event.SetInt(0);
                                event.SetString(obj->get_dev_id());

                                obj->set_online_state(true);
                            } else if (state == ConnectStatus::ConnectStatusFailed) {
                                if (m_device_manager->selected_machine != dev_id) return;

                                m_device_manager->set_selected_machine("");
                                wxString text;
                                if (msg == "5") {
                                    obj->set_access_code("");
                                    text = wxString::Format(_L("Incorrect password"));
                                    wxGetApp().show_dialog(text);
                                } else {
                                text = wxString::Format(_L("Connect %s failed! [SN:%s, code=%s]"), from_u8(obj->get_dev_name()), obj->get_dev_id(), msg);
                                    wxGetApp().show_dialog(text);
                                }
                                event.SetInt(-1);

                                obj->set_online_state(false);
                            } else if (state == ConnectStatus::ConnectStatusLost) {
                                m_device_manager->set_selected_machine("");
                                event.SetInt(-1);
                                BOOST_LOG_TRIVIAL(info) << "set_on_local_connect_fn: state = lost";

                                obj->set_online_state(false);
                            } else {
                                event.SetInt(-1);
                                BOOST_LOG_TRIVIAL(info) << "set_on_local_connect_fn: state = " << state;
                            }

                            obj->set_lan_mode_connection_state(false);
                        }
                        else {
                            if (state == ConnectStatus::ConnectStatusOk) {
                                event.SetInt(1);
                                event.SetString(obj->get_dev_id());
                            }
                            else if(msg == "5") {
                                event.SetInt(5);
                                event.SetString(obj->get_dev_id());
                            }
                            else {
                                event.SetInt(-2);
                                event.SetString(obj->get_dev_id());
                            }
                        }
                    }
                    if (wxGetApp().plater()->get_select_machine_dialog()) {
                        wxPostEvent(wxGetApp().plater()->get_select_machine_dialog(), event);
                    }
                });
            }
        );

        auto message_arrive_fn = [this](std::string dev_id, std::string msg) {
            if (is_closing()) {
                return;
            }
            CallAfter([this, dev_id, msg] {
                if (is_closing())
                    return;

                if (process_network_msg(dev_id, msg)) {
                    return;
                }

                const std::string provider = this->get_printer_cloud_provider();
                if (MachineObject* obj = this->m_device_manager->get_user_machine(dev_id, provider)) {
                    auto sel = this->m_device_manager->get_selected_machine();
                    if (sel && sel->get_dev_id() == dev_id) {
                        obj->parse_json("cloud", msg);
                        GUI::wxGetApp().sidebar().load_ams_list(obj);
                    } else {
                        obj->parse_json("cloud", msg, true);
                    }
                }

                if (GUI::wxGetApp().plater())
                    GUI::wxGetApp().plater()->update_machine_sync_status();
            });
        };

        m_agent->set_on_message_fn(message_arrive_fn);

        auto user_message_arrive_fn = [this](std::string user_id, std::string msg) {
            if (is_closing()) {
                return;
            }
            CallAfter([this, user_id, msg] {
                if (is_closing())
                    return;

                if (user_id == m_agent->get_user_id(get_printer_cloud_provider())) {
                    this->m_user_manager->parse_json(msg);
                }

            });
        };

        m_agent->set_on_user_message_fn(user_message_arrive_fn);


        auto lan_message_arrive_fn = [this](std::string dev_id, std::string msg) {
            if (is_closing()) {
                return;
            }
            CallAfter([this, dev_id, msg] {
                if (is_closing())
                    return;

                if (this->process_network_msg(dev_id, msg)) {
                    return;
                }

                if (MachineObject* obj = m_device_manager->get_my_machine(dev_id)) {
                    obj->parse_json("lan", msg);
                    if (this->m_device_manager->get_selected_machine() == obj &&
                        m_agent->get_filament_sync_mode() == FilamentSyncMode::subscription) {
                        GUI::wxGetApp().sidebar().load_ams_list(obj);
                    }
                }

                if (GUI::wxGetApp().plater())
                    GUI::wxGetApp().plater()->update_machine_sync_status();
                });
        };
        m_agent->set_on_local_message_fn(lan_message_arrive_fn);
        m_agent->set_queue_on_main_fn([this](std::function<void()> callback) {
            CallAfter(callback);
        });
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": exit, m_agent=%1%")%m_agent;
}

GUI_App::~GUI_App()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": enter");

    if (m_agent)
        m_agent->set_printer_agent(nullptr);
    NetworkAgentFactory::clear_printer_agent_cache();

    Slic3r::PluginManager::instance().shutdown();
    Slic3r::PythonInterpreter::instance().shutdown();

    if (app_config != nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": destroy app_config");
        delete app_config;
    }

    if (preset_bundle != nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": destroy preset_bundle");
        delete preset_bundle;
    }

    if (preset_updater != nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": destroy preset updater");
        delete preset_updater;
    }

    StaticBambuLib::release();
    BBLNetworkPlugin::shutdown();


    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": exit");
    shutdown_console_logging();
}

bool GUI_App::is_blocking_printing(MachineObject *obj_)
{
    PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    const std::string source_model = preset_bundle
        ? preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle)
        : std::string();
    return is_blocking_printing(obj_, source_model);
}

bool GUI_App::is_blocking_printing(MachineObject *obj_, const std::string& source_model)
{
    DeviceManager *dev = getDeviceManager();
    if (!dev) return true;
    if (obj_ == nullptr)
        obj_ = dev->get_selected_machine();
    if (!obj_)
        return false;

    return !DevPrinterConfigUtil::is_printer_model_compatible(source_model, *obj_);
}

std::string GUI_App::get_gl_info(bool for_github)
{
    return OpenGLManager::get_gl_info().to_string(for_github);
}

wxGLContext* GUI_App::init_glcontext(wxGLCanvas& canvas)
{
    return m_opengl_mgr.init_glcontext(canvas, init_params != nullptr ? init_params->opengl_version : std::make_pair(0, 0),
        init_params != nullptr ? init_params->opengl_compatibility_profile : false, init_params != nullptr ? init_params->opengl_debug : false);
}

bool GUI_App::init_opengl()
{
#ifdef __linux__
    bool status = m_opengl_mgr.init_gl();
    if (status)
        m_opengl_initialized = true;
    return status;
#else
    return m_opengl_mgr.init_gl();
#endif
}

static boost::optional<Semver> parse_semver_from_ini(std::string path)
{
    std::ifstream stream(path);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    std::string body = buffer.str();
    size_t start = body.find("OrcaSlicer ");
    if (start == std::string::npos) {
        start = body.find("OrcaSlicer ");
        if (start == std::string::npos)
            return boost::none;
    }
    body = body.substr(start + 12);
    size_t end = body.find_first_of(" \n");
    if (end < body.size())
        body.resize(end);
    return Semver::parse(body);
}

void GUI_App::init_download_path()
{
    std::string down_path = app_config->get("download_path");

    if (down_path.empty()) {
        std::string user_down_path = wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir_Downloads).ToUTF8().data();
        app_config->set("download_path", user_down_path);
    }
    else {
        fs::path dp(down_path);
        if (!fs::exists(dp)) {

            std::string user_down_path = wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir_Downloads).ToUTF8().data();
            app_config->set("download_path", user_down_path);
        }
    }
}

#if wxUSE_WEBVIEW_EDGE
void GUI_App::init_webview_runtime()
{
    if (WebView::CheckWebViewRuntime()) {
        BOOST_LOG_TRIVIAL(info) << "WebView2 runtime detected.";
        return;
    }

    BOOST_LOG_TRIVIAL(warning) << "WebView2 runtime not found; prompting user to install.";
    int nRet = wxMessageBox(_L("Orca Slicer requires the Microsoft WebView2 Runtime to operate certain features.\nClick Yes to install it now."),
                            _L("WebView2 Runtime"), wxYES_NO);
    if (nRet != wxYES) {
        BOOST_LOG_TRIVIAL(warning) << "User declined WebView2 runtime installation.";
        return;
    }

    bool installed = WebView::DownloadAndInstallWebViewRuntime();

    if (installed && WebView::CheckWebViewRuntime()) {
        BOOST_LOG_TRIVIAL(info) << "WebView2 runtime installed successfully.";
    } else {
        BOOST_LOG_TRIVIAL(error) << "WebView2 runtime installation failed or still not detected.";
        wxMessageBox(_L("The Microsoft WebView2 Runtime could not be installed.\n"
                        "Some features, including the setup wizard, may appear blank until it is installed.\n"
                        "Please install it manually from https://developer.microsoft.com/microsoft-edge/webview2/ and restart Orca Slicer."),
                     _L("WebView2 Runtime"), wxOK | wxICON_WARNING);
    }
}
#endif

void GUI_App::init_app_config()
{
    SetAppName(SLIC3R_APP_KEY);

    if (data_dir().empty()) {
        auto _app_folder = boost::filesystem::path(wxStandardPaths::Get().GetExecutablePath().ToUTF8().data()).parent_path();
#ifdef __APPLE__
        _app_folder = _app_folder.parent_path().parent_path().parent_path();
#endif
        boost::filesystem::path app_data_dir_path = _app_folder / "data_dir";
        if (boost::filesystem::exists(app_data_dir_path)) {
            set_data_dir(app_data_dir_path.string());
        }
        else{
            boost::filesystem::path data_dir_path;
            #ifndef __linux__
                std::string data_dir = wxStandardPaths::Get().GetUserDataDir().ToUTF8().data();
                data_dir_path = boost::filesystem::path(data_dir);
                set_data_dir(data_dir);
            #else
                wxString dir;
                if (! wxGetEnv(wxS("XDG_CONFIG_HOME"), &dir) || dir.empty() )
                    dir = wxFileName::GetHomeDir() + wxS("/.config");
                data_dir_path = boost::filesystem::path((dir + "/" + GetAppName()).ToUTF8().data());
                migrate_flatpak_legacy_datadir(data_dir_path);
                set_data_dir(data_dir_path.string());
            #endif
            if (!boost::filesystem::exists(data_dir_path)){
                boost::filesystem::create_directory(data_dir_path);
            }
        }

#ifdef _WIN32
    [[maybe_unused]] auto unused_result = _chdir(encode_path((Slic3r::data_dir() + "/log").c_str()).c_str());
#else
    [[maybe_unused]] auto unused_result = chdir(encode_path((Slic3r::data_dir() + "/log").c_str()).c_str());
#endif

    } else {
        m_datadir_redefined = true;
    }

    std::time_t       t        = std::time(0);
    std::tm *         now_time = std::localtime(&t);
    std::stringstream buf;
    buf << std::put_time(now_time, "debug_%a_%b_%d_%H_%M_%S_");
    buf << get_current_pid() << ".log";
    std::string log_filename = buf.str();
#if !BBL_RELEASE_TO_PUBLIC
    set_log_path_and_level(log_filename, 5);
#else
    set_log_path_and_level(log_filename, 3);
#endif

    BOOST_LOG_TRIVIAL(info) << boost::format("gui mode, Current OrcaSlicer Version %1% build %2%") % SoftFever_VERSION % build_commit_label;

	if (!app_config)
        app_config = new AppConfig();

    m_config_corrupted = false;
	m_app_conf_exists = app_config->exists();
	if (m_app_conf_exists) {
        std::string error = app_config->load();
        if (!error.empty()) {
            m_config_corrupted = true;

        }
        m_last_config_version = app_config->orig_version();
    }
    else {
#ifdef _WIN32
        if (is_associate_files(L"3mf")) {
            app_config->set("associate_3mf", "true");
        }
        if (is_associate_files(L"stl")) {
            app_config->set("associate_stl", "true");
        }
        if (is_associate_files(L"step") && is_associate_files(L"stp")) {
            app_config->set("associate_step", "true");
        }
#endif
    }
    if (app_config->get("enable_speed_dial").empty())
        app_config->set_bool("enable_speed_dial", true);
    set_logging_level(Slic3r::level_string_to_boost(app_config->get("log_severity_level")));

}

bool GUI_App::check_older_app_config(Semver current_version, bool backup)
{
    return false;
}

void GUI_App::copy_older_config()
{
    preset_bundle->copy_files(m_older_data_dir_path);
}

std::string GUI_App::get_bbl_client_version()
{
    if (BBLNetworkPlugin::instance().get_get_my_token() == nullptr) {
        return "01.10.01.50";
    }
    return VersionInfo::convert_full_version(SLIC3R_VERSION);
}

void GUI_App::on_start_subscribe_again(std::string dev_id)
{
    auto start_subscribe_timer = new wxTimer(this, wxID_ANY);
    Bind(wxEVT_TIMER, [start_subscribe_timer, dev_id](auto& e) {
        start_subscribe_timer->Stop();
        Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
        if (!dev) return;
        MachineObject* obj = dev->get_selected_machine();
        if (!obj) return;

        if ( (dev_id == obj->get_dev_id()) && obj->is_connecting() && obj->subscribe_counter > 0) {
            obj->subscribe_counter--;
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": dev_id=" << obj->get_dev_id();
        }
    });
    start_subscribe_timer->Start(5000, wxTIMER_ONE_SHOT);
}

std::string GUI_App::get_local_models_path()
{
    std::string local_path = "";
    if (data_dir().empty()) {
        return local_path;
    }

    auto models_folder = (boost::filesystem::path(data_dir()) / "models");
    local_path = models_folder.string();

    if (!fs::exists(models_folder)) {
        if (!fs::create_directory(models_folder)) {
            local_path = "";
        }
        BOOST_LOG_TRIVIAL(info) << "create models folder:" << models_folder.string();
    }
    return local_path;
}

void GUI_App::init_single_instance_checker(const std::string &name, const std::string &path)
{
    BOOST_LOG_TRIVIAL(debug) << "init wx instance checker " << name << " "<< path;
    m_single_instance_checker = std::make_unique<wxSingleInstanceChecker>(boost::nowide::widen(name), boost::nowide::widen(path));
}

bool GUI_App::OnInit()
{
    try {
        return on_init_inner();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(fatal) << "OnInit Got Fatal error: " << e.what();
        generic_exception_handle();
        return false;
    }
}

int GUI_App::OnExit()
{
    stop_http_server();
    stop_sync_user_preset();

    if (m_device_manager) {
        delete m_device_manager;
        m_device_manager = nullptr;
    }

    if (m_user_manager) {
        delete m_user_manager;
        m_user_manager = nullptr;
    }

    NetworkAgentFactory::clear_printer_agent_cache();

    if (m_agent) {
        stop_orca_health_check(m_agent);
#ifdef __WINDOWS__
        m_agent->start_discovery(false, false);
#endif
        delete m_agent;
        m_agent = nullptr;
    }

    try {
        auto              log_folder  = boost::filesystem::path(data_dir()) / "log";
        const std::string filePattern = R"(debug_network_.*\.log\.enc)";
        std::regex        pattern(filePattern);
        if (boost::filesystem::exists(log_folder)) {
            std::vector<boost::filesystem::path> network_logs;
            for (auto& it : boost::filesystem::directory_iterator(log_folder)) {
                auto temp_path = it.path();
                if (boost::filesystem::is_regular_file(temp_path) && std::regex_match(temp_path.filename().string(), pattern)) {
                    network_logs.push_back(temp_path.filename());
                }
            }
            for (auto f : network_logs) {
                boost::filesystem::remove(f);
            }
        }
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "Failed to clean up encrypt bbl network log file";
    }

    return wxApp::OnExit();
}

void GUI_App::CleanUp()
{
    stop_orca_health_check(m_agent);
    wxApp::CleanUp();
}

class wxBoostLog : public wxLog
{
    void DoLogText(const wxString &msg) override {

        BOOST_LOG_TRIVIAL(warning) << msg.ToUTF8().data();
    }
    ~wxBoostLog() override
    {
        auto t = wxLog::SetActiveTarget(this);
        wxLog::FlushActive();
        wxLog::SetActiveTarget(t);
    }
};

std::string get_system_info()
{
    std::stringstream out;

    std::string b_start  = "";
    std::string b_end    = "";
    std::string line_end = "\n";

    out << b_start << "Operating System:    " << b_end << wxPlatformInfo::Get().GetOperatingSystemFamilyName() << line_end;
    out << b_start << "System Architecture: " << b_end << wxPlatformInfo::Get().GetBitnessName() << line_end;
    out << b_start <<
#if defined _WIN32
        "Windows Version:     "
#else
        "System Version:      "
#endif
        << b_end << wxPlatformInfo::Get().GetOperatingSystemDescription() << line_end;
    out << b_start << "Total RAM size [MB]: " << b_end << Slic3r::format_memsize_MB(Slic3r::total_physical_memory());

    return out.str();
}

void GUI_App::init_plugin_gui_wiring()
{
    PluginManager& plugin_mgr = PluginManager::instance();

    auto refresh_plugins_dialog = [] {
        if (!wxTheApp)
            return;

        GUI_App* app = &GUI::wxGetApp();
        if (app->is_closing())
            return;

        app->CallAfter([app] {
            if (!app->is_closing() && app->m_plugins_dlg)
                app->m_plugins_dlg->update_plugin_dialog_ui();
        });
    };

    auto refresh_printer_agent_dropdown_after_load = [](const std::string&)
    {
        if (!wxTheApp)
            return;

        GUI_App* app = &GUI::wxGetApp();
        if (app->is_closing())
            return;

        app->CallAfter([app]
        {
            if (!app->is_closing())
                app->refresh_printer_agent_dropdown();
        });
    };

    auto switch_printer_agent_after_unload = [](const std::string&)
    {
        if (!wxTheApp)
            return;

        GUI_App* app = &GUI::wxGetApp();
        if (app->is_closing())
            return;

        app->CallAfter([app] {
            if (app->is_closing())
                return;

            app->switch_printer_agent();
            app->refresh_printer_agent_dropdown();
        });
    };

    plugin_mgr.subscribe_on_unload_callback(PluginHostUi::close_windows_for_plugin);
    plugin_mgr.subscribe_on_load_callback([refresh_plugins_dialog](const std::string&) { refresh_plugins_dialog(); });
    plugin_mgr.subscribe_on_unload_callback([refresh_plugins_dialog](const std::string&) { refresh_plugins_dialog(); });
    plugin_mgr.subscribe_on_load_callback(NetworkAgentFactory::register_python_plugin);
    plugin_mgr.subscribe_on_unload_callback(NetworkAgentFactory::deregister_python_plugin);
    plugin_mgr.subscribe_on_load_callback([](const std::string& plugin_key) {
        if (wxTheApp == nullptr || wxGetApp().is_closing() || wxGetApp().mainframe == nullptr)
            return;
        wxGetApp().mainframe->plugin_pages().on_plugin_register(plugin_key);
    });
    plugin_mgr.subscribe_on_unload_callback([](const std::string& plugin_key) {
        if (wxTheApp == nullptr || wxGetApp().is_closing() || wxGetApp().mainframe == nullptr)
            return;
        wxGetApp().mainframe->plugin_pages().on_plugin_deregister(plugin_key);
    });
    plugin_mgr.subscribe_on_load_callback(refresh_printer_agent_dropdown_after_load);
    plugin_mgr.subscribe_on_unload_callback(switch_printer_agent_after_unload);
    plugin_mgr.subscribe_on_capability_load_callback(
        [refresh_plugins_dialog, refresh_printer_agent_dropdown_after_load](const PluginCapabilityId& capability) {
            if (capability.type == PluginCapabilityType::PrinterConnection)
                NetworkAgentFactory::register_python_printer_agent(capability.plugin_key, capability.name);
            refresh_plugins_dialog();
            refresh_printer_agent_dropdown_after_load(capability.plugin_key);
            if (wxTheApp && !wxGetApp().is_closing())
                wxGetApp().CallAfter([]() {
                    if (Plater* plater = wxGetApp().plater())
                        plater->revalidate_current_plate_if_plugins_missing();
                });
            if (capability.type == PluginCapabilityType::Pages && wxTheApp && !wxGetApp().is_closing() && wxGetApp().mainframe)
                wxGetApp().mainframe->plugin_pages().on_cap_register(capability);
        });
    plugin_mgr.subscribe_on_capability_unload_callback(
        [refresh_plugins_dialog, switch_printer_agent_after_unload](const PluginCapabilityId& capability) {
            if (capability.type == PluginCapabilityType::PrinterConnection)
                NetworkAgentFactory::deregister_python_printer_agent(capability.plugin_key, capability.name);
            if (capability.type == PluginCapabilityType::Pages && wxTheApp && !wxGetApp().is_closing() && wxGetApp().mainframe)
                wxGetApp().mainframe->plugin_pages().on_cap_deregister(capability);
            refresh_plugins_dialog();
            switch_printer_agent_after_unload(capability.plugin_key);
        });
}

bool GUI_App::on_init_inner()
{
    wxLog::SetActiveTarget(new wxBoostLog());

#ifdef __APPLE__
    register_mac_deep_link_handler();
#endif
#if BBL_RELEASE_TO_PUBLIC
    wxLog::SetLogLevel(wxLOG_Message);
#endif

    ::Label::initSysFont();

    RegisterOrcaInspectorPlugins();

    wxInitAllImageHandlers();
#ifdef NDEBUG
    wxImage::SetDefaultLoadFlags(0);
#endif

#if defined(_WIN32) && ! defined(_WIN64)
#endif
#if defined(__WXGTK20__) || defined(__WXGTK3__)
    g_object_set (gtk_settings_get_default (), "gtk-menu-images", TRUE, NULL);
#endif

#if defined(__WXGTK20__) || defined(__WXGTK3__)
    g_log_set_handler("Gtk", G_LOG_LEVEL_CRITICAL,
        [](const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data) {
            if (message && (strstr(message, "gtk_widget_set_allocation") ||
                            strstr(message, "WIDGET_REALIZED_FOR_EVENT") ||
                            strstr(message, "gtk_widget_get_style_context") ||
                            strstr(message, "gtk_style_context_add_provider")))
                return;
            g_log_default_handler(log_domain, log_level, message, user_data);
        }, nullptr);
#endif

#ifdef WIN32
    CBaseException::set_log_folder(data_dir());
#endif

    wxGetApp().Bind(wxEVT_QUERY_END_SESSION, [this](auto & e) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< "received wxEVT_QUERY_END_SESSION";
        if (mainframe) {
            wxCloseEvent e2(wxEVT_CLOSE_WINDOW);
            e2.SetCanVeto(true);
            mainframe->GetEventHandler()->ProcessEvent(e2);
            if (e2.GetVeto()) {
                e.Veto();
                return;
            }
        }
        for (auto d : dialogStack)
            d->EndModal(wxID_ABORT);
    });

#ifdef __APPLE__
    wxGetApp().Bind(wxEVT_END_SESSION, [this](wxCloseEvent &e) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "received wxEVT_END_SESSION";
        stop_sync_user_preset();
        stop_orca_health_check(m_agent);
        Slic3r::NetworkAgent::unload_network_module();
        Slic3r::PluginManager::instance().shutdown();
        Slic3r::PythonInterpreter::instance().shutdown();
        e.Skip();
    });
#endif

    const wxString resources_dir = from_u8(Slic3r::resources_dir());
    wxCHECK_MSG(wxDirExists(resources_dir), false,
        wxString::Format(_L("Resources path does not exist or is not a directory: %s"), resources_dir));

#ifdef __linux__
    if (! check_old_linux_datadir(GetAppName())) {
        std::cerr << "Quitting, user chose to move their data to new location." << std::endl;
        return false;
    }
#endif

    BOOST_LOG_TRIVIAL(info) << get_system_info();

    init_label_colours();
    init_fonts();
    wxGetApp().Update_dark_mode_flag();
    
#if defined(__WINDOWS__)
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    m_is_arm64 = false;
    if (hKernel32) {
        auto fnIsWow64Process2 = (LPFN_ISWOW64PROCESS2)GetProcAddress(hKernel32, "IsWow64Process2");
        if (fnIsWow64Process2) {
            USHORT processMachine = 0;
            USHORT nativeMachine = 0;
            if (fnIsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine)) {
                if (nativeMachine == IMAGE_FILE_MACHINE_ARM64) {
                    m_is_arm64 = true;
                }
                BOOST_LOG_TRIVIAL(info) << boost::format("processMachine architecture %1%, nativeMachine %2% m_is_arm64 %3%")%(int)(processMachine) %(int) nativeMachine %m_is_arm64;
            }
            else {
                BOOST_LOG_TRIVIAL(info) << boost::format("IsWow64Process2 failed, set m_is_arm64 to %1%") %m_is_arm64;
            }
        }
        else {
            BOOST_LOG_TRIVIAL(info) << boost::format("can not find IsWow64Process2, set m_is_arm64 to %1%") %m_is_arm64;
        }
    }
    else {
        BOOST_LOG_TRIVIAL(info) << boost::format("can not find kernel32, set m_is_arm64 to %1%") %m_is_arm64;
    }
#endif
    if (is_editor()) {
        std::string msg = Slic3r::Http::tls_global_init();
        std::string ssl_cert_store = app_config->get("tls_accepted_cert_store_location");
        bool ssl_accept = app_config->get("tls_cert_store_accepted") == "yes" && ssl_cert_store == Slic3r::Http::tls_system_cert_store();

        if (!msg.empty() && !ssl_accept) {
            RichMessageDialog
                dlg(nullptr,
                    wxString::Format(_L("%s\nDo you want to continue?"), msg),
                    "OrcaSlicer", wxICON_QUESTION | wxYES_NO);
            dlg.ShowCheckBox(_L("Remember my choice"));
            if (dlg.ShowModal() != wxID_YES) return false;

            app_config->set("tls_cert_store_accepted",
                dlg.IsCheckBoxChecked() ? "yes" : "no");
            app_config->set("tls_accepted_cert_store_location",
                dlg.IsCheckBoxChecked() ? Slic3r::Http::tls_system_cert_store() : "");
        }
    }

    load_language(wxString(), true);
#ifdef _MSW_DARK_MODE

#ifndef __WINDOWS__
    wxSystemAppearance app = wxSystemSettings::GetAppearance();
    GUI::wxGetApp().app_config->set("dark_color_mode", app.IsDark() ? "1" : "0");
    GUI::wxGetApp().app_config->save();
#endif


    bool init_dark_color_mode = dark_mode();
    bool init_sys_menu_enabled = app_config->get("sys_menu_enabled") == "1";
#ifdef __WINDOWS__
     MSWEnableDarkMode(DarkMode_Auto);
     NppDarkMode::InitDarkMode(init_dark_color_mode, init_sys_menu_enabled);
#endif

#endif


#ifdef _MSW_DARK_MODE
    if (bool new_dark_color_mode = dark_mode();
        init_dark_color_mode != new_dark_color_mode) {

#ifdef __WINDOWS__
        NppDarkMode::SetDarkMode(new_dark_color_mode);
#endif

        init_label_colours();
    }
    if (bool new_sys_menu_enabled = app_config->get("sys_menu_enabled") == "1";
        init_sys_menu_enabled != new_sys_menu_enabled) {
#ifdef __WINDOWS__
        NppDarkMode::SetSystemMenuForApp(new_sys_menu_enabled);
#endif
    }
#endif

    if(app_config->get("version") != SoftFever_VERSION) {
        app_config->set("version", SoftFever_VERSION);
    }

    wxWeakRef<SplashScreen> scrn = nullptr;
    if (app_config->get("show_splash_screen") == "true") {
        wxPoint splashscreen_pos = wxDefaultPosition;
        if (app_config->has("window_mainframe")) {
            auto metrics = WindowMetrics::deserialize(app_config->get("window_mainframe"));
            if (metrics)
                splashscreen_pos = metrics->get_rect().GetPosition();
        }

        BOOST_LOG_TRIVIAL(info) << "begin to show the splash screen...";
        scrn = new SplashScreen(splashscreen_pos);
        wxYield();
        scrn->SetText(_L("Loading configuration") + dots, 5);
    }

    BOOST_LOG_TRIVIAL(info) << "loading systen presets...";
    preset_bundle = new PresetBundle();

    preset_bundle->setup_directories();


    if (m_init_app_config_from_older)
        copy_older_config();

    if (is_editor()) {
#ifdef __WXMSW__
        if (app_config->get("associate_3mf") == "true")
            associate_files(L"3mf");
        if (app_config->get("associate_stl") == "true")
            associate_files(L"stl");
        if (app_config->get("associate_step") == "true") {
            associate_files(L"step");
            associate_files(L"stp");
        }
        associate_url(L"orcaslicer");

        if (app_config->get("associate_gcode") == "true")
            associate_files(L"gcode");
#endif

        preset_updater = new PresetUpdater();
        Bind(EVT_SLIC3R_VERSION_ONLINE, [this](const wxCommandEvent& evt) {
            if (this->plater_ != nullptr) {
                std::string skip_version_str = this->app_config->get("app", "skip_version");
                bool skip_this_version = false;
                if (!skip_version_str.empty()) {
                    BOOST_LOG_TRIVIAL(info) << "new version = " << version_info.version_str << ", skip version = " << skip_version_str;
                    if (version_info.version_str <= skip_version_str) {
                        skip_this_version = true;
                    } else {
                        app_config->set("skip_version", "");
                        skip_this_version = false;
                    }
                }
                if (!skip_this_version
                    || evt.GetInt() != 0) {
                    UpdateVersionDialog dialog(this->mainframe);
                    wxString            extmsg = wxString::FromUTF8(version_info.description);
                    dialog.update_version_info(extmsg, version_info.version_str);
                    if (evt.GetInt() != 0) {
                        dialog.m_button_skip_version->Hide();
                    }
                    switch (dialog.ShowModal())
                    {
                    case wxID_YES:
                        if (is_running_in_msix())
                            open_ms_store_product_page();
                        else
                            wxLaunchDefaultBrowser(version_info.url);
                        break;
                    case wxID_NO:
                        break;
                    default:
                        ;
                    }
                }
            }
            });

        Bind(EVT_ENTER_FORCE_UPGRADE, [this](const wxCommandEvent& evt) {
                wxString      version_str = wxString::FromUTF8(this->app_config->get("upgrade", "version"));
                wxString      description_text = wxString::FromUTF8(this->app_config->get("upgrade", "description"));
                std::string   download_url = this->app_config->get("upgrade", "url");
                wxString tips = wxString::Format(_L("Click to download new version in default browser: %s"), version_str);
                DownloadDialog dialog(this->mainframe,
                    tips,
                    _L("OrcaSlicer needs an update"),
                    false,
                    wxCENTER | wxICON_INFORMATION);
                dialog.SetExtendedMessage(description_text);

                int result = dialog.ShowModal();
                switch (result)
                {
                 case wxID_YES:
                     wxLaunchDefaultBrowser(download_url);
                     break;
                 case wxID_NO:
                     wxGetApp().mainframe->Close(true);
                     break;
                 default:
                     wxGetApp().mainframe->Close(true);
                }
            });

        Bind(EVT_SHOW_NO_NEW_VERSION, [](const wxCommandEvent& evt) {
            wxString msg = _L("This is the newest version.");
            InfoDialog dlg(nullptr, _L("Info"), msg);
            dlg.ShowModal();
        });

        Bind(EVT_SHOW_DIALOG, [this](const wxCommandEvent& evt) {
            wxString msg = evt.GetString();
            InfoDialog dlg(this->mainframe, _L("Info"), msg);
            dlg.Bind(wxEVT_DESTROY, [this](auto& e) {
                m_info_dialog_content = wxEmptyString;
            });
            dlg.ShowModal();
        });
    }
    else {
#ifdef __WXMSW__
        if (app_config->get("associate_gcode") == "true")
            associate_files(L"gcode");
#endif
    }

    preset_bundle->set_default_suppressed(true);

    preset_bundle->backup_user_folder();

    Bind(EVT_UPDATE_MACHINE_LIST, &GUI_App::on_update_machine_list, this);
    Bind(EVT_USER_LOGIN, &GUI_App::on_user_login, this);
    Bind(EVT_USER_LOGIN_HANDLE, &GUI_App::on_user_login_handle, this);
    Bind(EVT_CHECK_PRIVACY_VER, &GUI_App::on_check_privacy_update, this);
    Bind(EVT_CHECK_PRIVACY_SHOW, &GUI_App::show_check_privacy_dlg, this);

    Bind(EVT_SHOW_IP_DIALOG, &GUI_App::show_ip_address_enter_dialog_handler, this);



    std::string configured_version = app_config->get_network_plugin_version();
    BOOST_LOG_TRIVIAL(info) << "Network plugin mode: "
        << (use_legacy_network_plugin() ? ("legacy (version: " + std::string(BAMBU_NETWORK_AGENT_VERSION_LEGACY) + ")") : ("modern (version: " + configured_version + ")"));


    if (scrn) {
        scrn->SetText(_L("Loading Plugins") + dots, 20);
        wxYield();
    }

    PluginManager& plugin_mgr = PluginManager::instance();
    plugin_mgr.initialize();

    const std::string preset_folder = app_config->get("preset_folder");
    if (!preset_folder.empty()) {
        plugin_mgr.set_cloud_user(preset_folder);
    }

    plugin_mgr.discover_plugins(false, true);

    init_plugin_gui_wiring();

    m_action_registry.init();

    for (const std::string& plugin_key : plugin_mgr.get_enabled_plugin_keys()) {
        if (!plugin_mgr.is_plugin_loaded(plugin_key)) {
            plugin_mgr.load_plugin(plugin_key, false);
            BOOST_LOG_TRIVIAL(info) << "Auto-loading plugin on startup: " << plugin_key;
        }
    }

    copy_network_if_available();
    on_init_network();

    if (m_agent)
        plugin_mgr.set_cloud_agent(std::dynamic_pointer_cast<OrcaCloudServiceAgent>(m_agent->get_cloud_agent()));

    if (m_agent && m_agent->is_user_login()) {
        enable_user_preset_folder(true);
        plugin_mgr.set_cloud_user(m_agent->get_user_id());
        std::vector<std::string> not_found, unauthorized;
        plugin_mgr.fetch_plugins_from_cloud(&not_found, &unauthorized);
        if (plater()) {
            for (const auto& uuid : not_found) {
                plater()->get_notification_manager()->push_notification(
                    NotificationType::CustomNotification,
                    NotificationManager::NotificationLevel::RegularNotificationLevel,
                    format(_L("Plugin %s is no longer available."), uuid));
            }
            for (const auto& uuid : unauthorized) {
                plater()->get_notification_manager()->push_notification(
                    NotificationType::CustomNotification,
                    NotificationManager::NotificationLevel::RegularNotificationLevel,
                    format(_L("Plugin %s access is unauthorized."), uuid));
            }
        }
    } else {
        enable_user_preset_folder(false);
        plugin_mgr.set_cloud_user("");
    }

        try {
            if (scrn) { scrn->SetText(_L("Loading printer & filament profiles") + dots, 30); wxYield(); }
            init_params->preset_substitutions = preset_bundle->load_presets(*app_config, ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
        }
        catch (const std::exception& ex) {
            show_error(nullptr, ex.what());
        }

#ifdef WIN32
    register_win32_device_notification_event();
#endif
    Slic3r::I18N::set_translate_callback(libslic3r_translate_callback);

#if defined(__WXGTK__) && wxHAS_EGL
    if (Slic3r::GUI::is_running_on_x11()) {
        wxGLCanvas::PreferGLX();
        BOOST_LOG_TRIVIAL(info) << "X11 detected, using GLX for OpenGL context";
    } else if (Slic3r::GUI::is_running_on_wayland()) {
        BOOST_LOG_TRIVIAL(info) << "Wayland detected, using EGL for OpenGL context";
    } else {
        BOOST_LOG_TRIVIAL(warning) << "Unknown display backend, defaulting to EGL";
    }
#endif

    if (scrn) {
        const auto scrn_txt = _L("Creating main window") + dots;
        scrn->SetText(scrn_txt, 70);
        wxYield();
    }
    BOOST_LOG_TRIVIAL(info) << "create the main window";
    mainframe = new MainFrame();
    plater_->get_camera().select_view("topfront");
    plater_->get_camera().requires_zoom_to_bed = true;
    if (is_editor()) {
        if (starts_on_prepare()) {
            mainframe->select_tab(TAB_ID_PREPARE);
        } else {
            mainframe->select_tab(TAB_ID_HOME);
        }
    }

    sidebar().obj_list()->init();
    SetTopWindow(mainframe);

    plater_->init_notification_manager();

    m_printhost_job_queue.reset(new PrintHostJobQueue(mainframe->printhost_queue_dlg()));

    if (is_gcode_viewer()) {
        mainframe->update_layout();
        if (plater_ != nullptr)
            plater_->set_printer_technology(ptFFF);
    }
    else {
        if (scrn) { scrn->SetText(_L("Loading current preset") + dots, 85); wxYield(); }
        load_current_presets();
    }

    if (plater_ != nullptr) {
        plater_->reset_project_dirty_initial_presets();
        plater_->update_project_dirty_from_presets();
        plater_->get_partplate_list().set_filament_count(preset_bundle->filament_presets.size());
    }

#ifdef __WINDOWS__
    mainframe->topbar()->SaveNormalRect();
#endif
    if (scrn) { scrn->SetText(_L("Showing main window") + dots, 95); wxYield(); }
    mainframe->Show(true);
    if (scrn) { scrn->SetText(_L("Showing main window") + dots, 100); scrn->Destroy(); scrn = nullptr; }
    BOOST_LOG_TRIVIAL(info) << "main frame firstly shown";

    obj_list()->set_min_height();

    update_mode();

#ifdef __APPLE__
   other_instance_message_handler()->bring_instance_forward();
#endif

    Bind(EVT_HTTP_ERROR, &GUI_App::on_http_error, this);


    Bind(wxEVT_IDLE, [this](wxIdleEvent& event)
    {
        bool curr_studio_active = this->is_studio_active();
        if (m_studio_active != curr_studio_active) {
            if (curr_studio_active) {
                BOOST_LOG_TRIVIAL(info) << "studio is active, start to subscribe";
                if (m_agent) {
                    json j = json::object();
                    m_agent->start_subscribe("app");
                }
            } else {
                BOOST_LOG_TRIVIAL(info) << "studio is inactive, stop to subscribe";
                if (m_agent) {
                    json j = json::object();
                    m_agent->stop_subscribe("app");
                }
            }
            m_studio_active = curr_studio_active;
        }


        if (! plater_)
            return;

        if (!m_post_initialized && !m_adding_script_handler) {
            m_post_initialized = true;
#ifdef WIN32
            this->mainframe->register_win32_callbacks();
#endif
            this->post_init();

            update_publish_status();
        }

        if (m_post_initialized && app_config->dirty() && app_config->save_due())
            app_config->save();

    });

    m_initialized = true;

    flush_logs();

    BOOST_LOG_TRIVIAL(info) << "finished the gui app init";
    if (m_config_corrupted) {
        m_config_corrupted = false;
        show_error(nullptr,
                   _u8L(
                       "The OrcaSlicer configuration file may be corrupted and cannot be parsed.\nOrcaSlicer has attempted to recreate the "
                       "configuration file.\nPlease note, application settings will be lost, but printer profiles will not be affected."));
    }
    return true;
}

void GUI_App::copy_network_if_available()
{
    if (app_config->get("update_network_plugin") != "true")
        return;

    bool had_cache = false;
    bool installed = install_network_plugin_from_ota(had_cache);
    if (installed || !had_cache)
        app_config->set("update_network_plugin", "false");
}

bool GUI_App::install_network_plugin_from_ota(bool& had_cache)
{
    had_cache = false;

    std::string data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    auto plugin_folder = data_dir_path / "plugins";
    auto cache_folder = data_dir_path / "ota" / "plugins";
    std::string changelog_file = cache_folder.string() + "/network_plugins.json";

    std::string cached_version;
    if (boost::filesystem::exists(changelog_file)) {
        try {
            boost::nowide::ifstream ifs(changelog_file);
            json j;
            ifs >> j;
            if (j.contains("version"))
                cached_version = j["version"];
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": cached_version = " << cached_version;
        } catch (nlohmann::detail::parse_error& err) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << changelog_file << " failed: " << err.what();
        }
    }

    if (cached_version.empty()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": no version found in changelog, nothing to install";
        return false;
    }
    had_cache = true;

    std::string network_library, player_library, live555_library, network_library_dst, player_library_dst, live555_library_dst;
#if defined(_MSC_VER) || defined(_WIN32)
    network_library = cache_folder.string() + "/bambu_networking.dll";
    player_library = cache_folder.string() + "/BambuSource.dll";
    live555_library = cache_folder.string() + "/live555.dll";
    network_library_dst = plugin_folder.string() + "/" + std::string(BAMBU_NETWORK_LIBRARY) + "_" + cached_version + ".dll";
    player_library_dst = plugin_folder.string() + "/BambuSource.dll";
    live555_library_dst = plugin_folder.string() + "/live555.dll";
#elif defined(__WXMAC__)
    network_library = cache_folder.string() + "/libbambu_networking.dylib";
    player_library = cache_folder.string() + "/libBambuSource.dylib";
    live555_library = cache_folder.string() + "/liblive555.dylib";
    network_library_dst = plugin_folder.string() + "/lib" + std::string(BAMBU_NETWORK_LIBRARY) + "_" + cached_version + ".dylib";
    player_library_dst = plugin_folder.string() + "/libBambuSource.dylib";
    live555_library_dst = plugin_folder.string() + "/liblive555.dylib";
#else
    network_library = cache_folder.string() + "/libbambu_networking.so";
    player_library = cache_folder.string() + "/libBambuSource.so";
    live555_library = cache_folder.string() + "/liblive555.so";
    network_library_dst = plugin_folder.string() + "/lib" + std::string(BAMBU_NETWORK_LIBRARY) + "_" + cached_version + ".so";
    player_library_dst = plugin_folder.string() + "/libBambuSource.so";
    live555_library_dst = plugin_folder.string() + "/liblive555.so";
#endif

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": checking network_library " << network_library << ", player_library " << player_library;
    if (!boost::filesystem::exists(plugin_folder)) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": create directory " << plugin_folder.string();
        boost::filesystem::create_directory(plugin_folder);
    }

    auto install_file = [](const std::string& src, const std::string& dst) -> bool {
        boost::system::error_code ec;
        if (boost::filesystem::exists(dst, ec)) {
            boost::filesystem::remove(dst, ec);
            if (ec) {
                boost::filesystem::path aside(dst);
                aside += ".old";
                boost::system::error_code ec2;
                boost::filesystem::remove(aside, ec2);
                boost::filesystem::rename(dst, aside, ec2);
                if (ec2) {
                    BOOST_LOG_TRIVIAL(error) << "install_network_plugin_from_ota: cannot replace in-use file " << dst << ": " << ec2.message();
                    return false;
                }
            }
        }
        std::string error_message;
        CopyFileResult cfr = copy_file(src, dst, error_message, false);
        if (cfr != CopyFileResult::SUCCESS) {
            BOOST_LOG_TRIVIAL(error) << "install_network_plugin_from_ota: copying " << src << " failed(" << cfr << "): " << error_message;
            return false;
        }
        static constexpr const auto perms = fs::owner_read | fs::owner_write | fs::group_read | fs::others_read;
        fs::permissions(dst, perms);
        boost::filesystem::remove(src, ec);
        return true;
    };

    if (boost::filesystem::exists(network_library)) {
        if (!install_file(network_library, network_library_dst))
            return false;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Copying network library from " << network_library << " to " << network_library_dst << " successfully.";

        app_config->set_network_plugin_version(cached_version);
        app_config->save();
    }

    if (boost::filesystem::exists(player_library)) {
        if (!install_file(player_library, player_library_dst))
            return false;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Copying player library from " << player_library << " to " << player_library_dst << " successfully.";
    }

    if (boost::filesystem::exists(live555_library)) {
        if (!install_file(live555_library, live555_library_dst))
            return false;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Copying live555 library from " << live555_library << " to " << live555_library_dst << " successfully.";
    }
    try {
        if (boost::filesystem::exists(cache_folder))
            fs::remove_all(cache_folder);
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": failed to remove the plugin cache folder " << cache_folder.string();
    }
    return true;
}

bool GUI_App::on_init_network(bool try_backup)
{
    {
        boost::filesystem::path plugin_folder = boost::filesystem::path(data_dir()) / "plugins";
        boost::system::error_code ec;
        if (boost::filesystem::is_directory(plugin_folder, ec)) {
            for (boost::filesystem::directory_iterator it(plugin_folder, ec), end; !ec && it != end; it.increment(ec)) {
                if (it->path().extension() == ".old") {
                    boost::system::error_code rm_ec;
                    boost::filesystem::remove(it->path(), rm_ec);
                    if (rm_ec)
                        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": could not remove stale " << it->path().filename().string()
                                                << " (" << rm_ec.message() << "), will retry next launch";
                }
            }
        }
    }

    auto should_load_networking_plugin = app_config->get_bool("installed_networking");

    migrate_network_plugin_config();

    std::string config_version = app_config->get_network_plugin_version();

    if (should_load_networking_plugin) {
        if (!config_version.empty() && !is_supported_network_version(config_version)) {
            std::string latest = get_latest_network_version();
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": configured plugin version " << config_version
                                       << " is no longer supported, falling back to " << latest;
            config_version = BBLNetworkPlugin::versioned_library_exists(latest) ? latest : "";
            app_config->set_network_plugin_version(config_version);
            app_config->save();
        }

        if (config_version.empty()) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": no version configured, need to download";
            m_networking_need_update = true;

            if (!m_device_manager)
                m_device_manager = new Slic3r::DeviceManager();
            if (!m_user_manager)
                m_user_manager = new Slic3r::UserManager();

            return false;
        }

        int load_agent_dll = Slic3r::NetworkAgent::initialize_network_module(false, config_version);
    __retry:
        if (!load_agent_dll) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_init_network, load dll ok";

            std::string loaded_version = Slic3r::NetworkAgent::get_version();
            if (app_config && !loaded_version.empty() && loaded_version != "00.00.00.00") {
                std::string config_version = app_config->get_network_plugin_version();
                std::string loaded_series  = network_plugin_series(loaded_version);
                if (network_plugin_series(config_version) != loaded_series) {
                    std::string synced = is_series_managed_version(loaded_version) ? loaded_series : loaded_version;
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": syncing config version from " << config_version
                                            << " to loaded " << loaded_version << " (stored as " << synced << ")";
                    app_config->set_network_plugin_version(synced);
                    app_config->save();
                }
            }

            if (check_networking_version()) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_init_network, compatibility version";
                auto bambu_source = Slic3r::NetworkAgent::get_bambu_source_entry();
                if (!bambu_source) {
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": can not get bambu source module!";
                    m_networking_compatible = false;
                    if (should_load_networking_plugin) {
                        m_networking_need_update = true;
                    }
                }
            } else {
                if (try_backup) {
                    int result = Slic3r::NetworkAgent::unload_network_module();
                    BOOST_LOG_TRIVIAL(info) << "on_init_network, version mismatch, unload_network_module, result = " << result;
                    load_agent_dll = Slic3r::NetworkAgent::initialize_network_module(true, config_version);
                    try_backup     = false;
                    goto __retry;
                }
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_init_network, version dismatch, need upload network module";
                if (should_load_networking_plugin) {
                    m_networking_need_update = true;
                }
            }
        } else {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_init_network, load dll failed";
            std::string latest = get_latest_network_version();
            if (config_version != latest && !BBLNetworkPlugin::versioned_library_exists(config_version)
                && BBLNetworkPlugin::versioned_library_exists(latest)) {
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": falling back to installed " << latest;
                config_version = latest;
                app_config->set_network_plugin_version(latest);
                app_config->save();
                load_agent_dll = Slic3r::NetworkAgent::initialize_network_module(false, config_version);
                goto __retry;
            }
            if (should_load_networking_plugin) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_init_network, need upload network module";
                m_networking_need_update = true;
            }
        }
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", create network agent...");
    std::string data_directory = data_dir();

    Slic3r::NetworkAgentFactory::register_all_agents();

    std::unique_ptr<Slic3r::NetworkAgent> agent_ptr = Slic3r::create_agent_from_config(data_directory, app_config);
    stop_orca_health_check(m_agent);
    m_agent = agent_ptr.release();

    if (!m_device_manager)
        m_device_manager = new Slic3r::DeviceManager(m_agent);
    else
        m_device_manager->set_agent(m_agent);

    if (!m_user_manager)
        m_user_manager = new Slic3r::UserManager(m_agent);
    else
        m_user_manager->set_agent(m_agent);

    if (this->is_enable_multi_machine()) {
        if (!m_task_manager) {
            m_task_manager = new Slic3r::TaskManager(m_agent);
            m_task_manager->start();
        }

        m_device_manager->EnableMultiMachine(true);
    } else {
        m_device_manager->EnableMultiMachine(false);
    }

    if (m_agent) {
        m_agent->set_config_dir(data_directory);
    }
    if (m_agent) {
        m_agent->init_log();
    }

    if (m_agent)
        m_agent->set_cert_file(resources_dir() + "/cert", "slicer_base64.cer");

    if (m_agent) {
        init_networking_callbacks();
        std::string country_code = app_config->get_country_code();
        m_agent->set_country_code(country_code);
        m_agent->start();
        check_track_enable();
    }

    if (should_load_networking_plugin && !m_networking_need_update) {
        auto& plugin = BBLNetworkPlugin::instance();
        if (plugin.is_loaded() && !plugin.has_agent()) {
            plugin.create_agent(data_directory);
        }
        if (plugin.has_agent()) {
            BBLCloudServiceAgent bbl;
            bbl.set_config_dir(data_directory);
            bbl.init_log();
            bbl.set_cert_file(resources_dir() + "/cert", "slicer_base64.cer");
            bbl.set_country_code(app_config->get_country_code());
            bbl.track_enable(false);
            bbl.track_remove_files();
            bbl.start();
        }
    }

    if (!should_load_networking_plugin) {
        int result = Slic3r::NetworkAgent::unload_network_module();
        BOOST_LOG_TRIVIAL(info) << "on_init_network, unload_network_module, result = " << result;

        if (!m_device_manager)
            m_device_manager = new Slic3r::DeviceManager();

        if (!m_user_manager)
            m_user_manager = new Slic3r::UserManager();
    }

    const std::string pinned_version = app_config->get_network_plugin_version();
    const bool pinned_to_older_series = !pinned_version.empty() &&
        network_plugin_series(pinned_version) != network_plugin_series(get_latest_network_version());

    if (should_load_networking_plugin && m_networking_compatible && !pinned_to_older_series) {
        app_config->clear_remind_network_update_later();

        if (has_network_update_available()) {
            std::string latest = get_latest_network_version();

            bool should_prompt = !app_config->is_network_update_prompt_disabled()
                && !app_config->is_network_version_skipped(latest)
                && !app_config->should_remind_network_update_later();

            if (should_prompt) {
                CallAfter([this]() {
                    show_network_plugin_download_dialog(true);
                });
            }
        }
    }

    return true;
}

unsigned GUI_App::get_colour_approx_luma(const wxColour &colour)
{
    double r = colour.Red();
    double g = colour.Green();
    double b = colour.Blue();

    return std::round(std::sqrt(
        r * r * .241 +
        g * g * .691 +
        b * b * .068
        ));
}

void GUI_App::refresh_printer_agent_dropdown()
{
    if (Tab* tab = get_tab(Preset::TYPE_PRINTER))
    {
        if (auto* printer_tab = dynamic_cast<TabPrinter*>(tab))
            printer_tab->refresh_printer_agent_dropdown();
    }
}

void GUI_App::set_live_printer_agent(std::shared_ptr<IPrinterAgent> agent)
{
    if (!m_agent)
        return;

    if (DeviceManager* dev = getDeviceManager())
    {
        dev->set_selected_machine("");
        m_agent->set_user_selected_machine("");
        dev->OnSelectedMachineLost();
        dev->clear_other_devices();
    }

    m_agent->set_printer_agent(agent);
    sidebar().update_all_preset_comboboxes();
}

std::string GUI_App::resolve_printer_agent_id(const std::string& stored_id) const
{
    if (!stored_id.empty())
        return stored_id;
    return (preset_bundle && preset_bundle->is_bbl_vendor()) ? BBL_PRINTER_AGENT_ID : ORCA_PRINTER_AGENT_ID;
}

std::string GUI_App::canonical_printer_agent_id(const std::string& picked_id)
{
    return picked_id == resolve_printer_agent_id("") ? std::string() : picked_id;
}

void GUI_App::switch_printer_agent()
{
    if (!m_agent) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": no agent exists";
        return;
    }

    const DynamicPrintConfig& config = preset_bundle->printers.get_edited_preset().config;
    const std::string effective_agent_id = resolve_printer_agent_id(config.opt_string("printer_agent"));

    const PrinterAgentInfo* agent_info_ptr = NetworkAgentFactory::get_printer_agent_info(effective_agent_id);
    if (!agent_info_ptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": agent ID '" << effective_agent_id
                                << "' is unregistered; clearing live printer agent";
        set_live_printer_agent(nullptr);
        return;
    }
    const PrinterAgentInfo agent_info = *agent_info_ptr;

    std::string log_dir        = data_dir();
    std::string cloud_agent_id = agent_info.id == BBL_PRINTER_AGENT_ID ? BBL_CLOUD_PROVIDER : ORCA_CLOUD_PROVIDER;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " " << agent_info.id;
    std::shared_ptr<ICloudServiceAgent> cloud_agent = m_agent->get_cloud_agent(cloud_agent_id);

    std::shared_ptr<IPrinterAgent> new_printer_agent =
        NetworkAgentFactory::create_printer_agent_by_id(effective_agent_id, cloud_agent, log_dir);

    if (!new_printer_agent) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": failed to create agent '" << effective_agent_id
                                   << "'; clearing live printer agent";
        set_live_printer_agent(nullptr);
        return;
    }

    const auto current_printer_agent = m_agent->get_printer_agent();
    if (current_printer_agent && current_printer_agent->get_agent_info().id == effective_agent_id) {
        if (effective_agent_id != BBL_PRINTER_AGENT_ID && m_device_manager && preset_bundle) {
            const std::string print_host = config.opt_string("print_host");
            if (!print_host.empty()) {
                const std::string dev_id = MachineObject::dev_id_from_address(print_host, config.opt_string("printhost_port"));
                MachineObject*    sel    = m_device_manager->get_selected_machine();
                if (!sel || sel->get_dev_id() != dev_id)
                    select_machine(effective_agent_id);
            }
        }
        return;
    }

    set_live_printer_agent(new_printer_agent);

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": printer agent switched to " << effective_agent_id;

    m_agent->start_discovery(true, false);

    select_machine(effective_agent_id);
}

void GUI_App::select_machine(const std::string& agent_id)
{
    if (agent_id == BBL_PRINTER_AGENT_ID) {
        return;
    }

    if (!m_device_manager || !preset_bundle) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": no device manager or preset bundle";
        return;
    }

    const auto& preset = preset_bundle->printers.get_edited_preset();
    const DynamicPrintConfig* host_cfg = &preset.config;

    std::string print_host = host_cfg->opt_string("print_host");
    if (print_host.empty()) {
        return;
    }
    std::string port = host_cfg->opt_string("printhost_port");

    std::string dev_id = MachineObject::dev_id_from_address(print_host, port);

    MachineObject* existing = m_device_manager->get_local_machine(dev_id);

    if (!existing) {
        auto local_machines = m_device_manager->get_local_machinelist();
        for (auto& [id, machine] : local_machines) {
            if (machine && machine->get_dev_ip() == dev_id) {
                existing = machine;
                break;
            }
        }
    }

    if (!existing) {
        BBLocalMachine machine;
        machine.dev_id = dev_id;
        machine.dev_ip = dev_id;
        machine.dev_name = dev_id;
        machine.printer_type = preset.config.opt_string("printer_model");
        auto access_code = preset.config.opt_string("printhost_apikey");
        if (access_code.empty()) {
            access_code = "88888888";
        }

        existing = m_device_manager->insert_local_device(
            machine, "lan", "free", "", access_code);

        if (!existing) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": failed to create machine dev_id=" << dev_id;
            return;
        }
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": created new machine dev_id=" << dev_id;
    }
    existing->local_use_ssl = boost::istarts_with(print_host, "https://");

    if (MonitorPanel* monitor = MonitorPanel::if_built()) {
        monitor->select_machine(dev_id);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": triggered select_machine for dev_id=" << dev_id;
    } else if (m_device_manager->set_selected_machine(dev_id)) {
        MonitorPanel::on_machine_selected(m_device_manager->get_selected_machine());
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": set_selected_machine dev_id=" << dev_id;
    }
}

bool GUI_App::dark_mode()
{
#ifdef SUPPORT_DARK_MODE
#if __APPLE__
    return wxPlatformInfo::Get().CheckOSVersion(10, 14) && mac_dark_mode();
#else
    const auto &val = wxGetApp().app_config->get("dark_color_mode");
    if (val == "1") return true;
    if (val == "0") return false;
    return check_dark_mode();
#endif
#else
    return false;
#endif
}

const wxColour GUI_App::get_label_default_clr_system()
{
    return dark_mode() ? wxColour(115, 220, 103) : wxColour(26, 132, 57);
}

const wxColour GUI_App::get_label_default_clr_modified()
{
    return dark_mode() ? wxColour(253, 111, 40) : wxColour(252, 77, 1);
}

void GUI_App::init_label_colours()
{
    bool is_dark_mode = dark_mode();
    m_color_label_modified = is_dark_mode ? wxColour("#F1754E") : wxColour("#F1754E");
    m_color_label_sys      = is_dark_mode ? wxColour("#B2B3B5") : wxColour("#363636");

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    m_color_label_default           = is_dark_mode ? wxColour(250, 250, 250) : m_color_label_sys;
    m_color_highlight_label_default = is_dark_mode ? wxColour(230, 230, 230): wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT);
    m_color_highlight_default       = is_dark_mode ? wxColour("#36363B") : wxColour("#F1F1F1");
    m_color_hovered_btn_label       = is_dark_mode ? wxColour(255, 255, 254) : wxColour(0,0,0);
    m_color_default_btn_label       = is_dark_mode ? wxColour(255, 255, 254): wxColour(0,0,0);
    m_color_selected_btn_bg         = is_dark_mode ? wxColour(84, 84, 91)   : wxColour(206, 206, 206);
#else
    m_color_label_default = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
#endif
    m_color_window_default          = is_dark_mode ? wxColour(43, 43, 43)   : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    StateColor::SetDarkMode(is_dark_mode);
}

void GUI_App::update_label_colours_from_appconfig()
{
    if (app_config->has("label_clr_sys")) {
        auto str = app_config->get("label_clr_sys");
        if (str != "")
            m_color_label_sys = wxColour(str);
    }

    if (app_config->has("label_clr_modified")) {
        auto str = app_config->get("label_clr_modified");
        if (str != "")
            m_color_label_modified = wxColour(str);
    }
}

void GUI_App::update_publish_status()
{
}

bool GUI_App::has_model_mall()
{
    if (auto cc = app_config->get_region(); cc == "CNH" || cc == "China" || cc == "")
        return false;
    return true;
}

void GUI_App::update_label_colours()
{
    for (Tab* tab : tabs_list)
        tab->update_label_colours();
}

#ifdef _WIN32
static bool is_focused(HWND hWnd)
{
    HWND hFocusedWnd = ::GetFocus();
    return hFocusedWnd && hWnd == hFocusedWnd;
}

static bool is_default(wxWindow* win)
{
    wxTopLevelWindow* tlw = find_toplevel_parent(win);
    if (!tlw)
        return false;

    return win == tlw->GetDefaultItem();
}
#endif

void GUI_App::UpdateDarkUI(wxWindow* window, bool highlited , bool just_font )
{
    if (wxButton *btn = dynamic_cast<wxButton*>(window)) {
        if (btn->GetWindowStyleFlag() & wxBU_AUTODRAW)
            return;
        else {
#ifdef _WIN32
            if (btn->GetId() == wxID_OK || btn->GetId() == wxID_CANCEL) {
                bool is_focused_button = false;
                bool is_default_button = false;

                if (!(btn->GetWindowStyle() & wxNO_BORDER)) {
                    btn->SetWindowStyle(btn->GetWindowStyle() | wxNO_BORDER);
                    highlited = true;
                }

                auto mark_button = [this, btn, highlited](const bool mark) {
                    btn->SetBackgroundColour(mark ? m_color_selected_btn_bg : highlited ? m_color_highlight_default : m_color_window_default);
                    btn->SetForegroundColour(mark ? m_color_hovered_btn_label :m_color_default_btn_label);
                    btn->Refresh();
                    btn->Update();
                };

                btn->Bind(wxEVT_ENTER_WINDOW, [mark_button](wxMouseEvent& event) { mark_button(true); event.Skip(); });
                btn->Bind(wxEVT_LEAVE_WINDOW, [mark_button, btn](wxMouseEvent& event) { mark_button(is_focused(btn->GetHWND())); event.Skip(); });
                btn->Bind(wxEVT_SET_FOCUS, [mark_button](wxFocusEvent& event) { mark_button(true); event.Skip(); });
                btn->Bind(wxEVT_KILL_FOCUS, [mark_button](wxFocusEvent& event) { mark_button(false); event.Skip(); });

                is_focused_button = is_focused(btn->GetHWND());
                is_default_button = is_default(btn);
                mark_button(is_focused_button);
            }
#endif
        }
    }

    if (Button* btn = dynamic_cast<Button*>(window)) {
        if (btn->GetWindowStyleFlag() & wxBU_AUTODRAW)
            return;
    }


    if (m_is_dark_mode) {

        auto orig_col = window->GetBackgroundColour();
        auto bg_col = StateColor::darkModeColorFor(orig_col);
        if (bg_col != orig_col) {
            window->SetBackgroundColour(bg_col);
        }

        orig_col = window->GetForegroundColour();
        auto fg_col = StateColor::darkModeColorFor(orig_col);
        auto fg_l = StateColor::GetLightness(fg_col);

        auto color_difference = StateColor::GetColorDifference(bg_col, fg_col);

        if (color_difference < 10) {
            fg_col = StateColor::SetLightness(fg_col, 90);
        }
        if (fg_l < 45) {
            fg_col = StateColor::SetLightness(fg_col, 70);
        }
        if (fg_col == bg_col) {
            fg_col = StateColor::SetLightness(fg_col, 70);
        }

        window->SetForegroundColour(fg_col);
    }
    else {
        auto original_col = window->GetBackgroundColour();
        auto bg_col = StateColor::lightModeColorFor(original_col);

        if (bg_col != original_col) {
            window->SetBackgroundColour(bg_col);
        }

        original_col = window->GetForegroundColour();
        auto fg_col = StateColor::lightModeColorFor(original_col);

        if (fg_col != original_col) {
            window->SetForegroundColour(fg_col);
        }
    }
}

static void update_dark_children_ui(wxWindow* window, bool just_buttons_update = false)
{
    if (!window) return;

    if (ScalableButton* btn = dynamic_cast<ScalableButton*>(window)) {
        btn->UpdateDarkUI();
    } else {
        wxGetApp().UpdateDarkUI(window);
    }

    auto children = window->GetChildren();
    for (auto child : children) {
        update_dark_children_ui(child);
    }
}

void GUI_App::UpdateDarkUIWin(wxWindow* win)
{
    update_dark_children_ui(win);
}

void GUI_App::Update_dark_mode_flag()
{
    m_is_dark_mode = dark_mode();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": switch the current dark mode status to %1% ")%m_is_dark_mode;
}

void GUI_App::UpdateDlgDarkUI(wxDialog* dlg)
{
#ifdef __WINDOWS__
    NppDarkMode::SetDarkExplorerTheme(dlg->GetHWND());
    NppDarkMode::SetDarkTitleBar(dlg->GetHWND());
#endif
    update_dark_children_ui(dlg);
}

void GUI_App::UpdateFrameDarkUI(wxFrame* dlg)
{
#ifdef __WINDOWS__
    NppDarkMode::SetDarkExplorerTheme(dlg->GetHWND());
    NppDarkMode::SetDarkTitleBar(dlg->GetHWND());
#endif
    update_dark_children_ui(dlg);
}

void GUI_App::UpdateDVCDarkUI(wxDataViewCtrl* dvc, bool highlited )
{
#ifdef __WINDOWS__
    UpdateDarkUI(dvc, highlited ? dark_mode() : false);
#ifdef _MSW_DARK_MODE
    HWND hwnd;
    if (!dvc->HasFlag(wxDV_NO_HEADER)) {
        hwnd = (HWND) dvc->GenericGetHeader()->GetHandle();
        hwnd = GetWindow(hwnd, GW_CHILD);
        if (hwnd != NULL)
            NppDarkMode::SetDarkListViewHeader(hwnd);
        wxItemAttr attr;
        attr.SetTextColour(NppDarkMode::GetTextColor());
        attr.SetFont(m_normal_font);
        dvc->SetHeaderAttr(attr);
    }
#endif
    if (dvc->HasFlag(wxDV_ROW_LINES))
        dvc->SetAlternateRowColour(m_color_highlight_default);
    if (dvc->GetBorder() != wxBORDER_SIMPLE)
        dvc->SetWindowStyle(dvc->GetWindowStyle() | wxBORDER_SIMPLE);
#endif
}

void GUI_App::UpdateAllStaticTextDarkUI(wxWindow* parent)
{
#ifdef __WINDOWS__
    wxGetApp().UpdateDarkUI(parent);

    auto children = parent->GetChildren();
    for (auto child : children) {
        if (dynamic_cast<wxStaticText*>(child))
            child->SetForegroundColour(m_color_label_default);
    }
#endif
}

void GUI_App::init_fonts()
{
    m_small_font = Label::Body_10;
    m_bold_font = Label::Body_10.Bold();
    m_normal_font = Label::Body_10;

#ifdef __WXMAC__
    m_small_font.SetPointSize(11);
    m_bold_font.SetPointSize(13);
#endif
    m_code_font = wxFont(wxFontInfo().Family(wxFONTFAMILY_TELETYPE));
    m_code_font.SetPointSize(m_small_font.GetPointSize());
}

void GUI_App::update_fonts(const MainFrame *main_frame)
{
	if (main_frame == nullptr)
		main_frame = this->mainframe;
    m_normal_font   = Label::Body_14;
    m_small_font    = m_normal_font;
    m_bold_font     = m_normal_font.Bold();
    m_link_font     = m_bold_font.Underlined();
    m_em_unit       = main_frame->em_unit();
    m_code_font.SetPointSize(m_small_font.GetPointSize());
}

void GUI_App::set_label_clr_modified(const wxColour& clr)
{
    return;
}

void GUI_App::set_label_clr_sys(const wxColour& clr)
{
    return;
}

bool GUI_App::get_side_menu_popup_status()
{
    return m_side_popup_status;
}

void GUI_App::set_side_menu_popup_status(bool status)
{
    m_side_popup_status = status;
}

std::string GUI_App::link_to_network_check()
{
    std::string url;
    std::string country_code = app_config->get_country_code();


    if (country_code == "US") {
        url = "https://status.bambulab.com";
    }
    else if (country_code == "CN") {
        url = "https://status.bambulab.cn";
    }
    else {
        url = "https://status.bambulab.com";
    }
    return url;
}

std::string GUI_App::link_to_lan_only_wiki()
{
    std::string url;
    std::string country_code = app_config->get_country_code();

    if (country_code == "US") {
        url = "https://wiki.bambulab.com/en/knowledge-sharing/enable-lan-mode";
    }
    else if (country_code == "CN") {
        url = "https://wiki.bambulab.com/zh/knowledge-sharing/enable-lan-mode";
    }
    else {
        url = "https://wiki.bambulab.com/en/knowledge-sharing/enable-lan-mode";
    }
    return url;
}

bool GUI_App::tabs_as_menu() const
{
    return false;
}

wxSize GUI_App::get_min_size() const
{
    return wxSize(76*m_em_unit, 49 * m_em_unit);
}

float GUI_App::toolbar_icon_scale(const bool is_limited ) const
{
#ifdef __APPLE__
    const float icon_sc = 1.0f;
#else
    const float icon_sc = m_em_unit * 0.1f;
#endif

    const std::string& auto_val = app_config->get("toolkit_size");

    if (auto_val.empty())
        return icon_sc;

    int int_val =  100;
    int_val = std::min(atoi(auto_val.c_str()), int_val);

    if (is_limited && int_val < 50)
        int_val = 50;

    return 0.01f * int_val * icon_sc;
}

void GUI_App::set_auto_toolbar_icon_scale(float scale) const
{
#ifdef __APPLE__
    const float icon_sc = 1.0f;
#else
    const float icon_sc = m_em_unit * 0.1f;
#endif

    long int_val = std::min(int(std::lround(scale / icon_sc * 100)), 100);
    std::string val = std::to_string(int_val);

    app_config->set("toolkit_size", val);
}

void GUI_App::check_printer_presets()
{
#if 0
    std::vector<std::string> preset_names = PhysicalPrinter::presets_with_print_host_information(preset_bundle->printers);
    if (preset_names.empty())
        return;

    preset_bundle->physical_printers.load_printers_from_presets(preset_bundle->printers);
#endif
}

void switch_window_pools();
void release_window_pools();

void GUI_App::recreate_GUI(const wxString &msg_name)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "recreate_GUI enter";
    m_is_recreating_gui = true;

    if (m_speed_dial_dialog) {
        m_speed_dial_dialog->Destroy();
        m_speed_dial_dialog = nullptr;
    }

    mainframe->shutdown();
    ProgressDialog dlg(msg_name, msg_name, 100, nullptr, wxPD_AUTO_HIDE);
    dlg.Pulse();
    dlg.Update(10, _L("Rebuild") + dots);

    MainFrame *old_main_frame = mainframe;
    struct ClientData : wxClientData
    {
        ~ClientData() { release_window_pools(); }
    };
    old_main_frame->SetClientObject(new ClientData);

    switch_window_pools();
    mainframe = new MainFrame();
    if (is_editor())
        mainframe->select_tab(TAB_ID_PREPARE);
    sidebar().obj_list()->init();
    SetTopWindow(mainframe);

    dlg.Update(30, _L("Rebuild") + dots);
    old_main_frame->Destroy();

    dlg.Update(80, _L("Loading current presets") + dots);
    m_printhost_job_queue.reset(new PrintHostJobQueue(mainframe->printhost_queue_dlg()));
    load_current_presets();
    mainframe->Show(true);

    dlg.Update(90, _L("Loading a mode view") + dots);

    obj_list()->set_min_height();
    update_mode();

    if (hms_query) hms_query->clear_hms_info();

    plater_->trigger_restore_project(1);
    mainframe->prebuild_pages_when_idle();

    update_publish_status();

    m_is_recreating_gui = false;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "recreate_GUI exit";
}

void GUI_App::system_info()
{
}

void GUI_App::keyboard_shortcuts(ShortcutContext page, wxWindow* parent)
{
    KBShortcutsDialog dlg(parent != nullptr ? parent : mainframe, page);
    dlg.ShowModal();
}

void GUI_App::on_shortcuts_changed()
{
    m_shortcuts->save(*app_config);
    app_config->save();
    if (mainframe == nullptr)
        return;
    mainframe->update_shortcut_labels();
    if (Plater* plater = this->plater(); plater != nullptr) {
        if (GLCanvas3D* canvas = plater->get_view3D_canvas3D(); canvas != nullptr)
            canvas->update_shortcut_tooltips();
#ifdef __WXOSX__
        obj_list()->update_shortcut_accelerators();
#endif
    }
}

void GUI_App::troubleshoot()
{
    TroubleshootDialog dlg;
    if (dlg.ShowModal() == wxID_REMOVE)
         wxGetApp().mainframe->Close(false);
}

void GUI_App::ShowUserGuide() {
    try {
        bool res = false;
        GuideFrame GuideDlg(this);
        res = GuideDlg.run();
if (res) {
            load_current_presets();
            update_publish_status();
            mainframe->refresh_plugin_tips();
        }
    } catch (std::exception &) {
    }
}

void GUI_App::ShowDownNetPluginDlg() {
    try {
        auto iter = std::find_if(dialogStack.begin(), dialogStack.end(), [](auto dialog) {
            return dynamic_cast<DownloadProgressDialog *>(dialog) != nullptr;
        });
        if (iter != dialogStack.end())
            return;
        DownloadProgressDialog dlg(_L("Downloading Bambu Network Plug-in"));
        dlg.ShowModal();
    } catch (std::exception &) {
        ;
    }
}

void GUI_App::ShowUserLogin(bool show, const std::string& provider)
{
    if (show) {
        try {
            delete login_dlg;
            auto cloud_agent = m_agent->get_cloud_agent(provider);
            login_dlg        = new ZUserLogin(cloud_agent);
            login_dlg->ShowModal();
        } catch (std::exception &) {
            ;
        }
    } else {
        if (login_dlg)
            login_dlg->EndModal(wxID_OK);
    }
}


void GUI_App::ShowOnlyFilament() {
    try {
        bool       res = false;
        GuideFrame GuideDlg(this);
        GuideDlg.SetStartPage(GuideFrame::GuidePage::BBL_FILAMENT_ONLY);
        res = GuideDlg.run();
        if (res) {
            load_current_presets();

        }
    } catch (std::exception &) {
    }
}



bool GUI_App::catch_error(std::function<void()> cb,
    const std::string& err )
{
    if (!err.empty()) {
        if (cb)
            cb();
        show_error( nullptr, err);
        return true;
    }
    return false;
}

void fatal_error(wxWindow* parent)
{
    show_error(parent, "");
}

#ifdef __WINDOWS__
#ifdef _MSW_DARK_MODE
static void update_scrolls(wxWindow* window)
{
    wxWindowList::compatibility_iterator node = window->GetChildren().GetFirst();
    while (node)
    {
        wxWindow* win = node->GetData();
        if (dynamic_cast<wxScrollHelper*>(win) ||
            dynamic_cast<wxTreeCtrl*>(win) ||
            dynamic_cast<wxTextCtrl*>(win))
            NppDarkMode::SetDarkExplorerTheme(win->GetHWND());

        update_scrolls(win);
        node = node->GetNext();
    }
}
#endif


#ifdef _MSW_DARK_MODE
void GUI_App::force_menu_update()
{
    NppDarkMode::SetSystemMenuForApp(app_config->get("sys_menu_enabled") == "1");
}
#endif
#endif

void GUI_App::force_colors_update()
{
#ifdef _MSW_DARK_MODE
#ifdef __WINDOWS__
    NppDarkMode::SetDarkMode(dark_mode());
#if wxVERSION_NUMBER < 3300
    if (WXHWND wxHWND = wxToolTip::GetToolTipCtrl())
        NppDarkMode::SetDarkExplorerTheme((HWND)wxHWND);
#endif
    NppDarkMode::SetDarkTitleBar(mainframe->GetHWND());


#endif
#endif
    m_force_colors_update = true;
}

void GUI_App::update_ui_from_settings()
{
    update_label_colours();
    if (m_force_colors_update) {
        m_force_colors_update = false;

        if (mainframe) {
#ifdef __WINDOWS__
            mainframe->force_color_changed();
            update_scrolls(mainframe);
            update_scrolls(&mainframe->m_settings_dialog);
#endif
            update_dark_children_ui(mainframe);
        }
    }

    if (mainframe) {mainframe->update_ui_from_settings();}
}

void GUI_App::persist_window_geometry(wxTopLevelWindow *window, bool default_maximized)
{
    const std::string name = into_u8(window->GetName());

    window->Bind(wxEVT_CLOSE_WINDOW, [=](wxCloseEvent &event) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< ": received wxEVT_CLOSE_WINDOW, trigger save for window_mainframe";
        window_pos_save(window, "mainframe");
        event.Skip();
    });

    if (window_pos_restore(window, "mainframe", default_maximized)) {
        on_window_geometry(window, [=]() {
            window_pos_sanitize(window);
        });
    } else {
        on_window_geometry(window, [=]() {
            window_pos_center(window);
        });
    }
}

void GUI_App::load_project(wxWindow *parent, wxString& input_file) const
{
    input_file.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _L("Choose one file (3MF):"),
        app_config->get_last_dir(), "",
        file_wildcards(FT_PROJECT), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
}

void GUI_App::import_model(wxWindow *parent, wxArrayString& input_files) const
{
    input_files.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
#ifdef __APPLE__
        _L("Choose one or more files (3MF/STEP/STL/SVG/OBJ/AMF/USD*/ABC/PLY):"),
#else
        _L("Choose one or more files (3MF/STEP/STL/SVG/OBJ/AMF):"),
#endif
        from_u8(app_config->get_last_dir()), "",
        file_wildcards(FT_MODEL), wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        dialog.GetPaths(input_files);
}

void GUI_App::import_zip(wxWindow* parent, wxString& input_file) const
{
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
                        _L("Choose ZIP file") + ":",
                        from_u8(app_config->get_last_dir()), "",
                        file_wildcards(FT_ZIP), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
}

void GUI_App::load_gcode(wxWindow* parent, wxString& input_file) const
{
    input_file.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _L("Choose one file (GCODE/3MF):"),
        app_config->get_last_dir(), "",
        file_wildcards(FT_GCODE), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
}

wxString GUI_App::transition_tridid(int trid_id) const
{
    if (trid_id == VIRTUAL_TRAY_MAIN_ID || trid_id == VIRTUAL_TRAY_DEPUTY_ID)
    {
        assert(0);
        return _L("Ext");
    }

    wxString maping_dict[] = { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z" };

    if (trid_id >= 128 * 4) {
        trid_id -= 128 * 4;
        int id_index = trid_id / 4;
        return wxString::Format("%s", maping_dict[id_index]);
    }
    else if (trid_id >= 0x80 && trid_id <= 0x87) {
        const char base = 'A' + (trid_id - 128);
        wxString prefix("HT-");
        prefix.append(base);
        return prefix;
    }
    else {
        int id_index = std::clamp((int)ceil(trid_id / 4), 0, 25);
        int id_suffix = trid_id % 4 + 1;
        return wxString::Format("%s%d", maping_dict[id_index], id_suffix);
    }
}

void GUI_App::request_login(bool show_user_info, const std::string& provider )
{
    ShowUserLogin(true, provider);

    if (show_user_info) {
        CallAfter([this, provider] {
            get_login_info(provider);
        });
    }
}

void GUI_App::get_login_info(const std::string& provider )
{
    if (m_agent) {
        if (m_agent->is_user_login(provider)) {
            std::string login_cmd = m_agent->build_login_cmd(provider);
            wxString strJS = wxString::Format("window.postMessage(%s)", from_u8(login_cmd));
            GUI::wxGetApp().run_script(strJS);
        } else {
            m_agent->user_logout(false, provider);
            std::string logout_cmd = m_agent->build_logout_cmd(provider);
            wxString    strJS      = wxString::Format("window.postMessage(%s)", from_u8(logout_cmd));
            GUI::wxGetApp().run_script(strJS);
        }
        if (WebViewPanel* home = WebViewPanel::if_built())
            home->SetLoginPanelVisibility(true);
    }
}

bool GUI_App::is_user_login(const std::string& provider )
{
    if (m_agent) {
        return m_agent->is_user_login(provider);
    }
    return false;
}

std::string GUI_App::get_printer_cloud_provider() const
{
    const std::string agent_id = resolve_printer_agent_id(
        preset_bundle ? preset_bundle->printers.get_edited_preset().config.opt_string("printer_agent")
                      : std::string());
    return agent_id == BBL_PRINTER_AGENT_ID ? BBL_CLOUD_PROVIDER : ORCA_CLOUD_PROVIDER;
}



bool GUI_App::check_login(const std::string& provider )
{
    bool result = false;
    if (m_agent) {
        result = m_agent->is_user_login(provider);
    }

    if (!result) {
        ShowUserLogin(true, provider);
    }
    return result;
}

void GUI_App::request_user_handle(int online_login, const std::string& provider )
{
    auto evt = new wxCommandEvent(EVT_USER_LOGIN_HANDLE);
    evt->SetInt(online_login);
    evt->SetString(wxString::FromUTF8(provider));
    wxQueueEvent(this, evt);
}

void GUI_App::request_user_login(int online_login, const std::string& provider )
{
    auto evt = new wxCommandEvent(EVT_USER_LOGIN);
    evt->SetInt(online_login);
    evt->SetString(wxString::FromUTF8(provider));
    wxQueueEvent(this, evt);
}

void GUI_App::post_logout_to_webview(const std::string& provider)
{
    std::string logout_cmd = m_agent->build_logout_cmd(provider);
    if (!logout_cmd.empty()) {
        wxString strJS = wxString::Format("window.postMessage(%s)", logout_cmd);
        GUI::wxGetApp().run_script(strJS);
    }
}

void GUI_App::request_user_logout(const std::string& provider )
{
    if (m_agent && m_agent->is_user_login(provider)) {
        m_agent->user_logout(true, provider);

        if (provider == get_printer_cloud_provider()) {
            m_agent->set_user_selected_machine("");
            if (m_device_manager) {
                m_device_manager->clean_user_info(true);
            }
        }

        if (provider == ORCA_CLOUD_PROVIDER) {
            bool     transfer_preset_changes = false;
            wxString header = _L("Some presets are modified.") + "\n" +
                _L("You can keep the modified presets for the new project, discard, or save changes as new presets.");
            wxGetApp().check_and_keep_current_preset_changes(_L("User logged out"), header, ActionButtons::KEEP | ActionButtons::SAVE, &transfer_preset_changes);

            remove_user_presets();
            enable_user_preset_folder(false);
            Slic3r::PluginManager::instance().unload_cloud_plugins();
            Slic3r::PluginManager::instance().clear_cloud_plugin_metadata();
            Slic3r::PluginManager::instance().set_cloud_user("");
            preset_bundle->load_user_presets(DEFAULT_USER_FOLDER_NAME, ForwardCompatibilitySubstitutionRule::Enable);
            mainframe->update_side_preset_ui();

            if (m_plugins_dlg)
                m_plugins_dlg->update_plugin_dialog_ui();

            GUI::wxGetApp().stop_sync_user_preset();
        }

        post_logout_to_webview(provider);
    }
}

int GUI_App::request_user_unbind(std::string dev_id, const std::string& provider )
{
    int result = -1;
    if (m_agent) {
        result = m_agent->unbind(dev_id);
        BOOST_LOG_TRIVIAL(info) << "request_user_unbind, dev_id = " << dev_id << ", result = " << result;
        return result;
    }
    return result;
}

std::string GUI_App::handle_web_request(std::string cmd)
{
    try {
        std::stringstream ss(cmd), oss;
        pt::ptree root, response;
        pt::read_json(ss, root);
        if (root.empty())
            return "";

        boost::optional<std::string> sequence_id = root.get_optional<std::string>("sequence_id");
        boost::optional<std::string> command = root.get_optional<std::string>("command");
        if (command.has_value()) {
            std::string command_str = command.value();
            static const std::unordered_set<std::string> stealth_blocked_info_commands = {
                "get_login_info",
                "get_orca_login_info",
                "get_bambu_login_info",
            };
            static const std::unordered_set<std::string> stealth_blocked_login_commands = {
                "homepage_login_or_register",
                "homepage_orca_login_or_register",
                "homepage_bambu_login_or_register",
            };
            if (app_config->get_stealth_mode() && stealth_blocked_info_commands.count(command_str)) {
                CallAfter([] {
                    if (WebViewPanel* home = WebViewPanel::if_built())
                        home->SendCloudProvidersInfo();
                });
                return "";
            }
            if (app_config->get_stealth_mode() && stealth_blocked_login_commands.count(command_str)) {
                CallAfter([this, command_str] {
                    MessageDialog dlg(mainframe,
                        _L("You are currently in Stealth Mode. To log into the Cloud, you need to disable Stealth Mode first."),
                        _L("Stealth Mode"),
                        wxOK | wxCANCEL | wxCENTRE);
                    dlg.SetButtonLabel(wxID_OK, _L("Quit Stealth Mode"));
                    if (dlg.ShowModal() == wxID_OK) {
                        app_config->set_bool("stealth_mode", false);
                        app_config->save();
                        if (WebViewPanel* home = WebViewPanel::if_built())
                            home->SendCloudProvidersInfo();
                        if (command_str == "homepage_login_or_register")
                            this->request_login(true);
                        else if (command_str == "homepage_orca_login_or_register")
                            this->request_login(true, ORCA_CLOUD_PROVIDER);
                        else if (command_str == "homepage_bambu_login_or_register")
                            this->request_login(true, BBL_CLOUD_PROVIDER);
                    }
                });
                return "";
            }
            if (command_str.compare("request_project_download") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree data_node = root.get_child("data");
                    boost::optional<std::string> project_id = data_node.get_optional<std::string>("project_id");
                    if (project_id.has_value()) {
                        this->request_project_download(project_id.value());
                    }
                }
            }
            else if (command_str.compare("open_project") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree data_node = root.get_child("data");
                    boost::optional<std::string> project_id = data_node.get_optional<std::string>("project_id");
                    if (project_id.has_value()) {
                        this->request_open_project(project_id.value());
                    }
                }
            }
            else if (command_str.compare("get_login_info") == 0) {
                CallAfter([this] {
                        get_login_info();
                    });
            }
            else if (command_str.compare("homepage_login_or_register") == 0) {
                CallAfter([this] {
                    this->request_login(true);
                });
            }
            else if (command_str.compare("homepage_logout") == 0) {
                CallAfter([this] {
                    BOOST_LOG_TRIVIAL(info) << "logout: homepage_logout";
                    request_user_logout();
                });
            }
            else if (command_str.compare("get_orca_login_info") == 0) {
                CallAfter([this] { get_login_info(ORCA_CLOUD_PROVIDER); });
            }
            else if (command_str.compare("get_bambu_login_info") == 0) {
                CallAfter([this] { get_login_info(BBL_CLOUD_PROVIDER); });
            }
            else if (command_str.compare("homepage_bambu_login_or_register") == 0) {
                CallAfter([this] { request_login(true, BBL_CLOUD_PROVIDER); });
            }
            else if (command_str.compare("homepage_bambu_logout") == 0) {
                CallAfter([this] {
                    BOOST_LOG_TRIVIAL(info) << "logout: homepage_bambu_logout";
                    request_user_logout(BBL_CLOUD_PROVIDER);
                });
            }
            else if (command_str.compare("homepage_orca_login_or_register") == 0) {
                CallAfter([this] { request_login(true, ORCA_CLOUD_PROVIDER); });
            }
            else if (command_str.compare("homepage_orca_logout") == 0) {
                CallAfter([this] {
                    BOOST_LOG_TRIVIAL(info) << "logout: homepage_orca_logout";
                    request_user_logout(ORCA_CLOUD_PROVIDER);
                });
            }
            else if (command_str.compare("homepage_modeldepot") == 0) {
                CallAfter([this] { open_mall_page_dialog(); });
            }
            else if (command_str.compare("homepage_newproject") == 0) {
                this->request_open_project("<new>");
            }
            else if (command_str.compare("homepage_openproject") == 0) {
                this->request_open_project({});
            }
            else if (command_str.compare("get_recent_projects") == 0) {
                if (mainframe) {
                    if (WebViewPanel* home = WebViewPanel::if_built()) {
                        home->SendRecentList(INT_MAX);
                    }
                }
            }
            else if (command_str.compare("homepage_open_recentfile") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree data_node = root.get_child("data");
                    boost::optional<std::string> path = data_node.get_optional<std::string>("path");
                    if (path.has_value()) {
                        this->request_open_project(path.value());
                    }
                }
            }
            else if (command_str.compare("homepage_delete_recentfile") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree                    data_node = root.get_child("data");
                    boost::optional<std::string> path      = data_node.get_optional<std::string>("path");
                    if (path.has_value()) {
                        this->request_remove_project(path.value());
                    }
                }
            }
            else if (command_str.compare("homepage_delete_all_recentfile") == 0) {
                this->request_remove_project("");
            }
            else if (command_str.compare("homepage_explore_recentfile") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree                    data_node = root.get_child("data");
                    boost::optional<std::string> path      = data_node.get_optional<std::string>("path");
                    if (path.has_value())
                    {
                        boost::filesystem::path NowFile(path.value());

                        std::string FilePath = NowFile.make_preferred().string();
                        desktop_open_any_folder(FilePath);
                    }
                }
            }
            else if (command_str.compare("homepage_open_hotspot") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree data_node = root.get_child("data");
                    boost::optional<std::string> url = data_node.get_optional<std::string>("url");
                    if (url.has_value()) {
                        this->request_open_project(url.value());
                    }
                }
            }
            else if (command_str.compare("begin_network_plugin_download") == 0) {
                CallAfter([] { wxGetApp().ShowDownNetPluginDlg(); });
            }
            else if (command_str.compare("get_web_shortcut") == 0) {
                if (root.get_child_optional("key_event") != boost::none) {
                    pt::ptree key_event_node = root.get_child("key_event");
                    auto keyCode = key_event_node.get<int>("key");
                    auto ctrlKey = key_event_node.get<bool>("ctrl");
                    auto shiftKey = key_event_node.get<bool>("shift");
                    auto cmdKey = key_event_node.get<bool>("cmd");

                    wxKeyEvent e(wxEVT_CHAR_HOOK);
#ifdef __APPLE__
                    e.SetControlDown(cmdKey);
                    e.SetRawControlDown(ctrlKey);
#else
                    e.SetControlDown(ctrlKey);
#endif
                    e.SetShiftDown(shiftKey);
                    keyCode     = keyCode == 188 ? ',' : keyCode;
                    e.m_keyCode = keyCode;
                    e.SetEventObject(mainframe);
                    wxPostEvent(mainframe, e);
                }
            }
            else if (command_str.compare("userguide_wiki_open") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree                    data_node = root.get_child("data");
                    boost::optional<std::string> path      = data_node.get_optional<std::string>("url");
                    if (path.has_value()) {
                        wxLaunchDefaultBrowser(path.value());
                    }
                }
            }
            else if (command_str.compare("homepage_open_ccabin") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree                    data_node = root.get_child("data");
                    boost::optional<std::string> path      = data_node.get_optional<std::string>("file");
                    if (path.has_value()) {
                        std::string Fullpath = resources_dir() + "/web/homepage/model/" + path.value();

                        this->request_open_project(Fullpath);
                    }
                }
            }
            else if (command_str.compare("common_openurl") == 0) {
                boost::optional<std::string> path      = root.get_optional<std::string>("url");
                if (path.has_value()) {
                    wxLaunchDefaultBrowser(path.value());
                }
            } 
            else if (command_str.compare("homepage_makerlab_get") == 0) {
            }
            else if (command_str.compare("makerworld_model_open") == 0) 
            {
                if (root.get_child_optional("model") != boost::none) {
                    pt::ptree                    data_node = root.get_child("model");
                    boost::optional<std::string> path      = data_node.get_optional<std::string>("url");
                    if (path.has_value()) 
                    { 
                        wxString realurl = from_u8(url_decode(path.value()));
                        wxGetApp().request_model_download(realurl);
                    }
                }
            }
        }
    }
    catch (...) {
        BOOST_LOG_TRIVIAL(warning) << "parse json cmd failed " << cmd;
        return "";
    }
    return "";
}

void GUI_App::handle_script_message(std::string msg, const std::string& provider)
{
    try {
        json j = json::parse(msg);
        if (j.contains("command")) {
            wxString cmd = j["command"];
            if (cmd == "user_login") {
                if (m_agent) {
                    m_agent->change_user(j.dump(), provider);
                    if (m_agent->is_user_login(provider)) {
                        request_user_login(1, provider);
                    }
                }
            }
        }
    }
    catch (...) {
        ;
    }
}

void GUI_App::request_model_download(wxString url)
{
    if (plater_) {
        plater_->request_model_download(url);
    }
}

void GUI_App::download_project(std::string project_id)
{
    if (plater_) {
        plater_->request_download_project(project_id);
    }
}

void GUI_App::request_project_download(std::string project_id)
{
    if (!check_login()) return;

    download_project(project_id);
}

void GUI_App::request_open_project(std::string project_id)
{
    if (plater()->is_background_process_slicing()) {
        Slic3r::GUI::show_info(nullptr, _L("new or open project file is not allowed during the slicing process!"), _L("Open Project"));
        return;
    }

    if (project_id == "<new>")
        plater()->new_project();
    else if (project_id.empty())
        plater()->load_project();
    else if (std::find_if_not(project_id.begin(), project_id.end(),
        [](char c) { return std::isdigit(c); }) == project_id.end())
        ;
    else if (boost::algorithm::starts_with(project_id, "http"))
        ;
    else
        CallAfter([this, project_id] { mainframe->open_recent_project(-1, wxString::FromUTF8(project_id)); });
}

void GUI_App::request_remove_project(std::string project_id)
{
    mainframe->remove_recent_project(-1, wxString::FromUTF8(project_id));
}

void GUI_App::handle_http_error(unsigned int status, std::string body, const std::string& provider)
{
    auto evt = new wxCommandEvent(EVT_HTTP_ERROR);
    evt->SetInt(status);
    json evt_data;
    evt_data["body"] = body;
    evt_data["provider"] = provider;
    evt->SetString(wxString::FromUTF8(evt_data.dump()));
    wxQueueEvent(this, evt);
}

static std::mutex conflict_ids_mutex;

void GUI_App::on_http_error(wxCommandEvent &evt)
{
    int status = evt.GetInt();
    std::string provider = "";
    std::string body_str;

    try {
        auto evt_str = evt.GetString().utf8_string();
        if (!evt_str.empty()) {
            json evt_data = json::parse(evt_str);
            if (evt_data.contains("provider"))
                provider = evt_data["provider"].get<std::string>();
            if (evt_data.contains("body"))
                body_str = evt_data["body"].get<std::string>();
        }
    } catch (...) {}

    int code = 0;
    std::string error;
    if (status >= 400 && status < 500) {
        try {
            if (!body_str.empty()) {
                json j = json::parse(body_str);
                if (j.contains("code")) {
                    if (!j["code"].is_null())
                        code = j["code"].get<int>();
                }
                if (j.contains("error"))
                    if (!j["error"].is_null())
                        error = j["error"].get<std::string>();
            }
        } catch (...) {}
    }

    if (code == HttpErrorVersionLimited) {
        MessageDialog msg_dlg(nullptr, _L("The version of Orca Slicer is too low and needs to be updated to the latest version before it can be used normally."), "", wxAPPLY | wxOK);
        if (msg_dlg.ShowModal() == wxOK) {
        }
    }

    if (status == 401) {
        if (m_agent) {
            if (!provider.empty() && m_agent->is_user_login(provider)) {
                if (std::chrono::steady_clock::now() - m_last_401_error_time > 30s) {
                    BOOST_LOG_TRIVIAL(warning) << "logout: http error 401.";
                    this->request_user_logout(provider);

                    if (!m_show_http_error_msgdlg) {
                        MessageDialog msg_dlg(nullptr, _L("Login information expired. Please login again."), "", wxAPPLY | wxOK);
                        m_show_http_error_msgdlg = true;
                        auto modal_result        = msg_dlg.ShowModal();
                        if (modal_result == wxOK || modal_result == wxCLOSE) {
                            m_show_http_error_msgdlg = false;
                            return;
                        }
                    }

                    m_last_401_error_time = std::chrono::steady_clock::now();
                } else {
                    BOOST_LOG_TRIVIAL(warning) << "401 encountered within grace period, suppressing logout";
                }
            }
        }
        return;
    }

    if (status == 410) {
        BOOST_LOG_TRIVIAL(info) << "Http error 410.";
        return;
    }

    if (status == 409 && provider == ORCA_CLOUD_PROVIDER) {
        BOOST_LOG_TRIVIAL(info) << "Http error 409.";
        int conflict_code = 0;
        std::string conflict_setting_id;
        std::string conflict_preset_name;
        try {
            json conflict_body = json::parse(body_str);
            if (conflict_body.contains("code"))
                conflict_code = conflict_body["code"].get<int>();
            if (conflict_body.contains("server_profile") && conflict_body["server_profile"].contains("id")
                && conflict_body["server_profile"]["id"].is_string())
                conflict_setting_id = conflict_body["server_profile"]["id"].get<std::string>();
            if (conflict_body.contains("name") && conflict_body["name"].is_string())
                conflict_preset_name = conflict_body["name"].get<std::string>();
        } catch (...) {
            BOOST_LOG_TRIVIAL(warning) << "Failed to parse 409 conflict body.";
        }
        std::string conflict_user_id = m_agent ? m_agent->get_user_id() : std::string();
        auto* plater = wxGetApp().plater();
        if (plater != nullptr && wxGetApp().imgui()->display_initialized()) {
            std::string text;

            const std::string header = conflict_preset_name.empty()
                ? _u8L("Cloud sync conflict:")
                : format(_u8L("Cloud sync conflict for preset \"%s\":"), conflict_preset_name);

            switch (conflict_code) {
            case -1:
                text = header + " " + _u8L("This preset has a newer version in OrcaCloud.\n"
                            "Pull downloads the cloud copy. Force push overwrites it with your local preset.");
                break;
            case -2:
                text = header + " " + _u8L("A preset with this name already exists in OrcaCloud.\n"
                            "Pull downloads the cloud copy. Force push overwrites it with your local preset.");
                break;
            case -3:
                text = header + " " + _u8L("A preset with the same name was previously deleted from the cloud.\n"
                            "Delete will delete your local preset. Force push overwrites it with your local preset.");
                break;
            default:
                text = header + " " + _u8L("There was an unexpected or unidentified preset conflict.\n"
                            "Pull downloads the cloud copy. Force push overwrites it with your local preset.");
                break;
            };

            plater->get_notification_manager()->push_orca_sync_conflict_notification(
                text, conflict_code,
                [this](wxEvtHandler*) {
                    if (is_closing() || !m_agent || !preset_bundle)
                        return false;
                    BOOST_LOG_TRIVIAL(info) << "Pulling Orca Cloud settings to resolve sync conflict.";
                    restart_sync_user_preset();
                    return true;
                },
                [this, conflict_setting_id, conflict_preset_name, conflict_user_id](wxEvtHandler*) {
                    if (mainframe == nullptr)
                        return false;
                    const wxString confirm_msg = conflict_preset_name.empty()
                        ? _L("Force push will overwrite the cloud copy with your local preset changes.\nDo you want to continue?")
                        : format_wxstr(_L("Force push will overwrite the cloud copy of preset \"%s\" with your local changes.\nDo you want to continue?"),
                                       conflict_preset_name);
                    MessageDialog dlg(mainframe, confirm_msg, _L("Resolve cloud sync conflict"),
                                      wxCENTER | wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
                    if (dlg.ShowModal() != wxID_YES)
                        return false;

                    std::string setting_id = conflict_setting_id;
                    if (setting_id.empty()) {
                        setting_id = OrcaCloudServiceAgent::generate_uuid_for_setting_id(conflict_preset_name, conflict_user_id);
                        BOOST_LOG_TRIVIAL(info) << "conflict setting id empty, generated one: " << setting_id;
                    }

                    force_push_conflicting_preset(setting_id);
                    return true;
                });
        }
        return;
    }

    if (provider == ORCA_CLOUD_PROVIDER && status >= 400 && code != HttpErrorVersionLimited) {
        BOOST_LOG_TRIVIAL(warning) << "API call to OrcaCloud failed with status=" << status;
    }
}

void GUI_App::enable_user_preset_folder(bool enable)
{
    if (enable) {
        std::string user_id = m_agent->get_user_id();
        app_config->set("preset_folder", user_id);
        GUI::wxGetApp().preset_bundle->update_user_presets_directory(user_id);
        PluginManager::instance().set_cloud_user(user_id);
    } else {
        BOOST_LOG_TRIVIAL(info) << "preset_folder: set to empty";
        app_config->set("preset_folder", "");
        GUI::wxGetApp().preset_bundle->update_user_presets_directory(DEFAULT_USER_FOLDER_NAME);
        PluginManager::instance().set_cloud_user("");
    }
}

void GUI_App::on_update_machine_list(wxCommandEvent &evt)
{
}

void GUI_App::on_user_login_handle(wxCommandEvent &evt)
{
    if (!m_agent) { return; }
    if (app_config->get_stealth_mode()) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": stealth mode enabled, skipping cloud connection";
        return;
    }

    int online_login = evt.GetInt();
    std::string provider = evt.GetString().ToStdString();
    if (provider.empty()) provider = ORCA_CLOUD_PROVIDER;

    m_last_401_error_time = std::chrono::steady_clock::now();

    m_agent->connect_server();
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    boost::thread update_thread = boost::thread([dev, provider] {
        dev->update_user_machine_list_info(provider);
    });

    if (online_login && provider == ORCA_CLOUD_PROVIDER) {
        ProgressDialog dlg(_L("Loading"), _L("Syncing your account…"), 100, mainframe, wxPD_AUTO_HIDE | wxPD_APP_MODAL);

        dlg.Update(10, _L("Migrating presets…"));
        maybe_migrate_user_presets_on_login();
        remove_user_presets();
        enable_user_preset_folder(true);

        dlg.Update(40, _L("Fetching plugins…"));
        PluginManager::instance().fetch_plugins_from_cloud();
        if (m_plugins_dlg)
            m_plugins_dlg->update_plugin_dialog_ui();

        dlg.Update(70, _L("Loading user presets…"));
        preset_bundle->load_user_presets(m_agent->get_user_id(provider), ForwardCompatibilitySubstitutionRule::Enable);
        mainframe->update_side_preset_ui();

        dlg.Update(100);
        GUI::wxGetApp().mainframe->show_sync_dialog();
    }

    if (app_config->get("sync_user_preset") == "true") {
        start_sync_user_preset();
    }
}


void GUI_App::check_track_enable()
{
    if (m_agent) {
        m_agent->track_enable(false);
        m_agent->track_remove_files();
    }
}

void GUI_App::on_user_login(wxCommandEvent &evt)
{
    if (!m_agent) { return; }
    int online_login = evt.GetInt();
    std::string provider = evt.GetString().ToStdString();
    if (provider.empty()) provider = ORCA_CLOUD_PROVIDER;

    check_privacy_version(online_login, provider);
    check_track_enable();
}

bool GUI_App::is_studio_active()
{
    auto curr_time = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - last_active_point);
    if (diff.count() < STUDIO_INACTIVE_TIMEOUT) {
        return true;
    }
    return false;
}

void GUI_App::reset_to_active()
{
    last_active_point = std::chrono::system_clock::now();
}

void GUI_App::check_update(bool show_tips, int by_user)
{
    if (version_info.version_str.empty()) return;
    if (version_info.url.empty()) return;

    auto curr_version = Semver::parse(SLIC3R_VERSION);
    auto remote_version = Semver::parse(version_info.version_str);
    if (curr_version && remote_version && (*remote_version > *curr_version)) {
        if (version_info.force_upgrade) {
            wxGetApp().app_config->set_bool("force_upgrade", version_info.force_upgrade);
            wxGetApp().app_config->set("upgrade", "force_upgrade", true);
            wxGetApp().app_config->set("upgrade", "description", version_info.description);
            wxGetApp().app_config->set("upgrade", "version", version_info.version_str);
            wxGetApp().app_config->set("upgrade", "url", version_info.url);
            GUI::wxGetApp().enter_force_upgrade();
        }
        else {
            GUI::wxGetApp().request_new_version(by_user);
        }
    } else {
        wxGetApp().app_config->set("upgrade", "force_upgrade", false);
        if (show_tips)
            this->no_new_version();
    }
}

void GUI_App::check_new_version(bool show_tips, int by_user)
{
    return;
    std::string platform = "windows";

#ifdef __WINDOWS__
    platform = "windows";
#endif
#ifdef __APPLE__
    platform = "macos";
#endif
#ifdef __LINUX__
    platform = "linux";
#endif
    std::string query_params = (boost::format("?name=slicer&version=%1%&guide_version=%2%")
        % VersionInfo::convert_full_version(SLIC3R_VERSION)
        % VersionInfo::convert_full_version("0.0.0.1")
        ).str();

    std::string url = get_http_url(app_config->get_country_code()) + query_params;
    Slic3r::Http http = Slic3r::Http::get(url);

    http.header("accept", "application/json")
        .timeout_connect(TIMEOUT_CONNECT)
        .timeout_max(TIMEOUT_RESPONSE)
        .on_complete([this, show_tips, by_user](std::string body, unsigned) {
        try {
            json j = json::parse(body);
            if (j.contains("message")) {
                if (j["message"].get<std::string>() == "success") {
                    if (j.contains("software")) {
                        if (j["software"].empty() && show_tips) {
                            this->no_new_version();
                        }
                        else {
                            if (j["software"].contains("url")
                                && j["software"].contains("version")
                                && j["software"].contains("description")) {
                                version_info.url = j["software"]["url"].get<std::string>();
                                version_info.version_str = j["software"]["version"].get<std::string>();
                                version_info.description = j["software"]["description"].get<std::string>();
                            }
                            if (j["software"].contains("force_update")) {
                                version_info.force_upgrade = j["software"]["force_update"].get<bool>();
                            }
                            CallAfter([this, show_tips, by_user](){
                                this->check_update(show_tips, by_user);
                            });
                        }
                    }
                }
            }
        }
        catch (...) {
            ;
        }
            })
        .on_error([this](std::string body, std::string error, unsigned int status) {
            handle_http_error(status, body);
            BOOST_LOG_TRIVIAL(error) << "check new version error" << body;
    }).perform();
}

Semver get_version(const std::string& str, const std::regex& regexp) {
    std::smatch match;
    if (std::regex_match(str, match, regexp)) {
        std::string version_cleaned = match[0];
        const boost::optional<Semver> version = Semver::parse(version_cleaned);
        if (version.has_value()) {
            return *version;
        }
    }
    return Semver::invalid();
}

namespace
{

struct UpdaterQuery
{
    std::string iid;
    std::string version;
    std::string os;
    std::string arch;
    std::string os_info;
};

std::string detect_updater_os_info()
{
    wxString description = wxPlatformInfo::Get().GetOperatingSystemDescription();
#if defined(__LINUX__) || defined(__linux__)
    wxLinuxDistributionInfo distro = wxGetLinuxDistributionInfo();
    if (!distro.Id.empty()) {
        wxString normalized = distro.Id;
        if (!distro.Release.empty())
            normalized << " " << distro.Release;
        normalized.Trim(true);
        normalized.Trim(false);
        if (!normalized.empty())
            description = normalized;
    }
#endif
    if (description.empty())
        description = wxGetOsDescription();

#if defined(_WIN32)
    int major = 0, minor = 0, micro = 0;
    wxGetOsVersion(&major, &minor, &micro);
    if (micro >= 22000) {
        description.Replace("Windows 10", "Windows 11");
    }
#endif
    std::string os_info = description.ToStdString();
    boost::replace_all(os_info, "\r", " ");
    boost::replace_all(os_info, "\n", " ");
    boost::algorithm::trim(os_info);
    if (os_info.size() > 120)
        os_info.resize(120);
    boost::algorithm::to_lower(os_info);
    return os_info;
}

std::string detect_updater_version()
{
    return SoftFever_VERSION;
}

std::string detect_updater_iid(AppConfig* config)
{
    if (config == nullptr)
        return {};
    return instance_id::ensure(*config);
}

std::string encode_uri_component(const std::string& value)
{
    static constexpr const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~' ||
            ch == '!' || ch == '*' || ch == '(' || ch == ')' || ch == '\'') {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(hex[(ch >> 4) & 0xF]);
            out.push_back(hex[ch & 0xF]);
        }
    }
    return out;
}

std::string build_updater_query(const UpdaterQuery& query)
{
    std::vector<std::pair<std::string, std::string>> params;

    auto add_param = [&params](const char* key, const std::string& value) {
        if (!value.empty())
            params.emplace_back(key, encode_uri_component(value));
    };

    add_param("iid", query.iid);
    add_param("v", query.version);
    add_param("os", query.os);
    add_param("arch", query.arch);
    add_param("os_info", query.os_info);

    std::sort(params.begin(), params.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });

    if (params.empty())
        return {};

    std::string encoded;
    for (size_t idx = 0; idx < params.size(); ++idx) {
        if (idx > 0)
            encoded.push_back('&');
        encoded += params[idx].first;
        encoded.push_back('=');
        encoded += params[idx].second;
    }
    return encoded;
}

std::string base64url_encode(const unsigned char* data, std::size_t length)
{
    std::string encoded;
    encoded.resize(boost::beast::detail::base64::encoded_size(length));
    encoded.resize(boost::beast::detail::base64::encode(encoded.data(), data, length));
    std::replace(encoded.begin(), encoded.end(), '+', '-');
    std::replace(encoded.begin(), encoded.end(), '/', '_');
    while (!encoded.empty() && encoded.back() == '=')
        encoded.pop_back();
    return encoded;
}

std::optional<std::vector<unsigned char>> load_signature_key()
{
#if ORCA_UPDATER_SIG_KEY_AVAILABLE
    std::string key = ORCA_UPDATER_SIG_KEY_B64;
    boost::algorithm::trim(key);
    if (key.empty())
        return std::nullopt;

    key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char ch) { return std::isspace(ch); }), key.end());
    std::replace(key.begin(), key.end(), '-', '+');
    std::replace(key.begin(), key.end(), '_', '/');
    while (key.size() % 4 != 0)
        key.push_back('=');

    std::string decoded;
    decoded.resize(boost::beast::detail::base64::decoded_size(key.size()));
    auto decode_result = boost::beast::detail::base64::decode(decoded.data(), key.data(), key.size());
    if (!decode_result.second)
        return std::nullopt;
    decoded.resize(decode_result.first);

    return std::vector<unsigned char>(decoded.begin(), decoded.end());
#else
    return std::nullopt;
#endif
}

const std::optional<std::vector<unsigned char>>& get_signature_key()
{
    static std::optional<std::vector<unsigned char>> cached;
    static bool loaded = false;
    if (!loaded) {
        cached = load_signature_key();
        loaded = true;
    }
    return cached;
}

std::string extract_path_from_url(const std::string& url)
{
    if (url.empty())
        return "/latest";

    std::string path;
    const auto scheme_pos = url.find("://");
    if (scheme_pos != std::string::npos) {
        const auto path_pos = url.find('/', scheme_pos + 3);
        if (path_pos != std::string::npos)
            path = url.substr(path_pos);
        else
            path = "/";
    } else {
        path = url;
    }

    const auto fragment_pos = path.find('#');
    if (fragment_pos != std::string::npos)
        path = path.substr(0, fragment_pos);

    const auto query_pos = path.find('?');
    if (query_pos != std::string::npos)
        path = path.substr(0, query_pos);

    if (path.empty())
        path = "/";
    return path;
}

void maybe_attach_updater_signature(Http& http, const std::string& canonical_query, const std::string& request_url)
{
    if (canonical_query.empty())
        return;

    const auto& key = get_signature_key();
    if (!key || key->empty())
        return;

    const auto now   = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    const std::string timestamp = std::to_string(now.time_since_epoch().count());
    const std::string path      = extract_path_from_url(request_url);

    std::string string_to_sign = "GET\n";
    string_to_sign += path;
    string_to_sign += "\n";
    string_to_sign += canonical_query;
    string_to_sign += "\n";
    string_to_sign += timestamp;

    unsigned int digest_length = 0;
    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    if (HMAC(EVP_sha256(), key->data(), static_cast<int>(key->size()),
             reinterpret_cast<const unsigned char*>(string_to_sign.data()),
             string_to_sign.size(), digest, &digest_length) == nullptr || digest_length == 0)
        return;

    const std::string signature = base64url_encode(digest, digest_length);
    http.header("X-Orca-Ts", timestamp);
    http.header("X-Orca-Sig", "v1:" + signature);
}

}

void GUI_App::check_new_version_sf(bool show_tips, int by_user)
{
    AppConfig* app_config = wxGetApp().app_config;
    bool       check_stable_only = app_config->get_bool("check_stable_update_only");
    auto version_check_url = app_config->version_check_url();

    UpdaterQuery query{
        detect_updater_iid(app_config),
        detect_updater_version(),
        platform_os_type(),
        platform_architecture(),
        detect_updater_os_info()
    };

    const std::string query_string = build_updater_query(query);
    if (!query_string.empty()) {
        const bool has_query = version_check_url.find('?') != std::string::npos;
        if (!has_query)
            version_check_url.push_back('?');
        else if (!version_check_url.empty() && version_check_url.back() != '&' && version_check_url.back() != '?')
            version_check_url.push_back('&');
        version_check_url += query_string;
    }

    auto http = Http::get(version_check_url);
    maybe_attach_updater_signature(http, query_string, version_check_url);

    http.header("accept", "application/vnd.github.v3+json")
        .timeout_connect(5)
        .timeout_max(10)
        .on_error([&](std::string body, std::string error, unsigned http_status) {
          (void)body;
          BOOST_LOG_TRIVIAL(error) << format("Error getting: `%1%`: HTTP %2%, %3%", "check_new_version_sf", http_status,
                                             error);
        })
        .on_complete([this, by_user, check_stable_only](std::string body, unsigned http_status) {
          if (http_status != 200)
            return;
          try {
            boost::trim(body);
            if (body.empty()) {
                if (by_user != 0)
                    this->no_new_version();
                return;
            }

            boost::property_tree::ptree root;
            std::stringstream           json_stream(body);
            boost::property_tree::read_json(json_stream, root);

            std::regex matcher("[0-9]+\\.[0-9]+(\\.[0-9]+)*(-[A-Za-z0-9]+)?(\\+[A-Za-z0-9]+)?");
            Semver    current_version = get_version(SoftFever_VERSION, matcher);
            Semver    best_pre(0, 0, 0);
            Semver    best_release(0, 0, 0);
            bool      best_pre_valid = false;
            bool      best_release_valid = false;
            std::string best_pre_url;
            std::string best_release_url;
            std::string best_release_content;
            std::string best_pre_content;

            auto consider_release = [&](const boost::property_tree::ptree& node) {
                auto tag_opt = node.get_optional<std::string>("tag_name");
                if (!tag_opt)
                    return;

                std::string tag = *tag_opt;
                if (!tag.empty() && tag.front() == 'v')
                    tag.erase(0, 1);

                Semver tag_version = get_version(tag, matcher);
                if (!tag_version.valid())
                    return;

                const bool is_prerelease = node.get_optional<bool>("prerelease").get_value_or(false);
                const std::string html_url = node.get_optional<std::string>("html_url").get_value_or(std::string());
                const std::string body_copy = node.get_optional<std::string>("body").get_value_or(std::string());

                if (is_prerelease) {
                    if (!best_pre_valid || best_pre < tag_version) {
                        best_pre        = tag_version;
                        best_pre_url    = html_url;
                        best_pre_content = body_copy;
                        best_pre_valid  = true;
                    }
                } else {
                    if (!best_release_valid || best_release < tag_version) {
                        best_release         = tag_version;
                        best_release_url     = html_url;
                        best_release_content = body_copy;
                        best_release_valid   = true;
                    }
                }
            };

            if (root.get_optional<std::string>("tag_name")) {
                consider_release(root);
            } else {
                for (const auto& child : root)
                    consider_release(child.second);
            }

            if (!best_release_valid && !best_pre_valid) {
                if (by_user != 0)
                    this->no_new_version();
                return;
            }

            if (best_pre_valid && best_release_valid && best_pre < best_release) {
                best_pre        = best_release;
                best_pre_url    = best_release_url;
                best_pre_content = best_release_content;
                best_pre_valid  = true;
            }

            const bool        prefer_release = check_stable_only || !best_pre_valid;
            const Semver&     chosen_version = prefer_release ? best_release : best_pre;
            const bool        chosen_valid   = prefer_release ? best_release_valid : best_pre_valid;

            if (!chosen_valid) {
                if (by_user != 0)
                    this->no_new_version();
                return;
            }

            if (current_version.valid() && chosen_version <= current_version) {
                if (by_user != 0)
                    this->no_new_version();
                return;
            }

            version_info.url           = prefer_release ? best_release_url : best_pre_url;
            version_info.version_str   = prefer_release ? best_release.to_string_sf() : best_pre.to_string_sf();
            version_info.description   = prefer_release ? best_release_content : best_pre_content;
            version_info.force_upgrade = false;

            wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_VERSION_ONLINE);
            evt->SetString((prefer_release ? best_release : best_pre).to_string());
            GUI::wxGetApp().QueueEvent(evt);
          } catch (...) {}
        });

    http.perform();
}

bool GUI_App::process_network_msg(std::string dev_id, std::string msg)
{
    if (dev_id.empty()) {
        if (msg == "wait_info") {
            BOOST_LOG_TRIVIAL(info) << "process_network_msg, wait_info";
            Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
            if (!dev)
                return true;
            MachineObject* obj = dev->get_selected_machine();
            if (obj && m_agent)
                m_agent->install_device_cert(obj->get_dev_id(), obj->is_lan_mode_printer());
            if (!m_show_error_msgdlg) {
                MessageDialog msg_dlg(nullptr, _L("Retrieving printer information, please try again later."), "", wxAPPLY | wxOK);
                m_show_error_msgdlg = true;
                msg_dlg.ShowModal();
                m_show_error_msgdlg = false;
            }
            return true;
        }
        else if (msg == "update_studio") {
            BOOST_LOG_TRIVIAL(info) << "process_network_msg, update_studio";
            if (!m_show_error_msgdlg) {
                MessageDialog msg_dlg(nullptr, _L("Please try updating OrcaSlicer and then try again."), "", wxAPPLY | wxOK);
                m_show_error_msgdlg = true;
                msg_dlg.ShowModal();
                m_show_error_msgdlg = false;
            }
            return true;
        }
        else if (msg == "update_fixed_studio") {
            BOOST_LOG_TRIVIAL(info) << "process_network_msg, update_fixed_studio";
            if (!m_show_error_msgdlg) {
                MessageDialog msg_dlg(nullptr, _L("Please try updating OrcaSlicer and then try again."), "", wxAPPLY | wxOK);
                m_show_error_msgdlg = true;
                msg_dlg.ShowModal();
                m_show_error_msgdlg = false;
            }
            return true;
        }
        else if (msg == "cert_expired") {
            BOOST_LOG_TRIVIAL(info) << "process_network_msg, cert_expired";
            if (!m_show_error_msgdlg) {
                MessageDialog msg_dlg(nullptr, _L("The certificate has expired. Please check the time settings or update OrcaSlicer and try again."), "", wxAPPLY | wxOK);
                m_show_error_msgdlg = true;
                msg_dlg.ShowModal();
                m_show_error_msgdlg = false;
            }
            return true;
        }
        else if (msg == "cert_revoked") {
            BOOST_LOG_TRIVIAL(info) << "process_network_msg, cert_revoked";
            if (!m_show_error_msgdlg) {
                MessageDialog msg_dlg(nullptr, _L("The certificate is no longer valid and the printing functions are unavailable."), "", wxAPPLY | wxOK);
                m_show_error_msgdlg = true;
                msg_dlg.ShowModal();
                m_show_error_msgdlg = false;
            }
            return true;
        }
        else if (msg == "update_firmware_studio") {
            BOOST_LOG_TRIVIAL(info) << "process_network_msg, firmware internal error";
            if (!m_show_error_msgdlg) {
                MessageDialog msg_dlg(nullptr, _L("Internal error. Please try upgrading the firmware and OrcaSlicer version. If the issue persists, contact support."), "", wxAPPLY | wxOK);
                m_show_error_msgdlg = true;
                msg_dlg.ShowModal();
                m_show_error_msgdlg = false;
            }
            return true;
        }
        else if (msg == "unsigned_studio") {
            BOOST_LOG_TRIVIAL(info) << "process_network_msg, unsigned_studio";
            if (!m_show_error_msgdlg && !m_unsigned_plugin_warning_shown) {
                m_unsigned_plugin_warning_shown = true;
                MessageDialog
                    msg_dlg(nullptr,
                            _L("To use OrcaSlicer with Bambu Lab printers, you need to enable LAN mode and Developer mode on your printer.\n\n"
                               "Please go to your printer's settings and:\n"
                               "1. Turn on LAN mode\n"
                               "2. Enable Developer mode\n\n"
                               "Developer mode allows the printer to work exclusively through local network access, "
                               "enabling full functionality with OrcaSlicer."),
                            _L("Network Plug-in Restriction"), wxAPPLY | wxOK);
                m_show_error_msgdlg = true;
                msg_dlg.ShowModal();
                m_show_error_msgdlg = false;
            }
            return true;
        }
    }
    else if (msg == "device_cert_installed") {
        BOOST_LOG_TRIVIAL(info) << "process_network_msg, device_cert_installed";
        if (Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager()) {
            if (MachineObject* obj = dev->get_my_machine(dev_id)) {
                obj->update_device_cert_state(true);
            }
        }
        return true;
    }
    else if (msg == "device_cert_uninstalled") {
        BOOST_LOG_TRIVIAL(info) << "process_network_msg, device_cert_uninstalled";
        if (Slic3r::DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager()) {
            if (MachineObject* obj = dev->get_my_machine(dev_id)){
                obj->update_device_cert_state(false);
            }
        }
        return true;
    }

    return false;
}

void GUI_App::request_new_version(int by_user)
{
    wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_VERSION_ONLINE);
    evt->SetString(GUI::from_u8(version_info.version_str));
    evt->SetInt(by_user);
    GUI::wxGetApp().QueueEvent(evt);
}

void GUI_App::enter_force_upgrade()
{
    wxCommandEvent *evt = new wxCommandEvent(EVT_ENTER_FORCE_UPGRADE);
    GUI::wxGetApp().QueueEvent(evt);
}

void GUI_App::set_skip_version(bool skip)
{
    BOOST_LOG_TRIVIAL(info) << "set_skip_version, skip = " << skip << ", version = " <<version_info.version_str;
    if (skip) {
        app_config->set("skip_version", version_info.version_str);
    }else {
        app_config->set("skip_version", "");
    }
}

void GUI_App::show_check_privacy_dlg(wxCommandEvent& evt)
{
    int online_login = evt.GetInt();
    std::string provider = evt.GetString().ToStdString();
    if (provider.empty()) provider = ORCA_CLOUD_PROVIDER;
    PrivacyUpdateDialog privacy_dlg(this->mainframe, wxID_ANY, _L("Privacy Policy Update"));
    privacy_dlg.Bind(EVT_PRIVACY_UPDATE_CONFIRM, [this, online_login, provider](wxCommandEvent &e) {
        app_config->set("privacy_version", privacy_version_info.version_str);
        app_config->set_bool("privacy_update_checked", true);
        request_user_handle(online_login, provider);
        });
    privacy_dlg.Bind(EVT_PRIVACY_UPDATE_CANCEL, [this, provider](wxCommandEvent &e) {
            app_config->set_bool("privacy_update_checked", false);
            if (m_agent) {
                BOOST_LOG_TRIVIAL(info) << "logout: Privacy update dialog cancelled.";
                m_agent->user_logout(false, provider);
                post_logout_to_webview(provider);
            }
        });

    privacy_dlg.set_text(privacy_version_info.description);
    privacy_dlg.on_show();
}

void GUI_App::on_show_check_privacy_dlg(int online_login, const std::string& provider)
{
    auto evt = new wxCommandEvent(EVT_CHECK_PRIVACY_SHOW);
    evt->SetInt(online_login);
    evt->SetString(wxString::FromUTF8(provider));
    wxQueueEvent(this, evt);
}

bool GUI_App::check_privacy_update()
{
    if (privacy_version_info.version_str.empty() || privacy_version_info.description.empty()
        || privacy_version_info.url.empty()) {
        return false;
    }

    std::string local_privacy_ver = app_config->get("privacy_version");
    auto curr_version = Semver::parse(local_privacy_ver);
    auto remote_version = Semver::parse(privacy_version_info.version_str);
    if (curr_version && remote_version) {
        if (*remote_version > *curr_version || app_config->get("privacy_update_checked") != "true") {
            return true;
        }
    }
    return false;
}

void GUI_App::on_check_privacy_update(wxCommandEvent& evt)
{
    int online_login = evt.GetInt();
    std::string provider = evt.GetString().ToStdString();
    if (provider.empty()) provider = ORCA_CLOUD_PROVIDER;
    bool result = check_privacy_update();
    if (result)
        on_show_check_privacy_dlg(online_login, provider);
    else
        request_user_handle(online_login, provider);
}

void GUI_App::check_privacy_version(int online_login, const std::string& provider)
{
    if (app_config->get_stealth_mode()) {
        request_user_handle(online_login);
        return;
    }

    std::string query_params = "?policy/privacy=00.00.00.00";
    std::string url = get_http_url(app_config->get_country_code()) + query_params;
    Slic3r::Http http = Slic3r::Http::get(url);

    http.header("accept", "application/json")
        .timeout_connect(TIMEOUT_CONNECT)
        .timeout_max(TIMEOUT_RESPONSE)
        .on_complete([this, online_login, provider](std::string body, unsigned) {
            try {
                json j = json::parse(body);
                if (j.contains("message")) {
                    if (j["message"].get<std::string>() == "success") {
                        if (j.contains("resources")) {
                            for (auto it = j["resources"].begin(); it != j["resources"].end(); it++) {
                                if (it->contains("type")) {
                                    if ((*it)["type"] == std::string("policy/privacy")
                                        && it->contains("version")
                                        && it->contains("description")
                                        && it->contains("url")
                                        && it->contains("force_update")) {
                                        privacy_version_info.version_str = (*it)["version"].get<std::string>();
                                        privacy_version_info.description = (*it)["description"].get<std::string>();
                                        privacy_version_info.url = (*it)["url"].get<std::string>();
                                        privacy_version_info.force_upgrade = (*it)["force_update"].get<bool>();
                                        break;
                                    }
                                }
                            }
                            CallAfter([this, online_login, provider]() {
                                auto evt = new wxCommandEvent(EVT_CHECK_PRIVACY_VER);
                                evt->SetInt(online_login);
                                evt->SetString(wxString::FromUTF8(provider));
                                wxQueueEvent(this, evt);
                            });
                        }
                    }
                }
            }
            catch (...) {
                request_user_handle(online_login, provider);
            }
        })
        .on_error([this, online_login, provider](std::string body, std::string error, unsigned int status) {
            request_user_handle(online_login, provider);
            BOOST_LOG_TRIVIAL(error) << "check privacy version error" << body;
    }).perform();
}

void GUI_App::no_new_version()
{
    wxCommandEvent* evt = new wxCommandEvent(EVT_SHOW_NO_NEW_VERSION);
    GUI::wxGetApp().QueueEvent(evt);
}

std::string GUI_App::version_display = "";
std::string GUI_App::format_display_version()
{
    if (!version_display.empty()) return version_display;

    version_display = SoftFever_VERSION;
    return version_display;
}

std::string GUI_App::format_IP(const std::string& ip)
{
    std::string format_ip = ip;
    size_t pos_st = 0;
    size_t pos_en = 0;

    for (int i = 0; i < 2; i++) {
        pos_en = format_ip.find('.', pos_st + 1);
        if (pos_en == std::string::npos) {
            return ip;
        }
        format_ip.replace(pos_st, pos_en - pos_st, "***");
        pos_st = pos_en + 1;
    }

    return format_ip;
}

void GUI_App::show_dialog(wxString msg)
{
    if (m_info_dialog_content.empty()) {
        wxCommandEvent* evt = new wxCommandEvent(EVT_SHOW_DIALOG);
        evt->SetString(msg);
        GUI::wxGetApp().QueueEvent(evt);
        m_info_dialog_content = msg;
    }
}

void  GUI_App::push_notification(const MachineObject* obj, wxString msg, wxString title, UserNotificationStyle style)
{
    if (this->is_enable_multi_machine())
    {
        if (m_device_manager && (obj != m_device_manager->get_selected_machine()))
        {
            return;
        }
    }

    if (style == UserNotificationStyle::UNS_NORMAL)
    {
        if (m_info_dialog_content.empty())
        {
            wxCommandEvent* evt = new wxCommandEvent(EVT_SHOW_DIALOG);
            evt->SetString(msg);
            GUI::wxGetApp().QueueEvent(evt);
            m_info_dialog_content = msg;
        }
    }
    else if (style == UserNotificationStyle::UNS_WARNING_CONFIRM)
    {
        GUI::wxGetApp().CallAfter([msg, title]
            {
                GUI::MessageDialog msg_dlg(nullptr, msg, title, wxICON_WARNING | wxOK);
                msg_dlg.ShowModal();
            });
    }
}

void GUI_App::reload_settings()
{
    if (preset_bundle && m_agent) {
        std::map<std::string, std::map<std::string, std::string>> user_presets;
        int result = m_agent->get_user_presets(&user_presets);
        if (result != 0) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": get_user_presets failed with code " << result << ", skipping sync";
            return;
        }
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << __LINE__ << " cloud user preset number is: " << user_presets.size();
        auto refresh_synced_ui = [this, user_presets = std::move(user_presets)]() mutable {
            if (is_closing() || !preset_bundle || !app_config || !mainframe)
                return;

            struct PresetSnapshot { std::string name; DynamicPrintConfig config; bool dirty; };
            auto snapshot_collection = [](const PresetCollection& col) -> PresetSnapshot {
                auto& sel = col.get_selected_preset();
                auto& ed  = col.get_edited_preset();
                return {sel.name, ed.config, sel.is_dirty};
            };
            PresetSnapshot print_snap    = snapshot_collection(preset_bundle->prints);
            PresetSnapshot filament_snap = snapshot_collection(preset_bundle->filaments);
            PresetSnapshot printer_snap  = snapshot_collection(preset_bundle->printers);

            for (auto data : user_presets) {
                if (!check_preset_parent_available(data))
                    add_pending_vendor_preset(data);
            }
            load_pending_vendors();

            preset_bundle->load_user_presets(*app_config, user_presets, ForwardCompatibilitySubstitutionRule::Enable);
            preset_bundle->save_user_presets(*app_config, get_delete_cache_presets());

            auto restore_snapshot = [](PresetCollection& col, const PresetSnapshot& snap, const char* label) {
                auto& ed = col.get_edited_preset();
                bool changed = !ed.config.equals(snap.config);
                BOOST_LOG_TRIVIAL(info) << "reload_settings restore " << label
                    << ": snap_name=" << snap.name << " snap_dirty=" << snap.dirty
                    << " current_name=" << ed.name << " changed=" << changed;
                if (!snap.dirty) return;
                Preset* p = col.find_preset(snap.name, false, true);
                if (p && p->name == snap.name) {
                    BOOST_LOG_TRIVIAL(info) << "reload_settings RESTORING " << label
                        << ": name=" << snap.name;
                    if (col.get_selected_preset().name != snap.name)
                        col.select_preset_by_name(snap.name, true);
                    ed = col.get_edited_preset();
                    ed.config = snap.config;
                    col.get_selected_preset().is_dirty = snap.dirty;
                    ed.is_dirty = snap.dirty;
                } else {
                    BOOST_LOG_TRIVIAL(info) << "reload_settings restore " << label
                        << ": preset not found name=" << snap.name;
                }
            };
            restore_snapshot(preset_bundle->prints, print_snap, "print");
            restore_snapshot(preset_bundle->filaments, filament_snap, "filament");
            restore_snapshot(preset_bundle->printers, printer_snap, "printer");

            mainframe->update_side_preset_ui();
            for (auto tab : tabs_list) {
                tab->reload_config();
                tab->update_changed_ui();
            }
            if (plater_) {
                plater_->sidebar().update_all_preset_comboboxes();
                plater_->normalize_bed_types(false);
            }
        };
        if (is_main_thread_active())
            refresh_synced_ui();
        else
            CallAfter(refresh_synced_ui);
    }
}

void GUI_App::remove_user_presets()
{
    if (preset_bundle && m_agent) {
        preset_bundle->remove_users_preset(*app_config);

        mainframe->update_side_preset_ui();
    }
}

bool GUI_App::maybe_migrate_user_presets_on_login()
{
    namespace fs = boost::filesystem;

    BOOST_LOG_TRIVIAL(info) << "Migrate user presets to the OrcaCloud user folder if needed.";

    if (!m_agent || !m_agent->is_user_login())
        return false;

    std::string new_user_id = m_agent->get_user_id();
    if (new_user_id.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "Failed to get user ID, skipping migration.";
        return false;
    }

    fs::path user_base = fs::path(data_dir()) / PRESET_USER_DIR;
    fs::path target_dir = user_base / new_user_id;

    {
        std::map<std::string, std::map<std::string, std::string>> cloud_presets;
        int ret = m_agent->get_user_presets(&cloud_presets);
        if (ret == 0 && !cloud_presets.empty()) {
            BOOST_LOG_TRIVIAL(info) << "OrcaCloud already has " << cloud_presets.size()
                                    << " presets, skipping migration for user: " << new_user_id;
            return false;
        }
        if (ret != 0) {
            BOOST_LOG_TRIVIAL(warning) << "Failed to query OrcaCloud presets (error " << ret
                                       << "), skipping migration to avoid overwriting cloud data.";
            if (std::chrono::steady_clock::now() - m_last_401_error_time < std::chrono::seconds(30)
                && !m_migration_retry_pending.exchange(true)) {
                BOOST_LOG_TRIVIAL(info) << "Scheduling migration retry after token propagation window.";
                boost::thread([this]() {
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    CallAfter([this]() {
                        m_migration_retry_pending = false;
                        if (is_closing() || !m_agent || !m_agent->is_user_login()) return;
                        BOOST_LOG_TRIVIAL(info) << "Retrying preset migration after token propagation window.";
                        if (maybe_migrate_user_presets_on_login()) {
                            const std::string user_id = m_agent->get_user_id();
                            preset_bundle->load_user_presets(user_id, ForwardCompatibilitySubstitutionRule::Enable);
                            if (mainframe) mainframe->update_side_preset_ui();
                        }
                    });
                }).detach();
            }
            return false;
        }
        BOOST_LOG_TRIVIAL(info) << "OrcaCloud has no presets for user " << new_user_id << ", proceeding with migration check.";
    }

    auto has_json_presets = [](const fs::path& dir) -> bool {
        try {
            if (!fs::exists(dir) || !fs::is_directory(dir))
                return false;
            boost::system::error_code ec;
            for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) {
                    BOOST_LOG_TRIVIAL(warning) << "Error scanning directory " << dir << ": " << ec.message();
                    continue;
                }
                if (fs::is_regular_file(*it) && it->path().extension() == ".json")
                    return true;
            }
        } catch (const fs::filesystem_error& e) {
            BOOST_LOG_TRIVIAL(warning) << "Failed to scan directory for presets: " << e.what();
        }
        return false;
    };

    fs::path source_dir;
    bool source_is_default = false;
    bool source_is_bbl = false;
    fs::path default_dir = user_base / DEFAULT_USER_FOLDER_NAME;

    if (m_agent->is_user_login(BBL_CLOUD_PROVIDER)) {
        std::string bbl_user_id = m_agent->get_user_id(BBL_CLOUD_PROVIDER);
        if (!bbl_user_id.empty() && bbl_user_id != new_user_id) {
            fs::path bbl_dir = user_base / bbl_user_id;
            if (has_json_presets(bbl_dir)) {
                source_dir = bbl_dir;
                source_is_bbl = true;
                BOOST_LOG_TRIVIAL(info) << "Migration source: Bambu Cloud user folder: " << source_dir;
            }
        }
    }

    if (source_dir.empty() && has_json_presets(default_dir)) {
        source_dir = default_dir;
        source_is_default = true;
        BOOST_LOG_TRIVIAL(info) << "Migration source: default user folder: " << source_dir;
    }

    if (source_dir.empty() && fs::exists(user_base) && fs::is_directory(user_base)) {
        for (auto& entry : fs::directory_iterator(user_base)) {
            if (!fs::is_directory(entry))
                continue;
            std::string folder_name = entry.path().filename().string();
            if (folder_name == new_user_id || folder_name == DEFAULT_USER_FOLDER_NAME)
                continue;
            if (has_json_presets(entry.path())) {
                source_dir = entry.path();
                BOOST_LOG_TRIVIAL(info) << "Migration source: user folder: " << source_dir;
                break;
            }
        }
    }

    if (source_dir.empty()) {
        BOOST_LOG_TRIVIAL(info) << "No existing user presets found to migrate.";
        return false;
    }

    wxString source_description;
    if (source_is_bbl) {
        source_description = wxString::Format(
            _L("your Orca Cloud profile (user ID: \"%s\")"),
            from_u8(source_dir.filename().string()));
    } else if (source_is_default) {
        source_description = _L("your default profile");
    } else {
        source_description = wxString::Format(
            _L("a user profile (folder: \"%s\")"),
            from_u8(source_dir.filename().string()));
    }

    wxString msg = wxString::Format(
        _L("Existing user presets were found in %s.\n"
           "Do you want to migrate them to your OrcaCloud profile?\n"
           "This will copy your presets so they are available under your new account."),
        source_description);

    MessageDialog dlg(mainframe, msg, _L("Migrate User Presets"),
                      wxCENTER | wxYES_DEFAULT | wxYES_NO | wxICON_INFORMATION);
    if (dlg.ShowModal() != wxID_YES) {
        BOOST_LOG_TRIVIAL(info) << "User declined preset migration.";
        return false;
    }

    try {
        wxBusyCursor busy;
        BOOST_LOG_TRIVIAL(info) << "Migrating user presets from " << source_dir << " to " << target_dir;

        auto info_filter = [](const std::string& filename) -> bool {
            return filename.size() >= 5 &&
                   filename.compare(filename.size() - 5, 5, ".info") == 0;
        };

        copy_directory_recursively(source_dir, target_dir, info_filter, true);
        BOOST_LOG_TRIVIAL(info) << "User preset migration completed successfully.";
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "Failed to migrate user presets: " << ex.what();
        wxString err_msg = wxString::Format(
            _L("Failed to migrate user presets:\n%s"),
            from_u8(ex.what()));
        show_error(nullptr, err_msg);
        return false;
    }

    return true;
}

bool GUI_App::check_preset_parent_available(const std::pair<std::string, std::map<std::string, std::string>>& preset_data)
{
    auto it = preset_data.second.find(BBL_JSON_KEY_INHERITS);
    if (it == preset_data.second.end() || it->second.empty())
        return true;
    const std::string& inherits_name = it->second;
    if (inherits_name.find("fdm_") != std::string::npos || inherits_name.find("@System") != std::string::npos || inherits_name.find("@base") != std::string::npos)
        return true;

    if (preset_data.second.at(BBL_JSON_KEY_TYPE) == PRESET_IOT_PRINT_TYPE)
        return preset_bundle->prints.find_preset2(inherits_name) != nullptr;
    else if (preset_data.second.at(BBL_JSON_KEY_TYPE) == PRESET_IOT_PRINTER_TYPE)
        return preset_bundle->printers.find_preset2(inherits_name) != nullptr;
    else if (preset_data.second.at(BBL_JSON_KEY_TYPE) == PRESET_IOT_FILAMENT_TYPE)
        return preset_bundle->filaments.find_preset2(inherits_name) != nullptr;
    return true;
}

void GUI_App::add_pending_vendor_preset(const std::pair<std::string, std::map<std::string, std::string>>& preset_data)
{
    Preset::Type type = Preset::Type::TYPE_INVALID;
    if (preset_data.second.at(BBL_JSON_KEY_TYPE) == PRESET_IOT_PRINT_TYPE)
        type = Preset::Type::TYPE_PRINT;
    else if (preset_data.second.at(BBL_JSON_KEY_TYPE) == PRESET_IOT_PRINTER_TYPE)
        type = Preset::Type::TYPE_PRINTER;
    else if (preset_data.second.at(BBL_JSON_KEY_TYPE) == PRESET_IOT_FILAMENT_TYPE)
        type = Preset::Type::TYPE_FILAMENT;
    std::string inherits_name = preset_data.second.at(BBL_JSON_KEY_INHERITS);

    std::string vendor_name = PresetBundle::find_preset_vendor(inherits_name, type);
    if (vendor_name.empty()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": no vendor carries " << inherits_name << ", skipping";
        return;
    }
    if (need_add_vendors.find(vendor_name) == need_add_vendors.end())
        need_add_vendors[vendor_name] = std::map<std::string, std::set<std::string>>();

    if (type == Preset::Type::TYPE_PRINTER) {
        std::string model_name = inherits_name;
        std::regex float_regex(R"((\b\d+\.\d+))");
        std::smatch match;
        if (std::regex_search(model_name, match, float_regex) && match.size() > 1) {
            std::string nozzle_diameter = match[1].str();
            model_name.erase(model_name.find(nozzle_diameter));
            model_name.erase(model_name.rfind(' '));
            if(need_add_vendors[vendor_name].find(model_name) == need_add_vendors[vendor_name].end())
                need_add_vendors[vendor_name][model_name] = std::set<std::string>();
            
            need_add_vendors[vendor_name][model_name].insert(nozzle_diameter);
        }
    }
    else if (type == Preset::Type::TYPE_FILAMENT) {
        need_add_filaments[inherits_name] = "true";
    }
}

void GUI_App::load_pending_vendors()
{
    if (need_add_vendors.size() == 0 && need_add_filaments.size() == 0)
        return;

    preset_bundle->apply_vendor_config(need_add_vendors, need_add_filaments, app_config, false);
    if (is_main_thread_active())
        app_config->save();
    else
        CallAfter([this] { app_config->save(); });
    need_add_vendors.clear();
    need_add_filaments.clear();
}

void GUI_App::sync_preset(Preset* preset, bool force)
{
    int result = -1;
    unsigned int http_code = 200;
    std::string updated_info;
    long long update_time = 0;
    if (!m_agent) return;
    if (!preset->is_user()) return;

    auto setting_id = preset->setting_id;
    std::map<std::string, std::string> values_map;

    bool needs_init = setting_id.empty() && preset->sync_info.empty();

    if (needs_init || preset->sync_info.compare("create") == 0) {
        if (m_create_preset_blocked[preset->type])
            return;
        int ret = preset_bundle->get_differed_values_to_update(*preset, values_map);
        if (!ret) {
            std::string new_setting_id = m_agent->request_setting_id(preset->name, &values_map, &http_code);
            if (!new_setting_id.empty()) {
                setting_id = new_setting_id;
                result = 0;
                auto update_time_str = values_map[ORCA_JSON_KEY_UPDATE_TIME];
                if (!update_time_str.empty())
                    update_time = std::atoll(update_time_str.c_str());
            }
            else {
                BOOST_LOG_TRIVIAL(trace) << "[sync_preset]init: request_setting_id failed, http code "<<http_code;
                if (http_code >= 400) {
                    result = 0;
                    updated_info = "hold";
                } else
                    result = -1;
            }
        }
        else {
            BOOST_LOG_TRIVIAL(trace) << "[sync_preset]init: can not generate differed key-values";
            result = 0;
            updated_info = "hold";
        }
    }
    else if (preset->sync_info.compare("create") == 0) {
        if (m_create_preset_blocked[preset->type])
            return;
        int ret = preset_bundle->get_differed_values_to_update(*preset, values_map);
        if (!ret) {
            std::string new_setting_id = m_agent->request_setting_id(preset->name, &values_map, &http_code);
            if (!new_setting_id.empty()) {
                setting_id = new_setting_id;
                result = 0;
                auto update_time_str = values_map[ORCA_JSON_KEY_UPDATE_TIME];
                if (!update_time_str.empty())
                    update_time = std::atoll(update_time_str.c_str());
            } else {
                BOOST_LOG_TRIVIAL(trace) << "[sync_preset]create: request_setting_id failed, http code "<<http_code;
                if (http_code >= 400) {
                    result = 0;
                    updated_info = "hold";
                }
            }
        } else {
            BOOST_LOG_TRIVIAL(trace) << "[sync_preset]create: can not generate differed preset";
        }
    } else if (preset->sync_info.compare("update") == 0) {
        if (!setting_id.empty()) {
            int ret = preset_bundle->get_differed_values_to_update(*preset, values_map);
            if (!ret) {
                if (auto iter = values_map.find(BBL_JSON_KEY_BASE_ID); iter != values_map.end() && iter->second == setting_id) {
                    setting_id.clear();
                    result = 0;
                }
                else {
                    result = m_agent->put_setting(setting_id, preset->name, &values_map, &http_code, ORCA_CLOUD_PROVIDER, force);
                    if (http_code >= 400) {
                        result       = 0;
                        updated_info = "hold";
                        BOOST_LOG_TRIVIAL(error) << "[sync_preset] put setting_id = " << setting_id << " failed, http_code = " << http_code;
                    } else {
                            auto update_time_str = values_map[ORCA_JSON_KEY_UPDATE_TIME];
                            if (!update_time_str.empty())
                                update_time = std::atoll(update_time_str.c_str());
                    }
                }

            }
            else {
                BOOST_LOG_TRIVIAL(trace) << "[sync_preset]update: can not generate differed key-values, we need to skip this preset "<< preset->name;
                result = 0;
            }
        }
        else {
            result = 0;
        }
    }

    if (http_code >= 400 && values_map["code"] == "14") {
        m_create_preset_blocked[preset->type] = true;
        CallAfter([this] {
            plater()->get_notification_manager()->push_notification(NotificationType::BBLUserPresetExceedLimit);
            static bool dialog_notified = false;
            if (dialog_notified)
                return;
            dialog_notified = true;
            if (mainframe == nullptr)
                return;
            auto msg = _L("The number of user presets cached in the cloud has exceeded the upper limit, newly created user presets can only be used locally.");
            MessageDialog(mainframe, msg, _L("Sync user presets"), wxICON_WARNING | wxOK).ShowModal();
        });
        return;
    }

    if (http_code == 413) {
        updated_info = "will_not_sync";
        result = 0;
        CallAfter([this, name = preset->name] {
            static bool size_limit_dialog_notified = false;
            if (size_limit_dialog_notified)
                return;
            size_limit_dialog_notified = true;
            if (mainframe == nullptr)
                return;
            auto msg = format_wxstr(_L("The preset \"%s\" is too large to sync to the cloud (exceeds 1MB). Please reduce the preset size by removing custom configurations or use it locally only."), name);
            MessageDialog(mainframe, msg, _L("Sync user presets"), wxICON_WARNING | wxOK).ShowModal();
        });
    }

    if (result == 0) {
        if (!this->preset_bundle) return;

        BOOST_LOG_TRIVIAL(trace) << "sync_preset: sync operation: " << preset->sync_info << " success! preset = " << preset->name;
        if (preset->type == Preset::Type::TYPE_FILAMENT) {
            preset_bundle->filaments.set_sync_info_and_save(preset->name, setting_id, updated_info, update_time, m_agent->get_user_id());
        } else if (preset->type == Preset::Type::TYPE_PRINT) {
            preset_bundle->prints.set_sync_info_and_save(preset->name, setting_id, updated_info, update_time, m_agent->get_user_id());
        } else if (preset->type == Preset::Type::TYPE_PRINTER) {
            preset_bundle->printers.set_sync_info_and_save(preset->name, setting_id, updated_info, update_time, m_agent->get_user_id());
        }
    }
}

void GUI_App::update_single_bundle(wxCommandEvent& evt)
{
    if (!m_agent || !m_agent->is_user_login()) return;
    auto orca_agent = std::dynamic_pointer_cast<OrcaCloudServiceAgent>(m_agent->get_cloud_agent());
    if (!orca_agent) return;

    const std::string bundle_id = evt.GetString().ToStdString();

    std::map<std::string, std::map<std::string, std::string>> bundle_presets;
    BundleMetadata remote_metadata;
    int result = orca_agent->get_shared_bundle(bundle_id, &bundle_presets, &remote_metadata);

    if (result != 0) {
        BOOST_LOG_TRIVIAL(warning) << "sync_bundle: failed to fetch bundle " << bundle_id << ", result=" << result;
        return;
    }

    CallAfter([this, bundle_id, bundle_presets, remote_metadata]() {
        if (!is_closing() && preset_bundle && app_config) {
            for (auto data : bundle_presets) {
                if (!check_preset_parent_available(data)) {
                    add_pending_vendor_preset(data);
                }
            }
            load_pending_vendors();

            preset_bundle->bundles.ReadLock();
            std::string initial_version = preset_bundle->bundles.m_bundles[bundle_id].version;
            preset_bundle->bundles.ReadUnlock();

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << __LINE__ << "ORCA : CallAfter from update_single_bundle function actually updating subscribed presets";
            
            preset_bundle->bundles.WriteLock();
            
            preset_bundle->update_subscribed_presets(*app_config, bundle_presets, remote_metadata, ForwardCompatibilitySubstitutionRule::Enable);

            preset_bundle->bundles.WriteUnlock();
            
            std::string text = format(_L("%s updated from %s to %s"), remote_metadata.name, initial_version, remote_metadata.version);
            wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::CustomNotification,NotificationManager::NotificationLevel::RegularNotificationLevel,text);
            
            auto* evt = new wxCommandEvent(EVT_UPDATE_BUNDLE_COMPLETE);                                                                                                                                                       
            if (m_preset_bundle_dlg)                                                                                                                                                                                                                                                          
                wxQueueEvent(m_preset_bundle_dlg, evt);                                                                                                                                                                                                                                       
            else                                                                                                                                                                                                                                                                                 
                delete evt;                                                                                                                                                                           
        
            if (mainframe)
                mainframe->update_side_preset_ui();
            BOOST_LOG_TRIVIAL(info) << "sync_bundle: successfully updated bundle " << bundle_id;
            
        }
    });
}

int GUI_App::sync_bundle(std::string bundle_id, std::string version)
{
    if (!m_agent || !m_agent->is_user_login()) return 0;
    auto orca_agent = std::dynamic_pointer_cast<OrcaCloudServiceAgent>(m_agent->get_cloud_agent());
    if (!orca_agent) return 0;

    BOOST_LOG_TRIVIAL(info) << "sync_bundle: checking bundle " << bundle_id << " for updates";

    bool is_new  =  false;
    bool is_update = false;

    preset_bundle->bundles.ReadLock();
    auto bundle_it = preset_bundle->bundles.m_bundles.find(bundle_id);
    if (bundle_it != preset_bundle->bundles.m_bundles.end()) {

        auto local_version = Semver::parse(bundle_it->second.version);
        auto remote_version = Semver::parse(version);
        
        BOOST_LOG_TRIVIAL(info) << "sync_bundle: comparing local version: " << local_version << " to remote version: " << remote_version;

        if (!local_version || !remote_version) {
            BOOST_LOG_TRIVIAL(warning) << "sync_bundle: failed to parse versions for bundle " << bundle_id
                                    << " (local: " << local_version << ", remote: " << remote_version << ")";
            preset_bundle->bundles.ReadUnlock();
            return -1;
        }
        if (remote_version <= local_version) {
            BOOST_LOG_TRIVIAL(info) << "sync_bundle: bundle " << bundle_id << " is up-to-date (version " << local_version << ")";
            preset_bundle->bundles.ReadUnlock();
            return -1;
        }
        BOOST_LOG_TRIVIAL(info) << "sync_bundle: updating bundle " << bundle_id
                                << " from version " << local_version
                                << " to version " << remote_version;
        is_update = true;

    }
    else {
        BOOST_LOG_TRIVIAL(info) << "sync_bundle: pulling newly subscribed bundle " << bundle_id << " at version " << version;
        is_new = true;
    }

    preset_bundle->bundles.ReadUnlock();
    std::string ver;
    if (is_update) {
        preset_bundle->bundles.WriteLock();
        preset_bundle->bundles.m_bundles[bundle_id].update_available = true;
        preset_bundle->bundles.m_bundles[bundle_id].is_subscribed = true;
        ver = preset_bundle->bundles.m_bundles[bundle_id].version;
        preset_bundle->bundles.WriteUnlock();
    }

    const bool auto_update = app_config->get_bool("preset_bundle_auto_update");

    if (is_update && !auto_update) {
        return 1;
    }

    if (auto_update || is_new) {
        std::map<std::string, std::map<std::string, std::string>> bundle_presets;
        BundleMetadata remote_metadata;
        int result = orca_agent->get_shared_bundle(bundle_id, &bundle_presets, &remote_metadata);

        if (result != 0) {
            BOOST_LOG_TRIVIAL(warning) << "sync_bundle: failed to fetch bundle " << bundle_id << ", result=" << result;
            return -1;
        }

        CallAfter(
            [this, bundle_id, bundle_presets, remote_metadata, is_new, is_update, ver]() {
                if (!is_closing() && preset_bundle && app_config) {
                    for (auto data : bundle_presets) {
                        if (!check_preset_parent_available(data)) {
                            add_pending_vendor_preset(data);
                        }
                    }
                    load_pending_vendors();

                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << __LINE__ << "ORCA : CallAfter from sync_bundle function actually updating subscribed presets";
                    
                    preset_bundle->bundles.WriteLock();
                    
                    preset_bundle->update_subscribed_presets(*app_config, bundle_presets, remote_metadata, ForwardCompatibilitySubstitutionRule::Enable);

                    preset_bundle->bundles.WriteUnlock();

                    if(is_new)
                    {
                        std::string text = format(_L("%s has been downloaded."), remote_metadata.name);
                        wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::CustomNotification,NotificationManager::NotificationLevel::RegularNotificationLevel,text);
                    }
                    else if(is_update)
                    {
                        std::string text = format(_L("%s updated from %s to %s"), remote_metadata.name, ver, remote_metadata.version);
                        wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::CustomNotification,NotificationManager::NotificationLevel::RegularNotificationLevel,text);
                    }

                    auto* evt = new wxCommandEvent(EVT_UPDATE_BUNDLE_COMPLETE);                                                                                                                                                       
                    if (m_preset_bundle_dlg)                                                                                                                                                                                                                                                          
                        wxQueueEvent(m_preset_bundle_dlg, evt);                                                                                                                                                                                                                                       
                    else                                                                                                                                                                                                                                                                                 
                        delete evt;
                
                    if (mainframe)
                        mainframe->update_side_preset_ui();
                    BOOST_LOG_TRIVIAL(info) << "sync_bundle: successfully updated bundle " << bundle_id;
                }
            });
    }

    return 0;
}


void GUI_App::check_bundle_updates()
{
    if (!m_agent || !m_agent->is_user_login()) return;
    auto orca_agent = std::dynamic_pointer_cast<OrcaCloudServiceAgent>(m_agent->get_cloud_agent());
    if (!orca_agent) return;

    BOOST_LOG_TRIVIAL(info) << "check_bundle_updates: checking for bundle updates";

    std::vector<std::pair<std::string, std::string>> subscribed_bundles;
    std::vector<std::string> notfound;
    std::vector<std::string> unauthorized;
    int result = orca_agent->get_subscribed_bundles(&subscribed_bundles,notfound,unauthorized);

    if (result != 0) {
        BOOST_LOG_TRIVIAL(warning) << "check_bundle_updates: failed to fetch subscribed bundles, result=" << result;
        return;
    }

    if(!notfound.empty())
    {
        for(auto& n : notfound)
        {
            std::string text = format(_L("Bundle %s is no longer available."), n);
            wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::CustomNotification,NotificationManager::NotificationLevel::RegularNotificationLevel,text);
        }
    }
    if(!unauthorized.empty())
    {
        for(auto& i : unauthorized)
        {
            std::string text = format(_L("Bundle %s access is unauthorized."), i);
            wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::CustomNotification,NotificationManager::NotificationLevel::RegularNotificationLevel,text);
        }
    }

    std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> subscribed_bundle_presets;
    std::map<std::string, BundleMetadata> subscribed_bundle_metadata;

    for (const auto& bundle : subscribed_bundles) {
        std::map<std::string, std::map<std::string, std::string>> presets;
        BundleMetadata metadata;
        int preset_result = orca_agent->get_shared_bundle(bundle.first, &presets, &metadata);

        if (preset_result == 0) {
            subscribed_bundle_presets[bundle.first] = presets;
            subscribed_bundle_metadata[bundle.first] = metadata;
        } else {
            BOOST_LOG_TRIVIAL(warning) << "check_bundle_updates: Failed to get presets for bundle_id=" << bundle.first << ", result=" << preset_result;
        }
    }

    if (!preset_bundle) return;

    int bundles_checked = 0;
    int updates_available = 0;

    for (auto& [bundle_id, local_metadata] : preset_bundle->bundles.m_bundles) {
        if (!local_metadata.is_subscribed) {
            continue;
        }

        auto remote_it = subscribed_bundle_metadata.find(bundle_id);
        if (remote_it == subscribed_bundle_metadata.end()) {
            BOOST_LOG_TRIVIAL(info) << "check_bundle_updates: bundle " << bundle_id << " not found in remote subscriptions";
            continue;
        }

        const auto& remote_metadata = remote_it->second;

        auto local_version = Semver::parse(local_metadata.version);
        auto remote_version = Semver::parse(remote_metadata.version);

        if (!local_version || !remote_version) {
            BOOST_LOG_TRIVIAL(warning) << "check_bundle_updates: failed to parse versions for bundle " << bundle_id
                                       << " (local: " << local_metadata.version << ", remote: " << remote_metadata.version << ")";
            continue;
        }

        bundles_checked++;

        if (remote_version > local_version) {
            local_metadata.update_available = true;
            updates_available++;
            BOOST_LOG_TRIVIAL(info) << "check_bundle_updates: bundle " << bundle_id << " (" << local_metadata.name
                                    << ") has update available: local=" << local_metadata.version
                                    << ", remote=" << remote_metadata.version;
        } else {
            local_metadata.update_available = false;
        }
    }

    BOOST_LOG_TRIVIAL(info) << "check_bundle_updates: checked " << bundles_checked
                            << " bundles, found " << updates_available << " updates available";
}

bool GUI_App::unsubscribe_bundle(const std::string& id)
{
    auto orca_agent = std::dynamic_pointer_cast<OrcaCloudServiceAgent>(m_agent->get_cloud_agent());
    return orca_agent->unsubscribe_bundle(id);
}

void GUI_App::start_sync_user_preset(bool with_progress_dlg)
{
    if (app_config->get_stealth_mode())
        return;

    if (!m_agent || !m_agent->is_user_login()) return;
    if(!m_agent->get_cloud_agent())
        return;

    if (m_user_sync_token) return;

    ProgressFn progressFn;
    WasCancelledFn cancelFn;
    std::function<void(bool)> finishFn;

    BOOST_LOG_TRIVIAL(info) << "start_sync_service...";
    m_user_sync_token.reset(new int(0));
    if (with_progress_dlg) {
        m_sync_user_preset_dlg_active = true;
        auto dlg = new ProgressDialog(_L("Loading"), "", 100, this->mainframe, wxPD_AUTO_HIDE | wxPD_APP_MODAL | wxPD_CAN_ABORT);
        dlg->Update(0, _L("Loading user preset"));
        progressFn = [this, dlg](int percent) {
            CallAfter([=]{
                dlg->Update(percent, _L("Loading user preset"));
            });
        };
        cancelFn = [this, dlg, t = std::weak_ptr<int>(m_user_sync_token)]() {
            return is_closing() || dlg->WasCanceled() || t.expired();
        };
        finishFn = [this, dlg](bool) {
            CallAfter([=]{ dlg->Destroy(); m_sync_user_preset_dlg_active = false; });
        };
    }
    else {
        finishFn = [](bool) {};
        cancelFn = [this, t = std::weak_ptr<int>(m_user_sync_token)]() {
            return is_closing() || t.expired();
        };
    }

    Bind(EVT_UPDATE_PRESET_BUNDLE,&GUI_App::update_single_bundle,this);

    m_sync_update_thread = Slic3r::create_thread(
        [this, progressFn, cancelFn, finishFn, t = std::weak_ptr<int>(m_user_sync_token)] {
            try {
            if (!m_agent) { finishFn(false); return; }

            scan_orphaned_info_files();
            process_delete_presets();

            std::string version = preset_bundle->get_vendor_profile_version(PresetBundle::ORCA_DEFAULT_BUNDLE).to_string();

            preset_bundle->check_and_fix_user_presets_syncinfo(m_agent->get_user_id());

            int ret = m_agent->get_setting_list2(version, [this](auto info) {
                auto type = info[BBL_JSON_KEY_TYPE];
                auto name = info[BBL_JSON_KEY_NAME];
                auto setting_id = info[BBL_JSON_KEY_SETTING_ID];
                auto update_time_str = info[ORCA_JSON_KEY_UPDATE_TIME];
                long long update_time = 0;
                if (!update_time_str.empty())
                    update_time = std::atoll(update_time_str.c_str());
                if (type == "filament") {
                    return preset_bundle->filaments.need_sync(name, setting_id, update_time);
                } else if (type == "print") {
                    return preset_bundle->prints.need_sync(name, setting_id, update_time);
                } else if (type == "printer") {
                    return preset_bundle->printers.need_sync(name, setting_id, update_time);
                } else {
                    return true;
                }
            }, progressFn, cancelFn);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << __LINE__ << " get_setting_list2 ret = " << ret << " m_is_closing = " << m_is_closing;

            finishFn(ret == 0);

            if (ret == 0 && m_agent && !t.expired())
                reload_settings();

            auto orca_agent = std::dynamic_pointer_cast<OrcaCloudServiceAgent>(m_agent->get_cloud_agent());
            int tick_tock = -1, sync_count = 0;
            std::vector<Preset> presets_to_sync;
            std::vector<std::pair<std::string, std::string>> bundles_to_sync;
            std::unordered_set<std::string> bundles_synced;

            std::unordered_set<std::string> known_available_updates;

            bool update_available = false;
            while (!t.expired()) {
                ++tick_tock;
                if (tick_tock % 120 == 0 || m_sync_user_presets_now.exchange(false, std::memory_order_acq_rel)) {
                    tick_tock = 0;
                    if (m_agent) {
                        if (!m_agent->is_user_login()) {
                            continue;
                        }
                        if (!preset_bundle) continue;

                        int total_count = 0;
                        sync_count = preset_bundle->prints.get_user_presets(preset_bundle, presets_to_sync);

                        auto sync_with_lock = [this](Preset& preset) {
                            bool force = false;
                            {
                                std::scoped_lock lock(conflict_ids_mutex);
                                auto it = std::find_if(m_pending_conflict_setting_ids.begin(), m_pending_conflict_setting_ids.end(),
                                                [&preset](const std::string& id) { return id == preset.setting_id; });
                                if (it != m_pending_conflict_setting_ids.end()) {
                                    force = true;
                                    m_pending_conflict_setting_ids.erase(it);
                                }
                            }
                            sync_preset(&preset, force);
                        };

                        if (sync_count > 0) {
                            for (Preset& preset : presets_to_sync) {
                                sync_with_lock(preset);
                                boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
                            }
                        }
                        total_count += sync_count;

                        sync_count = preset_bundle->filaments.get_user_presets(preset_bundle, presets_to_sync);
                        if (sync_count > 0) {
                            for (Preset& preset : presets_to_sync) {
                                sync_with_lock(preset);
                                boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
                            }
                        }
                        total_count += sync_count;

                        sync_count = preset_bundle->printers.get_user_presets(preset_bundle, presets_to_sync);
                        if (sync_count > 0) {
                            for (Preset& preset : presets_to_sync) {
                                sync_with_lock(preset);
                                boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
                            }
                        }
                        total_count += sync_count;

                        if (total_count == 0) {
                            CallAfter([this] {
                                if (!is_closing())
                                    plater()->get_notification_manager()->close_notification_of_type(NotificationType::BBLUserPresetExceedLimit);
                            });
                        }

                        process_delete_presets();
                    }

                    if (orca_agent)
                    {
                        bundles_to_sync.clear();
                        bundles_synced.clear();
                        std::vector<std::string> not_found;
                        std::vector<std::string> unauthorized;
                        
                        int result = orca_agent->get_subscribed_bundles(&bundles_to_sync, not_found, unauthorized);
                        if (result != 0) {
                            BOOST_LOG_TRIVIAL(warning) << "start_sync_user_preset: failed to fetch subscribed bundles, result=" << result;
                            continue;
                        }

                        if(!not_found.empty())
                        {
                            for(auto& n : not_found)
                            {
                                std::string text = format(_L("Bundle %s is no longer available."), n);
                                wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::CustomNotification,NotificationManager::NotificationLevel::RegularNotificationLevel,text);
                            }
                        }
                        if(!unauthorized.empty())
                        {
                            for(auto& i : unauthorized)
                            {
                                std::string text = format(_L("Bundle %s access is unauthorized."), i);
                                wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::CustomNotification,NotificationManager::NotificationLevel::RegularNotificationLevel,text);
                                preset_bundle->bundles.ReadLock();
                                if(preset_bundle->bundles.m_bundles.find(i) != preset_bundle->bundles.m_bundles.end())
                                {
                                    preset_bundle->bundles.m_bundles[i].unauthorized = true;
                                }
                                preset_bundle->bundles.ReadUnlock();
                            }
                        }
                        
                        for (const auto& bundle_entry : bundles_to_sync) {
                            bundles_synced.insert(bundle_entry.first);
                            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << __LINE__ << "ORCA : Update thread syncing bundles";
                            int res = sync_bundle(bundle_entry.first, bundle_entry.second);

                            const std::string known_update_key = bundle_entry.first + ":" + bundle_entry.second;
                            if (res == 1 && known_available_updates.insert(known_update_key).second) {
                                update_available = true;
                            }

                            boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
                        }

                        if (update_available) {
                            wxGetApp()
                                .plater()
                                ->get_notification_manager()
                                ->push_notification(NotificationType::CustomNotification,
                                                    NotificationManager::NotificationLevel::RegularNotificationLevel, _u8L("There is an update available. Open the preset bundle dialog to update it."));

                            update_available = false;
                        }
                        
                        std::vector<BundleMetadata> to_delete;
                        preset_bundle->bundles.ReadLock();
                        for (const auto& [id, bundle] : preset_bundle->bundles.m_bundles) {                                                                                                                                                    
                            if (bundle.bundle_type != BundleType::Subscribed)                                                                                                                                                                                         
                                continue;                                                                                                                                                                                                      
                            if (bundles_synced.find(id) != bundles_synced.end())                                                                                                                                                               
                                continue;
                            if(bundle.unauthorized && bundle.is_subscribed)
                                continue;
                            
                            to_delete.push_back(bundle);
                        }
                        preset_bundle->bundles.ReadUnlock();  

                        bool has_deletion = false;
                        for (const auto& bundle : to_delete) {

                            for (auto printer : bundle.printer_presets)
                                preset_bundle->printers.delete_preset(printer, true);
                            for (auto filament : bundle.filament_presets)
                                preset_bundle->filaments.delete_preset(filament, true);
                            for (auto print : bundle.print_presets)
                                preset_bundle->prints.delete_preset(print, true);

                            fs::path bundle_folder = fs::path(bundle.path.c_str()).parent_path();
                            {
                                boost::system::error_code ec;
                                InstanceLock instance_lock(user_presets_lock_path());
                                boost::filesystem::remove_all(bundle_folder, ec);
                            }

                            preset_bundle->bundles.WriteLock();
                            preset_bundle->bundles.m_bundles.erase(bundle.id);
                            preset_bundle->bundles.WriteUnlock();

                            std::string text = format(_L("%s has been removed."), bundle.name);
                            wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::CustomNotification,NotificationManager::NotificationLevel::RegularNotificationLevel,text);
                            has_deletion = true;
                        }

                        if (has_deletion)
                            CallAfter([this]() {
                                if (!is_closing() && preset_bundle && mainframe) {
                                    preset_bundle->update_compatible(PresetSelectCompatibleType::Never);
                                    preset_bundle->update_multi_material_filament_presets();
                                    mainframe->update_side_preset_ui();

                                    auto* evt = new wxCommandEvent(EVT_UPDATE_BUNDLE_COMPLETE);                                                                                                                                                       
                                    if (m_preset_bundle_dlg)                                                                                                                                                                                                                                                          
                                        wxQueueEvent(m_preset_bundle_dlg, evt);                                                                                                                                                                                                                                       
                                    else                                                                                                                                                                                                                                                                                 
                                        delete evt;
                                }
                        });
                    }
                } else {
                    boost::this_thread::sleep_for(boost::chrono::milliseconds(500));
                }
            }
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "user preset sync thread terminated by exception: " << e.what();
            } catch (...) {
                BOOST_LOG_TRIVIAL(error) << "user preset sync thread terminated by unknown exception";
            }
        });
}

void GUI_App::stop_sync_user_preset()
{
    if (!m_user_sync_token)
        return;

    m_user_sync_token.reset();
    if (m_sync_update_thread.joinable()) {
        if (is_closing())
            m_sync_update_thread.join();
        else
            m_sync_update_thread.detach();
    }
}

void GUI_App::restart_sync_user_preset()
{
    if (m_sync_user_preset_dlg_active)
        return;

    if (!m_user_sync_token) {
        if (!m_restart_sync_pending)
            start_sync_user_preset(true);
        return;
    }

    m_user_sync_token.reset();
    m_restart_sync_pending = true;

    auto old_thread = std::move(m_sync_update_thread);

    std::thread([this, old_thread = std::move(old_thread)]() mutable {
        if (old_thread.joinable())
            old_thread.join();
        m_restart_sync_pending = false;
        if (!is_closing())
            CallAfter([this]() {
                if (!is_closing())
                    start_sync_user_preset(true);
            });
    }).detach();
}

void GUI_App::force_push_conflicting_preset(const std::string& setting_id)
{
    if (setting_id.empty() || !preset_bundle)
        return;

    {
        std::scoped_lock lock(conflict_ids_mutex);
        m_pending_conflict_setting_ids.push_back(setting_id);
    }

    const std::string user_id = m_agent ? m_agent->get_user_id() : std::string();

    PresetCollection* collections[] = {&preset_bundle->prints, &preset_bundle->filaments, &preset_bundle->printers};
    for (PresetCollection* coll : collections) {
        for (const Preset& preset : coll->get_presets()) {
            if (preset.sync_info != "hold")
                continue;
            const std::string preset_id = preset.setting_id.empty()
                ? OrcaCloudServiceAgent::generate_uuid_for_setting_id(preset.name, user_id)
                : preset.setting_id;
            if (preset_id == setting_id) {
                coll->set_sync_info_and_save(preset.name, setting_id, "update", 0, user_id);
                break;
            }
        }
    }

    m_sync_user_presets_now.store(true, std::memory_order_release);
}

void GUI_App::on_stealth_mode_enter()
{
    stop_sync_user_preset();
    BOOST_LOG_TRIVIAL(info) << "logout: on_stealth_mode_enter";
    request_user_logout(ORCA_CLOUD_PROVIDER);
    request_user_logout(BBL_CLOUD_PROVIDER);
    if (WebViewPanel* home = WebViewPanel::if_built()) {
        home->SendCloudProvidersInfo();
    }
}

void GUI_App::start_http_server(const std::string& provider)
{
    m_http_server.set_request_handler([provider](const std::string& url) {
        return HttpServer::auth_handle_request(url, provider);
    });

    if (!m_http_server.is_started())
        m_http_server.start();
}

void GUI_App::start_http_server(int port, const std::string& provider)
{
    if (port <= 0) {
        start_http_server(provider);
        return;
    }

    m_http_server.set_request_handler([provider](const std::string& url) {
        return HttpServer::auth_handle_request(url, provider);
    });

    if (m_http_server.is_started()) {
        if (m_http_server.get_port() == static_cast<boost::asio::ip::port_type>(port)) {
            return;
        }
        m_http_server.stop();
    }

    m_http_server.set_port(static_cast<boost::asio::ip::port_type>(port));
    m_http_server.start();
}

void GUI_App::stop_http_server()
{
    m_http_server.stop();
}

#ifdef __linux__
static const wxLanguageInfo* linux_get_existing_locale_language(const wxLanguageInfo* language,
                                                                const wxLanguageInfo* system_language)
{
    constexpr size_t max_len = 50;
    char path[max_len] = "";
    std::vector<std::string> locales;
    const std::string lang_prefix = into_u8(language->CanonicalName.BeforeFirst('_'));

    FILE* fp = popen("locale -a", "r");
    if (fp != NULL) {
        while (fgets(path, max_len, fp) != NULL) {
            std::string line(path);
            line = line.substr(0, line.find('\n'));
            if (boost::starts_with(line, lang_prefix))
                locales.push_back(line);
        }
        pclose(fp);
    }

    std::sort(locales.begin(), locales.end(), [](const std::string& a, const std::string& b)
    {
        auto has_utf8 = [](const std::string & s) {
            auto S = boost::to_upper_copy(s);
            return S.find("UTF8") != std::string::npos || S.find("UTF-8") != std::string::npos;
        };
        return ! has_utf8(a) && has_utf8(b);
    });

    for (std::string& s : locales)
        s = s.substr(0, s.find("."));

    locales.erase(std::remove_if(locales.begin(),
                                 locales.end(),
                                 [](const std::string& s) {
                                     return ! std::regex_match(s,
                                         std::regex("^[a-z]+_[A-Z]{2}$"));
                                 }),
                   locales.end());

    std::string temp_local = into_u8(system_language->CanonicalName.AfterFirst('_'));
    if (temp_local.size() >= 2) {
        temp_local = temp_local.substr(0, 2);
    }
    std::string system_country = "_" + temp_local;
    int cnt = locales.size();
    for (int i=0; i<cnt; ++i)
        if (locales[i].find(system_country) != std::string::npos) {
            locales.emplace_back(std::move(locales[i]));
            locales[i].clear();
        }

    for (auto it = locales.rbegin(); it != locales.rend(); ++ it)
        if (! it->empty()) {
            const std::string &locale = *it;
            const wxLanguageInfo* lang = wxLocale::FindLanguageInfo(from_u8(locale));
            if (lang != nullptr && wxLocale::IsAvailable(lang->Language))
                return lang;
        }
    return language;
}
#endif

int GUI_App::GetSingleChoiceIndex(const wxString& message,
                                const wxString& caption,
                                const wxArrayString& choices,
                                int initialSelection)
{
#ifdef _WIN32
    wxSingleChoiceDialog dialog(nullptr, message, caption, choices);
    dialog.SetBackgroundColour(*wxWHITE);
    wxGetApp().UpdateDlgDarkUI(&dialog);

    dialog.SetSelection(initialSelection);
    return dialog.ShowModal() == wxID_OK ? dialog.GetSelection() : -1;
#else
    return wxGetSingleChoiceIndex(message, caption, choices, initialSelection);
#endif
}


bool GUI_App::load_language(wxString language, bool initial)
{
    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: language %2%, initial: %3%") %__FUNCTION__ %language %initial;
    if (initial) {
	    wxFileTranslationsLoader::AddCatalogLookupPathPrefix(from_u8(localization_dir()));
        language = app_config->get("language");
        if (! language.empty())
        	BOOST_LOG_TRIVIAL(info) << boost::format("language provided by OrcaSlicer.conf: %1%") % language;
        else {
            const wxLanguage lang_system = wxLanguage(wxLocale::GetSystemLanguage());
            if (lang_system != wxLANGUAGE_UNKNOWN) {
                m_language_info_system = wxLocale::GetLanguageInfo(lang_system);
#ifdef __WXMSW__
                WCHAR wszLanguagesBuffer[LOCALE_NAME_MAX_LENGTH];
                ::LCIDToLocaleName(LOCALE_USER_DEFAULT, wszLanguagesBuffer, LOCALE_NAME_MAX_LENGTH, 0);
                wxString lang(wszLanguagesBuffer);
                lang.Replace('-', '_');
                if (auto info = wxLocale::FindLanguageInfo(lang))
                    m_language_info_system = info;
#endif
                BOOST_LOG_TRIVIAL(info) << boost::format("System language detected (user locales and such): %1%") % m_language_info_system->CanonicalName.ToUTF8().data();
                app_config->set("language", m_language_info_system->CanonicalName.ToUTF8().data());
            } else {
                {
                    wxLocale temp_locale;
                    temp_locale.Init();
                    wxTranslations::Get()->SetLanguage(wxLANGUAGE_DEFAULT);
                    wxString best_language = wxTranslations::Get()->GetBestTranslation(SLIC3R_APP_KEY, wxLANGUAGE_ENGLISH);
                    if (!best_language.IsEmpty()) {
                        m_language_info_best = wxLocale::FindLanguageInfo(best_language);
                        BOOST_LOG_TRIVIAL(info) << boost::format("Best translation language detected (may be different from user locales): %1%") %
                                                        m_language_info_best->CanonicalName.ToUTF8().data();
                        app_config->set("language", m_language_info_best->CanonicalName.ToUTF8().data());
                    }
#ifdef __linux__
                    wxString lc_all;
                    if (wxGetEnv("LC_ALL", &lc_all) && !lc_all.IsEmpty()) {
                        m_language_info_best = nullptr;
                    }
#endif
                }
            }
        }
    }

	const wxLanguageInfo *language_info = language.empty() ? nullptr : wxLocale::FindLanguageInfo(language);
	if (! language.empty() && (language_info == nullptr || language_info->CanonicalName.empty())) {
		language_info = nullptr;
    	BOOST_LOG_TRIVIAL(error) << boost::format("Language code \"%1%\" is not supported") % language.ToUTF8().data();
	}

	if (language_info != nullptr && language_info->LayoutDirection == wxLayout_RightToLeft) {
    	BOOST_LOG_TRIVIAL(trace) << boost::format("The following language code requires right to left layout, which is not supported by OrcaSlicer: %1%") % language_info->CanonicalName.ToUTF8().data();
		language_info = nullptr;
	}

    if (language_info == nullptr) {
        if (m_language_info_system != nullptr && m_language_info_system->LayoutDirection != wxLayout_RightToLeft)
            language_info = m_language_info_system;
        if (m_language_info_best != nullptr && m_language_info_best->LayoutDirection != wxLayout_RightToLeft)
        	language_info = m_language_info_best;
	    if (language_info == nullptr)
			language_info = wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH_US);
    }

    const wxLanguageInfo *translation_language_info = language_info;
    const wxString requested_language_code = translation_language_info->CanonicalName;
    const wxLanguageInfo *locale_language_info = translation_language_info;
    BOOST_LOG_TRIVIAL(trace) << boost::format("Requested translation language %1%") % requested_language_code.ToUTF8().data();

    wxLanguage language_dict = wxLanguage(translation_language_info->Language);
    if (translation_language_info->CanonicalName.BeforeFirst('_') == "sk") {
    	language_dict = wxLANGUAGE_CZECH;
		BOOST_LOG_TRIVIAL(info) << "Using Czech dictionaries for Slovak language";
    }

#ifdef __linux__
    if (!wxLocale::IsAvailable(locale_language_info->Language) && m_language_info_system) {
        std::string original_lang = into_u8(locale_language_info->CanonicalName);
        locale_language_info = linux_get_existing_locale_language(locale_language_info, m_language_info_system);
        if (locale_language_info != nullptr && locale_language_info != translation_language_info) {
            BOOST_LOG_TRIVIAL(info) << boost::format("Can't use locale %1% directly (missing locales). Using locale %2% instead.")
                                        % original_lang % locale_language_info->CanonicalName.ToUTF8().data();
        }
    }
#endif

    if (locale_language_info == nullptr || !wxLocale::IsAvailable(locale_language_info->Language)) {
        wxString base_lang = requested_language_code.BeforeFirst('_');
        if (base_lang != requested_language_code) {
            const wxLanguageInfo *base_info = wxLocale::FindLanguageInfo(base_lang);
            if (base_info && wxLocale::IsAvailable(base_info->Language)) {
                BOOST_LOG_TRIVIAL(info) << boost::format("Locale %1% not available. Falling back to base language %2%.")
                    % requested_language_code.ToUTF8().data() % base_info->CanonicalName.ToUTF8().data();
                locale_language_info = base_info;
            }
        }
    }

    if (locale_language_info == nullptr || !wxLocale::IsAvailable(locale_language_info->Language)) {
        auto try_locale = [](const wxLanguageInfo* candidate) -> const wxLanguageInfo* {
            return (candidate && wxLocale::IsAvailable(candidate->Language)) ? candidate : nullptr;
        };
        const wxLanguageInfo* fallback_locale_info =
            try_locale(m_wxLocale ? wxLocale::GetLanguageInfo(wxLanguage(m_wxLocale->GetLanguage())) : nullptr);
        if (!fallback_locale_info) fallback_locale_info = try_locale(m_language_info_system);
        if (!fallback_locale_info) fallback_locale_info = try_locale(m_language_info_best);
        if (!fallback_locale_info) fallback_locale_info = try_locale(wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH_US));
        if (!fallback_locale_info) fallback_locale_info = try_locale(wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH_UK));
        if (fallback_locale_info != nullptr) {
            BOOST_LOG_TRIVIAL(info) << boost::format("Using fallback locale %1% while keeping translation dictionary %2%.")
                                        % fallback_locale_info->CanonicalName.ToUTF8().data() % requested_language_code.ToUTF8().data();
            locale_language_info = fallback_locale_info;
        }
    }

    if (initial) {
    }

	BOOST_LOG_TRIVIAL(trace) << boost::format("Switching wxLocales to %1%") % locale_language_info->CanonicalName.ToUTF8().data();

    if (!wxLocale::IsAvailable(locale_language_info->Language)) {
	    wxString message = wxString::Format(_L("Switching Orca Slicer to language %s failed."), requested_language_code);
#if !defined(_WIN32) && !defined(__APPLE__)
        message += _L("\nYou may need to reconfigure the missing locales, likely by running the \"locale-gen\" and \"dpkg-reconfigure locales\" commands.\n");
#endif
        if (initial)
        	message += "\n\n" + _L("Application will close.");
        wxMessageBox(message, _L("Orca Slicer - Switching language failed"), wxOK | wxICON_ERROR);
        if (initial)
			std::exit(EXIT_FAILURE);
		else
			return false;
    }

    m_wxLocale.release();
    m_wxLocale = Slic3r::make_unique<wxLocale>();
    m_wxLocale->Init(locale_language_info->Language);
    wxTranslations::Get()->SetLanguage(language_dict);
    m_wxLocale->AddCatalog(SLIC3R_APP_KEY);
    m_active_language_code = requested_language_code;
    m_imgui->set_language(into_u8(requested_language_code));

    Preset::update_suffix_modified((_L("*") + " ").ToUTF8().data());
    HintDatabase::get_instance().reinit();
	return true;
}

Tab* GUI_App::get_tab(Preset::Type type)
{
    for (Tab* tab: tabs_list)
        if (tab->type() == type)
            return tab->completed() ? tab : nullptr;
    return nullptr;
}

Tab* GUI_App::get_plate_tab()
{
    return plate_tab;
}

Tab* GUI_App::get_model_tab(bool part)
{
    return model_tabs_list[part ? 1 : 0];
}

Tab* GUI_App::get_layer_tab()
{
    return model_tabs_list[2];
}

namespace
{
ConfigOptionMode saved_mode_from_string(const std::string& mode)
{
    return mode == "expert" ? comExpert :
           mode == "advanced" ? comAdvanced :
           mode == "develop" ? comAdvanced :
           comSimple;
}

std::string saved_mode_to_string(ConfigOptionMode mode)
{
    return mode == comExpert ? "expert" :
           mode == comAdvanced ? "advanced" :
           "simple";
}

std::string effective_mode_to_string(ConfigOptionMode mode)
{
    return mode == comDevelop ? "develop" : saved_mode_to_string(mode);
}
}

ConfigOptionMode GUI_App::get_saved_mode()
{
    if (!app_config->has("user_mode"))
        return comSimple;

    return saved_mode_from_string(app_config->get("user_mode"));
}

bool GUI_App::starts_on_prepare() const
{
    return app_config->get("default_page") == "1";
}

int GUI_App::input_idle_ms() const
{
    return int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_last_input).count());
}

int GUI_App::FilterEvent(wxEvent& event)
{
    if ((!event.IsCommandEvent() && (event.GetEventCategory() & wxEVT_CATEGORY_USER_INPUT)) ||
        (event.GetEventType() == wxEVT_SIZE && event.GetEventObject() == mainframe))
        m_last_input = std::chrono::steady_clock::now();
    return Event_Skip;
}

ConfigOptionMode GUI_App::get_mode()
{
    return app_config->get_bool("developer_mode") ? comDevelop : get_saved_mode();
}

std::string GUI_App::get_saved_mode_str()
{
    return saved_mode_to_string(get_saved_mode());
}

std::string GUI_App::get_mode_str()
{
    return effective_mode_to_string(get_mode());
}

void GUI_App::save_mode(const int mode)
{
    const auto saved_mode = mode == comExpert ? comExpert :
                            mode == comAdvanced ? comAdvanced :
                            mode == comSimple ? comSimple :
                            get_saved_mode();
    app_config->set("user_mode", saved_mode_to_string(saved_mode));
    update_mode();
}

void GUI_App::set_mode(ConfigOptionMode mode)
{
    const bool was_developer = app_config->get_bool("developer_mode");
    if (was_developer)
        app_config->set_bool("developer_mode", false);
    save_mode(mode);
    if (was_developer)
        app_config->save();
}

void GUI_App::enable_developer_mode()
{
    app_config->set_bool("developer_mode", true);
    update_mode();
}

void GUI_App::update_mode()
{
    sidebar().update_mode();

    if (mainframe->m_param_panel)
        mainframe->m_param_panel->update_mode();
    if (mainframe->m_param_dialog)
        mainframe->m_param_dialog->panel()->update_mode();
    if (PrinterWebView* view = PrinterWebView::if_built())
        view->update_mode();
    if (WebViewPanel* home = WebViewPanel::if_built())
        home->update_mode();

#ifdef _MSW_DARK_MODE
    if (!wxGetApp().tabs_as_menu())
        dynamic_cast<Notebook*>(mainframe->m_tabpanel)->UpdateMode();
#endif

    for (auto tab : tabs_list)
        tab->update_mode();
    for (auto tab : model_tabs_list)
        tab->update_mode();

    plater()->canvas3D()->update_gizmos_on_off_state();
}

void GUI_App::update_internal_development() {
    if (WebViewPanel* home = WebViewPanel::if_built())
        home->update_mode();
    if (PrinterWebView* view = PrinterWebView::if_built())
        view->update_mode();
}

void GUI_App::show_ip_address_enter_dialog(wxString title)
{
    auto evt = new wxCommandEvent(EVT_SHOW_IP_DIALOG);
    evt->SetString(title);
    evt->SetInt(-1);
    wxQueueEvent(this, evt);
}

bool GUI_App::show_modal_ip_address_enter_dialog(bool input_sn, wxString title)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return false;
    if (!dev->get_selected_machine()) return false;
    auto obj = dev->get_selected_machine();

    InputIpAddressDialog dlg(nullptr);
    dlg.m_need_input_sn = input_sn;
    dlg.set_machine_obj(obj);
    if (!title.empty()) dlg.update_title(title);

    dlg.Bind(EVT_ENTER_IP_ADDRESS, [obj](wxCommandEvent& e) {
        auto selection_data_arr = wxSplit(e.GetString().ToStdString(), '|');

        if (selection_data_arr.size() == 2) {
            auto ip_address = selection_data_arr[0];
            auto access_code = selection_data_arr[1];

            BOOST_LOG_TRIVIAL(info) << "User enter IP address is " << ip_address;
            if (!ip_address.empty()) {
                wxGetApp().app_config->set_str("ip_address", obj->get_dev_id(), ip_address.ToStdString());
                wxGetApp().app_config->save();

                obj->set_dev_ip(ip_address.ToStdString());
                obj->set_access_code(access_code.ToStdString());
            }
        }
    });

    if (dlg.ShowModal() == wxID_YES) {
        return true;
    }
    return false;
}

void  GUI_App::show_ip_address_enter_dialog_handler(wxCommandEvent& evt)
{
    wxString title = evt.GetString();
    int mode = evt.GetInt();
    show_modal_ip_address_enter_dialog(mode == -1?false:true, title);
}

void GUI_App::open_presetbundledialog(size_t open_on_tab, const std::string& highlight_option)
{
    bool app_layout_changed = false;
    {

        if(m_preset_bundle_dlg)
        {
            return;
        }
        m_preset_bundle_dlg = new PresetBundleDialog(mainframe, open_on_tab, highlight_option);
        m_preset_bundle_dlg->Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent&) {                                                                                                                                                                                                          
            if (m_preset_bundle_dlg)                                                                                                                                                                                                                                           
                m_preset_bundle_dlg = nullptr;                                                                                                                                                                                                                                                
        });
        m_preset_bundle_dlg->ShowModal();
        if (m_preset_bundle_dlg) {                                                                                                                                                                                                                                             
            m_preset_bundle_dlg->Destroy();                                                                                                                                                                                                                                                              
            m_preset_bundle_dlg = nullptr;                                                                                                                                                                                                                                            
        }
        this->plater_->get_current_canvas3D()->force_set_focus();
        
    }
}

void GUI_App::open_plugins_dialog(size_t open_on_tab, const std::string& highlight_option)
{
    if (m_plugins_dlg) {
        m_plugins_dlg->Show();
        m_plugins_dlg->Raise();
        return;
    }

    try {
        m_plugins_dlg = new PluginsDialog(mainframe, wxID_ANY, _L("Plugins"));
        m_plugins_dlg->set_open_terminal_dlg_fn();
        m_plugins_dlg->Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent& event) {
            if (event.GetEventObject() == m_plugins_dlg)
                m_plugins_dlg = nullptr;
            event.Skip();
        });

        m_plugins_dlg->Show();
        m_plugins_dlg->Raise();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "open_plugins_dialog failed: " << e.what();
        if (m_plugins_dlg) {
            m_plugins_dlg->Destroy();
            m_plugins_dlg = nullptr;
        }
        wxMessageBox(wxString::Format(_L("Failed to open the Plugins dialog:\n%s"), from_u8(e.what())), _L("Plugins"),
                     wxOK | wxICON_ERROR, mainframe);
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "open_plugins_dialog failed with a non-standard exception";
        if (m_plugins_dlg) {
            m_plugins_dlg->Destroy();
            m_plugins_dlg = nullptr;
        }
        wxMessageBox(_L("Failed to open the Plugins dialog (unknown error)."), _L("Plugins"), wxOK | wxICON_ERROR, mainframe);
    }
}

void GUI_App::refresh_plugins()
{
    std::thread([]() {
        wxString error;
        try {
            refresh_plugin_metadata_blocking( true);
        } catch (const std::exception& ex) {
            error = from_u8(ex.what());
        } catch (...) {
            error = "Unknown error";
        }
        if (!wxTheApp)
            return;
        wxTheApp->CallAfter([error]() {
            if (wxGetApp().is_closing())
                return;
            Plater* plater = wxGetApp().plater();
            if (plater == nullptr)
                return;
            if (error.IsEmpty())
                plater->get_notification_manager()->push_notification(
                    NotificationType::CustomNotification,
                    NotificationManager::NotificationLevel::RegularNotificationLevel,
                    into_u8(_L("Plugins refreshed.")));
            else
                plater->get_notification_manager()->push_notification(
                    NotificationType::CustomNotification,
                    NotificationManager::NotificationLevel::ErrorNotificationLevel,
                    into_u8(wxString::Format(_L("Failed to refresh plugins: %s"), error)));
        });
    }).detach();
}

void GUI_App::install_local_plugin()
{
    if (mainframe == nullptr)
        return;

    wxFileDialog dialog(mainframe, _L("Select plugin package"), wxEmptyString, wxEmptyString, _L("Plugin files (*.py;*.whl)|*.py;*.whl"),
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
        return;

    wxString message;
    const bool ok = install_local_plugin_package(boost::filesystem::path(dialog.GetPath().ToUTF8().data()), mainframe, message);
    if (message.IsEmpty())
        return;

    Plater* plater = this->plater();
    if (plater == nullptr)
        return;
    plater->get_notification_manager()->push_notification(
        NotificationType::CustomNotification,
        ok ? NotificationManager::NotificationLevel::RegularNotificationLevel : NotificationManager::NotificationLevel::ErrorNotificationLevel,
        into_u8(message));
}

void GUI_App::open_terminal_dialog()
{
    CallAfter([this]() {
        if (m_terminal_dlg) {
            if (!m_terminal_dlg->IsShown())
                m_terminal_dlg->Show();
            m_terminal_dlg->Raise();
            return;
        }

        m_terminal_dlg = new TerminalDialog(mainframe, wxID_ANY, _L("Plugin Terminal"),
                                            wxDefaultPosition, wxSize(820, 600));
        m_terminal_dlg->Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent& event) {
            if (event.GetEventObject() == m_terminal_dlg)
                m_terminal_dlg = nullptr;
            event.Skip();
        });

        m_terminal_dlg->Show();
    });
}

void GUI_App::open_speed_dial()
{
    if (!mainframe)
        return;
    if (!m_speed_dial_dialog) {
        m_speed_dial_dialog = new SpeedDialWebDialog(mainframe);
        m_speed_dial_dialog->Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent& event) {
            if (event.GetEventObject() == m_speed_dial_dialog)
                m_speed_dial_dialog = nullptr;
            event.Skip();
        });
    }
    m_speed_dial_dialog->request_show();
}

void GUI_App::open_exportpresetbundledialog(size_t open_on_tab, const std::string& highlight_option)
{
    bool app_layout_changed = false;
    {
        ExportPresetBundleDialog dlg(mainframe, open_on_tab, highlight_option);
        dlg.ShowModal();
        this->plater_->get_current_canvas3D()->force_set_focus();
        #if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
            if (dlg.seq_top_layer_only_changed() || dlg.seq_seq_top_gcode_indices_changed())
        #else
                if (dlg.seq_top_layer_only_changed())
        #endif
                    this->plater_->reload_print();
    }
}

void GUI_App::open_preferences() { open_preferences(PreferencesTab::General); }

void GUI_App::open_preferences(PreferencesTab tab, const std::string& highlight_option)
{
    static constexpr const char* opengl_render_setting_keys[] = {
        SETTING_OPENGL_FXAA_ENABLED, SETTING_OPENGL_FPS_CAP, SETTING_OPENGL_SHOW_FPS_OVERLAY, SETTING_OPENGL_SHOW_RENDER_TIMINGS,
        SETTING_OPENGL_SCENE_CACHE, SETTING_OPENGL_SKIP_IDENTICAL_FRAMES, SETTING_OPENGL_REALISTIC_SHADOWS
    };
    std::vector<std::string> previous_opengl_render_settings;
    for (const char* key : opengl_render_setting_keys)
        previous_opengl_render_settings.emplace_back(app_config->get(key));

    bool need_recreate_gui = false;
    std::string pending_language;
    {
        PreferencesDialog dlg(mainframe);
        dlg.select_tab(tab, highlight_option);
        dlg.ShowModal();
        need_recreate_gui = dlg.recreate_GUI();
        pending_language = dlg.pending_language();
        if (!need_recreate_gui) {
            this->plater_->get_current_canvas3D()->force_set_focus();
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
            if (dlg.seq_top_layer_only_changed() || dlg.seq_seq_top_gcode_indices_changed())
#else
            if (dlg.seq_top_layer_only_changed())
#endif
                this->plater_->reload_print();
#ifdef _WIN32
            if (is_editor()) {
                if (app_config->get("associate_3mf") == "true")
                    associate_files(L"3mf");
                if (app_config->get("associate_stl") == "true")
                    associate_files(L"stl");
                if (app_config->get("associate_step") == "true") {
                    associate_files(L"step");
                    associate_files(L"stp");
                }
                associate_url(L"orcaslicer");
            }
            else {
                if (app_config->get("associate_gcode") == "true")
                    associate_files(L"gcode");
            }
#endif
        }
    }

    bool opengl_render_settings_changed = false;
    for (size_t i = 0; i < previous_opengl_render_settings.size(); ++i)
        opengl_render_settings_changed |= app_config->get(opengl_render_setting_keys[i]) != previous_opengl_render_settings[i];
    if (opengl_render_settings_changed && !need_recreate_gui && this->plater_ != nullptr) {
        this->plater_->set_current_canvas_as_dirty();
        this->plater_->get_current_canvas3D()->force_set_focus();
    }

    if (!pending_language.empty()) {
        const std::string previous_language = app_config->get("language");
        app_config->set("language", pending_language);
        if (!load_language(wxString::FromUTF8(pending_language), false)) {
            app_config->set("language", previous_language);
            if (this->plater_)
                this->plater_->get_current_canvas3D()->force_set_focus();
            return;
        }
        m_action_registry.relocalize_builtins();
    }

    if (need_recreate_gui)
        recreate_GUI(_L("Changing application language"));
}

bool GUI_App::has_unsaved_preset_changes() const
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (const Tab* const tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology) && tab->saved_preset_is_dirty())
            return true;
    }
    return false;
}

bool GUI_App::has_current_preset_changes() const
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (const Tab* const tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
            return true;
    }
    return false;
}

void GUI_App::update_saved_preset_from_current_preset()
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab* tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology))
            tab->update_saved_preset_from_current_preset();
    }
}

std::vector<std::pair<unsigned int, std::string>> GUI_App::get_selected_presets() const
{
    std::vector<std::pair<unsigned int, std::string>> ret;
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab* tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology)) {
            const PresetCollection* presets = tab->get_presets();
            ret.push_back({ static_cast<unsigned int>(presets->type()), presets->get_selected_preset_name() });
        }
    }
    return ret;
}

bool GUI_App::check_and_save_current_preset_changes(const wxString& caption, const wxString& header, bool remember_choice , bool dont_save_insted_of_discard )
{
    if (has_current_preset_changes()) {
        int act_buttons = ActionButtons::SAVE;
        if (dont_save_insted_of_discard)
            act_buttons |= ActionButtons::DONT_SAVE;
        if (remember_choice)
            act_buttons |= ActionButtons::REMEMBER_CHOISE;
        UnsavedChangesDialog dlg(caption, header, "", act_buttons);
        bool no_need_change = dlg.getUpdateItemCount() == 0 ? true : false;
        if (!no_need_change && dlg.ShowModal() == wxID_CANCEL)
            return false;

        if (dlg.save_preset())
        {
            for (const UnsavedChangesDialog::PresetData& nt : dlg.get_names_and_types())
                preset_bundle->save_changes_for_preset(nt.name, nt.type, dlg.get_unselected_options(nt.type), nt.save_to_project);

            load_current_presets(false);

            preset_bundle->export_selections(*app_config);

        }
    }

    return true;
}

void GUI_App::apply_keeped_preset_modifications()
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab* tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology))
            tab->apply_config_from_cache();
    }
    load_current_presets(false);
}

bool GUI_App::check_and_keep_current_preset_changes(const wxString& caption, const wxString& header, int action_buttons, bool* postponed_apply_of_keeped_changes )
{
    if (has_current_preset_changes()) {
        bool is_called_from_configwizard = postponed_apply_of_keeped_changes != nullptr;

        UnsavedChangesDialog dlg(caption, header, "", action_buttons);
        bool no_need_change = dlg.getUpdateItemCount() == 0 ? true : false;
        if (!no_need_change && dlg.ShowModal() == wxID_CANCEL)
            return false;

        auto reset_modifications = [this]() {

            PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
            for (const Tab* const tab : tabs_list) {
                if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
                    tab->m_presets->discard_current_changes();
            }
            load_current_presets(false);
        };

        if (dlg.discard() || no_need_change)
            reset_modifications();
        else
        {
            const auto& preset_names_and_types = dlg.get_names_and_types();
            if (dlg.save_preset()) {
                for (const UnsavedChangesDialog::PresetData& nt : preset_names_and_types)
                    preset_bundle->save_changes_for_preset(nt.name, nt.type, dlg.get_unselected_options(nt.type), nt.save_to_project);

                preset_bundle->export_selections(*app_config);

                reset_modifications();
            }
            else if (dlg.transfer_changes() && (dlg.has_unselected_options() || is_called_from_configwizard)) {
                for (const UnsavedChangesDialog::PresetData& nt : preset_names_and_types) {
                    Preset::Type type = nt.type;
                    Tab* tab = get_tab(type);
                    std::vector<std::string> selected_options = dlg.get_selected_options(type);
                    if (type == Preset::TYPE_PRINTER) {
                        auto it = std::find(selected_options.begin(), selected_options.end(), "extruders_count");
                        if (it != selected_options.end()) {
                            selected_options.erase(it);
                            static_cast<TabPrinter*>(tab)->cache_extruder_cnt();
                        }
                    }
                    std::vector<std::string> selected_options2;
                    std::transform(selected_options.begin(), selected_options.end(), std::back_inserter(selected_options2), [](auto & o) {
                        auto i = o.find('#');
                        return i != std::string::npos ? o.substr(0, i) : o;
                    });
                    tab->cache_config_diff(selected_options2);
                    if (!is_called_from_configwizard)
                        tab->m_presets->discard_current_changes();
                }
                if (is_called_from_configwizard)
                    *postponed_apply_of_keeped_changes = true;
                else
                    apply_keeped_preset_modifications();
            }
        }
    }

    return true;
}

bool GUI_App::can_load_project()
{
    return true;
}

bool GUI_App::check_print_host_queue()
{
    wxString dirty;
    std::vector<std::pair<std::string, std::string>> jobs;
    mainframe->m_printhost_queue_dlg->get_active_jobs(jobs);
    if (jobs.empty())
        return true;
    wxString job_string = wxString();
    for (const auto& job : jobs) {
        job_string += format_wxstr("   %1% : %2% \n", job.first, job.second);
    }
    wxString message;
    message += _(L("The uploads are still ongoing")) + ":\n\n" + job_string +"\n" + _(L("Stop them and continue anyway?"));
    MessageDialog dialog(mainframe,
        message,
        wxString(SLIC3R_APP_NAME) + " - " + _(L("Ongoing uploads")),
        wxICON_QUESTION | wxYES_NO | wxNO_DEFAULT);
    if (dialog.ShowModal() == wxID_YES)
        return true;

    mainframe->m_printhost_queue_dlg->Show();
    return false;
}

bool GUI_App::checked_tab(Tab* tab)
{
    bool ret = true;
    if (find(tabs_list.begin(), tabs_list.end(), tab) == tabs_list.end() &&
        find(model_tabs_list.begin(), model_tabs_list.end(), tab) == model_tabs_list.end())
        ret = false;
    return ret;
}

void GUI_App::load_current_presets(bool active_preset_combox , bool check_printer_presets_ )
{
    if (check_printer_presets_)
        check_printer_presets();

    auto& edited_printer_preset = preset_bundle->printers.get_edited_preset();
    PrinterTechnology printer_technology = edited_printer_preset.printer_technology();
    if (printer_technology == ptFFF && !edited_printer_preset.config.opt_bool("single_extruder_multi_material")) {
        auto* nozzle_diameter = edited_printer_preset.config.option<ConfigOptionFloats>("nozzle_diameter");
        if (nozzle_diameter) {
            const size_t target = nozzle_diameter->values.size() + preset_bundle->num_mixed_filaments();
            if (target > preset_bundle->filament_presets.size())
                preset_bundle->set_num_filaments(target);
        }
    }
	this->plater()->set_printer_technology(printer_technology);
    for (Tab *tab : tabs_list)
		if (tab->supports_printer_technology(printer_technology)) {
			if (tab->type() == Preset::TYPE_PRINTER) {
				static_cast<TabPrinter*>(tab)->update_pages();
				this->plater()->force_print_bed_update();
			}
			tab->load_current_preset();
			if (active_preset_combox)
				tab->reactive_preset_combo_box();
		}
    if (printer_technology == ptFFF)
        this->plater()->on_filament_count_change(preset_bundle->filament_presets.size());

    for (Tab *tab : model_tabs_list)
		if (tab->supports_printer_technology(printer_technology)) {
            tab->rebuild_page_tree();
        }
}

static std::mutex mutex_delete_cache_presets;

std::map<std::string, std::string> & GUI_App::get_delete_cache_presets()
{
    return need_delete_presets;
}

std::map<std::string, std::string> GUI_App::get_delete_cache_presets_lock()
{
    std::scoped_lock l(mutex_delete_cache_presets);
    return need_delete_presets;
}

void GUI_App::process_delete_presets()
{
    std::map<std::string, std::string> delete_cache_presets = get_delete_cache_presets_lock();
    for (auto it = delete_cache_presets.begin(); it != delete_cache_presets.end();) {
        if (it->first.empty()) continue;
        std::string del_setting_id = it->first;
        int result = m_agent->delete_setting(del_setting_id);
        if (result == 0) {
            preset_deleted_from_cloud(del_setting_id);
            it = delete_cache_presets.erase(it);
            m_create_preset_blocked = { false, false, false, false, false, false };
            BOOST_LOG_TRIVIAL(trace) << "sync_preset: sync operation: delete success! setting id = " << del_setting_id;
        }
        else {
            BOOST_LOG_TRIVIAL(info) << "delete setting = " <<del_setting_id << " failed";
            it++;
        }
    }
}

void GUI_App::delete_preset_from_cloud(std::string setting_id, std::string preset_file_path)
{
    std::scoped_lock l(mutex_delete_cache_presets);
    fs::path info_path = fs::path(preset_file_path);
    info_path.replace_extension("info");
    need_delete_presets.emplace(setting_id, info_path.string());
}

void GUI_App::preset_deleted_from_cloud(std::string setting_id)
{
    std::scoped_lock l(mutex_delete_cache_presets);

    std::string preset_file_path = need_delete_presets[setting_id];

    if (!preset_file_path.empty() && fs::exists(fs::path(preset_file_path))) {
        InstanceLock instance_lock(user_presets_lock_path());
        boost::nowide::remove(preset_file_path.c_str());
        BOOST_LOG_TRIVIAL(info) << "Deleted .info file after cloud confirmation: " << preset_file_path;
    }

    need_delete_presets.erase(setting_id);
}

std::string GUI_App::extract_setting_id_from_info(const std::string& info_file_path)
{
    boost::nowide::ifstream file(info_file_path);
    if (!file.is_open())
        return "";

    std::string line;
    while (std::getline(file, line)) {
        if (line.find("setting_id") == 0) {
            size_t pos = line.find("=");
            if (pos != std::string::npos) {
                std::string setting_id = line.substr(pos + 1);
                setting_id.erase(0, setting_id.find_first_not_of(" \t"));
                setting_id.erase(setting_id.find_last_not_of(" \t\r\n") + 1);
                return setting_id;
            }
        }
    }
    return "";
}

void GUI_App::scan_orphaned_info_files()
{
    std::string user_sub_folder = app_config->get("preset_folder");
    if (user_sub_folder.empty())
        user_sub_folder = DEFAULT_USER_FOLDER_NAME;
    const std::string dir_user_presets = data_dir() + "/" + PRESET_USER_DIR + "/" + user_sub_folder;

    std::vector<std::string> preset_types = {PRESET_PRINT_NAME, PRESET_FILAMENT_NAME, PRESET_PRINTER_NAME};

    for (const std::string& type : preset_types) {
        fs::path type_dir = fs::path(dir_user_presets) / type;
        if (!fs::exists(type_dir))
            continue;

        boost::system::error_code ec;
        for (boost::filesystem::directory_iterator it(type_dir, ec), end; !ec && it != end; it.increment(ec)) {
            const auto& entry = *it;
            if (entry.path().extension() != ".info")
                continue;

            fs::path info_file = entry.path();
            fs::path preset_file = info_file;
            preset_file.replace_extension(".json");

            std::string setting_id;
            {
                InstanceLock instance_lock(user_presets_lock_path());
                if (!fs::exists(preset_file))
                    setting_id = extract_setting_id_from_info(info_file.string());
            }
            if (!setting_id.empty()) {
                delete_preset_from_cloud(setting_id, info_file.string());
                BOOST_LOG_TRIVIAL(info) << "Found orphaned .info file on startup: " << info_file.string();
            }
        }
        if (ec)
            BOOST_LOG_TRIVIAL(warning) << "scan_orphaned_info_files: failed to scan " << type_dir.string() << ": " << ec.message();
    }
}

wxString GUI_App::filter_string(wxString str)
{
    std::string result = str.utf8_string();
    std::string input = str.utf8_string();


    std::regex domainRegex(R"(([a-zA-Z0-9.-]+\.[a-zA-Z]{2,}(?:\.[a-zA-Z]{2,})?))");
    std::sregex_iterator it(input.begin(), input.end(), domainRegex);
    std::sregex_iterator end;

    while (it != end) {
        std::smatch match = *it;
        std::string domain = match.str();
        result.replace(match.position(), domain.length(), "[***]");
        ++it;
    }

    return wxString::FromUTF8(result);
}

bool GUI_App::OnExceptionInMainLoop()
{
    generic_exception_handle();
    return false;
}

#ifdef __APPLE__
void GUI_App::OSXStoreOpenFiles(const wxArrayString &fileNames)
{
    wxApp::OSXStoreOpenFiles(fileNames);
}

void GUI_App::MacOpenURL(const wxString& url)
{
    if (url.empty())
        return;
    start_download(into_u8(url));
}

void GUI_App::MacOpenFiles(const wxArrayString &fileNames)
{
    bool single_instance = app_config->get("app", "single_instance") == "true";
    if (m_post_initialized && !single_instance) {
        bool has3mf = false;
        std::vector<wxString> names;
        for (auto & n : fileNames) {
            has3mf |= n.EndsWith(".3mf");
            names.push_back(n);
        }
        if (has3mf) {
            start_new_slicer(names);
            return;
        }
    }
    std::vector<std::string> files;
    std::vector<wxString>    gcode_files;
    std::vector<wxString>    non_gcode_files;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", open files, size " << fileNames.size();
    for (const auto& filename : fileNames) {
        if (is_gcode_file(into_u8(filename)))
            gcode_files.emplace_back(filename);
        else {
            files.emplace_back(into_u8(filename));
            non_gcode_files.emplace_back(filename);
        }
    }
    {
        if (! files.empty()) {
            if (m_post_initialized) {
                wxArrayString input_files;
                for (size_t i = 0; i < non_gcode_files.size(); ++i) {
                    input_files.push_back(non_gcode_files[i]);
                }
                this->plater()->load_files(input_files);
            } else {
                for (size_t i = 0; i < files.size(); ++i) {
                    this->init_params->input_files.emplace_back(files[i]);
                }
            }
        } else {
            if (m_post_initialized) {
                this->plater()->load_gcode(gcode_files.front());
            } else {
                this->init_params->input_gcode = true;
                this->init_params->input_files = { into_u8(gcode_files.front()) };
            }
        }
    }
}

#endif

Sidebar& GUI_App::sidebar()
{
    return plater_->sidebar();
}

GizmoObjectManipulation *GUI_App::obj_manipul()
{
    return (plater_ != nullptr) ? &plater_->get_view3D_canvas3D()->get_gizmos_manager().get_object_manipulation() : nullptr;
}

ObjectSettings* GUI_App::obj_settings()
{
    return sidebar().obj_settings();
}

ObjectList* GUI_App::obj_list()
{
    return sidebar().obj_list();
}

ObjectLayers* GUI_App::obj_layers()
{
    return sidebar().obj_layers();
}

Plater* GUI_App::plater()
{
    return plater_;
}

const Plater* GUI_App::plater() const
{
    return plater_;
}

ParamsPanel* GUI_App::params_panel()
{
    if (mainframe)
        return mainframe->m_param_panel;
    return nullptr;
}

ParamsDialog* GUI_App::params_dialog()
{
    if (mainframe)
        return mainframe->m_param_dialog;
    return nullptr;
}

Model& GUI_App::model()
{
    return plater_->model();
}

Downloader* GUI_App::downloader()
{
    return m_downloader.get();
}

void GUI_App::load_url(wxString url)
{
    if (mainframe)
        return mainframe->load_url(url);
}

void GUI_App::open_mall_page_dialog()
{
    std::string host_url;
    std::string model_url;
    std::string link_url;

    int result = -1;

    host_url = get_model_http_url(app_config->get_country_code());

    wxString language_code = this->current_language_code().BeforeFirst('_');
    model_url = language_code.ToStdString();

    if (getAgent() && mainframe) {

        if (getAgent()->is_user_login()) {
            std::string ticket;
            result = getAgent()->request_bind_ticket(&ticket);

            if(result == 0){
                link_url = host_url + "api/sign-in/ticket?to=" + host_url + url_encode(model_url) + "&ticket=" + ticket;
            }
        }
    }

    if (result < 0) {
       link_url = host_url + model_url;
    }

    if (link_url.find("?") != std::string::npos) {
        link_url += "&from=orcaslicer";
    } else {
        link_url += "?from=orcaslicer";
    }

    wxLaunchDefaultBrowser(link_url);
}

void GUI_App::open_publish_page_dialog()
{
    std::string host_url;
    std::string model_url;
    std::string link_url;

    int result = -1;

    host_url = get_model_http_url(app_config->get_country_code());

    wxString language_code = this->current_language_code().BeforeFirst('_');
    model_url += (language_code.ToStdString() + "/my/models/publish");

    if (getAgent() && mainframe) {

        if (getAgent()->is_user_login()) {
            std::string ticket;
            result = getAgent()->request_bind_ticket(&ticket);

            if (result == 0) {
                link_url = host_url + "api/sign-in/ticket?to=" + host_url + url_encode(model_url) + "&ticket=" + ticket;
            }
        }
    }

    if (result < 0) {
        link_url = host_url + model_url;
    }

    wxLaunchDefaultBrowser(link_url);
}

char GUI_App::from_hex(char ch) {
    return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

std::string GUI_App::url_decode(std::string value) {
    return Http::url_decode(value);
}

std::string GUI_App::url_encode(std::string value) {
    return Http::url_encode(value);
}

void GUI_App::popup_ping_bind_dialog()
{
    if (m_ping_code_binding_dialog == nullptr) {
        m_ping_code_binding_dialog = new PingCodeBindDialog();
        m_ping_code_binding_dialog->ShowModal();
        remove_ping_bind_dialog();
    }
}

void GUI_App::remove_ping_bind_dialog()
{
    if (m_ping_code_binding_dialog != nullptr) {
        m_ping_code_binding_dialog->Destroy();
        delete m_mall_publish_dialog;
        m_ping_code_binding_dialog = nullptr;
    }
}


void GUI_App::remove_mall_system_dialog()
{
    if (m_mall_publish_dialog != nullptr) {
        m_mall_publish_dialog->Destroy();
        delete m_mall_publish_dialog;
    }
}

void GUI_App::run_script(wxString js)
{
    if (mainframe)
        return mainframe->RunScript(js);
}

Notebook* GUI_App::tab_panel() const
{
    if (mainframe)
        return mainframe->m_tabpanel;
    return nullptr;
}

NotificationManager * GUI_App::notification_manager()
{
    if (plater_)
        return plater_->get_notification_manager();
    return nullptr;
}

int GUI_App::extruders_cnt() const
{
    const Preset& preset = preset_bundle->printers.get_selected_preset();
    return preset.printer_technology() == ptSLA ? 1 :
           preset.config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();
}

int GUI_App::extruders_edited_cnt() const
{
    const Preset& preset = preset_bundle->printers.get_edited_preset();
    return preset.printer_technology() == ptSLA ? 1 :
           preset.config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();
}

int GUI_App::filaments_cnt() const
{
    return preset_bundle->filament_presets.size();
}

PrintSequence GUI_App::global_print_sequence() const
{
    PrintSequence global_print_seq = PrintSequence::ByDefault;
    const auto &curr_preset_config = preset_bundle->prints.get_edited_preset().config;
    if (curr_preset_config.has("print_sequence"))
        global_print_seq = curr_preset_config.option<ConfigOptionEnum<PrintSequence>>("print_sequence")->value;
    return global_print_seq;
}

wxString GUI_App::current_language_code_safe() const
{
	const std::map<wxString, wxString> mapping {
		{ "cs", 	"cs_CZ", },
		{ "sk", 	"cs_CZ", },
		{ "de", 	"de_DE", },
		{ "nl", 	"nl_NL", },
		{ "sv", 	"sv_SE", },
		{ "es", 	"es_ES", },
		{ "fr", 	"fr_FR", },
		{ "it", 	"it_IT", },
		{ "ja", 	"ja_JP", },
		{ "ko", 	"ko_KR", },
		{ "pl", 	"pl_PL", },
		{ "uk", 	"uk_UA", },
		{ "zh", 	"zh_CN", },
		{ "ru", 	"ru_RU", },
        { "tr", 	"tr_TR", },
        { "pt", 	"pt_BR", },
        { "lt", 	"lt_LT", },
        { "vi", 	"vi_VN", },
        { "th", 	"th_TH", },
	};
	wxString language_code = this->current_language_code().BeforeFirst('_');
	auto it = mapping.find(language_code);
	if (it != mapping.end())
		language_code = it->second;
	else
		language_code = "en_US";
	return language_code;
}

void GUI_App::open_web_page_localized(const std::string &http_address)
{
    open_browser_with_warning_dialog(http_address + "&lng=" + this->current_language_code_safe());
}

bool GUI_App::may_switch_to_SLA_preset(const wxString& caption)
{
    if (model_has_multi_part_objects(model())) {
        return false;
    }
    return true;
}

bool GUI_App::run_wizard(ConfigWizard::RunReason reason, ConfigWizard::StartPage start_page)
{
    wxCHECK_MSG(mainframe != nullptr, false, "Internal error: Main frame not created / null");

#ifdef __APPLE__
     if (is_adding_script_handler()) {
        BOOST_LOG_TRIVIAL(info) << "run_wizard: Script handler is being added, delaying wizard creation";
        auto timer = new wxTimer();
        timer->Bind(wxEVT_TIMER, [this, reason, start_page, timer](wxTimerEvent &) {
            timer->Stop();
            run_wizard(reason, start_page);
            delete timer;
        });
        timer->StartOnce(200);

        return true;
    }
#endif

    std::string strFinish = wxGetApp().app_config->get("firstguide", "finish");
    long        pStyle    = wxCAPTION | wxCLOSE_BOX | wxSYSTEM_MENU;
    if (strFinish == "false" || strFinish.empty())
        pStyle = wxCAPTION | wxTAB_TRAVERSAL;

    GuideFrame wizard(this, pStyle);
    auto page = start_page == ConfigWizard::SP_WELCOME ? GuideFrame::BBL_WELCOME :
                start_page == ConfigWizard::SP_FILAMENTS ? GuideFrame::BBL_FILAMENT_ONLY :
                start_page == ConfigWizard::SP_PRINTERS ? GuideFrame::BBL_MODELS_ONLY :
                GuideFrame::BBL_MODELS;
    wizard.SetStartPage(page);
    bool       res = wizard.run();

    if (res) {
        load_current_presets();
        update_publish_status();
        mainframe->refresh_plugin_tips();
    }

    return res;
}

void GUI_App::show_desktop_integration_dialog()
{
#ifdef __linux__
    DesktopIntegrationDialog dialog(mainframe);
    dialog.ShowModal();
#endif
}

#if ENABLE_THUMBNAIL_GENERATOR_DEBUG
void GUI_App::gcode_thumbnails_debug()
{
    const std::string BEGIN_MASK = "; thumbnail begin";
    const std::string END_MASK = "; thumbnail end";
    std::string gcode_line;
    bool reading_image = false;
    unsigned int width = 0;
    unsigned int height = 0;

    wxFileDialog dialog(GetTopWindow(), _L("Select a G-code file:"), "", "", "G-code files (*.gcode)|*.gcode;*.GCODE;", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
        return;

    std::string in_filename = into_u8(dialog.GetPath());
    std::string out_path = boost::filesystem::path(in_filename).remove_filename().append(L"thumbnail").string();

    boost::nowide::ifstream in_file(in_filename.c_str());
    std::vector<std::string> rows;
    std::string row;
    if (in_file.good())
    {
        while (std::getline(in_file, gcode_line))
        {
            if (in_file.good())
            {
                if (boost::starts_with(gcode_line, BEGIN_MASK))
                {
                    reading_image = true;
                    gcode_line = gcode_line.substr(BEGIN_MASK.length() + 1);
                    std::string::size_type x_pos = gcode_line.find('x');
                    std::string width_str = gcode_line.substr(0, x_pos);
                    width = (unsigned int)::atoi(width_str.c_str());
                    std::string height_str = gcode_line.substr(x_pos + 1);
                    height = (unsigned int)::atoi(height_str.c_str());
                    row.clear();
                }
                else if (reading_image && boost::starts_with(gcode_line, END_MASK))
                {
                    std::string out_filename = out_path + std::to_string(width) + "x" + std::to_string(height) + ".png";
                    boost::nowide::ofstream out_file(out_filename.c_str(), std::ios::binary);
                    if (out_file.good())
                    {
                        std::string decoded;
                        decoded.resize(boost::beast::detail::base64::decoded_size(row.size()));
                        decoded.resize(boost::beast::detail::base64::decode((void*)&decoded[0], row.data(), row.size()).first);

                        out_file.write(decoded.c_str(), decoded.size());
                        out_file.close();
                    }

                    reading_image = false;
                    width = 0;
                    height = 0;
                    rows.clear();
                } else if (reading_image)
                    row += gcode_line.substr(2);
            }
        }

        in_file.close();
    }
}
#endif

void GUI_App::window_pos_save(wxTopLevelWindow* window, const std::string &name)
{
    if (name.empty()) { return; }
    const auto config_key = (boost::format("window_%1%") % name).str();

    WindowMetrics metrics = WindowMetrics::from_window(window);
    app_config->set(config_key, metrics.serialize());
    app_config->save();
}

bool GUI_App::window_pos_restore(wxTopLevelWindow* window, const std::string &name, bool default_maximized)
{
    if (name.empty()) { return false; }
    const auto config_key = (boost::format("window_%1%") % name).str();

    if (! app_config->has(config_key)) {
        return false;
    }

    auto metrics = WindowMetrics::deserialize(app_config->get(config_key));
    if (! metrics) {
        window->Maximize(default_maximized);
        return true;
    }

    const wxRect& rect = metrics->get_rect();
#if defined(__WXGTK__)
    if (!Slic3r::GUI::is_running_on_wayland())
#endif
        window->SetPosition(rect.GetPosition());
    window->SetSize(rect.GetSize());
    window->Maximize(metrics->get_maximized());
    return true;
}

void GUI_App::window_pos_sanitize(wxTopLevelWindow* window)
{
    int display_idx = wxDisplay::GetFromWindow(window);
    wxRect display;
    if (display_idx == wxNOT_FOUND) {
        display = wxDisplay(0u).GetClientArea();
        window->Move(display.GetTopLeft());
    } else {
        display = wxDisplay(display_idx).GetClientArea();
    }

    auto metrics = WindowMetrics::from_window(window);
    metrics.sanitize_for_display(display);
    if (window->GetScreenRect() != metrics.get_rect()) {
        window->SetSize(metrics.get_rect());
    }
}

void GUI_App::window_pos_center(wxTopLevelWindow *window)
{
    int display_idx = wxDisplay::GetFromWindow(window);
    wxRect display;
    if (display_idx == wxNOT_FOUND) {
        display = wxDisplay(0u).GetClientArea();
        window->Move(display.GetTopLeft());
    } else {
        display = wxDisplay(display_idx).GetClientArea();
    }

    auto metrics = WindowMetrics::from_window(window);
    metrics.center_for_display(display);
    if (window->GetScreenRect() != metrics.get_rect()) {
        window->SetSize(metrics.get_rect());
    }
}

bool GUI_App::config_wizard_startup()
{
    if (!m_app_conf_exists || preset_bundle->printers.only_default_printers()) {
        BOOST_LOG_TRIVIAL(info) << "run wizard...";
        run_wizard(ConfigWizard::RR_DATA_EMPTY);
        BOOST_LOG_TRIVIAL(info) << "finished run wizard";
        return true;
    }
    return false;
}

void GUI_App::check_updates(const bool verbose)
{
	PresetUpdater::UpdateResult updater_result;
	try {
		updater_result = preset_updater->config_update(app_config->orig_version(), verbose ? PresetUpdater::UpdateParams::SHOW_TEXT_BOX : PresetUpdater::UpdateParams::SHOW_NOTIFICATION);
		if (updater_result == PresetUpdater::R_INCOMPAT_EXIT) {
			mainframe->Close();
		}
		else if (updater_result == PresetUpdater::R_INCOMPAT_CONFIGURED) {
            m_app_conf_exists = true;
		}
		else if (verbose && updater_result == PresetUpdater::R_NOOP) {
			MsgNoUpdates dlg;
			dlg.ShowModal();
		}
	}
	catch (const std::exception & ex) {
		show_error(nullptr, ex.what());
	}
}


FilamentColorCodeQuery* GUI_App::get_filament_color_code_query()
{
    if (!m_filament_color_code_query)
    {
        m_filament_color_code_query = new FilamentColorCodeQuery();
    }

    return m_filament_color_code_query;
}

bool GUI_App::open_browser_with_warning_dialog(const wxString& url, int flags )
{
    return wxLaunchDefaultBrowser(url, flags);
}

#ifdef __WXMSW__
static bool set_into_win_registry(HKEY hkeyHive, const wchar_t* pszVar, const wchar_t* pszValue)
{
    wchar_t szValueCurrent[1000];
    DWORD dwType;
    DWORD dwSize = sizeof(szValueCurrent);

    int iRC = ::RegGetValueW(hkeyHive, pszVar, nullptr, RRF_RT_ANY, &dwType, szValueCurrent, &dwSize);

    bool bDidntExist = iRC == ERROR_FILE_NOT_FOUND;

    if ((iRC != ERROR_SUCCESS) && !bDidntExist)
        return false;

    if (!bDidntExist) {
        if (dwType != REG_SZ)
            return false;

        if (::wcscmp(szValueCurrent, pszValue) == 0)
            return false;
    }

    DWORD dwDisposition;
    HKEY hkey;
    iRC = ::RegCreateKeyExW(hkeyHive, pszVar, 0, 0, 0, KEY_ALL_ACCESS, nullptr, &hkey, &dwDisposition);
    bool ret = false;
    if (iRC == ERROR_SUCCESS) {
        iRC = ::RegSetValueExW(hkey, L"", 0, REG_SZ, (BYTE*)pszValue, (::wcslen(pszValue) + 1) * sizeof(wchar_t));
        if (iRC == ERROR_SUCCESS)
            ret = true;
    }

    RegCloseKey(hkey);
    return ret;
}

static bool del_win_registry(HKEY hkeyHive, const wchar_t *pszVar, const wchar_t *pszValue)
{
    wchar_t szValueCurrent[1000];
    DWORD   dwType;
    DWORD   dwSize = sizeof(szValueCurrent);

    int iRC = ::RegGetValueW(hkeyHive, pszVar, nullptr, RRF_RT_ANY, &dwType, szValueCurrent, &dwSize);

    bool bDidntExist = iRC == ERROR_FILE_NOT_FOUND;

    if ((iRC != ERROR_SUCCESS) && !bDidntExist)
        return false;

    if (!bDidntExist) {
        iRC      = ::RegDeleteKeyExW(hkeyHive, pszVar, KEY_ALL_ACCESS, 0);
        if (iRC == ERROR_SUCCESS) {
            return true;
        }
    }

    return false;
}

#endif

void GUI_App::associate_files(std::wstring extend)
{
#ifdef WIN32
    if (is_running_in_msix())
        return;
    wchar_t app_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, app_path, sizeof(app_path));

    std::wstring prog_path = L"\"" + std::wstring(app_path) + L"\"";
    std::wstring prog_id = L" Orca.Slicer.1";
    std::wstring prog_desc = L"OrcaSlicer";
    std::wstring prog_command = prog_path + L" \"%1\"";
    std::wstring reg_base = L"Software\\Classes";
    std::wstring reg_extension = reg_base + L"\\." + extend;
    std::wstring reg_prog_id = reg_base + L"\\" + prog_id;
    std::wstring reg_prog_id_command = reg_prog_id + L"\\Shell\\Open\\Command";

    bool is_new = false;
    is_new |= set_into_win_registry(HKEY_CURRENT_USER, reg_extension.c_str(), prog_id.c_str());
    is_new |= set_into_win_registry(HKEY_CURRENT_USER, reg_prog_id.c_str(), prog_desc.c_str());
    is_new |= set_into_win_registry(HKEY_CURRENT_USER, reg_prog_id_command.c_str(), prog_command.c_str());
    if (is_new)
        ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#endif
}

void GUI_App::disassociate_files(std::wstring extend)
{
#ifdef WIN32
    if (is_running_in_msix())
        return;
    wchar_t app_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, app_path, sizeof(app_path));

    std::wstring prog_path = L"\"" + std::wstring(app_path) + L"\"";
    std::wstring prog_id = L" Orca.Slicer.1";
    std::wstring prog_desc = L"OrcaSlicer";
    std::wstring prog_command = prog_path + L" \"%1\"";
    std::wstring reg_base = L"Software\\Classes";
    std::wstring reg_extension = reg_base + L"\\." + extend;
    std::wstring reg_prog_id = reg_base + L"\\" + prog_id;
    std::wstring reg_prog_id_command = reg_prog_id + L"\\Shell\\Open\\Command";

    bool is_new = false;
    is_new |= del_win_registry(HKEY_CURRENT_USER, reg_extension.c_str(), prog_id.c_str());

    bool is_associate_3mf  = app_config->get("associate_3mf") == "true";
    bool is_associate_stl  = app_config->get("associate_stl") == "true";
    bool is_associate_step = app_config->get("associate_step") == "true";
    if (!is_associate_3mf && !is_associate_stl && !is_associate_step)
    {
        is_new |= del_win_registry(HKEY_CURRENT_USER, reg_prog_id.c_str(), prog_desc.c_str());
        is_new |= del_win_registry(HKEY_CURRENT_USER, reg_prog_id_command.c_str(), prog_command.c_str());
    }

    if (is_new)
       ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#endif
}

bool GUI_App::check_url_association(std::wstring url_prefix, std::wstring& reg_bin)
{
    reg_bin = L"";
#ifdef WIN32
    wxRegKey key_full(wxRegKey::HKCU, L"Software\\Classes\\" + url_prefix + L"\\shell\\open\\command");
    if (!key_full.Exists()) {
        return false;
    }
    wxString reg_value = key_full.QueryDefaultValue();
    reg_bin = reg_value.ToStdWstring();

    boost::filesystem::path binary_path(boost::filesystem::canonical(boost::dll::program_location()));
    wxString key_string = "\"" + from_path(binary_path) + "\" \"%1\"";
    return key_string == reg_value;
#else
    return false;
#endif
}

void GUI_App::associate_url(std::wstring url_prefix)
{
#ifdef WIN32
    if (is_running_in_msix())
        return;
    boost::filesystem::path binary_path(boost::filesystem::canonical(boost::dll::program_location()));
    wxString wbinary = from_path(binary_path);
    BOOST_LOG_TRIVIAL(info) << "Downloader registration: Path of binary: " << wbinary.ToUTF8().data();

    wxString key_string = "\"" + wbinary + "\" \"%1\"";

    wxRegKey key_first(wxRegKey::HKCU, L"Software\\Classes\\" + url_prefix);
    wxRegKey key_full(wxRegKey::HKCU, L"Software\\Classes\\" + url_prefix + L"\\shell\\open\\command");
    if (!key_first.Exists()) {
        key_first.Create(false);
    }
    key_first.SetValue("URL Protocol", "");

    if (!key_full.Exists()) {
        key_full.Create(false);
    }
    key_full = key_string;
#elif defined(__linux__) && defined(SLIC3R_DESKTOP_INTEGRATION)
    DesktopIntegrationDialog::perform_downloader_desktop_integration(into_u8(url_prefix));
#endif
}

void GUI_App::disassociate_url(std::wstring url_prefix)
{
#ifdef WIN32
    if (is_running_in_msix())
        return;
    wxRegKey key_full(wxRegKey::HKCU, L"Software\\Classes\\" + url_prefix + L"\\shell\\open\\command");
    if (!key_full.Exists()) {
        return;
    }
    key_full = "";
#endif
}


void GUI_App::start_download(std::string url)
{
    if (!plater_) {
        BOOST_LOG_TRIVIAL(error) << "Could not start URL download: plater is nullptr.";
        return;
    }
    boost::filesystem::path dest_folder(app_config->get("download_path"));
    if (dest_folder.empty() || !boost::filesystem::is_directory(dest_folder)) {
        std::string msg = _u8L("Could not start URL download. Destination folder is not set. Please choose destination folder in Configuration Wizard.");
        BOOST_LOG_TRIVIAL(error) << msg;
        show_error(nullptr, msg);
        return;
    }
    m_downloader->init(dest_folder);
    m_downloader->start_download(url);

}

bool is_soluble_filament(int extruder_id)
{
    auto &filament_presets = Slic3r::GUI::wxGetApp().preset_bundle->filament_presets;
    auto &filaments        = Slic3r::GUI::wxGetApp().preset_bundle->filaments;

    if (extruder_id >= filament_presets.size()) return false;

    Slic3r::Preset *filament = filaments.find_preset(filament_presets[extruder_id]);
    if (filament == nullptr) return false;

    Slic3r::ConfigOptionBools *support_option = dynamic_cast<Slic3r::ConfigOptionBools *>(filament->config.option("filament_soluble"));
    if (support_option == nullptr) return false;

    return support_option->get_at(0);
};

bool has_filaments(const std::vector<std::string>& model_filaments) {
    auto &filament_presets = Slic3r::GUI::wxGetApp().preset_bundle->filament_presets;
    if (!Slic3r::GUI::wxGetApp().plater()) return false;
    auto model_objects = Slic3r::GUI::wxGetApp().plater()->model().objects;
    const Slic3r::DynamicPrintConfig &config = wxGetApp().preset_bundle->full_config();
    Model::setExtruderParams(config, filament_presets.size());

    auto get_filament_name = [](int id) { return Model::extruderParamsMap.find(id) != Model::extruderParamsMap.end() ? Model::extruderParamsMap.at(id).materialName : "PLA"; };
    for (const ModelObject *mo : model_objects) {
        for (auto vol : mo->volumes) {
            auto ve = vol->get_extruders();
            for (auto id : ve) {
                auto name = get_filament_name(id);
                if (find(model_filaments.begin(), model_filaments.end(), name) != model_filaments.end()) return true;
            }
        }
    }
    return false;
}

bool is_support_filament(int extruder_id, bool strict_check)
{
    auto &filament_presets = Slic3r::GUI::wxGetApp().preset_bundle->filament_presets;
    auto &filaments        = Slic3r::GUI::wxGetApp().preset_bundle->filaments;

    if (extruder_id >= filament_presets.size()) return false;

    Slic3r::Preset *filament = filaments.find_preset(filament_presets[extruder_id]);
    if (filament == nullptr) return false;

    std::string filament_type = filament->config.option<ConfigOptionStrings>("filament_type")->values[0];

    Slic3r::ConfigOptionBools *support_option = dynamic_cast<Slic3r::ConfigOptionBools *>(filament->config.option("filament_is_support"));

    if(!strict_check &&(filament_type == "PETG" || filament_type == "PLA")) {
        std::vector<std::string> model_filaments;
        if (filament_type == "PETG")
            model_filaments.emplace_back("PLA");
        else {
            model_filaments = {"PETG", "TPU", "TPU-AMS"};
        }
        if (has_filaments(model_filaments)) return true;
    }
    if (support_option == nullptr) return false;
    return support_option->get_at(0);
};

Vec3d build_plate_tilt_up_direction()
{
    const DynamicPrintConfig &cfg    = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    const auto               *opt_x  = cfg.option<ConfigOptionFloat>("build_plate_tilt_x");
    const auto               *opt_y  = cfg.option<ConfigOptionFloat>("build_plate_tilt_y");
    const double              tilt_x = opt_x != nullptr ? opt_x->value : 0.;
    const double              tilt_y = opt_y != nullptr ? opt_y->value : 0.;
    if (tilt_x == 0. && tilt_y == 0.)
        return Vec3d::UnitZ();
    return Vec3d(std::tan(Geometry::deg2rad(tilt_y)), std::tan(Geometry::deg2rad(tilt_x)), 1.).normalized();
}

}
}
