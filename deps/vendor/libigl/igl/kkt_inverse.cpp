// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "kkt_inverse.h"

#include <Eigen/Core>
#include <Eigen/LU>
#include "EPS.h"
#include <cstdio>

template <typename T>
IGL_INLINE void igl::kkt_inverse(
  const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& A,
  const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& Aeq,
  const bool use_lu_decomposition,
  Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& S)
{
  typedef Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> Mat;


  const T treshold = igl::FLOAT_EPS;


  const int n = A.rows();
  assert(A.cols() == n);
  const int m = Aeq.rows();
  assert(Aeq.cols() == n);


  Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> LM(n + m, n + m);
  LM.block(0, 0, n, n) = A;
  LM.block(0, n, n, m) = Aeq.transpose();
  LM.block(n, 0, m, n) = Aeq;
  LM.block(n, n, m, m).setZero();

  Mat LMpinv;
  if(use_lu_decomposition)
  {

    LMpinv = LM.inverse();
  }else
  {

    typedef Eigen::Matrix<T, Eigen::Dynamic, 1> Vec;
    Vec singValues;
    Eigen::JacobiSVD<Mat> svd;
    svd.compute(LM, Eigen::ComputeFullU | Eigen::ComputeFullV );
    const Mat& u = svd.matrixU();
    const Mat& v = svd.matrixV();
    const Vec& singVals = svd.singularValues();

    Vec pi_singVals(n + m);
    int zeroed = 0;
    for (int i=0; i<n + m; i++)
    {
      T sv = singVals(i, 0);
      assert(sv >= 0);

      if (sv > treshold) pi_singVals(i, 0) = T(1) / sv;
      else
      {
        pi_singVals(i, 0) = T(0);
        zeroed++;
      }
    }

    printf("kkt_inverse : %i singular values zeroed (threshold = %e)\n", zeroed, treshold);
    Eigen::DiagonalMatrix<T, Eigen::Dynamic> pi_diag(pi_singVals);

    LMpinv = v * pi_diag * u.transpose();
  }
  S = LMpinv.block(0, 0, n, n + m);















}

#ifdef IGL_STATIC_LIBRARY

template void igl::kkt_inverse<double>(Eigen::Matrix<double, -1, -1, 0, -1, -1> const&, Eigen::Matrix<double, -1, -1, 0, -1, -1> const&, bool, Eigen::Matrix<double, -1, -1, 0, -1, -1>&);
#endif
