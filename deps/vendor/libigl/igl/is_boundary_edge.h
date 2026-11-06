// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IS_BOUNDARY_EDGE_H
#define IS_BOUNDARY_EDGE_H
#include <Eigen/Dense>

namespace igl
{







  template <
    typename DerivedF,
    typename DerivedE,
    typename DerivedB>
  void is_boundary_edge(
    const Eigen::MatrixBase<DerivedE> & E,
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedB> & B);




  template <
    typename DerivedF,
    typename DerivedE,
    typename DerivedB,
    typename DerivedEMAP>
  void is_boundary_edge(
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedB> & B,
    Eigen::PlainObjectBase<DerivedE> & E,
    Eigen::PlainObjectBase<DerivedEMAP> & EMAP);
}

#ifndef IGL_STATIC_LIBRARY
#  include "is_boundary_edge.cpp"
#endif

#endif
