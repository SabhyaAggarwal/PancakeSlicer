// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_ITERATIVE_CLOSEST_POINT_H
#define IGL_ITERATIVE_CLOSEST_POINT_H
#include "igl_inline.h"
#include <Eigen/Core>
#include "AABB.h"

namespace igl
{



















  template <
    typename DerivedVX,
    typename DerivedFX,
    typename DerivedVY,
    typename DerivedFY,
    typename DerivedR,
    typename Derivedt
    >
  IGL_INLINE void iterative_closest_point(
    const Eigen::MatrixBase<DerivedVX> & VX,
    const Eigen::MatrixBase<DerivedFX> & FX,
    const Eigen::MatrixBase<DerivedVY> & VY,
    const Eigen::MatrixBase<DerivedFY> & FY,
    const int num_samples,
    const int max_iters,
    Eigen::PlainObjectBase<DerivedR> & R,
    Eigen::PlainObjectBase<Derivedt> & t);



  template <
    typename DerivedVX,
    typename DerivedFX,
    typename DerivedVY,
    typename DerivedFY,
    typename DerivedNY,
    typename DerivedR,
    typename Derivedt
    >
  IGL_INLINE void iterative_closest_point(
    const Eigen::MatrixBase<DerivedVX> & VX,
    const Eigen::MatrixBase<DerivedFX> & FX,
    const Eigen::MatrixBase<DerivedVY> & VY,
    const Eigen::MatrixBase<DerivedFY> & FY,
    const igl::AABB<DerivedVY,3> & Ytree,
    const Eigen::MatrixBase<DerivedNY> & NY,
    const int num_samples,
    const int max_iters,
    Eigen::PlainObjectBase<DerivedR> & R,
    Eigen::PlainObjectBase<Derivedt> & t);
}

#ifndef IGL_STATIC_LIBRARY
#  include "iterative_closest_point.cpp"
#endif

#endif
