// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Francis Williams <francis@fwilliams.info>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SPARSE_VOXEL_GRID_H
#define IGL_SPARSE_VOXEL_GRID_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{














  template <
    typename DerivedP0,
    typename Func,
    typename DerivedS,
    typename DerivedV,
    typename DerivedI>
  IGL_INLINE void sparse_voxel_grid(
    const Eigen::MatrixBase<DerivedP0>& p0,
    const Func& scalarFunc,
    const double eps,
    const int expected_number_of_cubes,
    Eigen::PlainObjectBase<DerivedS>& CS,
    Eigen::PlainObjectBase<DerivedV>& CV,
    Eigen::PlainObjectBase<DerivedI>& CI);
}

#ifndef IGL_STATIC_LIBRARY
#    include "sparse_voxel_grid.cpp"
#endif

#endif
