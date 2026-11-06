// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_EDGES_H
#define IGL_EDGES_H
#include "igl_inline.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>

namespace igl
{






  template <typename DerivedF, typename DerivedE>
  IGL_INLINE void edges(
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedE> & E);







  template <typename DerivedI, typename DerivedC, typename DerivedE>
  IGL_INLINE void edges(
    const Eigen::MatrixBase<DerivedI> & I,
    const Eigen::MatrixBase<DerivedC> & C,
    Eigen::PlainObjectBase<DerivedE> & E);




  template <typename T, typename DerivedE>
  IGL_INLINE void edges(
    const Eigen::SparseMatrix<T> & A,
    Eigen::PlainObjectBase<DerivedE> & E);
}

#ifndef IGL_STATIC_LIBRARY
#  include "edges.cpp"
#endif

#endif
