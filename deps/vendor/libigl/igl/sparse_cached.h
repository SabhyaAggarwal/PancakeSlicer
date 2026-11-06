// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2017 Daniele Panozzo <daniele.panozzo@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SPARSE_CACHED_H
#define IGL_SPARSE_CACHED_H
#include "igl_inline.h"
#define EIGEN_YES_I_KNOW_SPARSE_MODULE_IS_NOT_STABLE_YET
#include <Eigen/Dense>
#include <Eigen/Sparse>
namespace igl
{
























  template <typename DerivedI, typename Scalar>
  IGL_INLINE void sparse_cached_precompute(
    const Eigen::MatrixBase<DerivedI> & I,
    const Eigen::MatrixBase<DerivedI> & J,
    Eigen::VectorXi& data,
    Eigen::SparseMatrix<Scalar>& X
    );


  template <typename Scalar>
  IGL_INLINE void sparse_cached_precompute(
    const std::vector<Eigen::Triplet<Scalar> >& triplets,
    Eigen::VectorXi& data,
    Eigen::SparseMatrix<Scalar>& X
    );





  template <typename Scalar>
  IGL_INLINE void sparse_cached(
    const std::vector<Eigen::Triplet<Scalar> >& triplets,
    const Eigen::VectorXi& data,
    Eigen::SparseMatrix<Scalar>& X);


  template <typename DerivedV, typename Scalar>
  IGL_INLINE void sparse_cached(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::VectorXi& data,
    Eigen::SparseMatrix<Scalar>& X
    );

}

#ifndef IGL_STATIC_LIBRARY
#  include "sparse_cached.cpp"
#endif

#endif
