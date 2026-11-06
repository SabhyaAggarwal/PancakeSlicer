#pragma once



#include "libslic3r/Point.hpp"

namespace Slic3r::Nonplanar {

class FieldChart
{
public:
    virtual ~FieldChart() = default;

    virtual Vec3d forward(const Vec3d &p) const = 0;

    virtual bool  inverse(const Vec3d &q, Vec3d &p) const = 0;

    virtual Vec3d orientation(const Vec3d &p) const = 0;

    virtual bool  given() const = 0;
};

}
