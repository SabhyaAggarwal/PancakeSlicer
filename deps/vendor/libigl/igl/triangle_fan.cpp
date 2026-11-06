// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "triangle_fan.h"
#include "exterior_edges.h"
#include "list_to_matrix.h"

template <typename DerivedE, typename Derivedcap>
IGL_INLINE void igl::triangle_fan(
  const Eigen::MatrixBase<DerivedE> & E,
  Eigen::PlainObjectBase<Derivedcap> & cap)
{
  using namespace std;
  using namespace Eigen;


  if(E.size() == 0)
  {
    cap.resize(0,E.cols()+1);
    return;
  }



  assert(E.cols() == 2);


  int s = E(rand()%E.rows(),0);
  vector<vector<int> >  lcap;
  for(int i = 0;i<E.rows();i++)
  {

    if(E(i,0) == s || E(i,1) == s)
    {
      continue;
    }
    vector<int> e(3);
    e[0] = s;
    e[1] = E(i,0);
    e[2] = E(i,1);
    lcap.push_back(e);
  }
  list_to_matrix(lcap,cap);
}

#if IGL_STATIC_LIBRARY


template void igl::triangle_fan<Eigen::Matrix<int, -1, 2, 0, -1, 2>, Eigen::Matrix<int, -1, -1, 0, -1, -1>>(Eigen::MatrixBase<Eigen::Matrix<int, -1, 2, 0, -1, 2>> const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>>&);
#endif
