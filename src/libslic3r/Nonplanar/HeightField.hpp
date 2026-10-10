#pragma once
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Point.hpp"
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace Slic3r::Nonplanar {

class HeightField
{
public:
    static constexpr float NoData = std::numeric_limits<float>::quiet_NaN();

    HeightField() = default;
    HeightField(const BoundingBoxf &bbox, double pitch, float init = NoData);

    int          nx() const { return m_nx; }
    int          ny() const { return m_ny; }
    double       pitch() const { return m_pitch; }
    const Vec2d &origin() const { return m_origin; }
    BoundingBoxf bbox() const;
    bool         empty() const { return m_z.empty(); }

    float &at(int ix, int iy)       { return m_z[size_t(iy) * m_nx + ix]; }
    float  at(int ix, int iy) const { return m_z[size_t(iy) * m_nx + ix]; }
    Vec2d  node(int ix, int iy) const { return m_origin + m_pitch * Vec2d(ix, iy); }

    float sample(const Vec2d &p) const;
    Vec2d gradient(const Vec2d &p) const;
    bool  has_data(const Vec2d &p) const { return !std::isnan(this->sample(p)); }

    void min_with(const HeightField &rhs);
    void max_with(const HeightField &rhs);

    std::vector<float>       &data()       { return m_z; }
    const std::vector<float> &data() const { return m_z; }

private:
    Vec2d              m_origin { Vec2d::Zero() };
    double             m_pitch  { 0.2 };
    int                m_nx     { 0 };
    int                m_ny     { 0 };
    std::vector<float> m_z;
};

}
