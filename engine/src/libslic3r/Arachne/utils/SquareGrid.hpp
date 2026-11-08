//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef UTILS_SQUARE_GRID_H
#define UTILS_SQUARE_GRID_H

#include <stdint.h>
#include <cassert>
#include <vector>
#include <functional>
#include <utility>
#include <cinttypes>

#include "../../Point.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Arachne {

class SquareGrid
{
public:
    SquareGrid(const coord_t cell_size);

    coord_t getCellSize() const;

    using GridPoint = Point;
    using grid_coord_t = coord_t;

    bool processLineCells(const std::pair<Point, Point> line, const std::function<bool (GridPoint)>& process_cell_func);

    bool processLineCells(const std::pair<Point, Point> line, const std::function<bool (GridPoint)>& process_cell_func) const;

    bool processNearby(const Point &query_pt, coord_t radius, const std::function<bool(const GridPoint &)> &process_func) const;

    GridPoint toGridPoint(const Vec2i64 &point) const;

    grid_coord_t toGridCoord(const int64_t &coord) const;

    coord_t toLowerCoord(const grid_coord_t &grid_coord) const;

protected:
    coord_t cell_size;

    grid_coord_t nonzeroSign(grid_coord_t z) const;
};

}

#endif
