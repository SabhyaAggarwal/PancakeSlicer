// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2021 Alec Jacobson <alecjacobson@gmail.com>
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_PER_CORNER_NORMALS_H
#define IGL_PER_CORNER_NORMALS_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>

namespace igl
{









  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedCN>
  IGL_INLINE void per_corner_normals(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const typename DerivedV::Scalar corner_threshold_degrees,
    Eigen::PlainObjectBase<DerivedCN> & CN);









  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedVF,
    typename DerivedNI,
    typename DerivedCN>
  IGL_INLINE void per_corner_normals(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const typename DerivedV::Scalar corner_threshold_degrees,
    const Eigen::MatrixBase<DerivedVF> & VF,
    const Eigen::MatrixBase<DerivedNI> & NI,
    Eigen::PlainObjectBase<DerivedCN> & CN);





  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedCI,
    typename DerivedCC,
    typename DerivedCN>
  IGL_INLINE void per_corner_normals(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<DerivedCI> & CI,
    const Eigen::MatrixBase<DerivedCC> & CC,
    Eigen::PlainObjectBase<DerivedCN> & CN);







  template <typename DerivedNV, typename DerivedNF, typename DerivedCN>
  IGL_INLINE void per_corner_normals(
    const Eigen::MatrixBase<DerivedNV> & NV,
    const Eigen::MatrixBase<DerivedNF> & NF,
    Eigen::PlainObjectBase<DerivedCN> & CN);













  template <
    typename DerivedV,
    typename DerivedI,
    typename DerivedC,
    typename DerivedN,
    typename DerivedVV,
    typename DerivedFF,
    typename DerivedJ,
    typename DerivedNN>
  IGL_INLINE void per_corner_normals(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedI> & I,
    const Eigen::MatrixBase<DerivedC> & C,
    const typename DerivedV::Scalar corner_threshold_degrees,
    Eigen::PlainObjectBase<DerivedN>  & N,
    Eigen::PlainObjectBase<DerivedVV> & VV,
    Eigen::PlainObjectBase<DerivedFF> & FF,
    Eigen::PlainObjectBase<DerivedJ>  & J,
    Eigen::PlainObjectBase<DerivedNN> & NN);
}

#ifndef IGL_STATIC_LIBRARY
#  include "per_corner_normals.cpp"
#endif

#endif
