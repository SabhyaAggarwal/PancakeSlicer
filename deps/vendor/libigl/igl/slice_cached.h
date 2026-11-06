// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2017 Daniele Panozzo <daniele.panozzo@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SLICE_CACHED_H
#define IGL_SLICE_CACHED_H
#include "igl_inline.h"
#define EIGEN_YES_I_KNOW_SPARSE_MODULE_IS_NOT_STABLE_YET
#include <Eigen/Dense>
#include <Eigen/Sparse>
namespace igl
{





























  template <typename TX, typename TY, typename DerivedI>
  IGL_INLINE void slice_cached_precompute(
    const Eigen::SparseMatrix<TX>& X,
    const Eigen::Matrix<int,Eigen::Dynamic,1> & R,
    const Eigen::Matrix<int,Eigen::Dynamic,1> & C,
    Eigen::MatrixBase<DerivedI>& data,
    Eigen::SparseMatrix<TY>& Y);





  template <typename TX, typename TY, typename DerivedI>
  IGL_INLINE void slice_cached(
    const Eigen::SparseMatrix<TX>& X,
    const Eigen::MatrixBase<DerivedI>& data,
    Eigen::SparseMatrix<TY>& Y);
}

#ifndef IGL_STATIC_LIBRARY
#  include "slice_cached.cpp"
#endif

#endif
