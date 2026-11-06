// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_BARYCENTRIC_COORDINATES_H
#define IGL_BARYCENTRIC_COORDINATES_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{









  template <
    typename DerivedP,
    typename DerivedA,
    typename DerivedB,
    typename DerivedC,
    typename DerivedD,
    typename DerivedL>
  IGL_INLINE void barycentric_coordinates(
    const Eigen::MatrixBase<DerivedP> & P,
    const Eigen::MatrixBase<DerivedA> & A,
    const Eigen::MatrixBase<DerivedB> & B,
    const Eigen::MatrixBase<DerivedC> & C,
    const Eigen::MatrixBase<DerivedD> & D,
    Eigen::PlainObjectBase<DerivedL> & L);








  template <
    typename DerivedP,
    typename DerivedA,
    typename DerivedB,
    typename DerivedC,
    typename DerivedL>
  IGL_INLINE void barycentric_coordinates(
    const Eigen::MatrixBase<DerivedP> & P,
    const Eigen::MatrixBase<DerivedA> & A,
    const Eigen::MatrixBase<DerivedB> & B,
    const Eigen::MatrixBase<DerivedC> & C,
    Eigen::PlainObjectBase<DerivedL> & L);

}

#ifndef IGL_STATIC_LIBRARY
#  include "barycentric_coordinates.cpp"
#endif

#endif
