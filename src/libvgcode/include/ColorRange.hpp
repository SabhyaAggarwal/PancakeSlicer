///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/ libvgcode is released under the terms of the AGPLv3 or higher
#ifndef VGCODE_COLORRANGE_HPP
#define VGCODE_COLORRANGE_HPP

#include "../include/Types.hpp"

#include <array>
#include <cfloat>
#include <vector>
#include <cstddef>

namespace libvgcode {

static const Palette DEFAULT_RANGES_COLORS{ {
    {  11,  44, 122 },
    {  19,  89, 133 },
    {  28, 136, 145 },
    {   4, 214,  15 },
    { 170, 242,   0 },
    { 252, 249,   3 },
    { 245, 206,  10 },
    { 227, 136,  32 },
    { 209, 104,  48 },
    { 194,  82,  60 },
    { 148,  38,  22 }
} };

class ColorRange
{
public:
    explicit ColorRange(EColorRangeType type = EColorRangeType::Linear);
    EColorRangeType get_type() const;
    const Palette& get_palette() const;
    void set_palette(const Palette& palette);
    Color get_color_at(float value) const;
    const std::array<float, 2>& get_range() const;
    std::vector<float> get_values() const;
    std::size_t size_in_bytes_cpu() const;

    static const ColorRange DUMMY_COLOR_RANGE;

private:
    EColorRangeType m_type{ EColorRangeType::Linear };
    Palette m_palette;
    std::array<float, 2> m_range{ FLT_MAX, -FLT_MAX };
    std::size_t m_count{ 0 };

    void update(float value);
    void reset();

    friend class ViewerImpl;
};

}

#endif
