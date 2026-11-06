// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_UPSAMPLE_H
#define IGL_UPSAMPLE_H
#include "igl_inline.h"

#include <Eigen/Core>
#include <Eigen/Sparse>



namespace igl
{








  template <
    typename DerivedF,
    typename SType,
    typename DerivedNF>
  IGL_INLINE void upsample(
    const int n_verts,
    const Eigen::MatrixBase<DerivedF>& F,
    Eigen::SparseMatrix<SType>& S,
    Eigen::PlainObjectBase<DerivedNF>& NF);














  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedNV,
    typename DerivedNF>
  IGL_INLINE void upsample(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    Eigen::PlainObjectBase<DerivedNV>& NV,
    Eigen::PlainObjectBase<DerivedNF>& NF,
    const int number_of_subdivs = 1);


  template <
    typename MatV,
    typename MatF>
  IGL_INLINE void upsample(
    MatV& V,
    MatF& F,
    const int number_of_subdivs = 1);
}

#ifndef IGL_STATIC_LIBRARY
#  include "upsample.cpp"
#endif

#endif
