// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2024 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SMOOTH_CORNER_ADJACENCY_H
#define IGL_SMOOTH_CORNER_ADJACENCY_H

#include <Eigen/Core>
#include "igl_inline.h"

namespace igl
{










  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedCI,
    typename DerivedCC>
  void smooth_corner_adjacency(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const double corner_threshold_radians,
    Eigen::PlainObjectBase<DerivedCI> & CI,
    Eigen::PlainObjectBase<DerivedCC> & CC);









  template <
    typename DerivedFV,
    typename DerivedFN,
    typename DerivedCI,
    typename DerivedCC>
  void smooth_corner_adjacency(
    const Eigen::MatrixBase<DerivedFV> & FV,
    const Eigen::MatrixBase<DerivedFN> & FN,
    Eigen::PlainObjectBase<DerivedCI> & CI,
    Eigen::PlainObjectBase<DerivedCC> & CC);
}

#ifndef IGL_STATIC_LIBRARY
#  include "smooth_corner_adjacency.cpp"
#endif

#endif
