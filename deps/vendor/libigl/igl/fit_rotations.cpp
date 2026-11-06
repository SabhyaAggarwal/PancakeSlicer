// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "fit_rotations.h"
#include "polar_svd3x3.h"
#include "repmat.h"
#include "verbose.h"
#include "polar_dec.h"
#include "polar_svd.h"
#include "C_STR.h"
#include <iostream>

template <typename DerivedS, typename DerivedD>
IGL_INLINE void igl::fit_rotations(
  const Eigen::MatrixBase<DerivedS> & S,
  const bool single_precision,
  Eigen::PlainObjectBase<DerivedD> & R)
{
  using namespace std;
  const int dim = S.cols();
  const int nr = S.rows()/dim;
  assert(nr * dim == S.rows());
  assert(dim == 3);


  R.resize(dim,dim*nr);



  Eigen::Matrix<typename DerivedS::Scalar,3,3> si;

  for(int r = 0;r<nr;r++)
  {

    for(int i = 0;i<dim;i++)
    {
      for(int j = 0;j<dim;j++)
      {
        si(i,j) = S(i*nr+r,j);
      }
    }
    typedef Eigen::Matrix<typename DerivedD::Scalar,3,3> Mat3;
    typedef Eigen::Matrix<typename DerivedD::Scalar,3,1> Vec3;
    Mat3 ri;
    if(single_precision)
    {
      polar_svd3x3(si, ri);
    }else
    {
      Mat3 ti,ui,vi;
      Vec3 _;
      igl::polar_svd(si,ri,ti,ui,_,vi);
    }
    assert(ri.determinant() >= 0);
    R.block(0,r*dim,dim,dim) = ri.block(0,0,dim,dim).transpose();


  }
}

template <typename DerivedS, typename DerivedD>
IGL_INLINE void igl::fit_rotations_planar(
  const Eigen::MatrixBase<DerivedS> & S,
        Eigen::PlainObjectBase<DerivedD> & R)
{
  using namespace std;
  const int dim = S.cols();
  const int nr = S.rows()/dim;

  assert(nr * dim == S.rows());


  R.resize(dim,dim*nr);

  Eigen::Matrix<typename DerivedS::Scalar,2,2> si;

  for(int r = 0;r<nr;r++)
  {

    for(int i = 0;i<2;i++)
    {
      for(int j = 0;j<2;j++)
      {
        si(i,j) = S(i*nr+r,j);
      }
    }
    typedef Eigen::Matrix<typename DerivedD::Scalar,2,2> Mat2;
    typedef Eigen::Matrix<typename DerivedD::Scalar,2,1> Vec2;
    Mat2 ri,ti,ui,vi;
    Vec2 _;
    igl::polar_svd(si,ri,ti,ui,_,vi);
#ifndef FIT_ROTATIONS_ALLOW_FLIPS

    if(ri.determinant() < 0)
    {
      vi.col(1) *= -1.;
      ri = ui * vi.transpose();
    }
    assert(ri.determinant() >= 0);
#endif


    R.block(0,r*dim,dim,dim).setIdentity();
    R.block(0,r*dim,2,2) = ri.transpose();
  }
}


#ifdef __SSE__
IGL_INLINE void igl::fit_rotations_SSE(
  const Eigen::MatrixXf & S,
  Eigen::MatrixXf & R)
{
  const int cStep = 4;

  assert(S.cols() == 3);
  const int dim = 3;
  const int nr = S.rows()/dim;
  assert(nr * dim == S.rows());


  R.resize(dim,dim*nr);

  Eigen::Matrix<float, 3*cStep, 3> siBig;

  int r = 0;
  for( ; r<nr; r+=cStep)
  {
    int numMats = cStep;
    if (r + cStep >= nr) numMats = nr - r;

    for (int k=0; k<numMats; k++)
    {
      for(int i = 0;i<dim;i++)
      {
        for(int j = 0;j<dim;j++)
        {
          siBig(i + 3*k, j) = S(i*nr + r + k, j);
        }
      }
    }
    Eigen::Matrix<float, 3*cStep, 3> ri;
    polar_svd3x3_sse(siBig, ri);

    for (int k=0; k<cStep; k++)
      assert(ri.block(3*k, 0, 3, 3).determinant() >= 0);


    for (int k=0; k<numMats; k++)
    {
      R.block(0, (r + k)*dim, dim, dim) = ri.block(3*k, 0, dim, dim).transpose();
    }
  }
}

IGL_INLINE void igl::fit_rotations_SSE(
  const Eigen::MatrixXd & S,
  Eigen::MatrixXd & R)
{
  const Eigen::MatrixXf Sf = S.cast<float>();
  Eigen::MatrixXf Rf;
  fit_rotations_SSE(Sf,Rf);
  R = Rf.cast<double>();
}
#endif

#ifdef __AVX__
IGL_INLINE void igl::fit_rotations_AVX(
  const Eigen::MatrixXf & S,
  Eigen::MatrixXf & R)
{
  const int cStep = 8;

  assert(S.cols() == 3);
  const int dim = 3;
  const int nr = S.rows()/dim;
  assert(nr * dim == S.rows());


  R.resize(dim,dim*nr);

  Eigen::Matrix<float, 3*cStep, 3> siBig;

  int r = 0;
  for( ; r<nr; r+=cStep)
  {
    int numMats = cStep;
    if (r + cStep >= nr) numMats = nr - r;

    for (int k=0; k<numMats; k++)
    {
      for(int i = 0;i<dim;i++)
      {
        for(int j = 0;j<dim;j++)
        {
          siBig(i + 3*k, j) = S(i*nr + r + k, j);
        }
      }
    }
    Eigen::Matrix<float, 3*cStep, 3> ri;
    polar_svd3x3_avx(siBig, ri);

    for (int k=0; k<cStep; k++)
      assert(ri.block(3*k, 0, 3, 3).determinant() >= 0);


    for (int k=0; k<numMats; k++)
    {
      R.block(0, (r + k)*dim, dim, dim) = ri.block(3*k, 0, dim, dim).transpose();
    }
  }
}
#endif

#ifdef IGL_STATIC_LIBRARY

template void igl::fit_rotations<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, bool, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&);
template void igl::fit_rotations_planar<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&);
template void igl::fit_rotations_planar<Eigen::Matrix<float, -1, -1, 0, -1, -1>, Eigen::Matrix<float, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> >&);
template void igl::fit_rotations<Eigen::Matrix<float,-1,-1,0,-1,-1>,Eigen::Matrix<float,-1,-1,0,-1,-1> >(Eigen::MatrixBase<Eigen::Matrix<float,-1,-1,0,-1,-1> > const &,bool,Eigen::PlainObjectBase<Eigen::Matrix<float,-1,-1,0,-1,-1> > &);
template void igl::fit_rotations_planar<Eigen::Matrix<double, 2, 2, 0, 2, 2>, Eigen::Matrix<double, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, 2, 2, 0, 2, 2> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&);
#endif
