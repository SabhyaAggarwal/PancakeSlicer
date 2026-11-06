// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_PER_FACE_NORMALS_H
#define IGL_PER_FACE_NORMALS_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{










  template <typename DerivedV, typename DerivedF, typename DerivedZ, typename DerivedN>
  IGL_INLINE void per_face_normals(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    const Eigen::MatrixBase<DerivedZ> & Z,
    Eigen::PlainObjectBase<DerivedN> & N);



  template <typename DerivedV, typename DerivedF, typename DerivedN>
  IGL_INLINE void per_face_normals(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    Eigen::PlainObjectBase<DerivedN> & N);



  template <typename DerivedV, typename DerivedF, typename DerivedN>
  IGL_INLINE void per_face_normals_stable(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    Eigen::PlainObjectBase<DerivedN> & N);










  template <
    typename DerivedV,
    typename DerivedI,
    typename DerivedC,
    typename DerivedN,
    typename DerivedVV,
    typename DerivedFF,
    typename DerivedJ>
  IGL_INLINE void per_face_normals(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedI> & I,
    const Eigen::MatrixBase<DerivedC> & C,
    Eigen::PlainObjectBase<DerivedN> & N,
    Eigen::PlainObjectBase<DerivedVV> & VV,
    Eigen::PlainObjectBase<DerivedFF> & FF,
    Eigen::PlainObjectBase<DerivedJ> & J);
}

#ifndef IGL_STATIC_LIBRARY
#  include "per_face_normals.cpp"
#endif

#endif
