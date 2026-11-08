#include "WebDownPluginDlg.hpp"
#include "ConfigWizard.hpp"

#include "slic3r/GUI/GUI.hpp"
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>
#include <exception>
#include <string.h>
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r_version.h"

#include <wx/log.h>
#include <wx/gdicmn.h>
#include <wx/event.h>
#include <wx/setup.h>
#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>

#include <wx/webview.h>
#include <wx/utils.h>
#include <wx/wx.h>
#include <wx/fileconf.h>
#include <wx/file.h>
#include <wx/wfstream.h>

#include <boost/cast.hpp>
#include <boost/lexical_cast.hpp>

#include "MainFrame.hpp"
#include <boost/dll.hpp>
#include <slic3r/GUI/Widgets/WebView.hpp>
#include <slic3r/Utils/Http.hpp>
#include <libslic3r/miniz_extension.hpp>
#include <libslic3r/Utils.hpp>

using namespace nlohmann;

namespace Slic3r { namespace GUI {

DownPluginFrame::DownPluginFrame(GUI_App *pGUI) : wxDialog((wxWindow *) (pGUI->mainframe), wxID_ANY, "Orca Slicer"), m_appconfig_new()
{
    m_MainPtr = pGUI;

    wxBoxSizer *topsizer = new wxBoxSizer(wxVERTICAL);
    wxString TargetUrl    = file_url_from_path(boost::filesystem::path(resources_dir()) / "web/guide/6/index.html");

    m_browser = WebView::CreateWebView(this, TargetUrl);
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }

    SetSizer(topsizer);
    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    wxSize pSize = FromDIP(wxSize(410, 200));
    SetSize(pSize);

    CenterOnParent();
    Bind(wxEVT_WEBVIEW_NAVIGATING, &DownPluginFrame::OnNavigationRequest, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATED, &DownPluginFrame::OnNavigationComplete, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_LOADED, &DownPluginFrame::OnDocumentLoaded, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_ERROR, &DownPluginFrame::OnError, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &DownPluginFrame::OnNewWindow, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_TITLE_CHANGED, &DownPluginFrame::OnTitleChanged, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_FULLSCREEN_CHANGED, &DownPluginFrame::OnFullScreenChanged, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &DownPluginFrame::OnScriptMessage, this, m_browser->GetId());

}

DownPluginFrame::~DownPluginFrame()
{
    if (m_browser) {
        delete m_browser;
        m_browser = nullptr;
    }
}

void DownPluginFrame::load_url(wxString &url)
{
    BOOST_LOG_TRIVIAL(trace) << "app_start: DownPluginFrame url=" << url.ToStdString();
    this->Show();
    m_browser->LoadURL(url);
    m_browser->SetFocus();
    UpdateState();
}

void DownPluginFrame::UpdateState()
{
}

void DownPluginFrame::OnIdle(wxIdleEvent &WXUNUSED(evt))
{
    if (m_browser->IsBusy()) {
        wxSetCursor(wxCURSOR_ARROWWAIT);
    } else {
        wxSetCursor(wxNullCursor);
    }
}

void DownPluginFrame::OnNavigationRequest(wxWebViewEvent &evt)
{

    UpdateState();
}

void DownPluginFrame::OnNavigationComplete(wxWebViewEvent &evt)
{

    wxString NewUrl = evt.GetURL();

    UpdateState();
}

void DownPluginFrame::OnDocumentLoaded(wxWebViewEvent &evt)
{
    wxString tmpUrl = evt.GetURL();
    wxString NowUrl = m_browser->GetCurrentURL();

    if (evt.GetURL() == m_browser->GetCurrentURL()) {
    }
    UpdateState();

}

void DownPluginFrame::OnNewWindow(wxWebViewEvent &evt)
{
    wxString flag = " (other)";

    wxString NewUrl = evt.GetURL();
    wxLaunchDefaultBrowser(NewUrl);

    UpdateState();
}

void DownPluginFrame::OnTitleChanged(wxWebViewEvent &evt)
{
}

void DownPluginFrame::OnFullScreenChanged(wxWebViewEvent &evt)
{
    ShowFullScreen(evt.GetInt() != 0);
}

void DownPluginFrame::OnScriptMessage(wxWebViewEvent &evt)
{
    try {
        wxString strInput = evt.GetString();
        json     j        = json::parse(strInput.utf8_string());

        wxString strCmd = j["command"];

        if (strCmd == "Begin_Download_network_plugin") {
            wxGetApp().CallAfter([this] { DownloadPlugin(); });
        }
        else if (strCmd == "netplugin_download_cancel") {
            wxGetApp().cancel_networking_install();
            this->EndModal(wxID_CANCEL);
            this->Close();
        }
        else if (strCmd == "begin_install_plugin") {
            wxGetApp().CallAfter([this] { InstallPlugin(); });
        }
        else if (strCmd == "restart_studio") {
            wxGetApp().restart_networking();
            this->EndModal(wxID_OK);
            this->Close();
        }
        else if (strCmd == "close_download_dialog") {
            this->EndModal(wxID_OK);
            this->Close();
        }
        else if (strCmd == "open_plugin_folder") {
            auto plugin_folder = (boost::filesystem::path(wxStandardPaths::Get().GetUserDataDir().ToUTF8().data()) / "plugins").make_preferred().string();
            desktop_open_any_folder(plugin_folder);
        }
    } catch (std::exception &) {
    }
}

void DownPluginFrame::RunScript(const wxString &javascript)
{

    if (!m_browser) return;

    WebView::RunScript(m_browser, javascript);
}

#if wxUSE_WEBVIEW_IE
void DownPluginFrame::OnRunScriptObjectWithEmulationLevel(wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){var person = new Object();person.name = 'Foo'; \
    person.lastName = 'Bar';return person;}f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}

void DownPluginFrame::OnRunScriptDateWithEmulationLevel(wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){var d = new Date('10/08/2017 21:30:40'); \
    var tzoffset = d.getTimezoneOffset() * 60000; return \
    new Date(d.getTime() - tzoffset);}f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}

void DownPluginFrame::OnRunScriptArrayWithEmulationLevel(wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){ return [\"foo\", \"bar\"]; }f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}
#endif

void DownPluginFrame::OnError(wxWebViewEvent &evt)
{
#define WX_ERROR_CASE(type) \
    case type: category = #type; break;

    wxString category;
    switch (evt.GetInt()) {
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CONNECTION);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CERTIFICATE);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_AUTH);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_SECURITY);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_NOT_FOUND);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_REQUEST);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_USER_CANCELLED);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_OTHER);
    }

    BOOST_LOG_TRIVIAL(info) << "DownPluginFrame::OnError: An error occurred loading " << evt.GetURL() << category;

    UpdateState();
}

void DownPluginFrame::OnScriptResponseMessage(wxCommandEvent &WXUNUSED(evt))
{

}

int DownPluginFrame::DownloadPlugin()
{
    return wxGetApp().download_plugin(
        "plugins", "network_plugin.zip", [this](int status, int percent, bool &cancel) { return ShowPluginStatus(status, percent, cancel); }, nullptr);
}

int DownPluginFrame::InstallPlugin()
{
    return wxGetApp().install_plugin(
        "plugins", "network_plugin.zip", [this](int status, int percent, bool &cancel) { return ShowPluginStatus(status, percent, cancel); });
}

int DownPluginFrame::ShowPluginStatus(int status, int percent, bool &cancel)
{
    static int nPercent = 0;
    if (nPercent == percent)
        return 0;

    nPercent = percent;

    json m_Data = json::object();
    m_Data["status"] = status;
    m_Data["percent"] = percent;

    json m_Res           = json::object();
    m_Res["command"]     = "ShowStatusPercent";
    m_Res["sequence_id"] = "10001";
    m_Res["data"]    = m_Data;

    wxString strJS = wxString::Format("HandleStudio(%s)", m_Res.dump(-1, ' ', false, json::error_handler_t::ignore));

    wxGetApp().CallAfter([this, strJS] { RunScript(strJS); });

    return 0;
}

}}
