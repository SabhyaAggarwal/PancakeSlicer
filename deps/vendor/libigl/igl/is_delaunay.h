// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_IS_DELAUNAY_H
#define IGL_IS_DELAUNAY_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{







  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedD>
  IGL_INLINE void is_delaunay(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedD> & D);














  template <
    typename DerivedV,
    typename DerivedF,
    typename uE2EType,
    typename InCircle,
    typename ueiType>
  IGL_INLINE bool is_delaunay(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const std::vector<std::vector<uE2EType> > & uE2E,
    const InCircle incircle,
    const ueiType uei);

}
#ifndef IGL_STATIC_LIBRARY
#include "is_delaunay.cpp"
#endif
#endif
