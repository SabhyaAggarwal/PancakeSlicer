// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_MIN_QUAD_WITH_FIXED_H
#define IGL_MIN_QUAD_WITH_FIXED_H
#include "igl_inline.h"

#define EIGEN_YES_I_KNOW_SPARSE_MODULE_IS_NOT_STABLE_YET
#include <Eigen/Core>
#include <Eigen/Dense>
#include <Eigen/Sparse>

#include <iostream>
#include <unsupported/Eigen/SparseExtra>

namespace igl
{
  template <typename T>
  struct min_quad_with_fixed_data;

























  template <typename T, typename Derivedknown>
  IGL_INLINE bool min_quad_with_fixed_precompute(
    const Eigen::SparseMatrix<T>& A,
    const Eigen::MatrixBase<Derivedknown> & known,
    const Eigen::SparseMatrix<T>& Aeq,
    const bool pd,
    min_quad_with_fixed_data<T> & data
    );












  template <
    typename T,
    typename DerivedB,
    typename DerivedY,
    typename DerivedBeq,
    typename DerivedZ,
    typename Derivedsol>
  IGL_INLINE bool min_quad_with_fixed_solve(
    const min_quad_with_fixed_data<T> & data,
    const Eigen::MatrixBase<DerivedB> & B,
    const Eigen::MatrixBase<DerivedY> & Y,
    const Eigen::MatrixBase<DerivedBeq> & Beq,
    Eigen::PlainObjectBase<DerivedZ> & Z,
    Eigen::PlainObjectBase<Derivedsol> & sol);

  template <
    typename T,
    typename DerivedB,
    typename DerivedY,
    typename DerivedBeq,
    typename DerivedZ>
  IGL_INLINE bool min_quad_with_fixed_solve(
    const min_quad_with_fixed_data<T> & data,
    const Eigen::MatrixBase<DerivedB> & B,
    const Eigen::MatrixBase<DerivedY> & Y,
    const Eigen::MatrixBase<DerivedBeq> & Beq,
    Eigen::PlainObjectBase<DerivedZ> & Z);



  template <
    typename T,
    typename Derivedknown,
    typename DerivedB,
    typename DerivedY,
    typename DerivedBeq,
    typename DerivedZ>
  IGL_INLINE bool min_quad_with_fixed(
    const Eigen::SparseMatrix<T>& A,
    const Eigen::MatrixBase<DerivedB> & B,
    const Eigen::MatrixBase<Derivedknown> & known,
    const Eigen::MatrixBase<DerivedY> & Y,
    const Eigen::SparseMatrix<T>& Aeq,
    const Eigen::MatrixBase<DerivedBeq> & Beq,
    const bool pd,
    Eigen::PlainObjectBase<DerivedZ> & Z);




















  template <typename Scalar, int n, int m, bool Hpd=true>
  IGL_INLINE Eigen::Matrix<Scalar,n,1> min_quad_with_fixed(
    const Eigen::Matrix<Scalar,n,n> & H,
    const Eigen::Matrix<Scalar,n,1> & f,
    const Eigen::Array<bool,n,1> & k,
    const Eigen::Matrix<Scalar,n,1> & bc,
    const Eigen::Matrix<Scalar,m,n> & A,
    const Eigen::Matrix<Scalar,m,1> & b);

  template <typename Scalar, int n, bool Hpd=true>
  IGL_INLINE Eigen::Matrix<Scalar,n,1> min_quad_with_fixed(
    const Eigen::Matrix<Scalar,n,n> & H,
    const Eigen::Matrix<Scalar,n,1> & f,
    const Eigen::Array<bool,n,1> & k,
    const Eigen::Matrix<Scalar,n,1> & bc);





  template <typename Scalar, int n, int kcount, bool Hpd >
  IGL_INLINE Eigen::Matrix<Scalar,n,1> min_quad_with_fixed(
    const Eigen::Matrix<Scalar,n,n> & H,
    const Eigen::Matrix<Scalar,n,1> & f,
    const Eigen::Array<bool,n,1> & k,
    const Eigen::Matrix<Scalar,n,1> & bc);
}


template <typename T>
struct igl::min_quad_with_fixed_data
{

  int n;

  bool Auu_pd;

  bool Auu_sym;

  Eigen::VectorXi known;

  Eigen::VectorXi unknown;

  Eigen::VectorXi lagrange;

  Eigen::VectorXi unknown_lagrange;

  Eigen::SparseMatrix<T> preY;

  enum SolverType
  {
    LLT = 0,
    LDLT = 1,
    LU = 2,
    QR_LLT = 3,
    NUM_SOLVER_TYPES = 4
  } solver_type;

  Eigen::SimplicialLLT <Eigen::SparseMatrix<T > > llt;
  Eigen::SimplicialLDLT<Eigen::SparseMatrix<T > > ldlt;
  Eigen::SparseLU<Eigen::SparseMatrix<T, Eigen::ColMajor>, Eigen::COLAMDOrdering<int> >   lu;


  bool Aeq_li;

  int neq;
  Eigen::SparseQR<Eigen::SparseMatrix<T>, Eigen::COLAMDOrdering<int> >  AeqTQR;
  Eigen::SparseMatrix<T> Aeqk;
  Eigen::SparseMatrix<T> Aequ;
  Eigen::SparseMatrix<T> Auu;
  Eigen::SparseMatrix<T> AeqTQ1;
  Eigen::SparseMatrix<T> AeqTQ1T;
  Eigen::SparseMatrix<T> AeqTQ2;
  Eigen::SparseMatrix<T> AeqTQ2T;
  Eigen::SparseMatrix<T> AeqTR1;
  Eigen::SparseMatrix<T> AeqTR1T;
  Eigen::SparseMatrix<T> AeqTE;
  Eigen::SparseMatrix<T> AeqTET;

  Eigen::SparseMatrix<T> NA;
  Eigen::Matrix<T,Eigen::Dynamic,Eigen::Dynamic> NB;
};

#ifndef IGL_STATIC_LIBRARY
#  include "min_quad_with_fixed.impl.h"
#endif

#endif
