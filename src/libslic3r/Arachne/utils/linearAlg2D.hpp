//Copyright (c) 2020 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef UTILS_LINEAR_ALG_2D_H
#define UTILS_LINEAR_ALG_2D_H

#include "../../Point.hpp"
#include <cstdint>
#include <math.h>
#include <cmath>

namespace Slic3r::Arachne::LinearAlg2D
{












static inline int64_t pointIsLeftOfLine(const Point &p, const Point &a, const Point &b)
{
    return int64_t(b.x() - a.x()) * int64_t(p.y() - a.y()) - int64_t(b.y() - a.y()) * int64_t(p.x() - a.x());
}


















static inline float getAngleLeft(const Point &a, const Point &b, const Point &c)
{
    const Vec2i64 ba   = (a - b).cast<int64_t>();
    const Vec2i64 bc   = (c - b).cast<int64_t>();
    const int64_t dott = ba.dot(bc);
    const int64_t det  = cross2(ba, bc);
    if (det == 0) {
        if ((ba.x() != 0 && (ba.x() > 0) == (bc.x() > 0)) || (ba.x() == 0 && (ba.y() > 0) == (bc.y() > 0)))
            return 0;
        else
            return float(M_PI);
    }
    const float angle = -atan2(double(det), double(dott));
    if (angle >= 0)
        return angle;
    else
        return M_PI * 2 + angle;
}

}
#endif
