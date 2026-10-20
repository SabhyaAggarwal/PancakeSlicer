#pragma once
#include "libslic3r/Point.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r::Nonplanar {

class LayerField
{
public:
    virtual ~LayerField() = default;

    virtual double phi(const Vec3d &p) const = 0;

    virtual double z_at(const Vec2d &xy, double level) const
    {
        double lo = std::min(0., level), hi = std::max(1., 2. * level + 1.);
        while (this->phi(Vec3d(xy.x(), xy.y(), hi)) < level)
            hi = 2. * hi + 1.;
        for (int i = 0; i < 60 && hi - lo > 1e-7; ++i) {
            const double mid = 0.5 * (lo + hi);
            (this->phi(Vec3d(xy.x(), xy.y(), mid)) < level ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    virtual Vec3d gradient(const Vec3d &p) const
    {
        const double e = 1e-3;
        Vec3d        g;
        for (int i = 0; i < 3; ++i) {
            Vec3d a = p, b = p;
            a[i] -= e;
            b[i] += e;
            g[i] = (this->phi(b) - this->phi(a)) / (2. * e);
        }
        return g;
    }

    virtual double identity_below() const { return 0.; }

    virtual double max_slope_near(const Vec2d & , double ) const { return -1.; }
};

}
