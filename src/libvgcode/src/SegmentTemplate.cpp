///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/ libvgcode is released under the terms of the AGPLv3 or higher
#include "SegmentTemplate.hpp"
#include "OpenGLUtils.hpp"

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <array>
#include <vector>

namespace libvgcode {

static constexpr const std::array<uint8_t, 8> VERTEX_DATA = { 0, 1, 2, 3, 4, 5, 6, 7 };

static constexpr const std::array<uint8_t, 24> INDEX_DATA = {
    0, 1, 2,
    0, 2, 3,
    0, 3, 4,
    0, 4, 5,
    0, 5, 6,
    0, 6, 1,
    5, 4, 7,
    5, 7, 6,
};

static constexpr const std::array<uint8_t, 12> RIBBON_INDEX_DATA = { 0, 1, 4, 0, 4, 3, 1, 2, 5, 1, 5, 4 };

void SegmentTemplate::init()
{
    if (m_vao_id != 0)
        return;

    int curr_vertex_array;
    glsafe(glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &curr_vertex_array));
    int curr_array_buffer;
    glsafe(glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &curr_array_buffer));

    glsafe(glGenVertexArrays(1, &m_vao_id));
    glsafe(glBindVertexArray(m_vao_id));

#ifdef ENABLE_OPENGL_ES
    m_size_in_bytes_gpu += (VERTEX_DATA.size() + INDEX_DATA.size()) * sizeof(uint8_t);
    glsafe(glGenBuffers(1, &m_vbo_id));
    glsafe(glBindBuffer(GL_ARRAY_BUFFER, m_vbo_id));
    glsafe(glBufferData(GL_ARRAY_BUFFER, VERTEX_DATA.size() * sizeof(uint8_t), VERTEX_DATA.data(), GL_STATIC_DRAW));
    glsafe(glEnableVertexAttribArray(0));
    glsafe(glVertexAttribPointer(0, 1, GL_UNSIGNED_BYTE, GL_FALSE, 0, (const void*)0));
    const void* indices = INDEX_DATA.data();
    const size_t indices_size = INDEX_DATA.size() * sizeof(uint8_t);
#else
    std::vector<uint16_t> batch;
    batch.reserve(BATCH_SEGMENTS * INDEX_DATA.size());
    for (size_t k = 0; k < BATCH_SEGMENTS; ++k)
        for (const uint8_t index : INDEX_DATA)
            batch.push_back(static_cast<uint16_t>(k * VERTEX_DATA.size() + index));
    m_size_in_bytes_gpu += batch.size() * sizeof(uint16_t);
    const void* indices = batch.data();
    const size_t indices_size = batch.size() * sizeof(uint16_t);
#endif
    glsafe(glGenBuffers(1, &m_ibo_id));
    glsafe(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo_id));
    glsafe(glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_size, indices, GL_STATIC_DRAW));

#ifndef ENABLE_OPENGL_ES
    std::vector<uint16_t> ribbons;
    ribbons.reserve(BATCH_SEGMENTS * RIBBON_INDEX_DATA.size());
    for (size_t k = 0; k < BATCH_SEGMENTS; ++k)
        for (const uint8_t index : RIBBON_INDEX_DATA)
            ribbons.push_back(static_cast<uint16_t>(6 * k + index));
    m_size_in_bytes_gpu += ribbons.size() * sizeof(uint16_t);
    glsafe(glGenVertexArrays(1, &m_ribbon_vao_id));
    glsafe(glBindVertexArray(m_ribbon_vao_id));
    glsafe(glGenBuffers(1, &m_ribbon_ibo_id));
    glsafe(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ribbon_ibo_id));
    glsafe(glBufferData(GL_ELEMENT_ARRAY_BUFFER, ribbons.size() * sizeof(uint16_t), ribbons.data(), GL_STATIC_DRAW));
#endif

    glsafe(glBindBuffer(GL_ARRAY_BUFFER, curr_array_buffer));
    glsafe(glBindVertexArray(curr_vertex_array));
}

void SegmentTemplate::shutdown()
{
    if (m_ribbon_ibo_id != 0) {
        glsafe(glDeleteBuffers(1, &m_ribbon_ibo_id));
        m_ribbon_ibo_id = 0;
    }
    if (m_ribbon_vao_id != 0) {
        glsafe(glDeleteVertexArrays(1, &m_ribbon_vao_id));
        m_ribbon_vao_id = 0;
    }
    if (m_ibo_id != 0) {
        glsafe(glDeleteBuffers(1, &m_ibo_id));
        m_ibo_id = 0;
    }
    if (m_vbo_id != 0) {
        glsafe(glDeleteBuffers(1, &m_vbo_id));
        m_vbo_id = 0;
    }
    if (m_vao_id != 0) {
        glsafe(glDeleteVertexArrays(1, &m_vao_id));
        m_vao_id = 0;
    }

    m_size_in_bytes_gpu = 0;
}

void SegmentTemplate::render(size_t count, bool ribbons)
{
    if (m_vao_id == 0 || m_ibo_id == 0 || count == 0)
        return;

    int curr_vertex_array;
    glsafe(glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &curr_vertex_array));

#ifdef ENABLE_OPENGL_ES
    glsafe(glBindVertexArray(m_vao_id));
    glsafe(glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(INDEX_DATA.size()), GL_UNSIGNED_BYTE,
                                   nullptr, static_cast<GLsizei>(count)));
#else
    assert(count <= BATCH_SEGMENTS);
    glsafe(glBindVertexArray(ribbons ? m_ribbon_vao_id : m_vao_id));
    glsafe(glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count * (ribbons ? RIBBON_INDEX_DATA.size() : INDEX_DATA.size())),
                          GL_UNSIGNED_SHORT, nullptr));
#endif
    glsafe(glBindVertexArray(curr_vertex_array));
}

}
