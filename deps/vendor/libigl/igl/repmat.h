// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_REPMAT_H
#define IGL_REPMAT_H
#include "igl_inline.h"

#define EIGEN_YES_I_KNOW_SPARSE_MODULE_IS_NOT_STABLE_YET
#include <Eigen/Dense>
#include <Eigen/Sparse>

namespace igl
{











  template <typename DerivedA,typename DerivedB>
  IGL_INLINE void repmat(
    const Eigen::MatrixBase<DerivedA> & A,
    const int r,
    const int c,
    Eigen::PlainObjectBase<DerivedB> & B);

  template <typename T, int majorType>
  IGL_INLINE void repmat(
    const Eigen::SparseMatrix<T, majorType> & A,
    const int r,
    const int c,
    Eigen::SparseMatrix<T, majorType> & B);
}

#ifndef IGL_STATIC_LIBRARY
#  include "repmat.cpp"
#endif

#endif
