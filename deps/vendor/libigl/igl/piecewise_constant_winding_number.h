// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_PIECEWISE_CONSTANT_WINDING_NUMBER_H
#define IGL_PIECEWISE_CONSTANT_WINDING_NUMBER_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{



















  template <
    typename DerivedF,
    typename DeriveduE,
    typename DeriveduEC,
    typename DeriveduEE>
  IGL_INLINE bool piecewise_constant_winding_number(
    const Eigen::MatrixBase<DerivedF>& F,
    const Eigen::MatrixBase<DeriveduE>& uE,
    const Eigen::MatrixBase<DeriveduEC>& uEC,
    const Eigen::MatrixBase<DeriveduEE>& uEE);

  template <typename DerivedF>
  IGL_INLINE bool piecewise_constant_winding_number(
    const Eigen::MatrixBase<DerivedF>& F);
}
#ifndef IGL_STATIC_LIBRARY
#  include "piecewise_constant_winding_number.cpp"
#endif
#endif
