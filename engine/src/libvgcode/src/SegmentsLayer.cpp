///|/ libvgcode is released under the terms of the AGPLv3 or higher
#include "SegmentsLayer.hpp"
#include "OpenGLUtils.hpp"

#include <array>
#include <cstddef>
#include <cstring>

namespace libvgcode {

void SegmentsLayer::init(unsigned int program_id, unsigned int program_ms_id)
{
    m_program_id = program_id;
    m_program_ms_id = program_ms_id;
    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    m_mesa = (vendor != nullptr && std::strstr(vendor, "Mesa") != nullptr) || (version != nullptr && std::strstr(version, "Mesa") != nullptr);
    glsafe(glGenVertexArrays(1, &m_vao_id));
}

void SegmentsLayer::shutdown()
{
    if (m_fbo_id != 0) {
        glsafe(glDeleteFramebuffers(1, &m_fbo_id));
        m_fbo_id = 0;
    }
    if (m_color_tex_id != 0) {
        glsafe(glDeleteTextures(1, &m_color_tex_id));
        m_color_tex_id = 0;
    }
    if (m_depth_tex_id != 0) {
        glsafe(glDeleteTextures(1, &m_depth_tex_id));
        m_depth_tex_id = 0;
    }
    m_width = m_height = m_samples = m_depth_bits = 0;
    for (unsigned int* id : { &m_program_id, &m_program_ms_id }) {
        if (*id != 0) {
            glsafe(glDeleteProgram(*id));
            *id = 0;
        }
    }
    if (m_vao_id != 0) {
        glsafe(glDeleteVertexArrays(1, &m_vao_id));
        m_vao_id = 0;
    }
}

bool SegmentsLayer::begin(bool clear)
{
    glsafe(glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_prev_draw_fbo));
    glsafe(glGetIntegerv(GL_VIEWPORT, m_viewport.data()));
    int samples = 0;
    glsafe(glGetIntegerv(GL_SAMPLES, &samples));
    if (samples <= 1)
        samples = 0;
    int depth_bits = 24;
    glsafe(glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, m_prev_draw_fbo == 0 ? GL_DEPTH : GL_DEPTH_ATTACHMENT,
                                                 GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &depth_bits));

    const bool kept = m_fbo_id != 0 && m_width == m_viewport[2] && m_height == m_viewport[3] && m_samples == samples &&
                      m_depth_bits == depth_bits;
    if (!kept) {
        if (m_fbo_id == 0)
            glsafe(glGenFramebuffers(1, &m_fbo_id));
        if (m_color_tex_id != 0)
            glsafe(glDeleteTextures(1, &m_color_tex_id));
        if (m_depth_tex_id != 0)
            glsafe(glDeleteTextures(1, &m_depth_tex_id));
        m_width = m_viewport[2];
        m_height = m_viewport[3];
        m_samples = samples;
        m_depth_bits = depth_bits;
        const GLenum depth_format = depth_bits <= 16 ? GL_DEPTH_COMPONENT16 : depth_bits <= 24 ? GL_DEPTH_COMPONENT24 : GL_DEPTH_COMPONENT32F;
        const GLenum target = m_samples > 0 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
        int curr_texture = 0;
        glsafe(glGetIntegerv(m_samples > 0 ? GL_TEXTURE_BINDING_2D_MULTISAMPLE : GL_TEXTURE_BINDING_2D, &curr_texture));
        glsafe(glGenTextures(1, &m_color_tex_id));
        glsafe(glGenTextures(1, &m_depth_tex_id));
        if (m_samples > 0) {
            glsafe(glBindTexture(target, m_color_tex_id));
            glsafe(glTexImage2DMultisample(target, m_samples, GL_RGBA8, m_width, m_height, GL_TRUE));
            glsafe(glBindTexture(target, m_depth_tex_id));
            glsafe(glTexImage2DMultisample(target, m_samples, depth_format, m_width, m_height, GL_TRUE));
        }
        else {
            glsafe(glBindTexture(target, m_color_tex_id));
            glsafe(glTexImage2D(target, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
            glsafe(glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
            glsafe(glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
            glsafe(glBindTexture(target, m_depth_tex_id));
            glsafe(glTexImage2D(target, 0, depth_format, m_width, m_height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr));
            glsafe(glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
            glsafe(glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
        }
        glsafe(glBindTexture(target, curr_texture));
        glsafe(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo_id));
        glsafe(glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, m_color_tex_id, 0));
        glsafe(glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target, m_depth_tex_id, 0));
    }
    else
        glsafe(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo_id));

    m_flipped = m_mesa && m_prev_draw_fbo == 0;
    glsafe(glViewport(0, 0, m_width, m_height));
    if (clear || !kept) {
        std::array<float, 4> clear_color;
        float clear_depth = 1.0f;
        unsigned char depth_mask = GL_TRUE;
        std::array<unsigned char, 4> color_mask;
        glsafe(glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color.data()));
        glsafe(glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clear_depth));
        glsafe(glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask));
        glsafe(glGetBooleanv(GL_COLOR_WRITEMASK, color_mask.data()));
        const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
        glsafe(glDisable(GL_SCISSOR_TEST));
        glsafe(glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));
        glsafe(glDepthMask(GL_TRUE));
        glsafe(glClearColor(0.0f, 0.0f, 0.0f, 0.0f));
        glsafe(glClearDepth(1.0));
        glsafe(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
        glsafe(glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]));
        glsafe(glClearDepth(clear_depth));
        glsafe(glDepthMask(depth_mask));
        glsafe(glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]));
        if (scissor)
            glsafe(glEnable(GL_SCISSOR_TEST));
    }
    return kept;
}

void SegmentsLayer::end()
{
    glsafe(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_prev_draw_fbo));
    glsafe(glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]));
}

void SegmentsLayer::composite(const std::array<int, 4>& rect) const
{
    if (m_fbo_id == 0 || rect[2] <= 0 || rect[3] <= 0)
        return;

    const unsigned int program = m_samples > 0 ? m_program_ms_id : m_program_id;
    const GLenum target = m_samples > 0 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    int curr_program = 0;
    int curr_vertex_array = 0;
    int curr_active_texture = 0;
    std::array<int, 2> curr_textures = { 0, 0 };
    std::array<int, 4> curr_scissor_box;
    unsigned char depth_mask = GL_TRUE;
    glsafe(glGetIntegerv(GL_CURRENT_PROGRAM, &curr_program));
    glsafe(glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &curr_vertex_array));
    glsafe(glGetIntegerv(GL_ACTIVE_TEXTURE, &curr_active_texture));
    glsafe(glGetIntegerv(GL_SCISSOR_BOX, curr_scissor_box.data()));
    glsafe(glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask));
    const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
    const bool blend = glIsEnabled(GL_BLEND);
    const bool cull_face = glIsEnabled(GL_CULL_FACE);
    for (int i = 0; i < 2; ++i) {
        glsafe(glActiveTexture(GL_TEXTURE6 + i));
        glsafe(glGetIntegerv(m_samples > 0 ? GL_TEXTURE_BINDING_2D_MULTISAMPLE : GL_TEXTURE_BINDING_2D, &curr_textures[i]));
    }

    glsafe(glUseProgram(program));
    glsafe(glActiveTexture(GL_TEXTURE6));
    glsafe(glBindTexture(target, m_color_tex_id));
    glsafe(glActiveTexture(GL_TEXTURE7));
    glsafe(glBindTexture(target, m_depth_tex_id));
    glsafe(glUniform1i(glGetUniformLocation(program, "color_tex"), 6));
    glsafe(glUniform1i(glGetUniformLocation(program, "depth_tex"), 7));
    glsafe(glUniform2i(glGetUniformLocation(program, "origin"), m_viewport[0], m_viewport[1]));
    glsafe(glUniform1i(glGetUniformLocation(program, "flipped_height"), m_flipped ? m_height : 0));
    glsafe(glEnable(GL_SCISSOR_TEST));
    glsafe(glScissor(m_viewport[0] + rect[0], m_viewport[1] + rect[1], rect[2], rect[3]));
    glsafe(glDisable(GL_BLEND));
    glsafe(glDisable(GL_CULL_FACE));
    glsafe(glDepthMask(GL_TRUE));
    glsafe(glBindVertexArray(m_vao_id));
    if (m_samples > 0) {
        const int sample_index_id = glGetUniformLocation(program, "sample_index");
        glsafe(glEnable(GL_SAMPLE_MASK));
        for (int s = 0; s < m_samples; ++s) {
            glsafe(glSampleMaski(0, 1u << s));
            glsafe(glUniform1i(sample_index_id, s));
            glsafe(glDrawArrays(GL_TRIANGLES, 0, 3));
        }
        glsafe(glSampleMaski(0, ~0u));
        glsafe(glDisable(GL_SAMPLE_MASK));
    }
    else
        glsafe(glDrawArrays(GL_TRIANGLES, 0, 3));

    glsafe(glBindVertexArray(curr_vertex_array));
    glsafe(glDepthMask(depth_mask));
    if (cull_face)
        glsafe(glEnable(GL_CULL_FACE));
    if (blend)
        glsafe(glEnable(GL_BLEND));
    glsafe(glScissor(curr_scissor_box[0], curr_scissor_box[1], curr_scissor_box[2], curr_scissor_box[3]));
    if (!scissor)
        glsafe(glDisable(GL_SCISSOR_TEST));
    for (int i = 0; i < 2; ++i) {
        glsafe(glActiveTexture(GL_TEXTURE6 + i));
        glsafe(glBindTexture(target, curr_textures[i]));
    }
    glsafe(glActiveTexture(curr_active_texture));
    glsafe(glUseProgram(curr_program));
}

size_t SegmentsLayer::size_in_bytes_gpu() const
{
    return static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * static_cast<size_t>(m_samples > 0 ? m_samples : 1) * 8;
}

}
