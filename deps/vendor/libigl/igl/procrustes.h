// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Stefan Brugger <stefanbrugger@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_PROCRUSTES_H
#define IGL_PROCRUSTES_H
#include "igl_inline.h"

#include <Eigen/Dense>
#include <Eigen/Geometry>

namespace igl
{




























  template <
    typename DerivedX,
    typename DerivedY,
    typename Scalar,
    typename DerivedR,
    typename DerivedT>
  IGL_INLINE void procrustes(
    const Eigen::MatrixBase<DerivedX>& X,
    const Eigen::MatrixBase<DerivedY>& Y,
    const bool includeScaling,
    const bool includeReflections,
    Scalar& scale,
    Eigen::PlainObjectBase<DerivedR>& R,
    Eigen::PlainObjectBase<DerivedT>& t);












  template <
    typename DerivedX,
    typename DerivedY,
    typename Scalar,
    int DIM,
    int TType>
  IGL_INLINE void procrustes(
    const Eigen::MatrixBase<DerivedX>& X,
    const Eigen::MatrixBase<DerivedY>& Y,
    const bool includeScaling,
    const bool includeReflections,
    Eigen::Transform<Scalar,DIM,TType>& T);


  template <
    typename DerivedX,
    typename DerivedY,
    typename DerivedR,
    typename DerivedT>
  IGL_INLINE void procrustes(
    const Eigen::MatrixBase<DerivedX>& X,
    const Eigen::MatrixBase<DerivedY>& Y,
    const bool includeScaling,
    const bool includeReflections,
    Eigen::PlainObjectBase<DerivedR>& S,
    Eigen::PlainObjectBase<DerivedT>& t);


  template <
    typename DerivedX,
    typename DerivedY,
    typename DerivedR,
    typename DerivedT>
  IGL_INLINE void procrustes(
    const Eigen::MatrixBase<DerivedX>& X,
    const Eigen::MatrixBase<DerivedY>& Y,
    Eigen::PlainObjectBase<DerivedR>& R,
    Eigen::PlainObjectBase<DerivedT>& t);


  template <
    typename DerivedX,
    typename DerivedY,
    typename Scalar,
    typename DerivedT>
  IGL_INLINE void procrustes(
    const Eigen::MatrixBase<DerivedX>& X,
    const Eigen::MatrixBase<DerivedY>& Y,
    Eigen::Rotation2D<Scalar>& R,
    Eigen::PlainObjectBase<DerivedT>& t);
}

#ifndef IGL_STATIC_LIBRARY
  #include "procrustes.cpp"
#endif

#endif
