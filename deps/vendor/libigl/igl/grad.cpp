// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "grad.h"
#include <Eigen/Geometry>
#include <vector>

#include "PI.h"
#include "per_face_normals.h"
#include "volume.h"
#include "doublearea.h"

namespace igl {

namespace {

template <typename DerivedV, typename DerivedF>
IGL_INLINE void grad_tet(
  const Eigen::MatrixBase<DerivedV>&V,
  const Eigen::MatrixBase<DerivedF>&T,
  Eigen::SparseMatrix<typename DerivedV::Scalar> &G,
  bool uniform)
{
  using namespace Eigen;
  assert(T.cols() == 4);
  const int n = V.rows(); int m = T.rows();







  MatrixXi F(4*m,3);
  for (int i = 0; i < m; i++) {
    F.row(0*m + i) << T(i,0), T(i,1), T(i,2);
    F.row(1*m + i) << T(i,0), T(i,2), T(i,3);
    F.row(2*m + i) << T(i,0), T(i,3), T(i,1);
    F.row(3*m + i) << T(i,1), T(i,3), T(i,2);
  }

  Eigen::Matrix<typename DerivedV::Scalar, Eigen::Dynamic, 1> vol;
  igl::volume(V,T,vol);

  Eigen::Matrix<typename DerivedV::Scalar, Eigen::Dynamic, 1> A(F.rows());
  Eigen::Matrix<typename DerivedV::Scalar, Eigen::Dynamic, Eigen::Dynamic> N(F.rows(),3);
  if (!uniform) {

    igl::per_face_normals(V,F,N); int norm_rows = N.rows();
    for (int i = 0; i < norm_rows; i++)
      N.row(i) /= N.row(i).norm();
    igl::doublearea(V,F,A); A/=2.;
  } else {







    for (int i = 0; i < m; i++) {
      N.row(0*m+i) << 0,0,1;
      double a = sqrt(2)*std::cbrt(3*vol(i));
      A(0*m+i) = (pow(a,2)*sqrt(3))/4.;
    }
    for (int i = 0; i < m; i++) {
      N.row(1*m+i) << 0.8165,-0.4714,-0.3333;
      double a = sqrt(2)*std::cbrt(3*vol(i));
      A(1*m+i) = (pow(a,2)*sqrt(3))/4.;
    }
    for (int i = 0; i < m; i++) {
      N.row(2*m+i) << 0,0.9428,-0.3333;
      double a = sqrt(2)*std::cbrt(3*vol(i));
      A(2*m+i) = (pow(a,2)*sqrt(3))/4.;
    }
    for (int i = 0; i < m; i++) {
      N.row(3*m+i) << -0.8165,-0.4714,-0.3333;
      double a = sqrt(2)*std::cbrt(3*vol(i));
      A(3*m+i) = (pow(a,2)*sqrt(3))/4.;
    }

  }








  std::vector<Triplet<double> > G_t;
  for (int i = 0; i < 4*m; i++) {
    int T_j;
    switch (i/m) {
      case 0:
        T_j = 3;
        break;
      case 1:
        T_j = 1;
        break;
      case 2:
        T_j = 2;
        break;
      case 3:
        T_j = 0;
        break;
    }
    int i_idx = i%m;
    int j_idx = T(i_idx,T_j);

    double val_before_n = A(i)/(3*vol(i_idx));
    G_t.push_back(Triplet<double>(0*m+i_idx, j_idx, val_before_n * N(i,0)));
    G_t.push_back(Triplet<double>(1*m+i_idx, j_idx, val_before_n * N(i,1)));
    G_t.push_back(Triplet<double>(2*m+i_idx, j_idx, val_before_n * N(i,2)));
  }
  G.resize(3*m,n);
  G.setFromTriplets(G_t.begin(), G_t.end());
}

template <typename DerivedV, typename DerivedF>
IGL_INLINE void grad_tri(
  const Eigen::MatrixBase<DerivedV>&V,
  const Eigen::MatrixBase<DerivedF>&F,
  Eigen::SparseMatrix<typename DerivedV::Scalar> &G,
  bool uniform)
{

  const int m = F.rows();

  const int nv = V.rows();

  const int dims = V.cols();
  Eigen::Matrix<typename DerivedV::Scalar,Eigen::Dynamic,3>
    eperp21(m,3), eperp13(m,3);

  for (int i=0;i<m;++i)
  {

    int i1 = F(i,0);
    int i2 = F(i,1);
    int i3 = F(i,2);


    typedef Eigen::Matrix<typename DerivedV::Scalar, 1, 3> RowVector3S;
    RowVector3S v32 = RowVector3S::Zero(1,3);
    RowVector3S v13 = RowVector3S::Zero(1,3);
    RowVector3S v21 = RowVector3S::Zero(1,3);
    v32.head(V.cols()) = V.row(i3) - V.row(i2);
    v13.head(V.cols()) = V.row(i1) - V.row(i3);
    v21.head(V.cols()) = V.row(i2) - V.row(i1);
    RowVector3S n = v32.cross(v13);




    double dblA = std::sqrt(n.dot(n));
    Eigen::Matrix<typename DerivedV::Scalar, 1, 3> u(0,0,1);
    if (!uniform) {

      u = n / dblA;
    } else {



      double h = sqrt( (dblA)/sin(igl::PI / 3.0));

      Eigen::Matrix<typename DerivedV::Scalar, 3, 1> v1,v2,v3;
      v1 << 0,0,0;
      v2 << h,0,0;
      v3 << h/2.,(sqrt(3)/2.)*h,0;


      v32 = v3-v2;
      v13 = v1-v3;
      v21 = v2-v1;
      n = v32.cross(v13);
    }


    double norm21 = std::sqrt(v21.dot(v21));
    double norm13 = std::sqrt(v13.dot(v13));
    eperp21.row(i) = u.cross(v21);
    eperp21.row(i) = eperp21.row(i) / std::sqrt(eperp21.row(i).dot(eperp21.row(i)));
    eperp21.row(i) *= norm21 / dblA;
    eperp13.row(i) = u.cross(v13);
    eperp13.row(i) = eperp13.row(i) / std::sqrt(eperp13.row(i).dot(eperp13.row(i)));
    eperp13.row(i) *= norm13 / dblA;
  }


  G.resize(dims*m,nv);
  std::vector<Eigen::Triplet<typename DerivedV::Scalar> > Gijv;
  Gijv.reserve(4*dims*m);
  for(int f = 0;f<F.rows();f++)
  {
    for(int d = 0;d<dims;d++)
    {
      Gijv.emplace_back(f+d*m,F(f,1), eperp13(f,d));
      Gijv.emplace_back(f+d*m,F(f,0),-eperp13(f,d));
      Gijv.emplace_back(f+d*m,F(f,2), eperp21(f,d));
      Gijv.emplace_back(f+d*m,F(f,0),-eperp21(f,d));
    }
  }
  G.setFromTriplets(Gijv.begin(), Gijv.end());
}

}

}

template <typename DerivedV, typename DerivedF>
IGL_INLINE void igl::grad(
  const Eigen::MatrixBase<DerivedV>&V,
  const Eigen::MatrixBase<DerivedF>&F,
  Eigen::SparseMatrix<typename DerivedV::Scalar> &G,
  bool uniform)
{
  assert(F.cols() == 3 || F.cols() == 4);
  switch(F.cols())
  {
    case 3:
      return grad_tri(V,F,G,uniform);
    case 4:
      return grad_tet(V,F,G,uniform);
    default:
      assert(false);
  }
}

#ifdef IGL_STATIC_LIBRARY


template void igl::grad<Eigen::Matrix<double, -1, 2, 0, -1, 2>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, 2, 0, -1, 2> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::SparseMatrix<Eigen::Matrix<double, -1, 2, 0, -1, 2>::Scalar, 0, int>&, bool);
template void igl::grad<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::SparseMatrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 0, int>&, bool);
template void igl::grad<Eigen::Matrix<double, -1, 3, 0, -1, 3>, Eigen::Matrix<int, -1, 3, 0, -1, 3> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, 3, 0, -1, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 3, 0, -1, 3> > const&, Eigen::SparseMatrix<Eigen::Matrix<double, -1, 3, 0, -1, 3>::Scalar, 0, int>&, bool);
#endif
