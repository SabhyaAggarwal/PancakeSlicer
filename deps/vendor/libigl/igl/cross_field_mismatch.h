// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Daniele Panozzo <daniele.panozzo@gmail.com>, Olga Diamanti <olga.diam@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_CROSS_FIELD_MISMATCH_H
#define IGL_CROSS_FIELD_MISMATCH_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{













  template <typename DerivedV, typename DerivedF, typename DerivedM>
  IGL_INLINE void cross_field_mismatch(const Eigen::MatrixBase<DerivedV> &V,
                                       const Eigen::MatrixBase<DerivedF> &F,
                                       const Eigen::MatrixBase<DerivedV> &PD1,
                                       const Eigen::MatrixBase<DerivedV> &PD2,
                                       const bool isCombed,
                                       Eigen::PlainObjectBase<DerivedM> &mismatch);
}
#ifndef IGL_STATIC_LIBRARY
#include "cross_field_mismatch.cpp"
#endif

#endif
