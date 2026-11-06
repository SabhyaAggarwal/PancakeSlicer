// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "edge_collapse_is_valid.h"
#include "collapse_edge.h"
#include "circulation.h"
#include "intersect.h"
#include "unique.h"
#include "list_to_matrix.h"
#include <vector>

template <
  typename DerivedF,
  typename DerivedE,
  typename DerivedEMAP,
  typename DerivedEF,
  typename DerivedEI>
IGL_INLINE bool igl::edge_collapse_is_valid(
  const int e,
  const Eigen::MatrixBase<DerivedF> & F,
  const Eigen::MatrixBase<DerivedE> & E,
  const Eigen::MatrixBase<DerivedEMAP> & EMAP,
  const Eigen::MatrixBase<DerivedEF> & EF,
  const Eigen::MatrixBase<DerivedEI> & EI)
{
  using namespace Eigen;
  using namespace std;


  const int eflip = E(e,0)>E(e,1);

  const int s = eflip?E(e,1):E(e,0);
  const int d = eflip?E(e,0):E(e,1);

  if(s == IGL_COLLAPSE_EDGE_NULL && d==IGL_COLLAPSE_EDGE_NULL)
  {
    return false;
  }



  {

    const auto neighbors = [&F,&E,&EMAP,&EF,&EI](
      const int e,
      const bool ccw)
    {
      vector<int> N,uN;
      vector<int> V2Fe = circulation(e, ccw,EMAP,EF,EI);
      for(auto f : V2Fe)
      {
        N.push_back(F(f,0));
        N.push_back(F(f,1));
        N.push_back(F(f,2));
      }
      vector<size_t> _1,_2;
      igl::unique(N,uN,_1,_2);
      VectorXi uNm;
      list_to_matrix(uN,uNm);
      return uNm;
    };
    VectorXi Ns = neighbors(e, eflip);
    VectorXi Nd = neighbors(e,!eflip);
    VectorXi Nint = igl::intersect(Ns,Nd);
    if(Nint.size() != 4)
    {
      return false;
    }
    if(Ns.size() == 4 && Nd.size() == 4)
    {
      VectorXi NsNd(8);
      NsNd<<Ns,Nd;
      VectorXi Nun,_1,_2;
      igl::unique(NsNd,Nun,_1,_2);

      if(Nun.size() == 4)
      {
        return false;
      }
    }
  }
  return true;
}

IGL_INLINE bool igl::edge_collapse_is_valid(
  std::vector<int> & Nsv,
  std::vector<int> & Ndv)
{


  if(Nsv.size()<2 || Ndv.size()<2)
  {

    assert(false);
    return false;
  }



  const bool first_two_same = (Nsv[0] == Ndv[0]) && (Nsv[1] == Ndv[1]);
  if(Nsv.size() == 3 && Ndv.size() == 3 && first_two_same)
  {

    return false;
  }

  std::sort(Nsv.begin(), Nsv.end());
  std::sort(Ndv.begin(), Ndv.end());
  std::vector<int> Nint;
  std::set_intersection(
    Nsv.begin(), Nsv.end(), Ndv.begin(), Ndv.end(), std::back_inserter(Nint));



  if(Nint.size() != 2)
  {
    return false;
  }

  return true;
}

#ifdef IGL_STATIC_LIBRARY

#endif
