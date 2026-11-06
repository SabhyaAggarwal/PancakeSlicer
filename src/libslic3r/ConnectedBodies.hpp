#pragma once

#include "BoundingBox.hpp"
#include "ExPolygon.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "TriangleMesh.hpp"
#include "libslic3r.h"

#include <admesh/stl.h>

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace Slic3r {



std::vector<std::vector<size_t>> connected_bodies(const std::vector<const ExPolygons *> &layers, size_t &count,
                                                  const std::function<void()> &throw_if_canceled = nullptr);


class IslandLocator
{
public:

    IslandLocator(const ExPolygons &islands, coord_t margin);

    bool holds(size_t island, const Point &point, bool strict = false) const;


    std::pair<int, double> find(const Point &point, bool strict = false) const;
    const std::vector<BoundingBox> &boxes() const { return m_boxes; }

private:
    const ExPolygons        *m_islands;
    std::vector<BoundingBox> m_boxes;
    std::vector<bool>        m_alone;
};

using MeshInPlace = std::pair<const indexed_triangle_set *, Transform3d>;


struct SolidBody : MassProperties
{
    Polygon hull;
    double  z_min{ 0. };
    double  z_max{ 0. };


    BoundingBoxf3 bounding_box(const Transform3d &trafo) const;
};



std::vector<SolidBody> solid_bodies(const std::vector<MeshInPlace> &solids, const std::vector<double> &densities,
                                    const std::vector<MeshInPlace> &negatives, size_t slabs);

}
