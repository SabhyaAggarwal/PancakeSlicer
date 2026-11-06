// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_TRIANGULATEGRID_H
#define IGL_TRIANGULATEGRID_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{









  template <
    typename XType,
    typename YType,
    typename DerivedGV,
    typename DerivedGF>
  IGL_INLINE void triangulated_grid(
    const XType & nx,
    const YType & ny,
    Eigen::PlainObjectBase<DerivedGV> & GV,
    Eigen::PlainObjectBase<DerivedGF> & GF);

  template <
    typename XType,
    typename YType,
    typename DerivedGF>
  IGL_INLINE void triangulated_grid(
    const XType & nx,
    const YType & ny,
    Eigen::PlainObjectBase<DerivedGF> & GF);
}
#ifndef IGL_STATIC_LIBRARY
#  include "triangulated_grid.cpp"
#endif
#endif
