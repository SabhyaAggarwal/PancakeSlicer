// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2017 Amir Vaxman <avaxman@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SHAPEUP_H
#define IGL_SHAPEUP_H

#include "min_quad_with_fixed.h"
#include "igl_inline.h"
#include "setdiff.h"
#include "cat.h"
#include <Eigen/Core>
#include <vector>
#include "PI.h"








namespace igl
{



  struct ShapeupData{

    Eigen::MatrixXd P;
    Eigen::VectorXi SC;
    Eigen::MatrixXi S;
    Eigen::VectorXi b;
    int maxIterations;
    double pTolerance;
    double shapeCoeff, closeCoeff, smoothCoeff;

    Eigen::SparseMatrix<double> DShape, DClose, DSmooth, Q, A, At, W;
    min_quad_with_fixed_data<double> solver_data;
    ShapeupData():
    maxIterations(50),
    pTolerance(10e-6),
    shapeCoeff(1.0),
    closeCoeff(100.0),
    smoothCoeff(0.0){}
  };
























  using
    shapeup_projection_function
    =
    std::function<
    bool(
        const Eigen::MatrixBase<Eigen::MatrixXd>&,
        const Eigen::MatrixBase<Eigen::VectorXi>&,
        const Eigen::MatrixBase<Eigen::MatrixXi>&,
        Eigen::PlainObjectBase<Eigen::MatrixXd>&)> ;













  IGL_INLINE bool shapeup_identity_projection(
    const Eigen::MatrixBase<Eigen::MatrixXd>& P,
    const Eigen::MatrixBase<Eigen::VectorXi>& SC,
    const Eigen::MatrixBase<Eigen::MatrixXi>& S,
    Eigen::PlainObjectBase<Eigen::MatrixXd>& projP);














  IGL_INLINE bool shapeup_regular_face_projection(
    const Eigen::MatrixBase<Eigen::MatrixXd>& P,
    const Eigen::MatrixBase<Eigen::VectorXi>& SC,
    const Eigen::MatrixBase<Eigen::MatrixXi>& S,
    Eigen::PlainObjectBase<Eigen::MatrixXd>& projP);


















  template <
    typename DerivedP,
    typename DerivedSC,
    typename DerivedS,
    typename Derivedw>
  IGL_INLINE bool shapeup_precomputation(
    const Eigen::MatrixBase<DerivedP>& P,
    const Eigen::MatrixBase<DerivedSC>& SC,
    const Eigen::MatrixBase<DerivedS>& S,
    const Eigen::MatrixBase<DerivedS>& E,
    const Eigen::MatrixBase<DerivedSC>& b,
    const Eigen::MatrixBase<Derivedw>& wShape,
    const Eigen::MatrixBase<Derivedw>& wSmooth,
    ShapeupData & sudata);














  template <
    typename DerivedP,
    typename DerivedSC,
    typename DerivedS>
  IGL_INLINE bool shapeup_solve(
    const Eigen::MatrixBase<DerivedP>& bc,
    const std::function<bool(const Eigen::MatrixBase<DerivedP>&, const Eigen::MatrixBase<DerivedSC>&, const Eigen::MatrixBase<DerivedS>&,  Eigen::PlainObjectBase<DerivedP>&)>& local_projection,
    const Eigen::MatrixBase<DerivedP>& P0,
    const ShapeupData & sudata,
    const bool quietIterations,
    Eigen::PlainObjectBase<DerivedP>& P);

}

#ifndef IGL_STATIC_LIBRARY
#include "shapeup.cpp"
#endif

#endif
