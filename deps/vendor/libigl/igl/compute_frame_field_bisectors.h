// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Daniele Panozzo <daniele.panozzo@gmail.com>, Olga Diamanti <olga.diam@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_COMPUTE_FRAME_FIELD_BISECTORS_H
#define IGL_COMPUTE_FRAME_FIELD_BISECTORS_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{













  template <typename DerivedV, typename DerivedF>
  IGL_INLINE void compute_frame_field_bisectors(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    const Eigen::MatrixBase<DerivedV>& B1,
    const Eigen::MatrixBase<DerivedV>& B2,
    const Eigen::MatrixBase<DerivedV>& PD1,
    const Eigen::MatrixBase<DerivedV>& PD2,
    Eigen::PlainObjectBase<DerivedV>& BIS1,
    Eigen::PlainObjectBase<DerivedV>& BIS2);

  template <typename DerivedV, typename DerivedF>
  IGL_INLINE void compute_frame_field_bisectors(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    const Eigen::MatrixBase<DerivedV>& PD1,
    const Eigen::MatrixBase<DerivedV>& PD2,
    Eigen::PlainObjectBase<DerivedV>& BIS1,
    Eigen::PlainObjectBase<DerivedV>& BIS2);
}

#ifndef IGL_STATIC_LIBRARY
#  include "compute_frame_field_bisectors.cpp"
#endif

#endif
