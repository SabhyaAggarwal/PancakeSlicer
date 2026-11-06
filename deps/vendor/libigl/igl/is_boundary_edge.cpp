// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "is_boundary_edge.h"
#include "unique_rows.h"
#include "sort.h"

template <
  typename DerivedF,
  typename DerivedE,
  typename DerivedB>
void igl::is_boundary_edge(
  const Eigen::MatrixBase<DerivedE> & E,
  const Eigen::MatrixBase<DerivedF> & F,
  Eigen::PlainObjectBase<DerivedB> & B)
{
  using namespace Eigen;
  using namespace std;

  assert(F.cols() == 3);

  assert(E.cols() == 2);

  const int m = F.rows();

  MatrixXi EallE(E.rows()+3*m,2);
  EallE.block(0,0,E.rows(),E.cols()) = E;
  for(int e = 0;e<3;e++)
  {
    for(int f = 0;f<m;f++)
    {
      for(int c = 0;c<2;c++)
      {

        EallE(E.rows()+m*e+f,c) = F(f,(c+1+e)%3);
      }
    }
  }

  MatrixXi sorted_EallE;
  {
    MatrixXi _;
    sort(EallE,2,true,sorted_EallE,_);
  }

  MatrixXi uE;
  VectorXi EMAP;
  {
    VectorXi _;
    unique_rows(sorted_EallE,uE,_,EMAP);
  }

  VectorXi N = VectorXi::Zero(uE.rows());
  for(int e = 0;e<EMAP.rows();e++)
  {
    N(EMAP(e))++;
  }
  B.resize(E.rows());

  for(int e = 0;e<E.rows();e++)
  {
    B(e) = (N(EMAP(e)) == 2);
  }
}


template <
  typename DerivedF,
  typename DerivedE,
  typename DerivedB,
  typename DerivedEMAP>
void igl::is_boundary_edge(
  const Eigen::MatrixBase<DerivedF> & F,
  Eigen::PlainObjectBase<DerivedB> & B,
  Eigen::PlainObjectBase<DerivedE> & E,
  Eigen::PlainObjectBase<DerivedEMAP> & EMAP)
{

  static_assert(
    (DerivedB::RowsAtCompileTime == 1 || DerivedB::ColsAtCompileTime == 1) &&
    (DerivedEMAP::RowsAtCompileTime == 1 || DerivedEMAP::ColsAtCompileTime == 1),
    "B and EMAP need to have RowsAtCompileTime == 1 or ColsAtCompileTime == 1");
  using namespace Eigen;
  using namespace std;

  assert(F.cols() == 3);

  const int m = F.rows();

  MatrixXi allE(3*m,2);
  for(int e = 0;e<3;e++)
  {
    for(int f = 0;f<m;f++)
    {
      for(int c = 0;c<2;c++)
      {

        allE(m*e+f,c) = F(f,(c+1+e)%3);
      }
    }
  }

  MatrixXi sorted_allE;
  {
    Eigen::MatrixXi _;
    sort(allE,2,true,sorted_allE,_);
  }

  {
    Eigen::VectorXi _;
    unique_rows(sorted_allE,E,_,EMAP);
  }

  VectorXi N = VectorXi::Zero(E.rows());
  for(int e = 0;e<EMAP.rows();e++)
  {
    N(EMAP(e))++;
  }
  B.resize(E.rows());

  for(int e = 0;e<E.rows();e++)
  {
    B(e) = N(e) == 1;
  }
}

#ifdef IGL_STATIC_LIBRARY


template void igl::is_boundary_edge<Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Array<bool, -1, 1, 0, -1, 1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Array<bool, -1, 1, 0, -1, 1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&);
template void igl::is_boundary_edge<Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<bool, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<bool, -1, 1, 0, -1, 1> >&);
template void igl::is_boundary_edge<Eigen::Matrix<unsigned int, -1, -1, 1, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<bool, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<unsigned int, -1, -1, 1, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<bool, -1, 1, 0, -1, 1> >&);
template void igl::is_boundary_edge<Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<bool, -1, 1, 0, -1, 1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<bool, -1, 1, 0, -1, 1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&);
template void igl::is_boundary_edge<Eigen::Matrix<unsigned int, -1, -1, 1, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<bool, -1, 1, 0, -1, 1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<unsigned int, -1, -1, 1, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<bool, -1, 1, 0, -1, 1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&);
#endif
