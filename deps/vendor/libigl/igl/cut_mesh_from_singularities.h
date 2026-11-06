// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Daniele Panozzo <daniele.panozzo@gmail.com>, Olga Diamanti <olga.diam@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_CUT_MESH_FROM_SINGULARITIES_H
#define IGL_CUT_MESH_FROM_SINGULARITIES_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{










  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedM,
    typename DerivedO>
  IGL_INLINE void cut_mesh_from_singularities(
    const Eigen::MatrixBase<DerivedV> &V,
    const Eigen::MatrixBase<DerivedF> &F,
    const Eigen::MatrixBase<DerivedM> &MMatch,
    Eigen::PlainObjectBase<DerivedO> &seams);
}
#ifndef IGL_STATIC_LIBRARY
#include "cut_mesh_from_singularities.cpp"
#endif

#endif
