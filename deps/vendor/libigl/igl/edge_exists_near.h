// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_EDGE_EXISTS_NEAR_H
#define IGL_EDGE_EXISTS_NEAR_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{














  template <
    typename DeriveduE,
    typename DerivedEMAP,
    typename uE2EType,
    typename Index>
  IGL_INLINE bool edge_exists_near(
    const Eigen::MatrixBase<DeriveduE> & uE,
    const Eigen::MatrixBase<DerivedEMAP> & EMAP,
    const std::vector<std::vector< uE2EType> > & uE2E,
    const Index & a,
    const Index & b,
    const Index & uei);
}
#ifndef IGL_STATIC_LIBRARY
#  include "edge_exists_near.cpp"
#endif
#endif
