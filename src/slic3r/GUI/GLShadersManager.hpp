#ifndef slic3r_GLShadersManager_hpp_
#define slic3r_GLShadersManager_hpp_

#include "GLShader.hpp"

#include <utility>
#include <vector>
#include <string>
#include <memory>

namespace Slic3r {

class GLShadersManager
{
    std::vector<std::unique_ptr<GLShaderProgram>> m_shaders;

public:
    std::pair<bool, std::string> init();
    void shutdown();

    GLShaderProgram* get_shader(const std::string& shader_name);

    GLShaderProgram* get_current_shader();
};

}

#endif
