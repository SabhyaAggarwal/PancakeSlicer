// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_PER_VERTEX_NORMALS_H
#define IGL_PER_VERTEX_NORMALS_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{




  enum PerVertexNormalsWeightingType
  {

    PER_VERTEX_NORMALS_WEIGHTING_TYPE_UNIFORM = 0,

    PER_VERTEX_NORMALS_WEIGHTING_TYPE_AREA = 1,

    PER_VERTEX_NORMALS_WEIGHTING_TYPE_ANGLE = 2,

    PER_VERTEX_NORMALS_WEIGHTING_TYPE_DEFAULT = 3,

    NUM_PER_VERTEX_NORMALS_WEIGHTING_TYPE = 4
  };






  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedN>
  IGL_INLINE void per_vertex_normals(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    const igl::PerVertexNormalsWeightingType weighting,
    Eigen::PlainObjectBase<DerivedN> & N);

  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedN>
  IGL_INLINE void per_vertex_normals(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    Eigen::PlainObjectBase<DerivedN> & N);



  template <typename DerivedV, typename DerivedF, typename DerivedFN, typename DerivedN>
  IGL_INLINE void per_vertex_normals(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    const PerVertexNormalsWeightingType weighting,
    const Eigen::MatrixBase<DerivedFN>& FN,
    Eigen::PlainObjectBase<DerivedN> & N);

  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedFN,
    typename DerivedN>
  IGL_INLINE void per_vertex_normals(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    const Eigen::MatrixBase<DerivedFN>& FN,
    Eigen::PlainObjectBase<DerivedN> & N);
}

#ifndef IGL_STATIC_LIBRARY
#  include "per_vertex_normals.cpp"
#endif

#endif
