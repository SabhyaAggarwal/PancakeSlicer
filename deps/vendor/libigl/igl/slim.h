// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Michael Rabinovich
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef SLIM_H
#define SLIM_H

#include "igl_inline.h"
#include "MappingEnergyType.h"
#include <Eigen/Dense>
#include <Eigen/Sparse>





#define SLIM_CACHED

#ifdef SLIM_CACHED
#include "AtA_cached.h"
#endif

namespace igl
{





struct SLIMData
{

  Eigen::MatrixXd V;

  Eigen::MatrixXi F;

  MappingEnergyType slim_energy;



  Eigen::VectorXi b;

  Eigen::MatrixXd bc;

  double soft_const_p;


  double exp_factor;

  bool mesh_improvement_3d;



  Eigen::MatrixXd V_o;

  double energy;


  Eigen::VectorXd M;
  double mesh_area;
  double avg_edge_length;
  int v_num;
  int f_num;
  double proximal_p;

  Eigen::VectorXd WGL_M;
  Eigen::VectorXd rhs;
  Eigen::MatrixXd Ri,Ji;
  Eigen::MatrixXd W;
  Eigen::SparseMatrix<double> Dx,Dy,Dz;
  int f_n,v_n;
  bool first_solve;
  bool has_pre_calc = false;
  int dim;

  #ifdef SLIM_CACHED
  Eigen::SparseMatrix<double> A;
  Eigen::VectorXi A_data;
  Eigen::SparseMatrix<double> AtA;
  igl::AtA_cached_data AtA_data;
  #endif
};













IGL_INLINE void slim_precompute(
  const Eigen::MatrixXd& V,
  const Eigen::MatrixXi& F,
  const Eigen::MatrixXd& V_init,
  SLIMData& data,
  MappingEnergyType slim_energy,
  const Eigen::VectorXi& b,
  const Eigen::MatrixXd& bc,
  double soft_p);








IGL_INLINE Eigen::MatrixXd slim_solve(
  SLIMData& data,
  int iter_num);










IGL_INLINE void slim_update_weights_and_closest_rotations_with_jacobians(
  const Eigen::MatrixXd &Ji,
  igl::MappingEnergyType slim_energy,
  double exp_factor,
  Eigen::MatrixXd &W,
  Eigen::MatrixXd &Ri);








IGL_INLINE void slim_buildA(
  const Eigen::SparseMatrix<double> &Dx,
  const Eigen::SparseMatrix<double> &Dy,
  const Eigen::SparseMatrix<double> &Dz,
  const Eigen::MatrixXd &W,
  std::vector<Eigen::Triplet<double> > & IJV);
}

#ifndef IGL_STATIC_LIBRARY
#  include "slim.cpp"
#endif

#endif
