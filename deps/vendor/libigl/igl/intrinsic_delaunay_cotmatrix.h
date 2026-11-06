// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_INTRINSIC_DELAUNAY_COTMATRIX_H
#define IGL_INTRINSIC_DELAUNAY_COTMATRIX_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Sparse>
namespace igl
{











  template <
    typename DerivedV,
    typename DerivedF,
    typename Scalar,
    typename Derivedl_intrinsic,
    typename DerivedF_intrinsic>
  IGL_INLINE void intrinsic_delaunay_cotmatrix(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::SparseMatrix<Scalar>& L,
    Eigen::PlainObjectBase<Derivedl_intrinsic> & l_intrinsic,
    Eigen::PlainObjectBase<DerivedF_intrinsic> & F_intrinsic);

  template <typename DerivedV, typename DerivedF, typename Scalar>
  IGL_INLINE void intrinsic_delaunay_cotmatrix(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::SparseMatrix<Scalar>& L);
}

#ifndef IGL_STATIC_LIBRARY
#  include "intrinsic_delaunay_cotmatrix.cpp"
#endif

#endif
