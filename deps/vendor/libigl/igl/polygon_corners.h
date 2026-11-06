// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2020 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_POLYGON_CORNERS_H
#define IGL_POLYGON_CORNERS_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>

namespace igl
{









  template <
    typename PType,
    typename DerivedI,
    typename DerivedC>
  IGL_INLINE void polygon_corners(
    const std::vector<std::vector<PType> > & P,
    Eigen::PlainObjectBase<DerivedI> & I,
    Eigen::PlainObjectBase<DerivedC> & C);






  template <
    typename DerivedQ,
    typename DerivedI,
    typename DerivedC>
  IGL_INLINE void polygon_corners(
    const Eigen::MatrixBase<DerivedQ> & Q,
    Eigen::PlainObjectBase<DerivedI> & I,
    Eigen::PlainObjectBase<DerivedC> & C);
}

#ifndef IGL_STATIC_LIBRARY
#  include "polygon_corners.cpp"
#endif

#endif
