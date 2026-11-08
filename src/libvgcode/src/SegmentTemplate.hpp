///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/ libvgcode is released under the terms of the AGPLv3 or higher
#ifndef VGCODE_SEGMENTTEMPLATE_HPP
#define VGCODE_SEGMENTTEMPLATE_HPP

#include <cstddef>

namespace libvgcode {

class SegmentTemplate
{
public:
    static constexpr size_t BATCH_SEGMENTS = 8192;

    SegmentTemplate() = default;
    ~SegmentTemplate() { shutdown(); }
    SegmentTemplate(const SegmentTemplate& other) = delete;
    SegmentTemplate(SegmentTemplate&& other) = delete;
    SegmentTemplate& operator = (const SegmentTemplate& other) = delete;
    SegmentTemplate& operator = (SegmentTemplate&& other) = delete;

    void init();
    void shutdown();
    void render(size_t count, bool ribbons = false);

    size_t size_in_bytes_gpu() const { return m_size_in_bytes_gpu; }

private:
    unsigned int m_vao_id{ 0 };
    unsigned int m_vbo_id{ 0 };
    unsigned int m_ibo_id{ 0 };
    unsigned int m_ribbon_vao_id{ 0 };
    unsigned int m_ribbon_ibo_id{ 0 };
    size_t m_size_in_bytes_gpu{ 0 };
};

}

#endif