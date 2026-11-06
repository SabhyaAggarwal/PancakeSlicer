// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COTMATRIX_H
#define IGL_COTMATRIX_H
#include "igl_inline.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>








namespace igl
{




















  template <typename DerivedV, typename DerivedF, typename Scalar>
  IGL_INLINE void cotmatrix(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::SparseMatrix<Scalar>& L);











  template <
    typename DerivedV,
    typename DerivedI,
    typename DerivedC,
    typename Scalar>
  IGL_INLINE void cotmatrix(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedI> & I,
    const Eigen::MatrixBase<DerivedC> & C,
    Eigen::SparseMatrix<Scalar>& L,
    Eigen::SparseMatrix<Scalar>& M,
    Eigen::SparseMatrix<Scalar>& P);
}

#ifndef IGL_STATIC_LIBRARY
#  include "cotmatrix.cpp"
#endif

#endif
