// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_BOUNDARY_FACETS_H
#define IGL_BOUNDARY_FACETS_H
#include "igl_inline.h"

#include <Eigen/Dense>

#include <vector>

namespace igl
{










  template <
    typename DerivedT,
    typename DerivedF,
    typename DerivedJ,
    typename DerivedK>
  IGL_INLINE void boundary_facets(
    const Eigen::MatrixBase<DerivedT>& T,
    Eigen::PlainObjectBase<DerivedF>& F,
    Eigen::PlainObjectBase<DerivedJ>& J,
    Eigen::PlainObjectBase<DerivedK>& K);




  template <typename DerivedT, typename DerivedF>
  IGL_INLINE void boundary_facets(
    const Eigen::MatrixBase<DerivedT>& T,
    Eigen::PlainObjectBase<DerivedF>& F);




  template <typename DerivedT, typename Ret>
  Ret boundary_facets(
    const Eigen::MatrixBase<DerivedT>& T);
}

#ifndef IGL_STATIC_LIBRARY
#  include "boundary_facets.cpp"
#endif

#endif
