#include "libslic3r/libslic3r.h"
#include "libslic3r/Platform.hpp"
#include "GLShadersManager.hpp"
#include "3DScene.hpp"
#include "GUI_App.hpp"

#include <cassert>
#include <algorithm>
#include <string>
#include <initializer_list>
#include <memory>
#include "slic3r/GUI/OpenGLManager.hpp"
#include "libslic3r/Technologies.hpp"
#include <string_view>
#include <utility>
using namespace std::literals;

#include <glad/gl.h>

namespace Slic3r {

std::pair<bool, std::string> GLShadersManager::init()
{
    std::string error;

    auto append_shader = [this, &error](const std::string& name, const GLShaderProgram::ShaderFilenames& filenames,
        const std::initializer_list<std::string_view> &defines = {}) {
        m_shaders.push_back(std::make_unique<GLShaderProgram>());
        if (!m_shaders.back()->init_from_files(name, filenames, defines)) {
            error += name + "\n";
            m_shaders.pop_back();
            return false;
        }
        return true;
    };

    assert(m_shaders.empty());

    bool valid = true;

#if SLIC3R_OPENGL_ES
    const std::string prefix = "ES/";
    valid &= append_shader("wireframe", { prefix + "wireframe.vs", prefix + "wireframe.fs" });
#else
    const std::string prefix = GUI::wxGetApp().is_gl_version_greater_or_equal_to(3, 1) ? "140/" : "110/";
#endif
    valid &= append_shader("imgui", { prefix + "imgui.vs", prefix + "imgui.fs" });
    valid &= append_shader("flat", { prefix + "flat.vs", prefix + "flat.fs" });
    valid &= append_shader("flat_clip", { prefix + "flat_clip.vs", prefix + "flat_clip.fs" });
    valid &= append_shader("flat_texture", { prefix + "flat_texture.vs", prefix + "flat_texture.fs" });
    valid &= append_shader("fxaa", { prefix + "fxaa.vs", prefix + "fxaa.fs" });
    valid &= append_shader("ssao", { prefix + "ssao.vs", prefix + "ssao.fs" });
    valid &= append_shader("background", { prefix + "background.vs", prefix + "background.fs" });
#if SLIC3R_OPENGL_ES
    valid &= append_shader("dashed_lines", { prefix + "dashed_lines.vs", prefix + "dashed_lines.fs" });
#else
    if (GUI::OpenGLManager::get_gl_info().is_core_profile())
        valid &= append_shader("dashed_thick_lines", { prefix + "dashed_thick_lines.vs", prefix + "dashed_thick_lines.fs", prefix + "dashed_thick_lines.gs" });
#endif
    valid &= append_shader("gouraud_light", { prefix + "gouraud_light.vs", prefix + "gouraud_light.fs" });
    valid &= append_shader("thumbnail", { prefix + "thumbnail.vs", prefix + "thumbnail.fs"});
    valid &= append_shader("printbed", { prefix + "printbed.vs", prefix + "printbed.fs" });
#if !SLIC3R_OPENGL_ES
    valid &= append_shader("printbed_shadow", { prefix + "printbed_shadow.vs", prefix + "printbed_shadow.fs" });
#endif
    valid &= append_shader("hotbed", {prefix + "hotbed.vs", prefix + "hotbed.fs"});
    if (GUI::wxGetApp().is_gl_version_greater_or_equal_to(3, 3)) {
        valid &= append_shader("gouraud_light_instanced", { prefix + "gouraud_light_instanced.vs", prefix + "gouraud_light_instanced.fs" });
    }

    valid &= append_shader("gouraud", { prefix + "gouraud.vs", prefix + "gouraud.fs" }
#if ENABLE_ENVIRONMENT_MAP
        , { "ENABLE_ENVIRONMENT_MAP"sv }
#endif
        );
    valid &= append_shader("phong", { prefix + "phong.vs", prefix + "phong.fs" }
#if ENABLE_ENVIRONMENT_MAP
        , { "ENABLE_ENVIRONMENT_MAP"sv }
#endif
        );
    valid &= append_shader("xray", { prefix + "xray.vs", prefix + "xray.fs" });
    valid &= append_shader("variable_layer_height", { prefix + "variable_layer_height.vs", prefix + "variable_layer_height.fs" });
    valid &= append_shader("mm_contour", { prefix + "mm_contour.vs", prefix + "mm_contour.fs" });
    if (platform_flavor() == PlatformFlavor::OSXOnArm && wxPlatformInfo::Get().GetOSMajorVersion() < 12)
        valid &= append_shader("mm_gouraud", { prefix + "mm_gouraud.vs", prefix + "mm_gouraud.fs" }, { "FLIP_TRIANGLE_NORMALS"sv });
    else
        valid &= append_shader("mm_gouraud", { prefix + "mm_gouraud.vs", prefix + "mm_gouraud.fs" });
    valid &= append_shader("texture_displacement_shaded", { prefix + "texture_displacement_shaded.vs", prefix + "texture_displacement_shaded.fs" });
    valid &= append_shader("texture_displacement_uvcheck", { prefix + "texture_displacement_uvcheck.vs", prefix + "texture_displacement_uvcheck.fs" });

    return { valid, error };
}

void GLShadersManager::shutdown()
{
    m_shaders.clear();
}

GLShaderProgram* GLShadersManager::get_shader(const std::string& shader_name)
{
    auto it = std::find_if(m_shaders.begin(), m_shaders.end(), [&shader_name](std::unique_ptr<GLShaderProgram>& p) { return p->get_name() == shader_name; });
    return (it != m_shaders.end()) ? it->get() : nullptr;
}

GLShaderProgram* GLShadersManager::get_current_shader()
{
    GLint id = 0;
    glsafe(::glGetIntegerv(GL_CURRENT_PROGRAM, &id));
    if (id == 0)
        return nullptr;

    auto it = std::find_if(m_shaders.begin(), m_shaders.end(), [id](std::unique_ptr<GLShaderProgram>& p) { return static_cast<GLint>(p->get_id()) == id; });
    return (it != m_shaders.end()) ? it->get() : nullptr;
}

}
