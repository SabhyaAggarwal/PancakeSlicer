// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef UTILS_VOXEL_UTILS_H
#define UTILS_VOXEL_UTILS_H

#include <cstddef>
#include <cassert>
#include <functional>
#include <vector>

#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/ExPolygon.hpp"

namespace Slic3r
{

using GridPoint3 = Vec3crd;

struct DilationKernel
{
    enum class Type
    {
        CUBE,
        DIAMOND,
        PRISM
    };
    GridPoint3 kernel_size_;
    Type type_;
    std::vector<GridPoint3> relative_cells_;

    DilationKernel(GridPoint3 kernel_size, Type type);
};

class VoxelUtils
{
public:
    using grid_coord_t = coord_t;

    Vec3crd cell_size_;

    VoxelUtils(Vec3crd cell_size)
        : cell_size_(cell_size)
    {
    }

    bool walkLine(Vec3crd start, Vec3crd end, const std::function<bool(GridPoint3)>& process_cell_func) const;

    bool walkPolygons(const ExPolygon& polys, coord_t z, const std::function<bool(GridPoint3)>& process_cell_func) const;

    bool walkDilatedPolygons(const ExPolygon& polys, coord_t z, const DilationKernel& kernel, const std::function<bool(GridPoint3)>& process_cell_func) const;
    bool walkDilatedPolygons(const ExPolygons& polys, coord_t z, const DilationKernel& kernel, const std::function<bool(GridPoint3)>& process_cell_func) const
    {
        for (const auto & poly : polys) {
            if (!walkDilatedPolygons(poly, z, kernel, process_cell_func)) {
                return false;
            }
        }

        return true;
    }

private:
    bool _walkAreas(const ExPolygon& polys, coord_t z, const std::function<bool(GridPoint3)>& process_cell_func) const;

public:
    bool walkAreas(const ExPolygon& polys, coord_t z, const std::function<bool(GridPoint3)>& process_cell_func) const;

    bool walkDilatedAreas(const ExPolygon& polys, coord_t z, const DilationKernel& kernel, const std::function<bool(GridPoint3)>& process_cell_func) const;
    bool walkDilatedAreas(const ExPolygons& polys, coord_t z, const DilationKernel& kernel, const std::function<bool(GridPoint3)>& process_cell_func) const
    {
        for (const auto & poly : polys) {
            if (!walkDilatedAreas(poly, z, kernel, process_cell_func)) {
                return false;
            }
        }

        return true;
    }

    std::function<bool(GridPoint3)> dilate(const DilationKernel& kernel, const std::function<bool(GridPoint3)>& process_cell_func) const;

    GridPoint3 toGridPoint(const Vec3crd& point) const
    {
        return GridPoint3(toGridCoord(point.x(), 0), toGridCoord(point.y(), 1), toGridCoord(point.z(), 2));
    }

    grid_coord_t toGridCoord(const coord_t& coord, const size_t dim) const
    {
        assert(dim < 3);
        return coord / cell_size_[dim] - (coord < 0);
    }

    Vec3crd toLowerCorner(const GridPoint3& location) const
    {
        return Vec3crd(toLowerCoord(location.x(), 0), toLowerCoord(location.y(), 1), toLowerCoord(location.z(), 2));
    }

    coord_t toLowerCoord(const grid_coord_t& grid_coord, const size_t dim) const
    {
        assert(dim < 3);
        return grid_coord * cell_size_[dim];
    }

    Polygon toPolygon(const GridPoint3 p) const
    {
        Polygon ret;
        Vec3crd c = toLowerCorner(p);
        ret.append({c.x(), c.y()});
        ret.append({c.x() + cell_size_.x(), c.y()});
        ret.append({c.x() + cell_size_.x(), c.y() + cell_size_.y()});
        ret.append({c.x(), c.y() + cell_size_.y()});
        return ret;
    }
};

}

#endif
