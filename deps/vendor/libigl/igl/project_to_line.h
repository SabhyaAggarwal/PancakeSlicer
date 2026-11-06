// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_PROJECT_TO_LINE_H
#define IGL_PROJECT_TO_LINE_H
#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{













  template <
    typename DerivedP,
    typename DerivedS,
    typename DerivedD,
    typename Derivedt,
    typename DerivedsqrD>
  IGL_INLINE void project_to_line(
    const Eigen::MatrixBase<DerivedP> & P,
    const Eigen::MatrixBase<DerivedS> & S,
    const Eigen::MatrixBase<DerivedD> & D,
    Eigen::PlainObjectBase<Derivedt> & t,
    Eigen::PlainObjectBase<DerivedsqrD> & sqrD);


  template <typename Scalar>
  IGL_INLINE void project_to_line(
    const Scalar px,
    const Scalar py,
    const Scalar pz,
    const Scalar sx,
    const Scalar sy,
    const Scalar sz,
    const Scalar dx,
    const Scalar dy,
    const Scalar dz,
    Scalar & projpx,
    Scalar & projpy,
    Scalar & projpz,
    Scalar & t,
    Scalar & sqrd);

  template <typename Scalar>
  IGL_INLINE void project_to_line(
    const Scalar px,
    const Scalar py,
    const Scalar pz,
    const Scalar sx,
    const Scalar sy,
    const Scalar sz,
    const Scalar dx,
    const Scalar dy,
    const Scalar dz,
    Scalar & t,
    Scalar & sqrd);

}

#ifndef IGL_STATIC_LIBRARY
#  include "project_to_line.cpp"
#endif

#endif
