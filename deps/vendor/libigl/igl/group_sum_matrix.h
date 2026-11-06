// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_GROUP_SUM_MATRIX_H
#define IGL_GROUP_SUM_MATRIX_H
#include "igl_inline.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>

namespace igl
{









  template <typename T>
  IGL_INLINE void group_sum_matrix(
    const Eigen::Matrix<int,Eigen::Dynamic,1> & G,
    const int k,
    Eigen::SparseMatrix<T>& A);

  template <typename T>
  IGL_INLINE void group_sum_matrix(
    const Eigen::Matrix<int,Eigen::Dynamic,1> & G,
    Eigen::SparseMatrix<T>& A);
}
#ifndef IGL_STATIC_LIBRARY
#  include "group_sum_matrix.cpp"
#endif
#endif
