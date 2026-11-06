// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_MASSMATRIX_H
#define IGL_MASSMATRIX_H
#include "igl_inline.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>

namespace igl
{

  enum MassMatrixType
  {

    MASSMATRIX_TYPE_BARYCENTRIC = 0,


    MASSMATRIX_TYPE_VORONOI = 1,

    MASSMATRIX_TYPE_FULL = 2,


    MASSMATRIX_TYPE_DEFAULT = 3,

    NUM_MASSMATRIX_TYPES = 4
  };


















  template <typename DerivedV, typename DerivedF, typename Scalar>
  IGL_INLINE void massmatrix(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const MassMatrixType type,
    Eigen::SparseMatrix<Scalar>& M);
}

#ifndef IGL_STATIC_LIBRARY
#  include "massmatrix.cpp"
#endif

#endif
