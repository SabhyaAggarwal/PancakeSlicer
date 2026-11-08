#include "libslic3r/libslic3r.h"
#include "OpenGLManager.hpp"

#include "GUI.hpp"
#include "I18N.hpp"
#include "3DScene.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Platform.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <algorithm>
#include <boost/algorithm/string/constants.hpp>
#include <cstdlib>
#include <glad/gl.h>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/log/trivial.hpp>

#include <string>
#include <ostream>
#include "libslic3r/Utils.hpp"
#include <vector>
#include <sstream>
#include <utility>
#include <wx/gdicmn.h>
#include <wx/glcanvas.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/string.h>
#include <wx/version.h>

#include "GUI_Init.hpp"
#include "slic3r/GUI/GLShadersManager.hpp"

#ifdef __APPLE__
#include "../Utils/MacDarkMode.hpp"
#endif

#ifdef __WXGTK__
#include "LinuxDisplayBackend.hpp"
#ifdef wxHAS_EGL
#include <EGL/egl.h>
#endif
#endif

namespace Slic3r {
namespace GUI {

std::string gl_get_string_safe(GLenum param, const std::string& default_value)
{
    const char* value = (const char*)::glGetString(param);
    return std::string((value != nullptr) ? value : default_value);
}

const std::string& OpenGLManager::GLInfo::get_version() const
{
    if (!m_detected)
        detect();

    return m_version;
}

const std::string& OpenGLManager::GLInfo::get_glsl_version() const
{
    if (!m_detected)
        detect();

    return m_glsl_version;
}

const std::string& OpenGLManager::GLInfo::get_vendor() const
{
    if (!m_detected)
        detect();

    return m_vendor;
}

const std::string& OpenGLManager::GLInfo::get_renderer() const
{
    if (!m_detected)
        detect();

    return m_renderer;
}

bool OpenGLManager::GLInfo::is_mesa() const
{
    return boost::icontains(m_version, "mesa");
}

int OpenGLManager::GLInfo::get_max_tex_size() const
{
    if (!m_detected)
        detect();

#ifdef __APPLE__
    return (Slic3r::GUI::mac_max_scaling_factor() > 1.0) ? std::min(m_max_tex_size, 8192) : std::min(m_max_tex_size / 2, 4096);
#else
    return is_version_greater_or_equal_to(3, 0) ? std::min(m_max_tex_size, 8192) : std::min(m_max_tex_size / 2, 4096);
#endif
}

float OpenGLManager::GLInfo::get_max_anisotropy() const
{
    if (!m_detected)
        detect();

    return m_max_anisotropy;
}

void OpenGLManager::GLInfo::detect() const
{
    *const_cast<std::string*>(&m_version) = gl_get_string_safe(GL_VERSION, "N/A");
    *const_cast<std::string*>(&m_glsl_version) = gl_get_string_safe(GL_SHADING_LANGUAGE_VERSION, "N/A");
    *const_cast<std::string*>(&m_vendor) = gl_get_string_safe(GL_VENDOR, "N/A");
    *const_cast<std::string*>(&m_renderer) = gl_get_string_safe(GL_RENDERER, "N/A");

    BOOST_LOG_TRIVIAL(info) << boost::format("got opengl version %1%, glsl version %2%, vendor %3% , graphics card model %4%") % m_version % m_glsl_version % m_vendor % m_renderer << std::endl;

    int* max_tex_size = const_cast<int*>(&m_max_tex_size);
    glsafe(::glGetIntegerv(GL_MAX_TEXTURE_SIZE, max_tex_size));

    *max_tex_size /= 2;

    if (Slic3r::total_physical_memory() / (1024 * 1024 * 1024) < 6)
        *max_tex_size /= 2;

    if (GLAD_GL_EXT_texture_filter_anisotropic) {
        float* max_anisotropy = const_cast<float*>(&m_max_anisotropy);
        glsafe(::glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, max_anisotropy));
    }

    if (!GLAD_GL_ARB_compatibility)
        *const_cast<bool*>(&m_core_profile) = true;

    *const_cast<bool*>(&m_detected) = true;
}

static bool version_greater_or_equal_to(const std::string& version, unsigned int major, unsigned int minor)
{
    if (version == "N/A")
        return false;

    std::vector<std::string> tokens;
    boost::split(tokens, version, boost::is_any_of(" "), boost::token_compress_on);

    if (tokens.empty())
        return false;

    std::vector<std::string> numbers;
    boost::split(numbers, tokens[0], boost::is_any_of("."), boost::token_compress_on);

    unsigned int gl_major = 0;
    unsigned int gl_minor = 0;

    if (numbers.size() > 0)
        gl_major = ::atoi(numbers[0].c_str());

    if (numbers.size() > 1)
        gl_minor = ::atoi(numbers[1].c_str());

    if (gl_major < major)
        return false;
    else if (gl_major > major)
        return true;
    else
        return gl_minor >= minor;
}

bool OpenGLManager::GLInfo::is_version_greater_or_equal_to(unsigned int major, unsigned int minor) const
{
    if (!m_detected)
        detect();

    return version_greater_or_equal_to(m_version, major, minor);
}

bool OpenGLManager::GLInfo::is_glsl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const
{
    if (!m_detected)
        detect();

    return version_greater_or_equal_to(m_glsl_version, major, minor);
}

std::string OpenGLManager::GLInfo::to_string(bool for_github) const
{
    if (!m_detected)
        detect();

    std::stringstream out;

    const bool format_as_html = ! for_github;
    std::string h2_start = format_as_html ? "<b>" : "";
    std::string h2_end = format_as_html ? "</b>" : "";
    std::string b_start = format_as_html ? "<b>" : "";
    std::string b_end = format_as_html ? "</b>" : "";
    std::string line_end = format_as_html ? "<br>" : "\n";

    out << h2_start << "OpenGL installation" << h2_end << line_end;
    out << b_start << "GL version:   " << b_end << m_version << line_end;
#if !SLIC3R_OPENGL_ES
    out << b_start << "Profile:      " << b_end << (is_core_profile() ? "Core" : "Compatibility") << line_end;
#endif
    out << b_start << "Vendor:       " << b_end << m_vendor << line_end;
    out << b_start << "Renderer:     " << b_end << m_renderer << line_end;
    out << b_start << "GLSL version: " << b_end << m_glsl_version << line_end;

    {
        std::vector<std::string> extensions_list;
        std::string extensions_str = gl_get_string_safe(GL_EXTENSIONS, "");
        boost::split(extensions_list, extensions_str, boost::is_any_of(" "), boost::token_compress_on);

        if (!extensions_list.empty()) {
            if (for_github)
                out << "<details>\n<summary>Installed extensions:</summary>\n";
            else
                out << h2_start << "Installed extensions:" << h2_end << line_end;

            std::sort(extensions_list.begin(), extensions_list.end());
            for (const std::string& ext : extensions_list)
                if (! ext.empty())
                    out << ext << line_end;

            if (for_github)
                out << "</details>\n";
        }
    }

    return out.str();
}

OpenGLManager::GLInfo OpenGLManager::s_gl_info;
bool OpenGLManager::s_compressed_textures_supported = false;
bool OpenGLManager::s_force_power_of_two_textures = false;
OpenGLManager::EMultisampleState OpenGLManager::s_multisample = OpenGLManager::EMultisampleState::Unknown;
OpenGLManager::EFramebufferType OpenGLManager::s_framebuffers_type = OpenGLManager::EFramebufferType::Unknown;

OpenGLManager::~OpenGLManager()
{
    m_shaders_manager.shutdown();

    if (m_context != nullptr)
        delete m_context;
}

bool OpenGLManager::init_gl(bool popup_error)
{
    if (!m_gl_initialized) {
        int version = 0;
#if defined(__WXGTK__) && defined(wxHAS_EGL)
        if (is_running_on_wayland()) {
            version = gladLoadGL((GLADloadfunc)eglGetProcAddress);
        } else
#endif
        {
            version = gladLoaderLoadGL();
        }
        if (version == 0) {
            BOOST_LOG_TRIVIAL(error) << "Unable to init GLAD OpenGL loader";
            return false;
        }
        BOOST_LOG_TRIVIAL(info) << "GLAD loaded OpenGL " << GLAD_VERSION_MAJOR(version) << "." << GLAD_VERSION_MINOR(version);
        m_gl_initialized = true;
        if (GLAD_GL_EXT_texture_compression_s3tc)
            s_compressed_textures_supported = true;
        else
            s_compressed_textures_supported = false;

        if (s_gl_info.is_version_greater_or_equal_to(3, 0)) {
            s_framebuffers_type = EFramebufferType::Arb;
            BOOST_LOG_TRIVIAL(info) << "Opengl version >= 30, FrameBuffer Type ARB." << std::endl;
        }
        else if (GLAD_GL_ARB_framebuffer_object) {
            s_framebuffers_type = EFramebufferType::Arb;
            BOOST_LOG_TRIVIAL(info) << "Found Framebuffer Type ARB."<< std::endl;
        }
        else if (GLAD_GL_EXT_framebuffer_object) {
            BOOST_LOG_TRIVIAL(info) << "Found Framebuffer Type Ext."<< std::endl;
            s_framebuffers_type = EFramebufferType::Ext;
        }
        else {
            s_framebuffers_type = EFramebufferType::Unknown;
            BOOST_LOG_TRIVIAL(warning) << "Found Framebuffer Type unknown!"<< std::endl;
        }

        bool valid_version = s_gl_info.is_version_greater_or_equal_to(2, 0);
        if (!valid_version) {
            BOOST_LOG_TRIVIAL(error) << "Found opengl version < 2.0"<< std::endl;
            if (popup_error) {
                wxString message = from_u8((boost::format(
                    _utf8(L("The application cannot run normally because OpenGL version is lower than 3.2.\n")))).str());
                message += "\n";
                message += _L("Please upgrade your graphics card driver.");
                wxMessageBox(message, _L("Unsupported OpenGL version"), wxOK | wxICON_ERROR);
            }
        }

        if (valid_version)
        {
            auto [result, error] = m_shaders_manager.init();
            if (!result) {
                BOOST_LOG_TRIVIAL(error) << "Unable to load shaders: "<<error<< std::endl;
                if (popup_error) {
                    wxString message = from_u8((boost::format(
                        _utf8(L("Unable to load shaders:\n%s"))) % error).str());
                    wxMessageBox(message, _L("Error loading shaders"), wxOK | wxICON_ERROR);
                }
            }
        }

#ifdef _WIN32
        const auto& gl_info = OpenGLManager::get_gl_info();
        if (boost::contains(gl_info.get_vendor(), "ATI Technologies Inc.") &&
           (boost::contains(gl_info.get_renderer(), "Radeon") ||
            boost::contains(gl_info.get_renderer(), "Custom")))
            s_force_power_of_two_textures = true;
#endif
    }

    return true;
}

wxGLContext* OpenGLManager::init_glcontext(wxGLCanvas& canvas, const std::pair<int, int>& required_opengl_version, bool enable_compatibility_profile,
    bool enable_debug)
{
    if (m_context == nullptr) {

        const int gl_major = required_opengl_version.first;
        const int gl_minor = required_opengl_version.second;
        const bool supports_core_profile =
            std::find(OpenGLVersions::core.begin(), OpenGLVersions::core.end(), std::make_pair(gl_major, gl_minor)) != OpenGLVersions::core.end();

        if (gl_major == 0 && !enable_compatibility_profile) {
            wxLogNull logNo;
            for (auto v = OpenGLVersions::core.rbegin(); v != OpenGLVersions::core.rend(); ++v) {
                wxGLContextAttrs attrs;
                attrs.PlatformDefaults().MajorVersion(v->first).MinorVersion(v->second).CoreProfile().ForwardCompatible();
                attrs.EndList();
                m_context = new wxGLContext(&canvas, nullptr, &attrs);
                if (m_context->IsOK())
                    break;
                else {
                    delete m_context;
                    m_context = nullptr;
                }
            }
        }

        if (m_context == nullptr) {
            if (enable_compatibility_profile) {
                wxLogNull logNo;
                wxGLContextAttrs attrs;
                attrs.PlatformDefaults().CompatibilityProfile();
                attrs.EndList();
                m_context = new wxGLContext(&canvas, nullptr, &attrs);
                if (!m_context->IsOK()) {
                    delete m_context;
                    m_context = nullptr;
                }
            }
            else if (supports_core_profile) {
                wxLogNull logNo;
                wxGLContextAttrs attrs;
                attrs.PlatformDefaults().MajorVersion(gl_major).MinorVersion(gl_minor).CoreProfile().ForwardCompatible();
                attrs.EndList();
                m_context = new wxGLContext(&canvas, nullptr, &attrs);
                if (!m_context->IsOK()) {
                    delete m_context;
                    m_context = nullptr;
                }
            }
        }

        if (m_context == nullptr) {
            wxGLContextAttrs attrs;
            attrs.PlatformDefaults();
            attrs.EndList();
            m_context = new wxGLContext(&canvas, nullptr, &attrs);
        }

    }
    return m_context;
}

wxGLCanvas* OpenGLManager::create_wxglcanvas(wxWindow& parent)
{
    int antialiasing_samples = 4;
    if (const AppConfig* app_config = get_app_config(); app_config != nullptr) {
        const std::string value = app_config->get(SETTING_OPENGL_AA_SAMPLES);
        if (value == "0" || value == "2" || value == "4" || value == "8" || value == "16")
            antialiasing_samples = ::atoi(value.c_str());
    }

    int attribList[] = {
        WX_GL_RGBA,
        WX_GL_DOUBLEBUFFER,
        WX_GL_MIN_RED, 			8,
        WX_GL_MIN_GREEN, 		8,
        WX_GL_MIN_BLUE, 		8,
        WX_GL_MIN_ALPHA, 		8,
        WX_GL_DEPTH_SIZE, 		24,
        WX_GL_STENCIL_SIZE,     8,
        WX_GL_SAMPLE_BUFFERS, 	antialiasing_samples > 0 ? GL_TRUE : GL_FALSE,
	    WX_GL_SAMPLES, 			antialiasing_samples,
        0
    };

    constexpr int sample_buffers_value_idx = 15;
    constexpr int samples_value_idx        = 17;

    if (antialiasing_samples <= 0)
        s_multisample = EMultisampleState::Disabled;
    else if (s_multisample == EMultisampleState::Unknown) {
        detect_multisample(attribList);
    }

    if (! can_multisample()) {
        attribList[sample_buffers_value_idx] = GL_FALSE;
        attribList[samples_value_idx] = 0;
    }

    wxGLCanvas* canvas = new wxGLCanvas(&parent, wxID_ANY, attribList, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS);
    canvas->SetBackgroundStyle(wxBG_STYLE_PAINT);
    return canvas;
}

void OpenGLManager::detect_multisample(int* attribList)
{
    int wxVersion = wxMAJOR_VERSION * 10000 + wxMINOR_VERSION * 100 + wxRELEASE_NUMBER;
    bool enable_multisample = wxVersion >= 30003;

#if defined(__WXGTK__)
    if (is_running_on_wayland()) {
#if !defined(wxHAS_EGL) || !wxHAS_EGL
        BOOST_LOG_TRIVIAL(warning) << "Wayland without EGL: disabling multisample detection";
        s_multisample = EMultisampleState::Disabled;
        return;
#endif
    }
#endif

    s_multisample =
        enable_multisample &&
        platform_flavor() != PlatformFlavor::LinuxOnChromium &&
        wxGLCanvas::IsDisplaySupported(attribList)
        ? EMultisampleState::Enabled : EMultisampleState::Disabled;
}

}
}
