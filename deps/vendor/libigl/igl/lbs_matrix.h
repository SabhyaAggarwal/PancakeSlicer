// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_LBS_MATRIX_H
#define IGL_LBS_MATRIX_H
#include "igl_inline.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>

namespace igl
{














  IGL_INLINE void lbs_matrix(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXd & W,
    Eigen::MatrixXd & M);


















  IGL_INLINE void lbs_matrix_column(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXd & W,
    Eigen::SparseMatrix<double>& M);

  IGL_INLINE void lbs_matrix_column(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXd & W,
    Eigen::MatrixXd & M);








  IGL_INLINE void lbs_matrix_column(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXd & W,
    const Eigen::MatrixXi & WI,
    Eigen::SparseMatrix<double>& M);

  IGL_INLINE void lbs_matrix_column(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXd & W,
    const Eigen::MatrixXi & WI,
    Eigen::MatrixXd & M);
}
#ifndef IGL_STATIC_LIBRARY
#include "lbs_matrix.cpp"
#endif
#endif
