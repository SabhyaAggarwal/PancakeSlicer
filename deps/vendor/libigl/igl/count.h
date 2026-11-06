// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COUNT_H
#define IGL_COUNT_H
#include "igl_inline.h"
#include <Eigen/Sparse>

namespace igl
{












  template <typename XType, typename SType>
  IGL_INLINE void count(
    const Eigen::SparseMatrix<XType>& X,
    const int dim,
    Eigen::SparseVector<SType>& S);



  template <typename XType, typename DerivedS>
  IGL_INLINE void count(
    const Eigen::SparseMatrix<XType>& X,
    const int dim,
    Eigen::PlainObjectBase<DerivedS>& S);
}

#ifndef IGL_STATIC_LIBRARY
#  include "count.cpp"
#endif

#endif
