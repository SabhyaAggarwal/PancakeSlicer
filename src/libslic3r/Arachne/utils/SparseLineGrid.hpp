//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.


#ifndef UTILS_SPARSE_LINE_GRID_H
#define UTILS_SPARSE_LINE_GRID_H

#include <cassert>
#include <cstddef>
#include <utility>
#include <unordered_map>
#include <vector>
#include <functional>

#include "libslic3r/Point.hpp"
#include "SparseGrid.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Arachne {

template<class ElemT, class Locator> class SparseLineGrid : public SparseGrid<ElemT>
{
public:
    using Elem = ElemT;

    SparseLineGrid(coord_t cell_size, size_t elem_reserve = 0U, float max_load_factor = 1.0f);

    void insert(const Elem &elem);

protected:
    using GridPoint = typename SparseGrid<ElemT>::GridPoint;

    Locator m_locator;
};

template<class ElemT, class Locator>
SparseLineGrid<ElemT, Locator>::SparseLineGrid(coord_t cell_size, size_t elem_reserve, float max_load_factor)
    : SparseGrid<ElemT>(cell_size, elem_reserve, max_load_factor) {}

template<class ElemT, class Locator> void SparseLineGrid<ElemT, Locator>::insert(const Elem &elem)
{
    const std::pair<Point, Point> line = m_locator(elem);
    using GridMap                      = std::unordered_multimap<GridPoint, Elem, PointHash>;
    std::function<bool(GridMap *, const GridPoint)> process_cell_func_ = [&elem](GridMap *m_grid, const GridPoint grid_loc) {
        m_grid->emplace(grid_loc, elem);
        return true;
    };
    using namespace std::placeholders;
    GridMap                             *m_grid = &(this->m_grid);
    std::function<bool(const GridPoint)> process_cell_func(std::bind(process_cell_func_, m_grid, _1));

    SparseGrid<ElemT>::processLineCells(line, process_cell_func);
}

#undef SGI_TEMPLATE
#undef SGI_THIS

}

#endif
