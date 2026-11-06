// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "partition.h"
#include "min.h"

IGL_INLINE void igl::partition(
  const Eigen::MatrixXd & W,
  const int k,
  Eigen::Matrix<int,Eigen::Dynamic,1> & G,
  Eigen::Matrix<int,Eigen::Dynamic,1> & S,
  Eigen::Matrix<double,Eigen::Dynamic,1> & D)
{

  int n = W.rows();


  G.resize(n);
  S.resize(k);



  int s;
  (W.array().square().matrix()).rowwise().sum().maxCoeff(&s);

  S(0) = s;

  D = ((W.rowwise() - W.row(s)).array().square()).matrix().rowwise().sum();
  G.setZero();


  for(int i = 1;i<k;i++)
  {

    D.maxCoeff(&s);
    S(i) = s;

    Eigen::Matrix<double,Eigen::Dynamic,1> Ds =
      ((W.rowwise() - W.row(s)).array().square()).matrix().rowwise().sum();

    Eigen::Matrix<double,Eigen::Dynamic,Eigen::Dynamic> DDs;

    DDs.resize(D.rows(),2);
    DDs.col(0) = D;
    DDs.col(1) = Ds;



    Eigen::Matrix<int,Eigen::Dynamic,1> C;
    igl::min(DDs,2,D,C);
    G = (C.array() ==0).select(G,i);
  }


}
