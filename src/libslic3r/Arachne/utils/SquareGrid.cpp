//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "SquareGrid.hpp"

#include <cassert>
#include <cstdint>
#include <utility>
#include <functional>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"

using namespace Slic3r::Arachne;


SquareGrid::SquareGrid(coord_t cell_size) : cell_size(cell_size)
{
    assert(cell_size > 0U);
}


SquareGrid::GridPoint SquareGrid::toGridPoint(const Vec2i64 &point)  const
{
    return Point(toGridCoord(point.x()), toGridCoord(point.y()));
}


SquareGrid::grid_coord_t SquareGrid::toGridCoord(const int64_t &coord)  const
{






    return coord / cell_size;
}

coord_t SquareGrid::toLowerCoord(const grid_coord_t& grid_coord)  const
{






    return grid_coord * cell_size;
}


bool SquareGrid::processLineCells(const std::pair<Point, Point> line, const std::function<bool (GridPoint)>& process_cell_func)
{
    return static_cast<const SquareGrid*>(this)->processLineCells(line, process_cell_func);
}


bool SquareGrid::processLineCells(const std::pair<Point, Point> line, const std::function<bool (GridPoint)>& process_cell_func) const
{
    Point start = line.first;
    Point end = line.second;
    if (end.x() < start.x())
    {
        std::swap(start, end);
    }

    const GridPoint start_cell = toGridPoint(start.cast<int64_t>());
    const GridPoint end_cell = toGridPoint(end.cast<int64_t>());
    const int64_t y_diff = int64_t(end.y() - start.y());
    const grid_coord_t y_dir = nonzeroSign(y_diff);





    grid_coord_t x_cell_start = start_cell.x();
    for (grid_coord_t cell_y = start_cell.y(); cell_y * y_dir <= end_cell.y() * y_dir; cell_y += y_dir)
    {

        const coord_t nearest_next_y = toLowerCoord(cell_y + ((nonzeroSign(cell_y) == y_dir || cell_y == 0) ? y_dir : coord_t(0)));
        grid_coord_t x_cell_end;
        if (y_diff == 0)
        {
            x_cell_end = end_cell.x();
        }
        else
        {
            const int64_t area = int64_t(end.x() - start.x()) * int64_t(nearest_next_y - start.y());

            int64_t corresponding_x = int64_t(start.x()) + area / y_diff;
            x_cell_end = toGridCoord(corresponding_x + ((corresponding_x < 0) && ((area % y_diff) != 0)));
            if (x_cell_end < start_cell.x())
            {
                x_cell_end = x_cell_start;
            }
        }

        for (grid_coord_t cell_x = x_cell_start; cell_x <= x_cell_end; ++cell_x)
        {
            GridPoint grid_loc(cell_x, cell_y);
            if (! process_cell_func(grid_loc))
            {
                return false;
            }
            if (grid_loc == end_cell)
            {
                return true;
            }
        }



        x_cell_start = x_cell_end;
    }
    assert(false && "We should have returned already before here!");
    return false;
}

bool SquareGrid::processNearby
(
    const Point &query_pt,
    coord_t radius,
    const std::function<bool (const GridPoint&)>& process_func
) const
{
    const Point min_loc(query_pt.x() - radius, query_pt.y() - radius);
    const Point max_loc(query_pt.x() + radius, query_pt.y() + radius);

    GridPoint min_grid = toGridPoint(min_loc.cast<int64_t>());
    GridPoint max_grid = toGridPoint(max_loc.cast<int64_t>());

    for (coord_t grid_y = min_grid.y(); grid_y <= max_grid.y(); ++grid_y)
    {
        for (coord_t grid_x = min_grid.x(); grid_x <= max_grid.x(); ++grid_x)
        {
            GridPoint grid_pt(grid_x,grid_y);
            if (!process_func(grid_pt))
            {
                return false;
            }
        }
    }
    return true;
}

SquareGrid::grid_coord_t SquareGrid::nonzeroSign(const grid_coord_t z) const
{
    return (z >= 0) - (z < 0);
}

coord_t SquareGrid::getCellSize() const
{
    return cell_size;
}
