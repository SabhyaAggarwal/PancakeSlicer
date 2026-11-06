// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Nico Pietroni <nico.pietroni@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_LINE_FIELD_MISSMATCH_H
#define IGL_LINE_FIELD_MISSMATCH_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{













  template <typename DerivedV, typename DerivedF, typename DerivedO>
  IGL_INLINE void line_field_mismatch(
    const Eigen::MatrixBase<DerivedV> &V,
    const Eigen::MatrixBase<DerivedF> &F,
    const Eigen::MatrixBase<DerivedV> &PD1,
    const bool isCombed,
    Eigen::PlainObjectBase<DerivedO> &mismatch);
}
#ifndef IGL_STATIC_LIBRARY
#include "line_field_mismatch.cpp"
#endif

#endif
