// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_CONNECT_BOUNDARY_TO_INFINITY_H
#define IGL_CONNECT_BOUNDARY_TO_INFINITY_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{







  template <typename DerivedF, typename DerivedFO>
  IGL_INLINE void connect_boundary_to_infinity(
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedFO> & FO);








  template <typename DerivedF, typename DerivedFO>
  IGL_INLINE void connect_boundary_to_infinity(
    const Eigen::MatrixBase<DerivedF> & F,
    const typename DerivedF::Scalar inf_index,
    Eigen::PlainObjectBase<DerivedFO> & FO);








  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedVO,
    typename DerivedFO>
  IGL_INLINE void connect_boundary_to_infinity(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedVO> & VO,
    Eigen::PlainObjectBase<DerivedFO> & FO);
}
#ifndef IGL_STATIC_LIBRARY
#  include "connect_boundary_to_infinity.cpp"
#endif
#endif
