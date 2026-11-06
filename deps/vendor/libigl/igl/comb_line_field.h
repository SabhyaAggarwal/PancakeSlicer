// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Nico Pietroni <nico.pietroni@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_COMB_LINE_FIELD_H
#define IGL_COMB_LINE_FIELD_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{







  template <typename DerivedV, typename DerivedF>
  IGL_INLINE void comb_line_field(const Eigen::MatrixBase<DerivedV> &V,
                                  const Eigen::MatrixBase<DerivedF> &F,
                                  const Eigen::MatrixBase<DerivedV> &PD1in,
                                  Eigen::PlainObjectBase<DerivedV> &PD1out);
}
#ifndef IGL_STATIC_LIBRARY
#include "comb_line_field.cpp"
#endif

#endif
