// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_CAT_H
#define IGL_CAT_H
#include "igl_inline.h"

#define EIGEN_YES_I_KNOW_SPARSE_MODULE_IS_NOT_STABLE_YET
#include <Eigen/Sparse>
#include <Eigen/Dense>
#include <vector>

namespace igl
{













  template <typename Scalar>
  IGL_INLINE void cat(
      const int dim,
      const Eigen::SparseMatrix<Scalar> & A,
      const Eigen::SparseMatrix<Scalar> & B,
      Eigen::SparseMatrix<Scalar> & C);










  template <typename Derived, class MatC>
  IGL_INLINE void cat(
    const int dim,
    const Eigen::MatrixBase<Derived> & A,
    const Eigen::MatrixBase<Derived> & B,
    MatC & C);









  template <class Mat>
  IGL_INLINE Mat cat(const int dim, const Mat & A, const Mat & B);





  template <class Mat>
  IGL_INLINE void cat(const std::vector<std::vector< Mat > > & A, Mat & C);





  template <typename T, typename DerivedC>
  IGL_INLINE void cat(const int dim, const std::vector<T> & A, Eigen::PlainObjectBase<DerivedC> & C);
}

#ifndef IGL_STATIC_LIBRARY
#  include "cat.cpp"
#endif

#endif
