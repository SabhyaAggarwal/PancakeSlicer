// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_INTRINSIC_DELAUNAY_TRIANGULATION_H
#define IGL_INTRINSIC_DELAUNAY_TRIANGULATION_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{



















  template <
    typename Derivedl_in,
    typename DerivedF_in,
    typename Derivedl,
    typename DerivedF>
  IGL_INLINE void intrinsic_delaunay_triangulation(
    const Eigen::MatrixBase<Derivedl_in> & l_in,
    const Eigen::MatrixBase<DerivedF_in> & F_in,
    Eigen::PlainObjectBase<Derivedl> & l,
    Eigen::PlainObjectBase<DerivedF> & F);









  template <
    typename Derivedl_in,
    typename DerivedF_in,
    typename Derivedl,
    typename DerivedF,
    typename DerivedE,
    typename DeriveduE,
    typename DerivedEMAP,
    typename uE2EType>
  IGL_INLINE void intrinsic_delaunay_triangulation(
    const Eigen::MatrixBase<Derivedl_in> & l_in,
    const Eigen::MatrixBase<DerivedF_in> & F_in,
    Eigen::PlainObjectBase<Derivedl> & l,
    Eigen::PlainObjectBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedE> & E,
    Eigen::PlainObjectBase<DeriveduE> & uE,
    Eigen::PlainObjectBase<DerivedEMAP> & EMAP,
    std::vector<std::vector<uE2EType> > & uE2E);
}

#ifndef IGL_STATIC_LIBRARY
#  include "intrinsic_delaunay_triangulation.cpp"
#endif

#endif
