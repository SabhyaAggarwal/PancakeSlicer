#include "SendSystemInfoDialog.hpp"
#include "libslic3r/Semver.hpp"
#include <string>
#include <wx/string.h>
#include <wx/gdicmn.h>
#include <wx/toplevel.h>
#include <wx/textctrl.h>
#include "libslic3r_version.h"
#include <map>
#include <cstddef>
#include <cstdio>
#include <stdio.h>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <wx/language.h>
#include <wx/intl.h>
#include <wx/version.h>
#include <cmath>
#include <system_error>
#include <utility>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/constants.hpp>
#include <functional>
#include <boost/algorithm/string/trim.hpp>
#include <sstream>
#include <wx/dialog.h>
#include "libslic3r/libslic3r.h"
#include <cassert>
#include <wx/colour.h>
#include <wx/html/htmlwin.h>
#include <wx/event.h>
#include "slic3r/GUI/wxExtensions.hpp"

#if __APPLE__
#import <IOKit/IOKitLib.h>
#endif

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/BlacklistedLibraryCheck.hpp"
#include "libslic3r/Platform.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Color.hpp"

#include "slic3r/GUI/format.hpp"
#include "slic3r/Utils/Http.hpp"

#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#ifdef __WXGTK__
#include "LinuxDisplayBackend.hpp"
#endif
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "OpenGLManager.hpp"

#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim_all.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/uuid/detail/md5.hpp>

#include <glad/gl.h>

#include <wx/display.h>
#include <wx/htmllbox.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/utils.h>

#include <atomic>
#include <thread>
#include "slic3r/GUI/GUI.hpp"
#include <cstdlib>
#include <iomanip>

class wxWindow;

#ifdef _WIN32
    #include <windows.h>
    #include <netlistmgr.h>
    #include <Iphlpapi.h>
    #pragma comment(lib, "iphlpapi.lib")
#elif __APPLE__
    #include <CoreFoundation/CoreFoundation.h>
#else
    #include <charconv>
#endif

namespace Slic3r {
namespace GUI {

static const std::string SEND_SYSTEM_INFO_DOMAIN = "bambu-lab.com";
static const std::string SEND_SYSTEM_INFO_URL = "https://files." + SEND_SYSTEM_INFO_DOMAIN + "/wp-json/v1/ps";


std::string gl_get_string_safe(GLenum param, const std::string& default_value);


class SendSystemInfoDialog : public DPIDialog
{
    enum {
        MIN_WIDTH = 70,
        MIN_HEIGHT = 34
    };

public:
    SendSystemInfoDialog(wxWindow* parent);

private:
    bool send_info(wxString& message);
    const std::string m_system_info_json;
    wxButton* m_btn_show_data;
    wxButton* m_btn_send;
    wxButton* m_btn_dont_send;
    wxButton* m_btn_ask_later;

    void on_dpi_changed(const wxRect&) override;
};



class SendSystemInfoProgressDialog : public wxDialog
{
public:
    SendSystemInfoProgressDialog(wxWindow* parent, const wxString& message)
        : wxDialog(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxCAPTION)
    {
        auto* text = new wxStaticText(this, wxID_ANY, message, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
        auto* btn = new wxButton(this, wxID_CANCEL, wxEmptyString);
        auto* vsizer = new wxBoxSizer(wxVERTICAL);
        auto *top_sizer = new wxBoxSizer(wxVERTICAL);
        vsizer->Add(text, 1, wxEXPAND);
        vsizer->AddSpacer(5);
        vsizer->Add(btn, 0, wxALIGN_CENTER_HORIZONTAL);
        top_sizer->Add(vsizer, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT | wxBOTTOM, 10);
        SetSizer(top_sizer);
        #ifdef _WIN32
            wxGetApp().UpdateDlgDarkUI(this);
        #endif
    }
};



class ShowJsonDialog : public wxDialog
{
public:
    ShowJsonDialog(wxWindow* parent, const wxString& json, const wxSize& size)
        : wxDialog(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, size, wxCAPTION|wxRESIZE_BORDER)
    {
        auto* text = new wxTextCtrl(this, wxID_ANY, json,
                                    wxDefaultPosition, wxDefaultSize,
                                    wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
        text->SetFont(wxGetApp().code_font());
        text->ShowPosition(0);

        auto* btn = new wxButton(this, wxID_CANCEL, wxEmptyString);
        auto* vsizer = new wxBoxSizer(wxVERTICAL);
        auto *top_sizer = new wxBoxSizer(wxVERTICAL);
        vsizer->Add(text, 1, wxEXPAND);
        vsizer->AddSpacer(5);
        vsizer->Add(btn, 0, wxALIGN_CENTER_HORIZONTAL);
        top_sizer->Add(vsizer, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT | wxBOTTOM, 10);
        SetSizer(top_sizer);
        #ifdef _WIN32
            wxGetApp().UpdateDlgDarkUI(this);
        #endif
    }
};


static bool should_dialog_be_shown()
{
    std::string last_sent_version = SLIC3R_VERSION;

    Semver semver_current(SLIC3R_VERSION);
    Semver semver_last_sent;
    if (! last_sent_version.empty())
        semver_last_sent = Semver(last_sent_version);

    const bool show_in_alphas = true;

    if (! show_in_alphas && semver_current.prerelease()
       && std::string(semver_current.prerelease()).find("alpha") != std::string::npos)
            return false;

    bool new_version = ((semver_current.maj() > semver_last_sent.maj())
        || (semver_current.maj() == semver_last_sent.maj() && semver_current.min() > semver_last_sent.min() ));

    if (! new_version)
        return false;

#ifdef _WIN32
    return true;
#else
    return true;
#endif
}



static void save_version()
{
}



#ifdef _WIN32
static std::map<std::string, std::string> get_cpu_info_from_registry()
{
    std::map<std::string, std::string> out;

    int idx = -1;
    constexpr DWORD bufsize_ = 500;
    DWORD bufsize = bufsize_-1;
    char buf[bufsize_] = "";
    memset(buf, 0, bufsize_);
    const std::string reg_dir = "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\";
    std::string reg_path = reg_dir;

    while (true) {
        if (RegGetValueA(HKEY_LOCAL_MACHINE, reg_path.c_str(), "ProcessorNameString",
            RRF_RT_REG_SZ, NULL, &buf, &bufsize) == ERROR_SUCCESS) {
            out["Model"] = buf;
            out["Cores"] = std::to_string(std::max(1, idx + 1));
            if (RegGetValueA(HKEY_LOCAL_MACHINE, reg_path.c_str(),
                "VendorIdentifier", RRF_RT_REG_SZ, NULL, &buf, &bufsize) == ERROR_SUCCESS)
                out["Vendor"] = buf;
        }
        else {
            if (idx >= 0)
                break;
        }
        ++idx;
        reg_path = reg_dir + std::to_string(idx) + "\\";
        bufsize = bufsize_-1;
    }
    return out;
}
#else
static std::map<std::string, std::string> parse_lscpu_etc(const std::string& name, char delimiter)
{
    std::map<std::string, std::string> out;
    constexpr size_t max_len = 1000;
    char cline[max_len] = "";
    FILE* fp = popen(name.data(), "r");
    if (fp != NULL) {
        while (fgets(cline, max_len, fp) != NULL) {
            std::string line(cline);
            line.erase(std::remove_if(line.begin(), line.end(),
                [](char c) { return c == '\"' || c == '\r' || c == '\n'; }),
                line.end());
            size_t pos = line.find(delimiter);
            if (pos < line.size() - 1) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                boost::trim_all(key);
                boost::trim_all(value);
                out[key] = value;
            }
        }
        pclose(fp);
    }
    return out;
}
#endif



static std::string get_unique_id()
{
    std::vector<unsigned char> unique;

#ifdef _WIN32

    DWORD dwBufLen = sizeof(IP_ADAPTER_INFO);
    PIP_ADAPTER_INFO AdapterInfo = (PIP_ADAPTER_INFO)malloc(dwBufLen);

    if (GetAdaptersInfo(AdapterInfo, &dwBufLen) == ERROR_BUFFER_OVERFLOW) {
        free(AdapterInfo);
        AdapterInfo = (IP_ADAPTER_INFO*)malloc(dwBufLen);
    }
    if (GetAdaptersInfo(AdapterInfo, &dwBufLen) == NO_ERROR) {
        const IP_ADAPTER_INFO* pAdapterInfo = AdapterInfo;
        std::vector<std::vector<unsigned char>> macs;
        bool ethernet_seen = false;
        while (pAdapterInfo) {
            macs.emplace_back();
            for (unsigned char i = 0; i < pAdapterInfo->AddressLength; ++i)
                macs.back().emplace_back(pAdapterInfo->Address[i]);
            if (! ethernet_seen) {
                if ((pAdapterInfo->Type == MIB_IF_TYPE_ETHERNET && (ethernet_seen = true))
                 ||  pAdapterInfo->Type == IF_TYPE_IEEE80211)
                    std::swap(macs.front(), macs.back());
            }
            pAdapterInfo = pAdapterInfo->Next;
        }
        if (! macs.empty())
            unique = macs.front();
    }
    free(AdapterInfo);
#elif __APPLE__
    constexpr int buf_size = 100;
    char buf[buf_size] = "";
    memset(&buf, 0, sizeof(buf));
    io_registry_entry_t ioRegistryRoot = IORegistryEntryFromPath(kIOMasterPortDefault, "IOService:/");
    if (ioRegistryRoot != MACH_PORT_NULL) {
        CFStringRef uuidCf = (CFStringRef)IORegistryEntryCreateCFProperty(ioRegistryRoot, CFSTR(kIOPlatformUUIDKey), kCFAllocatorDefault, 0);
        IOObjectRelease(ioRegistryRoot);
        CFStringGetCString(uuidCf, buf, buf_size, kCFStringEncodingMacRoman);
        CFRelease(uuidCf);
    }
    for (char* c = buf; *c != 0; ++c)
        unique.emplace_back((unsigned char)(*c));
#else
    constexpr size_t max_len = 100;
    char cline[max_len] = "";
    FILE* fp = popen("cat /etc/machine-id", "r");
    if (fp != NULL) {
        [[maybe_unused]]auto dummy = fgets(cline, max_len, fp);
        pclose(fp);
    }
    for (char* c = cline; *c != 0; ++c)
        unique.emplace_back((unsigned char)(*c));
#endif

    if (unique.empty())
        return "";

    uint64_t prime = 1171432692373;
    size_t beg = unique.size();
    unique.resize(beg + 8);
    memcpy(&unique[beg], &prime, 8);

    using boost::uuids::detail::md5;
    md5 hash;
    md5::digest_type digest;
    hash.process_bytes(unique.data(), unique.size());
    hash.get_digest(digest);
    const unsigned char* charDigest = reinterpret_cast<const unsigned char*>(&digest);
    std::string result;
    boost::algorithm::hex(charDigest, charDigest + sizeof(md5::digest_type), std::back_inserter(result));
    return result;
}


static std::string generate_system_info_json()
{
    std::string unique_id = get_unique_id();

    std::string sys_language = "Unknown";
    #ifndef __APPLE__
        const wxLanguage lang_system = wxLanguage(wxLocale::GetSystemLanguage());
        if (lang_system != wxLANGUAGE_UNKNOWN)
            sys_language = wxLocale::GetLanguageInfo(lang_system)->CanonicalName.ToUTF8().data();
    #else
        CFLocaleRef cflocale = CFLocaleCopyCurrent();
        CFStringRef value = (CFStringRef)CFLocaleGetValue(cflocale, kCFLocaleLanguageCode);
        char temp[10] = "";
        CFStringGetCString(value, temp, 10, kCFStringEncodingUTF8);
        sys_language = temp;
        CFRelease(cflocale);
    #endif
    namespace pt = boost::property_tree;

    pt::ptree data_node;
    data_node.put("OrcaSlicerVersion", SLIC3R_VERSION);
    data_node.put("BuildID", SLIC3R_BUILD_ID);
    data_node.put("UniqueID", unique_id);
    data_node.put("Platform", platform_to_string(platform()));
    data_node.put("PlatformFlavor", platform_flavor_to_string(platform_flavor()));
    data_node.put("OSDescription", wxPlatformInfo::Get().GetOperatingSystemDescription().ToUTF8().data());
#ifdef __linux__
    std::string distro_id = wxGetLinuxDistributionInfo().Id.ToUTF8().data();
    std::string distro_ver = wxGetLinuxDistributionInfo().Release.ToUTF8().data();
    if (distro_id.empty()) {
        std::map<std::string, std::string> dist_info = parse_lscpu_etc("cat /etc/*release", '=');
        distro_id = dist_info["ID"];
        distro_ver = dist_info["VERSION_ID"];
    }
    data_node.put("Linux_DistroID", distro_id);
    data_node.put("Linux_DistroVer", distro_ver);
    data_node.put("Linux_Wayland", wxGetEnv("WAYLAND_DISPLAY", nullptr));
#ifdef __WXGTK__
    const LinuxDisplayBackend display_backend = get_linux_display_backend();
    const char* display_backend_name = "unknown";
    const char* gl_backend_hint = "Unknown";
    switch (display_backend) {
    case LinuxDisplayBackend::Wayland:
        display_backend_name = "wayland";
        gl_backend_hint = "EGL";
        break;
    case LinuxDisplayBackend::X11:
        display_backend_name = "x11";
        gl_backend_hint = "GLX";
        break;
    default:
        break;
    }
    data_node.put("Linux_DisplayBackend", display_backend_name);
    data_node.put("Linux_GLBackendHint", gl_backend_hint);
#else
    data_node.put("Linux_DisplayBackend", "unknown");
    data_node.put("Linux_GLBackendHint", "Unknown");
#endif
#endif
    data_node.put("wxWidgets", wxVERSION_NUM_DOT_STRING);
#ifdef __WXGTK__
    data_node.put("GTK",
    #if defined(__WXGTK2__)
        2
    #elif defined(__WXGTK3__)
        3
    #elif defined(__WXGTK4__)
        4
    #elif defined(__WXGTK5__)
        5
    #else
        "Unknown"
    #endif
    );
#endif
    data_node.put("SystemLanguage", sys_language);
    data_node.put("TranslationLanguage: ", wxGetApp().current_language_code_safe());


    pt::ptree hw_node;
    {
        hw_node.put("ArchName", wxPlatformInfo::Get().GetBitnessName());
        size_t num = std::round(Slic3r::total_physical_memory()/107374100.);
        hw_node.put("RAM_GiB", std::to_string(num / 10) + "." + std::to_string(num % 10));
    }

    pt::ptree cpu_node;
#ifdef _WIN32
    std::map<std::string, std::string> cpu_info = get_cpu_info_from_registry();
    cpu_node.put("Cores",  cpu_info["Cores"]);
    cpu_node.put("Model",  cpu_info["Model"]);
    cpu_node.put("Vendor", cpu_info["Vendor"]);
#elif __APPLE__
     std::map<std::string, std::string> sysctl = parse_lscpu_etc("sysctl -a", ':');
     cpu_node.put("Cores",  sysctl["hw.ncpu"]);
     cpu_node.put("Model",  sysctl["machdep.cpu.brand_string"]);
     cpu_node.put("Vendor", sysctl["machdep.cpu.vendor"]);
#else
    std::map<std::string, std::string> lscpu = parse_lscpu_etc("cat /proc/cpuinfo", ':');
    if (auto ncpu_it = lscpu.find("processor"); ncpu_it != lscpu.end()) {
        std::string& ncpu = ncpu_it->second;
        if (int num=0; std::from_chars(ncpu.data(), ncpu.data() + ncpu.size(), num).ec != std::errc::invalid_argument)
            ncpu = std::to_string(num + 1);
    }
    cpu_node.put("Cores",  lscpu["processor"]);
    cpu_node.put("Model",  lscpu["model name"]);
    cpu_node.put("Vendor", lscpu["vendor_id"]);
#endif
    hw_node.add_child("CPU", cpu_node);

    pt::ptree monitors_node;
    for (int i=0; i<int(wxDisplay::GetCount()); ++i) {
        wxDisplay display(i);
        pt::ptree monitor_node;
        monitor_node.put("width", display.GetGeometry().GetWidth());
        monitor_node.put("height", display.GetGeometry().GetHeight());

        #if defined(_WIN32)
            double scaling = display.GetPPI().GetWidth() / 96.;
            std::stringstream ss;
            ss << std::setprecision(3) << scaling;
            monitor_node.put("scaling", ss.str() );
        #endif
        monitors_node.push_back(std::make_pair("", monitor_node));
    }
    hw_node.add_child("Monitors", monitors_node);
    data_node.add_child("Hardware", hw_node);

#ifdef _WIN32
    {
        pt::ptree blacklisted_node;
        std::vector<std::wstring> blacklisted_libraries;
        BlacklistedLibraryCheck::get_instance().get_blacklisted(blacklisted_libraries);
        for (const std::wstring& wstr : blacklisted_libraries) {
            std::string utf8 = into_u8(wstr);
            if (size_t last_bs_pos = utf8.find_last_of("\\"); last_bs_pos < utf8.size() - 1) {
                utf8.erase(0, last_bs_pos + 1);
            }
            pt::ptree node;
            node.put("", utf8);
            blacklisted_node.push_back(std::make_pair("", node));
        }
        if (! blacklisted_libraries.empty())
            data_node.add_child("Blacklisted libraries", blacklisted_node);
    }
#endif

    pt::ptree opengl_node;
    opengl_node.put("Version", OpenGLManager::get_gl_info().get_version());
    opengl_node.put("GLSLVersion", OpenGLManager::get_gl_info().get_glsl_version());
    opengl_node.put("Vendor", OpenGLManager::get_gl_info().get_vendor());
    opengl_node.put("Renderer", OpenGLManager::get_gl_info().get_renderer());
    std::string extensions_str = gl_get_string_safe(GL_EXTENSIONS, "");
    std::vector<std::string> extensions_list;
    boost::split(extensions_list, extensions_str, boost::is_any_of(" "), boost::token_compress_off);
    std::sort(extensions_list.begin(), extensions_list.end());
    pt::ptree extensions_node;
    for (const std::string& s : extensions_list) {
        if (s.empty())
            continue;
        pt::ptree ext_node;
        ext_node.put("", s);
        extensions_node.push_back(std::make_pair("", ext_node));
    }
    opengl_node.add_child("Extensions", extensions_node);
    data_node.add_child("OpenGL", opengl_node);

    pt::ptree root;
    root.add_child("data", data_node);

    std::function<void(pt::ptree&)> remove_whitespace;
    remove_whitespace = [&remove_whitespace](pt::ptree& t) -> void
    {
        if (t.empty())
            boost::algorithm::trim(t.data());
        else
            for (auto it = t.begin(); it != t.end(); ++it)
                remove_whitespace(it->second);
    };
    remove_whitespace(root);

    std::stringstream ss;
    pt::write_json(ss, root);
    return ss.str();
}



SendSystemInfoDialog::SendSystemInfoDialog(wxWindow* parent)
    : m_system_info_json{generate_system_info_json()},
    GUI::DPIDialog(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE)
{
    const int em = GUI::wxGetApp().em_unit();

    std::string app_name;
    {
        Semver semver(SLIC3R_VERSION);
        bool is_alpha = false;
        if (semver.prerelease()) {
            is_alpha = std::string{ semver.prerelease() }.find("alpha") != std::string::npos;
        }

        bool is_beta = false;
        if (semver.prerelease()) {
            is_beta = std::string{ semver.prerelease() }.find("beta") != std::string::npos;
        }
        app_name = std::string(SLIC3R_APP_FULL_NAME) + " " + std::to_string(semver.maj())
                               + "." + std::to_string(semver.min()) + " "
                               + (is_alpha ? "Alpha" : is_beta ? "Beta" : "");
    }

    const char* filename = "SendSystemInfoDialog.cpp";
    assert(strstr(__FILE__, filename));

    SetFont(wxGetApp().normal_font());
    wxColour bgr_clr = wxGetApp().get_window_default_clr();
    SetBackgroundColour(bgr_clr);
    const auto text_clr = wxGetApp().get_label_clr_default();
    auto text_clr_str = encode_color(ColorRGB(text_clr.Red(), text_clr.Green(), text_clr.Blue()));
    auto bgr_clr_str = encode_color(ColorRGB(bgr_clr.Red(), bgr_clr.Green(), bgr_clr.Blue()));

    auto *topSizer = new wxBoxSizer(wxVERTICAL);
    auto *vsizer = new wxBoxSizer(wxVERTICAL);

    wxString text0  = GUI::format_wxstr(wxEmptyString, app_name);
    wxString text1 = wxEmptyString;
    wxString label2 = wxEmptyString;

     wxString text2 = GUI::format_wxstr(wxEmptyString, std::string("<i>") + filename + "</i>");

    auto* html_window = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxSize(70*em, 34*em), wxHW_SCROLLBAR_NEVER);
    wxString html = GUI::format_wxstr(
            "<html><body bgcolor=%1%><font color=%2%>"
            "<table><tr><td>"
            "<img src = \"" + resources_dir() + "/images/OrcaSlicer_192px.png\" />"
            "</td><td align=\"left\">"
            + text0 + "<br / ><br / >"
            + text1 + "<br /><br />"
            "</td></tr></table>"
            + "<b>" + label2 + "</b><br />"
            + text2
            + "</font></body></html>", bgr_clr_str, text_clr_str);
    html_window->SetPage(html);

    vsizer->Add(html_window, 1, wxEXPAND);

    m_btn_show_data = new wxButton(this, wxID_ANY, wxEmptyString);

    m_btn_ask_later = new wxButton(this, wxID_ANY, wxEmptyString);

    m_btn_dont_send = new wxButton(this, wxID_ANY, wxEmptyString);

    m_btn_send = new wxButton(this, wxID_ANY, wxEmptyString);

    auto* hsizer = new wxBoxSizer(wxHORIZONTAL);
    hsizer->Add(m_btn_ask_later);
    hsizer->AddSpacer(em);
    hsizer->Add(m_btn_dont_send);
    hsizer->AddSpacer(em);
    hsizer->Add(m_btn_send);

    vsizer->Add(m_btn_show_data, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 20);
    vsizer->Add(hsizer, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 10);
    topSizer->Add(vsizer, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, 10);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);


#ifdef _WIN32
    wxGetApp().UpdateDlgDarkUI(this);
#endif

    const auto size = GetSize();
    SetSize(std::max(size.GetWidth(), MIN_WIDTH * em),
            std::max(size.GetHeight(), MIN_HEIGHT * em));

    CenterOnParent();

    m_btn_show_data->Bind(wxEVT_BUTTON, [this](wxEvent&) {
                                            ShowJsonDialog dlg(this, m_system_info_json, GetSize().Scale(0.9, 0.7));
                                            dlg.ShowModal();
                                        });

    m_btn_send->Bind(wxEVT_BUTTON, [this](const wxEvent&)
                                    {
                                        wxString message;
                                        bool success = send_info(message);
                                        if (! message.IsEmpty())
                                            InfoDialog(nullptr, wxEmptyString, message).ShowModal();
                                        if (success) {
                                            save_version();
                                            EndModal(0);
                                        }
                                    });
    m_btn_dont_send->Bind(wxEVT_BUTTON, [this](const wxEvent&)
                                         {
                                             save_version();
                                             EndModal(0);
                                         });
    m_btn_ask_later->Bind(wxEVT_BUTTON, [this](const wxEvent&) { EndModal(0); });
}



void SendSystemInfoDialog::on_dpi_changed(const wxRect&)
{
    const int& em = em_unit();
    msw_buttons_rescale(this, em, { m_btn_send->GetId(),
                                    m_btn_dont_send->GetId(),
                                    m_btn_ask_later->GetId() });
    SetMinSize(wxSize(MIN_WIDTH * em, MIN_HEIGHT * em));
    Fit();
    Refresh();
}



bool SendSystemInfoDialog::send_info(wxString& message)
{
    std::atomic<int> job_done = false;
    struct Result {
        enum {
            Success,
            Cancelled,
            Error
        } value;
        wxString str;
    } result;

    auto send = [&job_done, &result](const std::string& data) {
        Slic3r::Http http = Slic3r::Http::post(SEND_SYSTEM_INFO_URL);
        http.header("Content-Type", "application/json")
            .timeout_max(6)
            .set_post_body(data)
            .on_complete([&result](std::string body, unsigned status) {
                result = {Result::Success, wxEmptyString};
            })
            .on_error([&result](std::string body, std::string error, unsigned status) {
                result = {Result::Error, wxEmptyString};
                BOOST_LOG_TRIVIAL(error) << "" << status;
            })
            .on_progress([&job_done, &result](Slic3r::Http::Progress, bool &cancel) {
                if (job_done)
                    cancel = true;
                if (cancel)
                    result = {Result::Cancelled, wxEmptyString};
            })
            .perform_sync();
        job_done = true;
    };

    std::thread sending_thread(send, m_system_info_json);
    SendSystemInfoProgressDialog dlg(this, wxEmptyString);
    wxTimer timer(&dlg);
    dlg.Bind(wxEVT_TIMER, [&dlg, &job_done](wxTimerEvent&){ if (job_done) dlg.EndModal(0); });
    timer.Start(50);
    dlg.ShowModal();
    job_done = true;
    sending_thread.join();

    message = result.value == Result::Cancelled ? wxString("") : result.str;
    return result.value == Result::Success;
}



void show_send_system_info_dialog_if_needed()
{
    if (wxGetApp().is_gcode_viewer() || ! should_dialog_be_shown())
        return;

    SendSystemInfoDialog dlg(wxGetApp().mainframe);
    dlg.ShowModal();
}





}
}
