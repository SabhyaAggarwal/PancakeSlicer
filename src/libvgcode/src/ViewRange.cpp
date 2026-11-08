///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/ libvgcode is released under the terms of the AGPLv3 or higher
#include "ViewRange.hpp"
#include "../include/Types.hpp"

namespace libvgcode {

void ViewRange::set_full(Interval::value_type min, Interval::value_type max)
{
    m_full.set(min, max);
    m_full.clamp(m_enabled);
    m_enabled.clamp(m_visible);
}

void ViewRange::set_enabled(Interval::value_type min, Interval::value_type max)
{
    m_enabled.set(min, max);
    m_enabled.clamp(m_visible);
}

void ViewRange::set_visible(Interval::value_type min, Interval::value_type max)
{
    m_visible.set(min, max);
    m_enabled.clamp(m_visible);
}

void ViewRange::reset()
{
    m_full.reset();
    m_enabled.reset();
    m_visible.reset();
}

}
