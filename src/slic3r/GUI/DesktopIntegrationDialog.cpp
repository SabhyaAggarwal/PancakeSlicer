#include <string>
#include <vector>
#include <cstddef>
#include <wx/string.h>
#include <wx/utils.h>
#include <boost/nowide/convert.hpp>
#include <boost/filesystem/operations.hpp>
#include <fstream>
#include <cstdlib>
#include <exception>
#include "libslic3r_version.h"
#include <cstdio>
#include <cassert>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/dialog.h>
#include <wx/toplevel.h>
#include <wx/sizer.h>
#include "slic3r/GUI/Plater.hpp"
#ifdef __linux__
#include "DesktopIntegrationDialog.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "format.hpp"
#include "I18N.hpp"
#include "NotificationManager.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Platform.hpp"
#include "libslic3r/Config.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <wx/filename.h>
#include <wx/stattext.h>

namespace Slic3r {
namespace GUI {

namespace {

std::string escape_string(const std::string& str)
{
    std::vector<char> out(str.size() * 4, 0);
    char *outptr = out.data();
    for (size_t i = 0; i < str.size(); ++ i) {
        char c = str[i];
        if (c == '\"') {
            (*outptr ++) = '\\';
            (*outptr ++) = '\"';
        } else if (c == '`') {
            (*outptr ++) = '\\';
            (*outptr ++) = '`';
        } else if (c == '$') {
            (*outptr ++) = '\\';
            (*outptr ++) = '$';
        } else if (c == '\\') {
            (*outptr ++) = '\\';
            (*outptr ++) = '\\';
            (*outptr ++) = '\\';
            (*outptr ++) = '\\';
        } else
            (*outptr ++) = c;
    }
    return std::string(out.data(), outptr - out.data());
}
void resolve_path_from_var(const std::string& var, std::vector<std::string>& paths)
{
    wxString wxdirs;
    if (! wxGetEnv(boost::nowide::widen(var), &wxdirs) || wxdirs.empty() )
        return;
    std::string dirs = into_u8(wxdirs);
    for (size_t i = dirs.find(':'); i != std::string::npos; i = dirs.find(':'))
    {
        paths.push_back(dirs.substr(0, i));
        if (dirs.size() > i+1)
            dirs = dirs.substr(i+1);
    }
    paths.push_back(dirs);
}
bool contains_path_dir(const std::string& p, const std::string& dir_name)
{
    if (p.empty() || dir_name.empty()) 
       return false;
    boost::filesystem::path path(p + (p[p.size()-1] == '/' ? "" : "/") + dir_name);
    if (boost::filesystem::exists(path) && boost::filesystem::is_directory(path)) {
        return true;
    } else
        BOOST_LOG_TRIVIAL(debug) << path.string() << " doesnt exists";
    return false;
}
void create_dir(const boost::filesystem::path& path)
{
    if (boost::filesystem::exists(path))
        return;
    BOOST_LOG_TRIVIAL(debug)<< "creating " << path.string();
    boost::system::error_code ec;
    boost::filesystem::create_directory(path, ec);
    if (ec)
        BOOST_LOG_TRIVIAL(error)<< "create directory failed: " << ec.message();
}
void create_path(const std::string& basic_path, const std::string& dir_path)
{
    if (basic_path.empty() || dir_path.empty())
       return;

    boost::filesystem::path path(basic_path);
    std::string dirs = dir_path;
    for (size_t i = dirs.find('/'); i != std::string::npos; i = dirs.find('/'))
    {
        std::string dir = dirs.substr(0, i);
        path = boost::filesystem::path(path.string() +"/"+ dir);
        create_dir(path);
        dirs = dirs.substr(i+1);
    }
    path = boost::filesystem::path(path.string() +"/"+ dirs);
    create_dir(path);
}
bool copy_icon(const std::string& icon_path, const std::string& dest_path)
{
    BOOST_LOG_TRIVIAL(debug) <<"icon from "<< icon_path;
    BOOST_LOG_TRIVIAL(debug) <<"icon to "<< dest_path;
    std::string error_message;
    auto cfr = copy_file(icon_path, dest_path, error_message, false);
    if (cfr) {
        BOOST_LOG_TRIVIAL(debug) << "Copy icon fail(" << cfr << "): " << error_message;
        return false;
    }
    BOOST_LOG_TRIVIAL(debug) << "Copy icon success.";
    return true;
}
bool create_desktop_file(const std::string& path, const std::string& data)
{    
    BOOST_LOG_TRIVIAL(debug) <<".desktop to "<< path;
    std::ofstream output(path);
    output << data;
    struct stat buffer;
    if (stat(path.c_str(), &buffer) == 0)
    {
        BOOST_LOG_TRIVIAL(debug) << "Desktop file created.";
        return true;
    }
    BOOST_LOG_TRIVIAL(debug) << "Desktop file NOT created.";
    return false;
}
}
bool DesktopIntegrationDialog::is_integrated()
{
    const AppConfig *app_config = wxGetApp().app_config;
    std::string path(app_config->get("desktop_integration_app_path"));
    BOOST_LOG_TRIVIAL(debug) << "Desktop integration desktop file path: " << path;

    if (path.empty())
        return false;

    struct stat buffer;   
    return (stat (path.c_str(), &buffer) == 0);
}
bool DesktopIntegrationDialog::integration_possible()
{
    return true;
}
void DesktopIntegrationDialog::perform_desktop_integration()
{
	BOOST_LOG_TRIVIAL(debug) << "performing desktop integration";

    const char *appimage_env = std::getenv("APPIMAGE");
    std::string excutable_path;
    if (appimage_env) {
        try {
            excutable_path = boost::filesystem::canonical(boost::filesystem::path(appimage_env)).string();
        } catch (std::exception &) {            
            BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed - boost::filesystem::canonical did not return appimage path.";
            show_error(nullptr, _L("Performing desktop integration failed - boost::filesystem::canonical did not return appimage path."));
            return;
        }
    } else {
        excutable_path = boost::dll::program_location().string();
        BOOST_LOG_TRIVIAL(debug) << "non-appimage path to executable: " << excutable_path;
        if (excutable_path.empty())
        {
            BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed - no executable found.";
            show_error(nullptr, _L("Performing desktop integration failed - Could not find executable."));
            return; 
        }
    }

    excutable_path = escape_string(excutable_path);

    std::vector<std::string>target_candidates;
    resolve_path_from_var("XDG_DATA_HOME", target_candidates);
    resolve_path_from_var("XDG_DATA_DIRS", target_candidates);

    AppConfig *app_config = wxGetApp().app_config;
    
    std::string version_suffix;
    std::string name_suffix;
    std::string version(SLIC3R_VERSION);
    if (version.find("alpha") != std::string::npos)
    {
        version_suffix = "-alpha";
        name_suffix = " - alpha";
    }else if (version.find("beta") != std::string::npos)
    {
        version_suffix = "-beta";
        name_suffix = " - beta";
    }

    std::string icon_theme_path;
    std::string icon_theme_dirs;

    if (platform_flavor() == PlatformFlavor::LinuxOnChromium) {
        icon_theme_path = "hicolor/96x96/apps/";
        icon_theme_dirs = "/hicolor/96x96/apps";
    }
    
    std::string target_dir_icons;
    std::string target_dir_desktop;
    
    for (size_t i = 0; i < target_candidates.size(); ++i) {
        if (contains_path_dir(target_candidates[i], "images")) {
            target_dir_icons = target_candidates[i];
            std::string icon_path = GUI::format("%1%/images/OrcaSlicer.png",resources_dir());
            std::string dest_path = GUI::format("%1%/images/%2%OrcaSlicer%3%.png", target_dir_icons, icon_theme_path, version_suffix);
            if (copy_icon(icon_path, dest_path))
                break;
            else
                target_dir_icons.clear();
            if (i == target_candidates.size() - 1) {
                create_path(into_u8(wxFileName::GetHomeDir()), ".local/share/icons" + icon_theme_dirs);
                target_dir_icons = GUI::format("%1%/.local/share",wxFileName::GetHomeDir());
                std::string icon_path = GUI::format("%1%/images/OrcaSlicer.png",resources_dir());
                std::string dest_path = GUI::format("%1%/images/%2%OrcaSlicer%3%.png", target_dir_icons, icon_theme_path, version_suffix);
                if (!contains_path_dir(target_dir_icons, "images") 
                    || !copy_icon(icon_path, dest_path)) {
                    target_dir_icons.clear(); 
                }
            }
        }
    }
    if(target_dir_icons.empty()) {
        BOOST_LOG_TRIVIAL(error) << "Copying OrcaSlicer icon to icons directory failed.";
    } else 
        app_config->set("desktop_integration_icon_slicer_path", GUI::format("%1%/images/%2%OrcaSlicer%3%.png", target_dir_icons, icon_theme_path, version_suffix));

    for (size_t i = 0; i < target_candidates.size(); ++i)
    {
        if (contains_path_dir(target_candidates[i], "applications")) {
            target_dir_desktop = target_candidates[i];
            std::string desktop_file = GUI::format(
                "[Desktop Entry]\n"
                "Name=OrcaSlicer%1%\n"
                "GenericName=3D Printing Software\n"
                "Icon=OrcaSlicer%2%\n"
                "Exec=\"%3%\" %%F\n"
                "Terminal=false\n"
                "Type=Application\n"
                "PrefersNonDefaultGPU=true\n"
                "X-KDE-RunOnDiscreteGpu=true\n"
                "MimeType=model/stl;application/vnd.ms-3mfdocument;application/prs.wavefront-obj;application/x-amf;\n"
                "Categories=Graphics;3DGraphics;Engineering;\n"
                "Keywords=3D;Printing;Slicer;slice;3D;printer;convert;gcode;stl;obj;amf;SLA\n"
                "StartupNotify=false\n"
                "StartupWMClass=orca-slicer\n", name_suffix, version_suffix, excutable_path);

            std::string path = GUI::format("%1%/applications/com.orcaslicer.OrcaSlicer%2%.desktop", target_dir_desktop, version_suffix);
            if (create_desktop_file(path, desktop_file)){
                BOOST_LOG_TRIVIAL(debug) << "com.orcaslicer.OrcaSlicer.desktop file installation success.";
                break;
            } else {
                BOOST_LOG_TRIVIAL(debug) << "Attempt to com.orcaslicer.OrcaSlicer.desktop file installation failed. failed path: " << target_candidates[i];
                target_dir_desktop.clear(); 
            }
            if (i == target_candidates.size() - 1) {
                create_path(into_u8(wxFileName::GetHomeDir()), ".local/share/applications");
                target_dir_desktop = GUI::format("%1%/.local/share",wxFileName::GetHomeDir());
                std::string path = GUI::format("%1%/applications/com.orcaslicer.OrcaSlicer%2%.desktop", target_dir_desktop, version_suffix);
                if (contains_path_dir(target_dir_desktop, "applications")) {
                    if (!create_desktop_file(path, desktop_file)) {    
                        BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed - could not create desktop file";
                        return;
                    }
                } else {
                    BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed because the application directory was not found.";
                    return;
                }
            }
        }
    }
    if(target_dir_desktop.empty()) {
        BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed because the application directory was not found.";
        show_error(nullptr, _L("Performing desktop integration failed because the application directory was not found."));
        return;
    }
    app_config->set("desktop_integration_app_path", GUI::format("%1%/applications/com.orcaslicer.OrcaSlicer%2%.desktop", target_dir_desktop, version_suffix));

    if (platform_flavor() != PlatformFlavor::LinuxOnChromium) {
        if (!target_dir_icons.empty())
        {
            std::string icon_path = GUI::format("%1%/images/OrcaSlicer-gcodeviewer_192px.png",resources_dir());
            std::string dest_path = GUI::format("%1%/images/%2%OrcaSlicer-gcodeviewer%3%.png", target_dir_icons, icon_theme_path, version_suffix);
            if (copy_icon(icon_path, dest_path))
                app_config->set("desktop_integration_icon_viewer_path", dest_path);
            else
                BOOST_LOG_TRIVIAL(error) << "Copying Gcode Viewer icon to icons directory failed.";
        }

        std::string desktop_file = GUI::format(
            "[Desktop Entry]\n"
            "Name=Bambu Gcode Viewer%1%\n"
            "GenericName=3D Printing Software\n"
            "Icon=OrcaSlicer-gcodeviewer%2%\n"
            "Exec=\"%3%\" --gcodeviewer %%F\n"
            "Terminal=false\n"
            "Type=Application\n"
            "PrefersNonDefaultGPU=true\n"
            "X-KDE-RunOnDiscreteGpu=true\n"
            "MimeType=text/x.gcode;\n"
            "Categories=Graphics;3DGraphics;\n"
            "Keywords=3D;Printing;Slicer;\n"
            "StartupNotify=false\n", name_suffix, version_suffix, excutable_path);

        std::string desktop_path = GUI::format("%1%/applications/OrcaSlicerGcodeViewer%2%.desktop", target_dir_desktop, version_suffix);
        if (create_desktop_file(desktop_path, desktop_file))
            app_config->set("desktop_integration_app_viewer_path", desktop_path);
        else {
            BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed - could not create Gcodeviewer desktop file";
            show_error(nullptr, _L("Performing desktop integration failed - could not create Gcodeviewer desktop file. OrcaSlicer desktop file was probably created successfully."));
        }
    }
    
    wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::DesktopIntegrationSuccess);
}
void DesktopIntegrationDialog::undo_desktop_intgration()
{
    const AppConfig *app_config = wxGetApp().app_config;
    std::string path = std::string(app_config->get("desktop_integration_app_path"));
    if (!path.empty()) {
    	BOOST_LOG_TRIVIAL(debug) << "removing " << path;
        std::remove(path.c_str());  
    }
    path = std::string(app_config->get("desktop_integration_icon_slicer_path"));
    if (!path.empty()) {
    	BOOST_LOG_TRIVIAL(debug) << "removing " << path;
        std::remove(path.c_str());
    }
    if (platform_flavor() != PlatformFlavor::LinuxOnChromium) {
        path = std::string(app_config->get("desktop_integration_app_viewer_path"));
        if (!path.empty()) {
        	BOOST_LOG_TRIVIAL(debug) << "removing " << path;
            std::remove(path.c_str());
        }
        path = std::string(app_config->get("desktop_integration_icon_viewer_path"));
        if (!path.empty()) {
        	BOOST_LOG_TRIVIAL(debug) << "removing " << path;
            std::remove(path.c_str());
        }
    }
    wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::UndoDesktopIntegrationSuccess);
}

void DesktopIntegrationDialog::perform_downloader_desktop_integration(std::string url_prefix)
{
    BOOST_LOG_TRIVIAL(debug) << "performing downloader desktop integration. " << url_prefix ;
    const char* appimage_env = std::getenv("APPIMAGE");
    std::string excutable_path;
    if (appimage_env) {
        try {
            excutable_path = boost::filesystem::canonical(boost::filesystem::path(appimage_env)).string();
        }
        catch (std::exception&) {
            BOOST_LOG_TRIVIAL(error) << "Performing downloader desktop integration failed - boost::filesystem::canonical did not return appimage path.";
            show_error(nullptr, _L("Performing downloader desktop integration failed - boost::filesystem::canonical did not return appimage path."));
            return;
        }
    }
    else {
        excutable_path = boost::dll::program_location().string();
        BOOST_LOG_TRIVIAL(debug) << "non-appimage path to executable: " << excutable_path;
        if (excutable_path.empty())
        {
            BOOST_LOG_TRIVIAL(error) << "Performing downloader desktop integration failed - no executable found.";
            show_error(nullptr, _L("Performing downloader desktop integration failed - Could not find executable."));
            return;
        }
    }

    excutable_path = escape_string(excutable_path);

    std::vector<std::string>target_candidates;
    resolve_path_from_var("XDG_DATA_HOME", target_candidates);
    resolve_path_from_var("XDG_DATA_DIRS", target_candidates);

    AppConfig* app_config = wxGetApp().app_config;

    std::string version_suffix;
    std::string name_suffix;
    std::string version(SLIC3R_VERSION);
    if (version.find("alpha") != std::string::npos)
    {
        version_suffix = "-alpha";
        name_suffix = " - alpha";
    }
    else if (version.find("beta") != std::string::npos)
    {
        version_suffix = "-beta";
        name_suffix = " - beta";
    }

    std::string icon_theme_path;
    std::string icon_theme_dirs;

    if (platform_flavor() == PlatformFlavor::LinuxOnChromium) {
        icon_theme_path = "hicolor/96x96/apps/";
        icon_theme_dirs = "/hicolor/96x96/apps";
    }

    std::string target_dir_desktop;

    std::string desktop_file_downloader = GUI::format(
        "[Desktop Entry]\n"
        "Name=OrcaSlicer URL Protocol %1% %2%\n"
        "Exec=%3% %%u\n"
        "Terminal=false\n"
        "Type=Application\n"
        "MimeType=x-scheme-handler/%1%;\n"
        "StartupNotify=false\n"
        "NoDisplay=true\n"
        , url_prefix, name_suffix, excutable_path);

    bool candidate_found = false;
    for (size_t i = 0; i < target_candidates.size(); ++i) {
        if (contains_path_dir(target_candidates[i], "applications")) {
            target_dir_desktop = target_candidates[i];
            std::string path = GUI::format("%1%/applications/OrcaSlicerURLProtocol-%2%%3%.desktop", target_dir_desktop, url_prefix, version_suffix);
            if (create_desktop_file(path, desktop_file_downloader)) {
                app_config->set("desktop_integration_URL_path", path);
                candidate_found = true;
                BOOST_LOG_TRIVIAL(debug) << "OrcaSlicerURLProtocol.desktop file installation success.";
                break;
            }
            else {
                BOOST_LOG_TRIVIAL(debug) << "Attempt to OrcaSlicerURLProtocol.desktop file installation failed. failed path: " << target_candidates[i];
                target_dir_desktop.clear();
            }
        }
    }
    if (!candidate_found) {
        create_path(into_u8(wxFileName::GetHomeDir()), ".local/share/applications");
        target_dir_desktop = GUI::format("%1%/.local/share", wxFileName::GetHomeDir());
        std::string path = GUI::format("%1%/applications/OrcaSlicerURLProtocol-%2%%3%.desktop", target_dir_desktop, url_prefix, version_suffix);
        if (contains_path_dir(target_dir_desktop, "applications")) {
            if (!create_desktop_file(path, desktop_file_downloader)) {
                BOOST_LOG_TRIVIAL(error) << "Performing downloader desktop integration failed - could not create desktop file.";
                return;
            }
            app_config->set("desktop_integration_URL_path", path);
        }
        else {
            BOOST_LOG_TRIVIAL(error) << "Performing downloader desktop integration failed because the application directory was not found.";
            return;
        }
    }
    assert(!target_dir_desktop.empty());
    if (target_dir_desktop.empty()) {
        BOOST_LOG_TRIVIAL(error) << "Performing downloader desktop integration failed because the application directory was not found.";
        show_error(nullptr, _L("Performing downloader desktop integration failed because the application directory was not found."));
        return;
    }

    std::string desktop_path = GUI::format("%1%/applications/OrcaSlicerURLProtocol-%2%%3%.desktop", target_dir_desktop, url_prefix, version_suffix);
    if (create_desktop_file(desktop_path, desktop_file_downloader)) {
        app_config->set("desktop_integration_URL_path", desktop_path);
        std::string command = GUI::format("xdg-mime default OrcaSlicerURLProtocol-%1%%2%.desktop x-scheme-handler/%1%", url_prefix, version_suffix);
        BOOST_LOG_TRIVIAL(debug) << "system command: " << command;
        int r = system(command.c_str());
        BOOST_LOG_TRIVIAL(debug) << "system result: " << r;
    }

    std::string command = GUI::format("xdg-mime default OrcaSlicerURLProtocol-%1%%2%.desktop x-scheme-handler/%1%", url_prefix, version_suffix);
    BOOST_LOG_TRIVIAL(debug) << "system command: " << command;
    int r = system(command.c_str());
    BOOST_LOG_TRIVIAL(debug) << "system result: " << r;

    wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::DesktopIntegrationSuccess);
}
void DesktopIntegrationDialog::undo_downloader_registration()
{
    const AppConfig *app_config = wxGetApp().app_config;
    std::string path = std::string(app_config->get("desktop_integration_URL_path"));
    if (!path.empty()) {
        BOOST_LOG_TRIVIAL(debug) << "removing " << path;
        std::remove(path.c_str());  
    }
}

DesktopIntegrationDialog::DesktopIntegrationDialog(wxWindow *parent)
: wxDialog(parent, wxID_ANY, _(L("Desktop Integration")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER)
{
	bool can_undo = DesktopIntegrationDialog::is_integrated();

	wxBoxSizer *vbox = new wxBoxSizer(wxVERTICAL);


	wxString text = _L("Desktop Integration sets this binary to be searchable by the system.\n\nPress \"Perform\" to proceed.");
	if (can_undo)
		text += "\n" + _L("Press \"Undo\" to remove previous integration.");

    vbox->Add(
        new wxStaticText( this, wxID_ANY, text),
        1,
        wxEXPAND |
        wxALL,
        10 );
	

	wxBoxSizer *btn_szr = new wxBoxSizer(wxHORIZONTAL);
	wxButton *btn_perform = new wxButton(this, wxID_ANY, _L("Apply"));
	btn_szr->Add(btn_perform, 0, wxALL, 10);

	btn_perform->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { DesktopIntegrationDialog::perform_desktop_integration(); EndModal(wxID_ANY); });
	
	if (can_undo){
		wxButton *btn_undo = new wxButton(this, wxID_ANY, _L("Undo"));
		btn_szr->Add(btn_undo, 0, wxALL, 10);
		btn_undo->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { DesktopIntegrationDialog::undo_desktop_intgration(); EndModal(wxID_ANY); });
	}
	wxButton *btn_cancel = new wxButton(this, wxID_ANY, _L("Cancel"));
	btn_szr->Add(btn_cancel, 0, wxALL, 10);
	btn_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_ANY); });

	vbox->Add(btn_szr, 0, wxALIGN_CENTER);

    SetSizerAndFit(vbox);
}

DesktopIntegrationDialog::~DesktopIntegrationDialog()
{

}

}
}
#endif
