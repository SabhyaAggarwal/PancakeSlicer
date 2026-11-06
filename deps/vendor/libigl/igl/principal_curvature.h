// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Daniele Panozzo <daniele.panozzo@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_PRINCIPAL_CURVATURE_H
#define IGL_PRINCIPAL_CURVATURE_H


#include <Eigen/Geometry>
#include <Eigen/Dense>

#include <vector>

#include "igl_inline.h"



namespace igl
{




















  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedPD1,
    typename DerivedPD2,
    typename DerivedPV1,
    typename DerivedPV2>
  IGL_INLINE void principal_curvature(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    Eigen::PlainObjectBase<DerivedPD1>& PD1,
    Eigen::PlainObjectBase<DerivedPD2>& PD2,
    Eigen::PlainObjectBase<DerivedPV1>& PV1,
    Eigen::PlainObjectBase<DerivedPV2>& PV2,
    unsigned radius = 5,
    bool useKring = true);


  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedPD1,
    typename DerivedPD2,
    typename DerivedPV1,
    typename DerivedPV2,
    typename Index>
  IGL_INLINE void principal_curvature(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    Eigen::PlainObjectBase<DerivedPD1>& PD1,
    Eigen::PlainObjectBase<DerivedPD2>& PD2,
    Eigen::PlainObjectBase<DerivedPV1>& PV1,
    Eigen::PlainObjectBase<DerivedPV2>& PV2,
    std::vector<Index>& bad_vertices,
    unsigned radius = 5,
    bool useKring = true);
}


#ifndef IGL_STATIC_LIBRARY
#include "principal_curvature.cpp"
#endif

#endif
