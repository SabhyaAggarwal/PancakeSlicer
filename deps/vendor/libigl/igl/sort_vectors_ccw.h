// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Olga Diamanti <olga.diam@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_SORT_VECTORS_CCW
#define IGL_SORT_VECTORS_CCW
#include "igl_inline.h"

#include <Eigen/Core>

namespace igl {













  template <typename DerivedS, typename DerivedI>
  IGL_INLINE void sort_vectors_ccw(
    const Eigen::MatrixBase<DerivedS>& P,
    const Eigen::MatrixBase<DerivedS>& N,
    Eigen::PlainObjectBase<DerivedI> &order,
    Eigen::PlainObjectBase<DerivedS> &sorted,
    Eigen::PlainObjectBase<DerivedI> &inv_order);

  template <typename DerivedS, typename DerivedI>
  IGL_INLINE void sort_vectors_ccw(
    const Eigen::MatrixBase<DerivedS>& P,
    const Eigen::MatrixBase<DerivedS>& N,
    Eigen::PlainObjectBase<DerivedI> &order,
    Eigen::PlainObjectBase<DerivedS> &sorted);

  template <typename DerivedS, typename DerivedI>
  IGL_INLINE void sort_vectors_ccw(
    const Eigen::MatrixBase<DerivedS>& P,
    const Eigen::MatrixBase<DerivedS>& N,
    Eigen::PlainObjectBase<DerivedI> &order,
    Eigen::PlainObjectBase<DerivedI> &inv_order);

  template <typename DerivedS, typename DerivedI>
  IGL_INLINE void sort_vectors_ccw(
    const Eigen::MatrixBase<DerivedS>& P,
    const Eigen::MatrixBase<DerivedS>& N,
    Eigen::PlainObjectBase<DerivedI> &order);

};


#ifndef IGL_STATIC_LIBRARY
#include "sort_vectors_ccw.cpp"
#endif


#endif
