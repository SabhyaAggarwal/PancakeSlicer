#ifndef slic3r_Geometry_ConvexHull_hpp_
#define slic3r_Geometry_ConvexHull_hpp_

#include <utility>
#include <vector>

#include "../Polygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"

namespace Slic3r {

class ExPolygon;
using ExPolygons = std::vector<ExPolygon>;

namespace Geometry {

Pointf3s convex_hull(Pointf3s points);
Polygon convex_hull(Points points);
Polygon convex_hull(const Polygons &polygons);
Polygon convex_hull(const ExPolygons &expolygons);
Polygon convex_hulll(const Polylines &polylines);

bool convex_polygons_intersect(const Polygon &A, const Polygon &B);

std::pair<std::vector<Vec2d>, std::vector<Vec2d>> decompose_convex_polygon_top_bottom(const std::vector<Vec2d> &src);

bool inside_convex_polygon(const std::pair<std::vector<Vec2d>, std::vector<Vec2d>> &top_bottom_decomposition, const Vec2d &pt);

} }

#endif
