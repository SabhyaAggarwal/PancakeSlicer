// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2020 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_TETRAHEDRALIZED_GRID_H
#define IGL_TETRAHEDRALIZED_GRID_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{

  enum TetrahedralizedGripType
  {

    TETRAHEDRALIZED_GRID_TYPE_5 = 0,

    TETRAHEDRALIZED_GRID_TYPE_6_ROTATIONAL = 1,

    NUM_TETRAHEDRALIZED_GRID_TYPE = 2
  };












  template <
    typename DerivedGV,
    typename DerivedGT>
  IGL_INLINE void tetrahedralized_grid(
    const int nx,
    const int ny,
    const int nz,
    const TetrahedralizedGripType type,
    Eigen::PlainObjectBase<DerivedGV> & GV,
    Eigen::PlainObjectBase<DerivedGT> & GT);


  template <
    typename Derivedside,
    typename DerivedGT>
  IGL_INLINE void tetrahedralized_grid(
    const Eigen::MatrixBase<Derivedside> & side,
    const TetrahedralizedGripType type,
    Eigen::PlainObjectBase<DerivedGT> & GT);
}
#ifndef IGL_STATIC_LIBRARY
#  include "tetrahedralized_grid.cpp"
#endif
#endif
