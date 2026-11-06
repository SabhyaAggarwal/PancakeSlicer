// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_MARCHINGCUBES_H
#define IGL_COPYLEFT_MARCHINGCUBES_H
#include "../igl_inline.h"

#include <Eigen/Core>
namespace igl
{
  namespace copyleft
  {






















    template <typename DerivedValues, typename DerivedPoints, typename DerivedVertices, typename DerivedFaces>
    IGL_INLINE void marching_cubes(
        const Eigen::MatrixBase<DerivedValues> &values,
        const Eigen::MatrixBase<DerivedPoints> &points,
        const unsigned x_res,
        const unsigned y_res,
        const unsigned z_res,
        const double isovalue,
        Eigen::PlainObjectBase<DerivedVertices> &vertices,
        Eigen::PlainObjectBase<DerivedFaces> &faces);


    template <typename DerivedValues, typename DerivedPoints, typename DerivedVertices, typename DerivedFaces>
    IGL_INLINE void marching_cubes(
      const Eigen::MatrixBase<DerivedValues> &values,
      const Eigen::MatrixBase<DerivedPoints> &points,
      const unsigned x_res,
      const unsigned y_res,
      const unsigned z_res,
      Eigen::PlainObjectBase<DerivedVertices> &vertices,
      Eigen::PlainObjectBase<DerivedFaces> &faces);


    template <
      typename DerivedValue,
      typename DerivedPoint,
      typename DerivedPoints,
      typename DerivedVertices,
      typename DerivedFaces>
    IGL_INLINE void marching_cubes(
        const std::function< DerivedValue(const DerivedPoint & ) > & value_fun,
        const Eigen::MatrixBase<DerivedPoints> &points,
        const unsigned x_res,
        const unsigned y_res,
        const unsigned z_res,
        const double isovalue,
        Eigen::PlainObjectBase<DerivedVertices> &vertices,
        Eigen::PlainObjectBase<DerivedFaces> &faces);

















    template <typename DerivedValues, typename DerivedPoints, typename DerivedVertices, typename DerivedIndices, typename DerivedFaces>
    IGL_INLINE void marching_cubes(
      const Eigen::MatrixBase<DerivedValues> &values,
      const Eigen::MatrixBase<DerivedPoints> &points,
      const Eigen::MatrixBase<DerivedIndices> &indices,
      const double isovalue,
      Eigen::PlainObjectBase<DerivedVertices> &vertices,
      Eigen::PlainObjectBase<DerivedFaces> &faces);


    template <typename DerivedValues, typename DerivedPoints, typename DerivedVertices, typename DerivedIndices, typename DerivedFaces>
    IGL_INLINE void marching_cubes(
      const Eigen::MatrixBase<DerivedValues> &values,
      const Eigen::MatrixBase<DerivedPoints> &points,
      const Eigen::MatrixBase<DerivedIndices> &indices,
      Eigen::PlainObjectBase<DerivedVertices> &vertices,
      Eigen::PlainObjectBase<DerivedFaces> &faces);
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "marching_cubes.cpp"
#endif

#endif
