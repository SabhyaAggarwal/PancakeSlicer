#include "HeightField.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>

namespace Slic3r::Nonplanar {

HeightField::HeightField(const BoundingBoxf &bbox, double pitch, float init) : m_origin(bbox.min), m_pitch(pitch)
{
    assert(pitch > 0.);
    const Vec2d size = bbox.max - bbox.min;
    m_nx             = std::max(1, int(std::floor(size.x() / pitch + 1e-9)) + 1);
    m_ny             = std::max(1, int(std::floor(size.y() / pitch + 1e-9)) + 1);
    m_z.assign(size_t(m_nx) * size_t(m_ny), init);
}

BoundingBoxf HeightField::bbox() const
{
    return BoundingBoxf(m_origin, m_origin + m_pitch * Vec2d(std::max(0, m_nx - 1), std::max(0, m_ny - 1)));
}

float HeightField::sample(const Vec2d &p) const
{
    if (this->empty())
        return NoData;
    const Vec2d u  = (p - m_origin) / m_pitch;
    const double fx = std::floor(u.x()), fy = std::floor(u.y());
    if (u.x() < 0. || u.y() < 0. || u.x() > m_nx - 1 || u.y() > m_ny - 1)
        return NoData;
    const int    ix = std::min(int(fx), std::max(0, m_nx - 2));
    const int    iy = std::min(int(fy), std::max(0, m_ny - 2));
    const double tx = m_nx > 1 ? u.x() - ix : 0.;
    const double ty = m_ny > 1 ? u.y() - iy : 0.;
    const int    jx = std::min(ix + 1, m_nx - 1);
    const int    jy = std::min(iy + 1, m_ny - 1);
    const double z00 = this->at(ix, iy), z10 = this->at(jx, iy), z01 = this->at(ix, jy), z11 = this->at(jx, jy);
    return float((1. - ty) * ((1. - tx) * z00 + tx * z10) + ty * ((1. - tx) * z01 + tx * z11));
}

Vec2d HeightField::gradient(const Vec2d &p) const
{
    const double h = 0.5 * m_pitch;
    return Vec2d((double(this->sample(p + Vec2d(h, 0.))) - double(this->sample(p - Vec2d(h, 0.)))) / m_pitch,
                 (double(this->sample(p + Vec2d(0., h))) - double(this->sample(p - Vec2d(0., h)))) / m_pitch);
}

void HeightField::min_with(const HeightField &rhs)
{
    assert(rhs.m_nx == m_nx && rhs.m_ny == m_ny);
    for (size_t i = 0; i < m_z.size(); ++i)
        m_z[i] = std::isnan(rhs.m_z[i]) ? rhs.m_z[i] : std::isnan(m_z[i]) ? m_z[i] : std::min(m_z[i], rhs.m_z[i]);
}

void HeightField::max_with(const HeightField &rhs)
{
    assert(rhs.m_nx == m_nx && rhs.m_ny == m_ny);
    for (size_t i = 0; i < m_z.size(); ++i)
        m_z[i] = std::isnan(rhs.m_z[i]) ? rhs.m_z[i] : std::isnan(m_z[i]) ? m_z[i] : std::max(m_z[i], rhs.m_z[i]);
}

}
