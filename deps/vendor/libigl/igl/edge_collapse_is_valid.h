// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_EDGE_COLLAPSE_IS_VALID_H
#define IGL_EDGE_COLLAPSE_IS_VALID_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{

















  template <
    typename DerivedF,
    typename DerivedE,
    typename DerivedEMAP,
    typename DerivedEF,
    typename DerivedEI>
  IGL_INLINE bool edge_collapse_is_valid(
    const int e,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<DerivedE> & E,
    const Eigen::MatrixBase<DerivedEMAP> & EMAP,
    const Eigen::MatrixBase<DerivedEF> & EF,
    const Eigen::MatrixBase<DerivedEI> & EI);













  IGL_INLINE bool edge_collapse_is_valid(
      std::vector<int> & Nsv,
      std::vector<int> & Ndv);
}
#ifndef IGL_STATIC_LIBRARY
#  include "edge_collapse_is_valid.cpp"
#endif
#endif
