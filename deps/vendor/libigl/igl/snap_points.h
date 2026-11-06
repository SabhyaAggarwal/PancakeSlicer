// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SNAP_POINTS_H
#define IGL_SNAP_POINTS_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{







  template <
    typename DerivedC,
    typename DerivedV,
    typename DerivedI,
    typename DerivedminD,
    typename DerivedVI>
  IGL_INLINE void snap_points(
    const Eigen::MatrixBase<DerivedC > & C,
    const Eigen::MatrixBase<DerivedV > & V,
    Eigen::PlainObjectBase<DerivedI > & I,
    Eigen::PlainObjectBase<DerivedminD > & minD,
    Eigen::PlainObjectBase<DerivedVI > & VI);

  template <
    typename DerivedC,
    typename DerivedV,
    typename DerivedI,
    typename DerivedminD>
  IGL_INLINE void snap_points(
    const Eigen::MatrixBase<DerivedC > & C,
    const Eigen::MatrixBase<DerivedV > & V,
    Eigen::PlainObjectBase<DerivedI > & I,
    Eigen::PlainObjectBase<DerivedminD > & minD);

  template <
    typename DerivedC,
    typename DerivedV,
    typename DerivedI >
  IGL_INLINE void snap_points(
    const Eigen::MatrixBase<DerivedC > & C,
    const Eigen::MatrixBase<DerivedV > & V,
    Eigen::PlainObjectBase<DerivedI > & I);
}

#ifndef IGL_STATIC_LIBRARY
#  include "snap_points.cpp"
#endif

#endif
