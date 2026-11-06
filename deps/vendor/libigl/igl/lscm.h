// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Daniele Panozzo <daniele.panozzo@gmail.com>
//               2015 Alec Jacobson
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_LSCM_H
#define IGL_LSCM_H
#include "igl_inline.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>

namespace igl
{

















  template <
    typename DerivedV,
    typename DerivedF,
    typename Derivedb,
    typename Derivedbc,
    typename DerivedV_uv,
    typename QScalar>
  IGL_INLINE bool lscm(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<Derivedb> & b,
    const Eigen::MatrixBase<Derivedbc> & bc,
    Eigen::PlainObjectBase<DerivedV_uv> & V_uv,
    Eigen::SparseMatrix<QScalar> & Q);

  template <
    typename DerivedV,
    typename DerivedF,
    typename Derivedb,
    typename Derivedbc,
    typename DerivedV_uv>
  IGL_INLINE bool lscm(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<Derivedb> & b,
    const Eigen::MatrixBase<Derivedbc> & bc,
    Eigen::PlainObjectBase<DerivedV_uv> & V_uv);





  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedV_uv>
  IGL_INLINE bool lscm(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedV_uv> & V_uv);
}

#ifndef IGL_STATIC_LIBRARY
#  include "lscm.cpp"
#endif

#endif
