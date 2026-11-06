// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Daniele Panozzo <daniele.panozzo@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_VERTEX_TRIANGLE_ADJACENCY_H
#define IGL_VERTEX_TRIANGLE_ADJACENCY_H
#include "igl_inline.h"

#include <Eigen/Dense>
#include <vector>

namespace igl
{














  template <typename DerivedF, typename VFType, typename VFiType>
  IGL_INLINE void vertex_triangle_adjacency(
    const typename DerivedF::Scalar n,
    const Eigen::MatrixBase<DerivedF>& F,
    std::vector<std::vector<VFType> >& VF,
    std::vector<std::vector<VFiType> >& VFi);

  template <typename DerivedV, typename DerivedF, typename IndexType>
  IGL_INLINE void vertex_triangle_adjacency(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    std::vector<std::vector<IndexType> >& VF,
    std::vector<std::vector<IndexType> >& VFi);










  template <
    typename DerivedF,
    typename DerivedVF,
    typename DerivedNI>
  IGL_INLINE void vertex_triangle_adjacency(
    const Eigen::MatrixBase<DerivedF> & F,
    const int n,
    Eigen::PlainObjectBase<DerivedVF> & VF,
    Eigen::PlainObjectBase<DerivedNI> & NI);
}

#ifndef IGL_STATIC_LIBRARY
#  include "vertex_triangle_adjacency.cpp"
#endif

#endif
