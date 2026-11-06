// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SORT_H
#define IGL_SORT_H
#include "igl_inline.h"

#include <vector>
#include <Eigen/Core>
namespace igl
{














  template <typename DerivedX, typename DerivedY, typename DerivedIX>
  IGL_INLINE void sort(
    const Eigen::DenseBase<DerivedX>& X,
    const int dim,
    const bool ascending,
    Eigen::PlainObjectBase<DerivedY>& Y,
    Eigen::PlainObjectBase<DerivedIX>& IX);

  template <typename DerivedX, typename DerivedY>
  IGL_INLINE void sort(
    const Eigen::DenseBase<DerivedX>& X,
    const int dim,
    const bool ascending,
    Eigen::PlainObjectBase<DerivedY>& Y);




  template <typename DerivedX, typename DerivedY, typename DerivedIX>
  IGL_INLINE void sort_new(
    const Eigen::DenseBase<DerivedX>& X,
    const int dim,
    const bool ascending,
    Eigen::PlainObjectBase<DerivedY>& Y,
    Eigen::PlainObjectBase<DerivedIX>& IX);


  template <typename DerivedX, typename DerivedY, typename DerivedIX>
  IGL_INLINE void sort2(
    const Eigen::DenseBase<DerivedX>& X,
    const int dim,
    const bool ascending,
    Eigen::PlainObjectBase<DerivedY>& Y,
    Eigen::PlainObjectBase<DerivedIX>& IX);


  template <typename DerivedX, typename DerivedY, typename DerivedIX>
  IGL_INLINE void sort3(
    const Eigen::DenseBase<DerivedX>& X,
    const int dim,
    const bool ascending,
    Eigen::PlainObjectBase<DerivedY>& Y,
    Eigen::PlainObjectBase<DerivedIX>& IX);






  template <class T>
  IGL_INLINE void sort(
    const std::vector<T> &unsorted,
    const bool ascending,
    std::vector<T> &sorted,
    std::vector<size_t> &index_map);
}

#ifndef IGL_STATIC_LIBRARY
#  include "sort.cpp"
#endif

#endif
