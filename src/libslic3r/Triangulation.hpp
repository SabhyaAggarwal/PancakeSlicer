#ifndef libslic3r_Triangulation_hpp_
#define libslic3r_Triangulation_hpp_

#include <utility>
#include <cstdint>
#include <vector>
#include <set>
#include <libslic3r/Point.hpp>
#include <libslic3r/Polygon.hpp>
#include <libslic3r/ExPolygon.hpp>

namespace Slic3r {

class Triangulation
{
public:
    Triangulation() = delete;


    using HalfEdge  = std::pair<uint32_t, uint32_t>;
    using HalfEdges = std::vector<HalfEdge>;
    using Indices   = std::vector<Vec3i32>;










    static Indices triangulate(const Points &points,
                               const HalfEdges &half_edges);
    static Indices triangulate(const Polygon &polygon);
    static Indices triangulate(const Polygons &polygons);
    static Indices triangulate(const ExPolygon &expolygon);
    static Indices triangulate(const ExPolygons &expolygons);



    using Changes = std::vector<uint32_t>;








    static Changes create_changes(const Points &points, const Points &duplicits);









    static Indices triangulate(const ExPolygons &expolygons, const Points& points);








    static Indices triangulate(const ExPolygons &expolygons, const Points& points, const Changes& changes);
};

}
#endif
