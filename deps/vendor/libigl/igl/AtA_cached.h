// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2017 Daniele Panozzo <daniele.panozzo@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_ATA_CACHED_H
#define IGL_ATA_CACHED_H
#include "igl_inline.h"
#define EIGEN_YES_I_KNOW_SPARSE_MODULE_IS_NOT_STABLE_YET
#include <Eigen/Dense>
#include <Eigen/Sparse>
namespace igl
{

  struct AtA_cached_data
  {

    Eigen::VectorXd W;



    std::vector<int> I_row;

    std::vector<int> I_col;

    std::vector<int> I_w;




    std::vector<int> I_outer;
  };






















  template <typename Scalar>
  IGL_INLINE void AtA_cached_precompute(
    const Eigen::SparseMatrix<Scalar>& A,
    AtA_cached_data& data,
    Eigen::SparseMatrix<Scalar>& AtA
    );






  template <typename Scalar>
  IGL_INLINE void AtA_cached(
    const Eigen::SparseMatrix<Scalar>& A,
    const AtA_cached_data& data,
    Eigen::SparseMatrix<Scalar>& AtA
    );

}

#ifndef IGL_STATIC_LIBRARY
#  include "AtA_cached.cpp"
#endif

#endif
