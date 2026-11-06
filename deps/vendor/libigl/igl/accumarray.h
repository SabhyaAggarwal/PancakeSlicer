// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef ACCUMARRY_H
#define ACCUMARRY_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{





  template <
    typename DerivedS,
    typename DerivedV,
    typename DerivedA
    >
  void accumarray(
    const Eigen::MatrixBase<DerivedS> & S,
    const Eigen::MatrixBase<DerivedV> & V,
    Eigen::PlainObjectBase<DerivedA> & A);





  template <
    typename DerivedS,
    typename DerivedA
    >
  void accumarray(
    const Eigen::MatrixBase<DerivedS> & S,
    const typename DerivedA::Scalar V,
    Eigen::PlainObjectBase<DerivedA> & A);
}

#ifndef IGL_STATIC_LIBRARY
#  include "accumarray.cpp"
#endif

#endif
