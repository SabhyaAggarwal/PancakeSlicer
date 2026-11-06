// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Daniele Panozzo <daniele.panozzo@gmail.com>, Olga Diamanti <olga.diam@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_COMB_FRAME_FIELD_H
#define IGL_COMB_FRAME_FIELD_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{














  template <typename DerivedV, typename DerivedF, typename DerivedP>
  IGL_INLINE void comb_frame_field(const Eigen::MatrixBase<DerivedV> &V,
                                        const Eigen::MatrixBase<DerivedF> &F,
                                        const Eigen::MatrixBase<DerivedP> &PD1,
                                        const Eigen::MatrixBase<DerivedP> &PD2,
                                        const Eigen::MatrixBase<DerivedP> &BIS1_combed,
                                        const Eigen::MatrixBase<DerivedP> &BIS2_combed,
                                        Eigen::PlainObjectBase<DerivedP> &PD1_combed,
                                        Eigen::PlainObjectBase<DerivedP> &PD2_combed);
}
#ifndef IGL_STATIC_LIBRARY
#include "comb_frame_field.cpp"
#endif

#endif
