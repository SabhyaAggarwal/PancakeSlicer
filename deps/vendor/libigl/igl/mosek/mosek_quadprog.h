// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_MOSEK_MOSEK_QUADPROG_H
#define IGL_MOSEK_MOSEK_QUADPROG_H
#include "../igl_inline.h"
#include <vector>
#include <map>
#include <mosek.h>


#define EIGEN_YES_I_KNOW_SPARSE_MODULE_IS_NOT_STABLE_YET
#include <Eigen/Dense>
#include <Eigen/Sparse>

namespace igl
{
  namespace mosek
  {

    struct MosekData
    {

      std::map<MSKiparame,int> intparam;

      std::map<MSKdparame,double> douparam;

      IGL_INLINE MosekData();
    };






















































    template <typename Index, typename Scalar>
    IGL_INLINE bool mosek_quadprog(
      const Index n,
        std::vector<Index> & Qi,
        std::vector<Index> & Qj,
        std::vector<Scalar> & Qv,
      const std::vector<Scalar> & c,
      const Scalar cf,
      const Index m,
        std::vector<Scalar> & Av,
        std::vector<Index> & Ari,
      const std::vector<Index> & Acp,
      const std::vector<Scalar> & lc,
      const std::vector<Scalar> & uc,
      const std::vector<Scalar> & lx,
      const std::vector<Scalar> & ux,
      MosekData & mosek_data,
      std::vector<Scalar> & x);















    IGL_INLINE bool mosek_quadprog(
      const Eigen::SparseMatrix<double> & Q,
      const Eigen::VectorXd & c,
      const double cf,
      const Eigen::SparseMatrix<double> & A,
      const Eigen::VectorXd & lc,
      const Eigen::VectorXd & uc,
      const Eigen::VectorXd & lx,
      const Eigen::VectorXd & ux,
      MosekData & mosek_data,
      Eigen::VectorXd & x);
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "mosek_quadprog.cpp"
#endif

#endif
