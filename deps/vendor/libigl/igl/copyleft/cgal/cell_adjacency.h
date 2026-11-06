// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
//
#ifndef IGL_COPYLEFT_CGAL_CELL_ADJACENCY_H
#define IGL_COPYLEFT_CGAL_CELL_ADJACENCY_H
#include "../../igl_inline.h"
#include <Eigen/Core>
#include <set>
#include <tuple>
#include <vector>

namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {










      template < typename DerivedC >
      IGL_INLINE void cell_adjacency(
          const Eigen::MatrixBase<DerivedC>& per_patch_cells,
          const size_t num_cells,
          std::vector<std::set<std::tuple<typename DerivedC::Scalar, bool, size_t> > >&
          adjacency_list);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "cell_adjacency.cpp"
#endif
#endif
