// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_NORMAL_DERIVATIVE_H
#define IGL_NORMAL_DERIVATIVE_H
#include "igl_inline.h"

#include <Eigen/Core>
#include <Eigen/Sparse>
namespace igl
{










  template <
    typename DerivedV,
    typename DerivedEle,
    typename Scalar>
  IGL_INLINE void normal_derivative(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedEle> & Ele,
    Eigen::SparseMatrix<Scalar>& DD);
}

#ifndef IGL_STATIC_LIBRARY
#  include "normal_derivative.cpp"
#endif

#endif
