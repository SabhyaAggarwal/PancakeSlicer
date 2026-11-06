// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Qingan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_FLIP_EDGE_H
#define IGL_FLIP_EDGE_H

#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>

namespace igl
{












  template <
    typename DerivedF,
    typename DerivedE,
    typename DeriveduE,
    typename DerivedEMAP,
    typename uE2EType>
  IGL_INLINE void flip_edge(
    Eigen::PlainObjectBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedE> & E,
    Eigen::PlainObjectBase<DeriveduE> & uE,
    Eigen::PlainObjectBase<DerivedEMAP> & EMAP,
    std::vector<std::vector<uE2EType> > & uE2E,
    const size_t uei);
}

#ifndef IGL_STATIC_LIBRARY
#  include "flip_edge.cpp"
#endif
#endif
