// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_ARAP_ENERGY_TYPE_DOF_H
#define IGL_ARAP_ENERGY_TYPE_DOF_H
#include "igl_inline.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include "ARAPEnergyType.h"
#include <vector>







































namespace igl
{

  template <typename LbsMatrixType, typename SSCALAR>
  struct ArapDOFData;



























  template <typename LbsMatrixType, typename SSCALAR>
  IGL_INLINE bool arap_dof_precomputation(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi & F,
    const LbsMatrixType & M,
    const Eigen::Matrix<int,Eigen::Dynamic,1> & G,
    ArapDOFData<LbsMatrixType, SSCALAR> & data);






















  template <typename LbsMatrixType, typename SSCALAR>
  IGL_INLINE bool arap_dof_recomputation(
    const Eigen::Matrix<int,Eigen::Dynamic,1> & fixed_dim,
    const Eigen::SparseMatrix<double> & A_eq,
    ArapDOFData<LbsMatrixType, SSCALAR> & data);

















  template <typename LbsMatrixType, typename SSCALAR>
  IGL_INLINE bool arap_dof_update(
    const ArapDOFData<LbsMatrixType,SSCALAR> & data,
    const Eigen::Matrix<double,Eigen::Dynamic,1> & B_eq,
    const Eigen::MatrixXd & L0,
    const int max_iters,
    const double tol,
    Eigen::MatrixXd & L
    );





  template <typename LbsMatrixType, typename SSCALAR>
  struct ArapDOFData
  {

    typedef Eigen::Matrix<SSCALAR, Eigen::Dynamic, Eigen::Dynamic> MatrixXS;

    igl::ARAPEnergyType energy;

    Eigen::Matrix<int,Eigen::Dynamic,1> fixed_dim;


    std::vector<Eigen::MatrixXd> CSM_M;

    LbsMatrixType M_KG;

    int n;

    int m;

    int dim;

    int effective_dim;

    Eigen::Matrix<int,Eigen::Dynamic,1> interpolated;

    std::vector<bool> free_mask;

    LbsMatrixType Q;







    MatrixXS M_FullSolve;


    MatrixXS CSM;

    MatrixXS CSolveBlock1;


    bool print_timings;


    bool with_dynamics;




    double h;



    MatrixXS L0;




    MatrixXS Lvel0;


    MatrixXS fext;


    LbsMatrixType Mass_tilde;


    Eigen::MatrixXd fgrav;

    Eigen::Vector3d grav_dir;

    double grav_mag;


    MatrixXS Pi_1;


    ArapDOFData():
      energy(igl::ARAP_ENERGY_TYPE_SPOKES),
      with_dynamics(false),
      h(1),
      grav_dir(0,-1,0),
      grav_mag(0)
    {
    }
  };
}

#ifndef IGL_STATIC_LIBRARY
#  include "arap_dof.cpp"
#endif

#endif
