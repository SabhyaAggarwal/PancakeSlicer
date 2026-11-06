// Copyright (c) 2016 Scott Lenser
// Copyright (c) 2020 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef UTILS_SPARSE_POINT_GRID_H
#define UTILS_SPARSE_POINT_GRID_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "libslic3r/Point.hpp"
#include "SparseGrid.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Arachne {








template<class ElemT, class Locator> class SparsePointGrid : public SparseGrid<ElemT>
{
public:
    using Elem = ElemT;








    SparsePointGrid(coord_t cell_size, size_t elem_reserve = 0U, float max_load_factor = 1.0f);





    void insert(const Elem &elem);

protected:
    using GridPoint = typename SparseGrid<ElemT>::GridPoint;


    Locator m_locator;
};

template<class ElemT, class Locator>
SparsePointGrid<ElemT, Locator>::SparsePointGrid(coord_t cell_size, size_t elem_reserve, float max_load_factor) : SparseGrid<ElemT>(cell_size, elem_reserve, max_load_factor) {}

template<class ElemT, class Locator>
void SparsePointGrid<ElemT, Locator>::insert(const Elem &elem)
{
    Point     loc      = m_locator(elem);
    GridPoint grid_loc = SparseGrid<ElemT>::toGridPoint(loc.template cast<int64_t>());

    SparseGrid<ElemT>::m_grid.emplace(grid_loc, elem);
}

}

#endif
