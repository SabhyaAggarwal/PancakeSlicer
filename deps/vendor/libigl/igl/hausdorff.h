// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_HAUSDORFF_H
#define IGL_HAUSDORFF_H
#include "igl_inline.h"

#include <Eigen/Dense>
#include <functional>

namespace igl
{























  template <
    typename DerivedVA,
    typename DerivedFA,
    typename DerivedVB,
    typename DerivedFB,
    typename Scalar>
  IGL_INLINE void hausdorff(
    const Eigen::MatrixBase<DerivedVA> & VA,
    const Eigen::MatrixBase<DerivedFA> & FA,
    const Eigen::MatrixBase<DerivedVB> & VB,
    const Eigen::MatrixBase<DerivedFB> & FB,
    Scalar & d);











  template <
    typename DerivedV,
    typename Scalar>
  IGL_INLINE void hausdorff(
    const Eigen::MatrixBase<DerivedV>& V,
    const std::function<
      Scalar(const Scalar &,const Scalar &, const Scalar &)> & dist_to_B,
    Scalar & l,
    Scalar & u);
}

#ifndef IGL_STATIC_LIBRARY
#  include "hausdorff.cpp"
#endif

#endif
