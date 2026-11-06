// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2017 Zhongshi Jiang <jiangzs@nyu.edu>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_EXACT_GEODESIC_H
#define IGL_EXACT_GEODESIC_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{













    template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedVS,
    typename DerivedFS,
    typename DerivedVT,
    typename DerivedFT,
    typename DerivedD>
    IGL_INLINE void exact_geodesic(
      const Eigen::MatrixBase<DerivedV> &V,
      const Eigen::MatrixBase<DerivedF> &F,
      const Eigen::MatrixBase<DerivedVS> &VS,
      const Eigen::MatrixBase<DerivedFS> &FS,
      const Eigen::MatrixBase<DerivedVT> &VT,
      const Eigen::MatrixBase<DerivedFT> &FT,
      Eigen::PlainObjectBase<DerivedD> &D);
}

#ifndef IGL_STATIC_LIBRARY
#  include "exact_geodesic.cpp"
#endif

#endif
