// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2022 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_MOMENTS_H
#define IGL_MOMENTS_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{









  template <
    typename DerivedV,
    typename DerivedF,
    typename Derivedm0,
    typename Derivedm1,
    typename Derivedm2>
  IGL_INLINE void moments(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    Derivedm0 & m0,
    Eigen::PlainObjectBase<Derivedm1>& m1,
    Eigen::PlainObjectBase<Derivedm2>& m2);
}

#ifndef IGL_STATIC_LIBRARY
#  include "moments.cpp"
#endif
#endif
