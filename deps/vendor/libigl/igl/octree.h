// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Gavin Barill <gavinpcb@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/

#ifndef IGL_OCTREE
#define IGL_OCTREE
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>




namespace igl
{
























  template <typename DerivedP, typename IndexType, typename DerivedCH,
  typename DerivedCN, typename DerivedW>
  IGL_INLINE void octree(const Eigen::MatrixBase<DerivedP>& P,
    std::vector<std::vector<IndexType> > & point_indices,
    Eigen::PlainObjectBase<DerivedCH>& CH,
    Eigen::PlainObjectBase<DerivedCN>& CN,
    Eigen::PlainObjectBase<DerivedW>& W);
}

#ifndef IGL_STATIC_LIBRARY
#  include "octree.cpp"
#endif

#endif
