// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_RAY_MESH_INTERSECT_H
#define IGL_RAY_MESH_INTERSECT_H
#include "igl_inline.h"
#include "Hit.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{











  template <
    typename Derivedsource,
    typename Deriveddir,
    typename DerivedV,
    typename DerivedF>
  IGL_INLINE bool ray_mesh_intersect(
    const Eigen::MatrixBase<Derivedsource> & source,
    const Eigen::MatrixBase<Deriveddir> & dir,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    std::vector<igl::Hit<typename DerivedV::Scalar>> & hits);


  template <
    typename Derivedsource,
    typename Deriveddir,
    typename DerivedV,
    typename DerivedF>
  IGL_INLINE bool ray_mesh_intersect(
    const Eigen::MatrixBase<Derivedsource> & source,
    const Eigen::MatrixBase<Deriveddir> & dir,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    igl::Hit<typename DerivedV::Scalar> & hit);






  template <
    typename Derivedsource,
    typename Deriveddir,
    typename DerivedV,
    typename DerivedF>
  IGL_INLINE bool ray_triangle_intersect(
    const Eigen::MatrixBase<Derivedsource> & source,
    const Eigen::MatrixBase<Deriveddir> & dir,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const int f,
    igl::Hit<typename DerivedV::Scalar>& hit);
}
#ifndef IGL_STATIC_LIBRARY
#  include "ray_mesh_intersect.cpp"
#endif
#endif
