// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_REMESH_ALONG_ISOLINE_H
#define IGL_REMESH_ALONG_ISOLINE_H
#include "igl_inline.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>

namespace igl
{















  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedS,
    typename DerivedU,
    typename DerivedG,
    typename DerivedJ,
    typename BCtype,
    typename DerivedSU,
    typename DerivedL>
    IGL_INLINE void remesh_along_isoline(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedF> & F,
      const Eigen::MatrixBase<DerivedS> & S,
      const typename DerivedS::Scalar val,
      Eigen::PlainObjectBase<DerivedU> & U,
      Eigen::PlainObjectBase<DerivedG> & G,
      Eigen::PlainObjectBase<DerivedSU> & SU,
      Eigen::PlainObjectBase<DerivedJ> & J,
      Eigen::SparseMatrix<BCtype> & BC,
      Eigen::PlainObjectBase<DerivedL> & L);


  template <
    typename DerivedF,
    typename DerivedS,
    typename DerivedG,
    typename DerivedJ,
    typename BCtype,
    typename DerivedSU,
    typename DerivedL>
    IGL_INLINE void remesh_along_isoline(
      const int n,
      const Eigen::MatrixBase<DerivedF> & F,
      const Eigen::MatrixBase<DerivedS> & S,
      const typename DerivedS::Scalar val,
      Eigen::PlainObjectBase<DerivedG> & G,
      Eigen::PlainObjectBase<DerivedSU> & SU,
      Eigen::PlainObjectBase<DerivedJ> & J,
      Eigen::SparseMatrix<BCtype> & BC,
      Eigen::PlainObjectBase<DerivedL> & L);
}

#ifndef IGL_STATIC_LIBRARY
#  include "remesh_along_isoline.cpp"
#endif

#endif
