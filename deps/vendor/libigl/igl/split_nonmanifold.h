// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2022 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_SPLIT_NONMANIFOLD_H
#define IGL_SPLIT_NONMANIFOLD_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{








  template <
    typename DerivedF,
    typename DerivedSF,
    typename DerivedSVI
    >
  IGL_INLINE void split_nonmanifold(
      const Eigen::MatrixBase<DerivedF> & F,
      Eigen::PlainObjectBase <DerivedSF> & SF,
      Eigen::PlainObjectBase <DerivedSVI> & SVI);



  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedSV,
    typename DerivedSF,
    typename DerivedSVI
    >
  IGL_INLINE void split_nonmanifold(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase <DerivedSV> & SV,
    Eigen::PlainObjectBase <DerivedSF> & SF,
    Eigen::PlainObjectBase <DerivedSVI> & SVI);
}

#ifndef IGL_STATIC_LIBRARY
#  include "split_nonmanifold.cpp"
#endif

#endif
