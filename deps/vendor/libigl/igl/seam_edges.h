// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Yotam Gingold <yotam@yotamgingold.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SEAM_EDGES_H
#define IGL_SEAM_EDGES_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{




























  template <
    typename DerivedV,
    typename DerivedTC,
    typename DerivedF,
    typename DerivedFTC,
    typename Derivedseams,
    typename Derivedboundaries,
    typename Derivedfoldovers>
  IGL_INLINE void seam_edges(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedTC>& TC,
    const Eigen::MatrixBase<DerivedF>& F,
    const Eigen::MatrixBase<DerivedFTC>& FTC,
    Eigen::PlainObjectBase<Derivedseams>& seams,
    Eigen::PlainObjectBase<Derivedboundaries>& boundaries,
    Eigen::PlainObjectBase<Derivedfoldovers>& foldovers);
}
#ifndef IGL_STATIC_LIBRARY
#  include "seam_edges.cpp"
#endif
#endif
