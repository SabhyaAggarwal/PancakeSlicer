// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SPARSE_H
#define IGL_SPARSE_H
#include "igl_inline.h"
#define EIGEN_YES_I_KNOW_SPARSE_MODULE_IS_NOT_STABLE_YET
#include <Eigen/Dense>
#include <Eigen/Sparse>
namespace igl
{

















  template <
    class IndexVectorI,
    class IndexVectorJ,
    class ValueVector,
    typename T>
  IGL_INLINE void sparse(
    const IndexVectorI & I,
    const IndexVectorJ & J,
    const ValueVector & V,
    const size_t m,
    const size_t n,
    Eigen::SparseMatrix<T>& X);

  template <class IndexVector, class ValueVector, typename T>
  IGL_INLINE void sparse(
    const IndexVector & I,
    const IndexVector & J,
    const ValueVector & V,
    Eigen::SparseMatrix<T>& X);







  template <typename DerivedD, typename T>
  IGL_INLINE void sparse(
    const Eigen::MatrixBase<DerivedD>& D,
    Eigen::SparseMatrix<T>& X);

  template <typename DerivedD>
  IGL_INLINE Eigen::SparseMatrix<typename DerivedD::Scalar > sparse(
    const Eigen::MatrixBase<DerivedD>& D);

}

#ifndef IGL_STATIC_LIBRARY
#  include "sparse.cpp"
#endif

#endif
