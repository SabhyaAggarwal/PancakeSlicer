// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2020 Oded Stein <oded.stein@columbia.edu>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SCALAR_TO_CR_VECTOT_GRADIENT_H
#define IGL_SCALAR_TO_CR_VECTOT_GRADIENT_H

#include "igl_inline.h"

#include <Eigen/Core>
#include <Eigen/Sparse>


namespace igl
{










  template <typename DerivedV, typename DerivedF, typename DerivedE,
    typename DerivedOE, typename ScalarG>
  IGL_INLINE void
  scalar_to_cr_vector_gradient(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    const Eigen::MatrixBase<DerivedE>& E,
    const Eigen::MatrixBase<DerivedOE>& oE,
    Eigen::SparseMatrix<ScalarG>& G);



  template <typename DerivedV, typename DerivedF, typename DerivedE,
    typename DerivedOE, typename ScalarG>
  IGL_INLINE void
  scalar_to_cr_vector_gradient(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    Eigen::PlainObjectBase<DerivedE>& E,
    Eigen::PlainObjectBase<DerivedOE>& oE,
    Eigen::SparseMatrix<ScalarG>& G);







  template <typename DerivedF, typename DerivedL_sq, typename DeriveddA,
    typename DerivedE, typename DerivedOE, typename ScalarG>
  IGL_INLINE void scalar_to_cr_vector_gradient_intrinsic(
    const Eigen::MatrixBase<DerivedF>& F,
    const Eigen::MatrixBase<DerivedL_sq>& l_sq,
    const Eigen::MatrixBase<DeriveddA>& dA,
    const Eigen::MatrixBase<DerivedE>& E,
    const Eigen::MatrixBase<DerivedOE>& oE,
    Eigen::SparseMatrix<ScalarG>& G);


  template <typename DerivedF, typename DerivedL_sq, typename DerivedE,
    typename DerivedOE, typename ScalarG>
  IGL_INLINE void scalar_to_cr_vector_gradient_intrinsic(
    const Eigen::MatrixBase<DerivedF>& F,
    const Eigen::MatrixBase<DerivedL_sq>& l_sq,
    const Eigen::MatrixBase<DerivedE>& E,
    const Eigen::MatrixBase<DerivedOE>& oE,
    Eigen::SparseMatrix<ScalarG>& G);
}


#ifndef IGL_STATIC_LIBRARY
#  include "scalar_to_cr_vector_gradient.cpp"
#endif

#endif
