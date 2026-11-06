//Copyright (c) 2020 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.


#ifndef UTILS_EXTRUSION_JUNCTION_H
#define UTILS_EXTRUSION_JUNCTION_H

#include "../../Point.hpp"
#include "libslic3r/libslic3r.h"
#include <cstddef>
#include <vector>

namespace Slic3r::Arachne
{







struct ExtrusionJunction
{




    Point p;




    coord_t w;







    size_t perimeter_index;

    ExtrusionJunction(const Point p, const coord_t w, const coord_t perimeter_index) : p(p), w(w), perimeter_index(perimeter_index) {}

    bool operator==(const ExtrusionJunction &other) const {
        return p == other.p && w == other.w && perimeter_index == other.perimeter_index;
    }

    coord_t x() const { return p.x(); }
    coord_t y() const { return p.y(); }
    coord_t z() const { return w; }
};

inline Point operator-(const ExtrusionJunction& a, const ExtrusionJunction& b)
{
    return a.p - b.p;
}


inline const Point& make_point(const ExtrusionJunction& ej)
{
    return ej.p;
}

using LineJunctions      = std::vector<ExtrusionJunction>;
using ExtrusionJunctions = std::vector<ExtrusionJunction>;

}
#endif
