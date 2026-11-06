// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_IS_INTRINSIC_DELAUNAY_H
#define IGL_IS_INTRINSIC_DELAUNAY_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{







  template <
    typename Derivedl,
    typename DerivedF,
    typename DerivedD>
  IGL_INLINE void is_intrinsic_delaunay(
    const Eigen::MatrixBase<Derivedl> & l,
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedD> & D);


  template <
    typename Derivedl,
    typename DerivedF,
    typename uE2EType,
    typename DerivedD>
  IGL_INLINE void is_intrinsic_delaunay(
    const Eigen::MatrixBase<Derivedl> & l,
    const Eigen::MatrixBase<DerivedF> & F,
    const std::vector<std::vector<uE2EType> > & uE2E,
    Eigen::PlainObjectBase<DerivedD> & D);









  template <
    typename Derivedl,
    typename uE2EType,
    typename Index>
  IGL_INLINE bool is_intrinsic_delaunay(
    const Eigen::MatrixBase<Derivedl> & l,
    const std::vector<std::vector<uE2EType> > & uE2E,
    const Index num_faces,
    const Index uei);

}
#ifndef IGL_STATIC_LIBRARY
#include "is_intrinsic_delaunay.cpp"
#endif
#endif
