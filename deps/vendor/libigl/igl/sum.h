// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SUM_H
#define IGL_SUM_H
#include "igl_inline.h"
#include <Eigen/Sparse>

namespace igl
{













  template <typename T>
  IGL_INLINE void sum(
    const Eigen::SparseMatrix<T>& X,
    const int dim,
    Eigen::SparseVector<T>& S);




  template <typename AType, typename DerivedB>
  IGL_INLINE void sum(
    const Eigen::SparseMatrix<AType> & A,
    const int dim,
    Eigen::PlainObjectBase<DerivedB>& B);
}

#ifndef IGL_STATIC_LIBRARY
#  include "sum.cpp"
#endif

#endif
