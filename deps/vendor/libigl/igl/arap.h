// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_ARAP_H
#define IGL_ARAP_H
#include "igl_inline.h"
#include "min_quad_with_fixed.h"
#include "ARAPEnergyType.h"
#include <Eigen/Core>
#include <Eigen/Sparse>

namespace igl
{



  struct ARAPData
  {

    int n;


    Eigen::VectorXi G;

    ARAPEnergyType energy;

    bool with_dynamics;

    Eigen::MatrixXd f_ext;

    Eigen::MatrixXd vel;

    double h;

    double ym;

    int max_iter;

    Eigen::SparseMatrix<double> K;

    Eigen::SparseMatrix<double> M;

    Eigen::SparseMatrix<double> CSM;

    min_quad_with_fixed_data<double> solver_data;

    Eigen::VectorXi b;

    int dim;
      ARAPData():
        n(0),
        G(),
        energy(ARAP_ENERGY_TYPE_DEFAULT),
        with_dynamics(false),
        f_ext(),
        h(1),
        ym(1),
        max_iter(10),
        K(),
        CSM(),
        solver_data(),
        b(),
        dim(-1)
    {
    };
  };














  template <
    typename DerivedV,
    typename DerivedF,
    typename Derivedb>
  IGL_INLINE bool arap_precomputation(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const int dim,
    const Eigen::MatrixBase<Derivedb> & b,
    ARAPData & data);















  template <
    typename Derivedbc,
    typename DerivedU>
  IGL_INLINE bool arap_solve(
    const Eigen::MatrixBase<Derivedbc> & bc,
    ARAPData & data,
    Eigen::MatrixBase<DerivedU> & U);
};

#ifndef IGL_STATIC_LIBRARY
#include "arap.cpp"
#endif

#endif
