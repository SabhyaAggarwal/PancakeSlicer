// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_REPDIAG_H
#define IGL_REPDIAG_H
#include "igl_inline.h"

#define EIGEN_YES_I_KNOW_SPARSE_MODULE_IS_NOT_STABLE_YET
#include <Eigen/Dense>
#include <Eigen/Sparse>

namespace igl
{











  template <typename T>
  IGL_INLINE void repdiag(
    const Eigen::SparseMatrix<T>& A,
    const int d,
    Eigen::SparseMatrix<T>& B);

  template <typename T>
  IGL_INLINE void repdiag(
    const Eigen::Matrix<T,Eigen::Dynamic,Eigen::Dynamic> & A,
    const int d,
    Eigen::Matrix<T,Eigen::Dynamic,Eigen::Dynamic> & B);

  template <class Mat>
  IGL_INLINE Mat repdiag(const Mat & A, const int d);
}

#ifndef IGL_STATIC_LIBRARY
#  include "repdiag.cpp"
#endif

#endif
