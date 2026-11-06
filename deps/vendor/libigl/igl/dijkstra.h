// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Olga Diamanti <olga.diam@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_DIJKSTRA
#define IGL_DIJKSTRA
#include "igl_inline.h"

#include <Eigen/Core>
#include <vector>
#include <set>

namespace igl {












  template <typename IndexType, typename DerivedD, typename DerivedP>
  IGL_INLINE int dijkstra(
    const IndexType &source,
    const std::set<IndexType> &targets,
    const std::vector<std::vector<IndexType> >& VV,
    const std::vector<double>& weights,
    Eigen::PlainObjectBase<DerivedD> &min_distance,
    Eigen::PlainObjectBase<DerivedP> &previous);

  template <typename IndexType, typename DerivedV,
  typename DerivedD, typename DerivedP>
  IGL_INLINE int dijkstra(
    const Eigen::MatrixBase<DerivedV> &V,
    const std::vector<std::vector<IndexType> >& VV,
    const IndexType &source,
    const std::set<IndexType> &targets,
    Eigen::PlainObjectBase<DerivedD> &min_distance,
    Eigen::PlainObjectBase<DerivedP> &previous);

  template <typename IndexType, typename DerivedD, typename DerivedP>
  IGL_INLINE int dijkstra(
    const IndexType &source,
    const std::set<IndexType> &targets,
    const std::vector<std::vector<IndexType> >& VV,
    Eigen::PlainObjectBase<DerivedD> &min_distance,
    Eigen::PlainObjectBase<DerivedP> &previous);





  template <typename IndexType, typename DerivedP>
  IGL_INLINE void dijkstra(
    const IndexType &vertex,
    const Eigen::MatrixBase<DerivedP> &previous,
    std::vector<IndexType> &path);
}

#ifndef IGL_STATIC_LIBRARY
#include "dijkstra.cpp"
#endif


#endif
