// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COLLAPSE_EDGE_H
#define IGL_COLLAPSE_EDGE_H
#include "igl_inline.h"
#include "COLLAPSE_EDGE_NULL.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{
























































































  template <
    typename Derivedp,
    typename DerivedV,
    typename DerivedF,
    typename DerivedE,
    typename DerivedEMAP,
    typename DerivedEF,
    typename DerivedEI>
  IGL_INLINE bool collapse_edge(
    const int e,
    const Eigen::MatrixBase<Derivedp> & p,
    Eigen::MatrixBase<DerivedV> & V,
    Eigen::MatrixBase<DerivedF> & F,
    Eigen::MatrixBase<DerivedE> & E,
    Eigen::MatrixBase<DerivedEMAP> & EMAP,
    Eigen::MatrixBase<DerivedEF> & EF,
    Eigen::MatrixBase<DerivedEI> & EI,
    int & e1,
    int & e2,
    int & f1,
    int & f2);






  template
  <
    typename Derivedp,
    typename DerivedV,
    typename DerivedF,
    typename DerivedE,
    typename DerivedEMAP,
    typename DerivedEF,
    typename DerivedEI>
  IGL_INLINE bool collapse_edge(
    const int e,
    const Eigen::MatrixBase<Derivedp> & p,
      std::vector<int> & Nsv,
    const std::vector<int> & Nsf,
      std::vector<int> & Ndv,
    const std::vector<int> & Ndf,
    Eigen::MatrixBase<DerivedV> & V,
    Eigen::MatrixBase<DerivedF> & F,
    Eigen::MatrixBase<DerivedE> & E,
    Eigen::MatrixBase<DerivedEMAP> & EMAP,
    Eigen::MatrixBase<DerivedEF> & EF,
    Eigen::MatrixBase<DerivedEI> & EI,
    int & a_e1,
    int & a_e2,
    int & a_f1,
    int & a_f2);
}

#ifndef IGL_STATIC_LIBRARY
#  include "collapse_edge.cpp"
#endif
#endif
