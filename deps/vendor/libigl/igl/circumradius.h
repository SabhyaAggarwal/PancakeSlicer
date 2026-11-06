// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_CIRCUMRADIUS_H
#define IGL_CIRCUMRADIUS_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{






  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedR>
  IGL_INLINE void circumradius(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedR> & R);







  template <
    typename DerivedV,
    typename DerivedT,
    typename DerivedR,
    typename DerivedC,
    typename DerivedB>
  IGL_INLINE void circumradius(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedT> & T,
    Eigen::PlainObjectBase<DerivedR> & R,
    Eigen::PlainObjectBase<DerivedC> & C,
    Eigen::PlainObjectBase<DerivedB> & B);

}
#ifndef IGL_STATIC_LIBRARY
#  include "circumradius.cpp"
#endif
#endif
