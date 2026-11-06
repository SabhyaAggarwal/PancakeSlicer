// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_FACE_AREAS_H
#define IGL_FACE_AREAS_H
#include "igl_inline.h"

#include <Eigen/Dense>

namespace igl
{







  template <typename DerivedV, typename DerivedT, typename DerivedA>
  IGL_INLINE void face_areas(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedT>& T,
    Eigen::PlainObjectBase<DerivedA>& A);





  template <typename DerivedL, typename DerivedA>
  IGL_INLINE void face_areas(
    const Eigen::MatrixBase<DerivedL>& L,
    Eigen::PlainObjectBase<DerivedA>& A);



  template <typename DerivedL, typename DerivedA>
  IGL_INLINE void face_areas(
    const Eigen::MatrixBase<DerivedL>& L,
    const typename DerivedL::Scalar doublearea_nan_replacement,
    Eigen::PlainObjectBase<DerivedA>& A);
}

#ifndef IGL_STATIC_LIBRARY
#  include "face_areas.cpp"
#endif

#endif
