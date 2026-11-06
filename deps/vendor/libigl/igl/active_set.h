// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_ACTIVE_SET_H
#define IGL_ACTIVE_SET_H

#include "igl_inline.h"
#include "SolverStatus.h"
#include <Eigen/Core>
#include <Eigen/Sparse>

namespace igl
{
  struct active_set_params;







































  template <
    typename AT,
    typename DerivedB,
    typename Derivedknown,
    typename DerivedY,
    typename AeqT,
    typename DerivedBeq,
    typename AieqT,
    typename DerivedBieq,
    typename Derivedlx,
    typename Derivedux,
    typename DerivedZ
    >
  IGL_INLINE igl::SolverStatus active_set(
    const Eigen::SparseMatrix<AT>& A,
    const Eigen::MatrixBase<DerivedB> & B,
    const Eigen::MatrixBase<Derivedknown> & known,
    const Eigen::MatrixBase<DerivedY> & Y,
    const Eigen::SparseMatrix<AeqT>& Aeq,
    const Eigen::MatrixBase<DerivedBeq> & Beq,
    const Eigen::SparseMatrix<AieqT>& Aieq,
    const Eigen::MatrixBase<DerivedBieq> & Bieq,
    const Eigen::MatrixBase<Derivedlx> & lx,
    const Eigen::MatrixBase<Derivedux> & ux,
    const igl::active_set_params & params,
    Eigen::PlainObjectBase<DerivedZ> & Z
    );
};

#include "EPS.h"



struct igl::active_set_params
{

  bool Auu_pd;

  int max_iter;


  double inactive_threshold;


  double constraint_threshold;


  double solution_diff_threshold;

  active_set_params():
    Auu_pd(false),
    max_iter(100),
    inactive_threshold(igl::DOUBLE_EPS),
    constraint_threshold(igl::DOUBLE_EPS),
    solution_diff_threshold(igl::DOUBLE_EPS)
    {};
};

#ifndef IGL_STATIC_LIBRARY
#  include "active_set.cpp"
#endif

#endif
