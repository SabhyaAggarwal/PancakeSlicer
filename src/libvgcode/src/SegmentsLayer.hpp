///|/ libvgcode is released under the terms of the AGPLv3 or higher
#ifndef VGCODE_SEGMENTSLAYER_HPP
#define VGCODE_SEGMENTSLAYER_HPP

#include <array>
#include <cstddef>

namespace libvgcode {

class SegmentsLayer
{
public:
    SegmentsLayer() = default;
    ~SegmentsLayer() { shutdown(); }
    SegmentsLayer(const SegmentsLayer& other) = delete;
    SegmentsLayer(SegmentsLayer&& other) = delete;
    SegmentsLayer& operator = (const SegmentsLayer& other) = delete;
    SegmentsLayer& operator = (SegmentsLayer&& other) = delete;

    void init(unsigned int program_id, unsigned int program_ms_id);
    void shutdown();

    bool begin(bool clear);
    void end();
    void composite(const std::array<int, 4>& rect) const;
    bool flipped() const { return m_flipped; }

    size_t size_in_bytes_gpu() const;

private:
    unsigned int m_fbo_id{ 0 };
    unsigned int m_color_tex_id{ 0 };
    unsigned int m_depth_tex_id{ 0 };
    int m_width{ 0 };
    int m_height{ 0 };
    int m_samples{ 0 };
    int m_depth_bits{ 0 };
    bool m_flipped{ false };
    bool m_mesa{ false };

    unsigned int m_program_id{ 0 };
    unsigned int m_program_ms_id{ 0 };
    unsigned int m_vao_id{ 0 };

    int m_prev_draw_fbo{ 0 };
    std::array<int, 4> m_viewport{ 0, 0, 0, 0 };
};

}

#endif
