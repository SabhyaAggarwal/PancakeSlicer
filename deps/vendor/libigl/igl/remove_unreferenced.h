// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
//
//  remove_unreferenced.h
//  Preview3D
//
//  Created by Daniele Panozzo on 17/11/11.

#ifndef IGL_REMOVE_UNREFERENCED_H
#define IGL_REMOVE_UNREFERENCED_H
#include "igl_inline.h"

#include <Eigen/Core>
namespace igl
{









  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedNV,
    typename DerivedNF,
    typename DerivedI,
    typename DerivedJ>
  IGL_INLINE void remove_unreferenced(
    const Eigen::MatrixBase<DerivedV> &V,
    const Eigen::MatrixBase<DerivedF> &F,
    Eigen::PlainObjectBase<DerivedNV> &NV,
    Eigen::PlainObjectBase<DerivedNF> &NF,
    Eigen::PlainObjectBase<DerivedI> &I,
    Eigen::PlainObjectBase<DerivedJ> &J);

  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedNV,
    typename DerivedNF,
    typename DerivedI>
  IGL_INLINE void remove_unreferenced(
    const Eigen::MatrixBase<DerivedV> &V,
    const Eigen::MatrixBase<DerivedF> &F,
    Eigen::PlainObjectBase<DerivedNV> &NV,
    Eigen::PlainObjectBase<DerivedNF> &NF,
    Eigen::PlainObjectBase<DerivedI> &I);


  template <
    typename DerivedF,
    typename DerivedI,
    typename DerivedJ>
  IGL_INLINE void remove_unreferenced(
    const size_t n,
    const Eigen::MatrixBase<DerivedF> &F,
    Eigen::PlainObjectBase<DerivedI> &I,
    Eigen::PlainObjectBase<DerivedJ> &J);

}

#ifndef IGL_STATIC_LIBRARY
#  include "remove_unreferenced.cpp"
#endif

#endif
