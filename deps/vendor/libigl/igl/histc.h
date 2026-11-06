// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_HISTC_H
#define IGL_HISTC_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{











  template <typename DerivedX, typename DerivedE, typename DerivedN, typename DerivedB>
  IGL_INLINE void histc(
    const Eigen::MatrixBase<DerivedX > & X,
    const Eigen::MatrixBase<DerivedE > & E,
    Eigen::PlainObjectBase<DerivedN > & N,
    Eigen::PlainObjectBase<DerivedB > & B);


  template <typename DerivedX, typename DerivedE, typename DerivedB>
  IGL_INLINE void histc(
    const Eigen::MatrixBase<DerivedX > & X,
    const Eigen::MatrixBase<DerivedE > & E,
    Eigen::PlainObjectBase<DerivedB > & B);


  template <typename DerivedE>
  IGL_INLINE void histc(
    const typename DerivedE::Scalar & x,
    const Eigen::MatrixBase<DerivedE > & E,
    typename DerivedE::Index & b);
}

#ifndef IGL_STATIC_LIBRARY
#  include "histc.cpp"
#endif

#endif
