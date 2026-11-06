#ifndef slic3r_PrincipalComponents2D_hpp_
#define slic3r_PrincipalComponents2D_hpp_

#include "Point.hpp"
#include <tuple>
#include <vector>
#include "Polygon.hpp"

namespace Slic3r {






std::tuple<float, Vec2f, Vec2f, float> compute_moments_of_area_of_triangle(const Vec2f &a, const Vec2f &b, const Vec2f &c);


std::tuple<Vec2f, Vec2f> compute_principal_components(const Polygons &polys);

}

#endif
