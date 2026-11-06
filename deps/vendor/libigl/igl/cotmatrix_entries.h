// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
// Copyright (C) 2018 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COTMATRIX_ENTRIES_H
#define IGL_COTMATRIX_ENTRIES_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{











  template <typename DerivedV, typename DerivedF, typename DerivedC>
  IGL_INLINE void cotmatrix_entries(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    Eigen::PlainObjectBase<DerivedC>& C);







  template <typename Derivedl, typename DerivedC>
  IGL_INLINE void cotmatrix_entries(
    const Eigen::MatrixBase<Derivedl>& l,
    Eigen::PlainObjectBase<DerivedC>& C);
}

#ifndef IGL_STATIC_LIBRARY
#  include "cotmatrix_entries.cpp"
#endif

#endif
