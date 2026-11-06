// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Oded Stein <oded.stein@columbia.edu>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_LOOP_H
#define IGL_LOOP_H

#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Sparse>

namespace igl
{








  template <
    typename DerivedF,
    typename SType,
    typename DerivedNF>
  IGL_INLINE void loop(
    const int n_verts,
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::SparseMatrix<SType>& S,
    Eigen::PlainObjectBase<DerivedNF> & NF);







  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedNV,
    typename DerivedNF>
  IGL_INLINE void loop(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    Eigen::PlainObjectBase<DerivedNV>& NV,
    Eigen::PlainObjectBase<DerivedNF>& NF,
    const int number_of_subdivs = 1);
}

#ifndef IGL_STATIC_LIBRARY
#  include "loop.cpp"
#endif

#endif
