// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Zhongshi Jiang <jiangzs@nyu.edu>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SCAF_H
#define IGL_SCAF_H

#include "../slim.h"
#include "../igl_inline.h"
#include "../MappingEnergyType.h"

namespace igl
{
  namespace triangle
  {





    struct SCAFData
    {
      double scaffold_factor = 10;
      igl::MappingEnergyType scaf_energy = igl::MappingEnergyType::SYMMETRIC_DIRICHLET;
      igl::MappingEnergyType slim_energy = igl::MappingEnergyType::SYMMETRIC_DIRICHLET;


      int dim = 2;

      double total_energy;

      double energy;

      long mv_num = 0, mf_num = 0;
      long sv_num = 0, sf_num = 0;
      long v_num{}, f_num = 0;

      Eigen::MatrixXd m_V;

      Eigen::MatrixXi m_T;


      Eigen::MatrixXd w_uv;

      Eigen::MatrixXi s_T;
      Eigen::MatrixXi w_T;


      Eigen::VectorXd m_M;

      Eigen::VectorXd s_M;

      Eigen::VectorXd w_M;

      double mesh_measure = 0;
      double proximal_p = 0;

      Eigen::VectorXi frame_ids;
      Eigen::VectorXi fixed_ids;

      std::map<int, Eigen::RowVectorXd> soft_cons;
      double soft_const_p = 1e4;

      Eigen::VectorXi internal_bnd;
      Eigen::MatrixXd rect_frame_V;

      std::vector<int> component_sizes;
      std::vector<int> bnd_sizes;


      bool has_pre_calc = false;
      Eigen::SparseMatrix<double> Dx_s, Dy_s, Dz_s;
      Eigen::SparseMatrix<double> Dx_m, Dy_m, Dz_m;
      Eigen::MatrixXd Ri_m, Ji_m, Ri_s, Ji_s;
      Eigen::MatrixXd W_m, W_s;
    };














    IGL_INLINE void scaf_precompute(
        const Eigen::MatrixXd &V,
        const Eigen::MatrixXi &F,
        const Eigen::MatrixXd &V_init,
        const MappingEnergyType slim_energy,
        const Eigen::VectorXi& b,
        const Eigen::MatrixXd& bc,
        const double soft_p,
        triangle::SCAFData &data);





    IGL_INLINE Eigen::MatrixXd scaf_solve(const int iter_num, triangle::SCAFData &data);






    IGL_INLINE void scaf_system(triangle::SCAFData &s, Eigen::SparseMatrix<double> &L, Eigen::VectorXd &rhs);

    namespace scaf
    {





      IGL_INLINE double compute_energy(SCAFData &s, const Eigen::MatrixXd &w_uv, bool whole);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "scaf.cpp"
#endif

#endif
