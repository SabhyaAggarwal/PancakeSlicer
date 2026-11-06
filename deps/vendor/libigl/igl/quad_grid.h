// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_QUAD_GRID_H
#define IGL_QUAD_GRID_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{









  template<
    typename DerivedV,
    typename DerivedQ,
    typename DerivedE>
  IGL_INLINE void quad_grid(
    const int nx,
    const int ny,
    Eigen::PlainObjectBase<DerivedV> & V,
    Eigen::PlainObjectBase<DerivedQ> & Q,
    Eigen::PlainObjectBase<DerivedE> & E);

  template<
    typename DerivedQ,
    typename DerivedE>
  IGL_INLINE void quad_grid(
    const int nx,
    const int ny,
    Eigen::PlainObjectBase<DerivedQ> & Q,
    Eigen::PlainObjectBase<DerivedE> & E);
}

#ifndef IGL_STATIC_LIBRARY
#  include "quad_grid.cpp"
#endif
#endif
