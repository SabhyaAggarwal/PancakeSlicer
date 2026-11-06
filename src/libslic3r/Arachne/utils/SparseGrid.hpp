//Copyright (c) 2016 Scott Lenser
//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef UTILS_SPARSE_GRID_H
#define UTILS_SPARSE_GRID_H

#include <cassert>
#include <unordered_map>
#include <cstddef>
#include <vector>
#include <functional>

#include "../../Point.hpp"
#include "SquareGrid.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Arachne {








template<class ElemT> class SparseGrid : public SquareGrid
{
public:
    using Elem = ElemT;

    using GridPoint    = SquareGrid::GridPoint;
    using grid_coord_t = SquareGrid::grid_coord_t;
    using GridMap       = std::unordered_multimap<GridPoint, Elem, PointHash>;

    using iterator       = typename GridMap::iterator;
    using const_iterator = typename GridMap::const_iterator;








    SparseGrid(coord_t cell_size, size_t elem_reserve=0U, float max_load_factor=1.0f);

    iterator begin() { return m_grid.begin(); }
    iterator end() { return m_grid.end(); }
    const_iterator begin() const { return m_grid.begin(); }
    const_iterator end() const { return m_grid.end(); }
















    std::vector<Elem> getNearby(const Point &query_pt, coord_t radius) const;














    bool processNearby(const Point &query_pt, coord_t radius, const std::function<bool(const ElemT &)> &process_func) const;

protected:







    bool processFromCell(const GridPoint &grid_pt, const std::function<bool(const Elem &)> &process_func) const;


    GridMap m_grid;
};

template<class ElemT> SparseGrid<ElemT>::SparseGrid(coord_t cell_size, size_t elem_reserve, float max_load_factor) : SquareGrid(cell_size)
{

    m_grid.max_load_factor(max_load_factor);
    if (elem_reserve != 0U)
        m_grid.reserve(elem_reserve);
}

template<class ElemT> bool SparseGrid<ElemT>::processFromCell(const GridPoint &grid_pt, const std::function<bool(const Elem &)> &process_func) const
{
    auto grid_range = m_grid.equal_range(grid_pt);
    for (auto iter = grid_range.first; iter != grid_range.second; ++iter)
        if (!process_func(iter->second))
            return false;
    return true;
}

template<class ElemT>
bool SparseGrid<ElemT>::processNearby(const Point &query_pt, coord_t radius, const std::function<bool(const Elem &)> &process_func) const
{
    return SquareGrid::processNearby(query_pt, radius, [&process_func, this](const GridPoint &grid_pt) { return processFromCell(grid_pt, process_func); });
}

template<class ElemT> std::vector<typename SparseGrid<ElemT>::Elem> SparseGrid<ElemT>::getNearby(const Point &query_pt, coord_t radius) const
{
    std::vector<Elem>                       ret;
    const std::function<bool(const Elem &)> process_func = [&ret](const Elem &elem) {
        ret.push_back(elem);
        return true;
    };
    processNearby(query_pt, radius, process_func);
    return ret;
}

}

#endif
