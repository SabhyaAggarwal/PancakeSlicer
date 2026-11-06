//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef UTILS_POLYGONS_POINT_INDEX_H
#define UTILS_POLYGONS_POINT_INDEX_H

#include <utility>
#include <functional>
#include <cstddef>
#include <vector>

#include "../../Point.hpp"
#include "../../Polygon.hpp"


namespace Slic3r::Arachne
{


inline const Point &make_point(const Point &p) { return p; }




template<typename Paths>
class PathsPointIndex
{
public:



    const Paths* polygons;

    unsigned int poly_idx;

    unsigned int point_idx;








    PathsPointIndex() : polygons(nullptr), poly_idx(0), point_idx(0) {}







    PathsPointIndex(const Paths *polygons, unsigned int poly_idx, unsigned int point_idx) : polygons(polygons), poly_idx(poly_idx), point_idx(point_idx) {}




    PathsPointIndex(const PathsPointIndex& original) = default;

    Point p() const
    {
        if (!polygons)
            return {0, 0};

        return make_point((*polygons)[poly_idx][point_idx]);
    }




    bool initialized() const { return polygons; }




    const Polygon &getPolygon() const { return (*polygons)[poly_idx]; }







    bool operator==(const PathsPointIndex &other) const
    {
        return polygons == other.polygons && poly_idx == other.poly_idx && point_idx == other.point_idx;
    }
    bool operator!=(const PathsPointIndex &other) const
    {
        return !(*this == other);
    }
    bool operator<(const PathsPointIndex &other) const
    {
        return this->p() < other.p();
    }
    PathsPointIndex &operator=(const PathsPointIndex &other)
    {
        polygons  = other.polygons;
        poly_idx  = other.poly_idx;
        point_idx = other.point_idx;
        return *this;
    }

    PathsPointIndex &operator++()
    {
        point_idx = (point_idx + 1) % (*polygons)[poly_idx].size();
        return *this;
    }

    PathsPointIndex &operator--()
    {
        if (point_idx == 0)
            point_idx = (*polygons)[poly_idx].size();
        point_idx--;
        return *this;
    }

    PathsPointIndex next() const
    {
        PathsPointIndex ret(*this);
        ++ret;
        return ret;
    }

    PathsPointIndex prev() const
    {
        PathsPointIndex ret(*this);
        --ret;
        return ret;
    }
};

using PolygonsPointIndex = PathsPointIndex<Polygons>;




struct PolygonsPointIndexSegmentLocator
{
    std::pair<Point, Point> operator()(const PolygonsPointIndex &val) const
    {
        const Polygon &poly           = (*val.polygons)[val.poly_idx];
        Point          start          = poly[val.point_idx];
        unsigned int   next_point_idx = (val.point_idx + 1) % poly.size();
        Point          end            = poly[next_point_idx];
        return std::pair<Point, Point>(start, end);
    }
};




template<typename Paths>
struct PathsPointIndexLocator
{
    Point operator()(const PathsPointIndex<Paths>& val) const
    {
        return make_point(val.p());
    }
};

}

namespace std
{



template <>
struct hash<Slic3r::Arachne::PolygonsPointIndex>
{
    size_t operator()(const Slic3r::Arachne::PolygonsPointIndex& lpi) const
    {
        return Slic3r::PointHash{}(lpi.p());
    }
};
}



#endif
