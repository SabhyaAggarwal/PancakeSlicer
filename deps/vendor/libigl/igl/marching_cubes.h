// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2020 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_MARCHING_CUBES_H
#define IGL_MARCHING_CUBES_H
#include "igl_inline.h"
#include <unordered_map>

#include <Eigen/Core>
namespace igl
{













  template <
    typename DerivedS,
    typename DerivedGV,
    typename DerivedV,
    typename DerivedF>
  IGL_INLINE void marching_cubes(
    const Eigen::MatrixBase<DerivedS> & S,
    const Eigen::MatrixBase<DerivedGV> & GV,
    const unsigned nx,
    const unsigned ny,
    const unsigned nz,
    const typename DerivedS::Scalar isovalue,
    Eigen::PlainObjectBase<DerivedV> &V,
    Eigen::PlainObjectBase<DerivedF> &F);






  template <
    typename DerivedS,
    typename DerivedGV,
    typename DerivedV,
    typename DerivedF>
  IGL_INLINE void marching_cubes(
    const Eigen::MatrixBase<DerivedS> & S,
    const Eigen::MatrixBase<DerivedGV> & GV,
    const unsigned nx,
    const unsigned ny,
    const unsigned nz,
    const typename DerivedS::Scalar isovalue,
    Eigen::PlainObjectBase<DerivedV> &V,
    Eigen::PlainObjectBase<DerivedF> &F,
    std::unordered_map<std::int64_t,int> &E2V);







  template <
    typename DerivedS,
    typename DerivedGV,
    typename DerivedGI,
    typename DerivedV,
    typename DerivedF>
  IGL_INLINE void marching_cubes(
    const Eigen::MatrixBase<DerivedS> & S,
    const Eigen::MatrixBase<DerivedGV> & GV,
    const Eigen::MatrixBase<DerivedGI> & GI,
    const typename DerivedS::Scalar isovalue,
    Eigen::PlainObjectBase<DerivedV> &V,
    Eigen::PlainObjectBase<DerivedF> &F);
}

#ifndef IGL_STATIC_LIBRARY
#  include "marching_cubes.cpp"
#endif

#endif
