// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Gavin Barill <gavinpcb@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/

#ifndef IGL_KNN_H
#define IGL_KNN_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>

namespace igl
{


















  template <
    typename DerivedP,
    typename IndexType,
    typename DerivedCH,
    typename DerivedCN,
    typename DerivedW,
    typename DerivedI>
  IGL_INLINE void knn(
    const Eigen::MatrixBase<DerivedP>& P,
    size_t k,
    const std::vector<std::vector<IndexType> > & point_indices,
    const Eigen::MatrixBase<DerivedCH>& CH,
    const Eigen::MatrixBase<DerivedCN>& CN,
    const Eigen::MatrixBase<DerivedW>& W,
    Eigen::PlainObjectBase<DerivedI> & I);




  template <
    typename DerivedP,
    typename DerivedV,
    typename IndexType,
    typename DerivedCH,
    typename DerivedCN,
    typename DerivedW,
    typename DerivedI>
  IGL_INLINE void knn(
    const Eigen::MatrixBase<DerivedP>& P,
    const Eigen::MatrixBase<DerivedV>& V,
    size_t k,
    const std::vector<std::vector<IndexType> > & point_indices,
    const Eigen::MatrixBase<DerivedCH>& CH,
    const Eigen::MatrixBase<DerivedCN>& CN,
    const Eigen::MatrixBase<DerivedW>& W,
    Eigen::PlainObjectBase<DerivedI> & I);
}
#ifndef IGL_STATIC_LIBRARY
#  include "knn.cpp"
#endif
#endif
