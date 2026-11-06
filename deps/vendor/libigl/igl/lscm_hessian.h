// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2023 Alec Jacobson
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_LSCM_HESSIAN_H
#define IGL_LSCM_HESSIAN_H
#include <Eigen/Dense>
#include <Eigen/Sparse>

namespace igl
{


















  template <typename DerivedV, typename DerivedF, typename QScalar>
  void lscm_hessian(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::SparseMatrix<QScalar> & Q);
}

#ifndef IGL_STATIC_LIBRARY
#include "lscm_hessian.cpp"
#endif

#endif
