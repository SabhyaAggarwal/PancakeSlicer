// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_PER_EDGE_NORMALS_H
#define IGL_PER_EDGE_NORMALS_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{

  enum PerEdgeNormalsWeightingType
  {

    PER_EDGE_NORMALS_WEIGHTING_TYPE_UNIFORM = 0,

    PER_EDGE_NORMALS_WEIGHTING_TYPE_AREA = 1,

    PER_EDGE_NORMALS_WEIGHTING_TYPE_DEFAULT = 2,

    NUM_PER_EDGE_NORMALS_WEIGHTING_TYPE = 3
  };










  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedFN,
    typename DerivedN,
    typename DerivedE,
    typename DerivedEMAP>
  IGL_INLINE void per_edge_normals(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    const PerEdgeNormalsWeightingType weight,
    const Eigen::MatrixBase<DerivedFN>& FN,
    Eigen::PlainObjectBase<DerivedN> & N,
    Eigen::PlainObjectBase<DerivedE> & E,
    Eigen::PlainObjectBase<DerivedEMAP> & EMAP);

  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedN,
    typename DerivedE,
    typename DerivedEMAP>
  IGL_INLINE void per_edge_normals(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    const PerEdgeNormalsWeightingType weight,
    Eigen::PlainObjectBase<DerivedN> & N,
    Eigen::PlainObjectBase<DerivedE> & E,
    Eigen::PlainObjectBase<DerivedEMAP> & EMAP);

  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedN,
    typename DerivedE,
    typename DerivedEMAP>
  IGL_INLINE void per_edge_normals(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    Eigen::PlainObjectBase<DerivedN> & N,
    Eigen::PlainObjectBase<DerivedE> & E,
    Eigen::PlainObjectBase<DerivedEMAP> & EMAP);
}

#ifndef IGL_STATIC_LIBRARY
#  include "per_edge_normals.cpp"
#endif

#endif
