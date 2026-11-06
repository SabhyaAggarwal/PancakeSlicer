// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "collapse_edge.h"
#include "circulation.h"
#include "edge_collapse_is_valid.h"
#include <vector>

template <
  typename Derivedp,
  typename DerivedV,
  typename DerivedF,
  typename DerivedE,
  typename DerivedEMAP,
  typename DerivedEF,
  typename DerivedEI>
IGL_INLINE bool igl::collapse_edge(
  const int e,
  const Eigen::MatrixBase<Derivedp> & p,
  Eigen::MatrixBase<DerivedV> & V,
  Eigen::MatrixBase<DerivedF> & F,
  Eigen::MatrixBase<DerivedE> & E,
  Eigen::MatrixBase<DerivedEMAP> & EMAP,
  Eigen::MatrixBase<DerivedEF> & EF,
  Eigen::MatrixBase<DerivedEI> & EI,
  int & e1,
  int & e2,
  int & f1,
  int & f2)
{
  std::vector<int>  Nsf,Nsv;
  circulation(e, true,F,EMAP,EF,EI, Nsv,Nsf);
  std::vector<int>  Ndf,Ndv;
  circulation(e, false,F,EMAP,EF,EI, Ndv,Ndf);
  return collapse_edge(
    e,p,Nsv,Nsf,Ndv,Ndf,V,F,E,EMAP,EF,EI,e1,e2,f1,f2);
}

template
<
  typename Derivedp,
  typename DerivedV,
  typename DerivedF,
  typename DerivedE,
  typename DerivedEMAP,
  typename DerivedEF,
  typename DerivedEI>
IGL_INLINE bool igl::collapse_edge(
  const int e,
  const Eigen::MatrixBase<Derivedp> & p,
    std::vector<int> & Nsv,
  const std::vector<int> & Nsf,
    std::vector<int> & Ndv,
  const std::vector<int> & Ndf,
  Eigen::MatrixBase<DerivedV> & V,
  Eigen::MatrixBase<DerivedF> & F,
  Eigen::MatrixBase<DerivedE> & E,
  Eigen::MatrixBase<DerivedEMAP> & EMAP,
  Eigen::MatrixBase<DerivedEF> & EF,
  Eigen::MatrixBase<DerivedEI> & EI,
  int & a_e1,
  int & a_e2,
  int & a_f1,
  int & a_f2)
{



  using namespace Eigen;
  using namespace std;
  const int eflip = E(e,0)>E(e,1);

  const int s = eflip?E(e,1):E(e,0);
  const int d = eflip?E(e,0):E(e,1);

  if(!edge_collapse_is_valid(Nsv,Ndv))
  {
    return false;
  }




  const std::vector<int> & nV2Fd = (!eflip ? Nsf : Ndf);


  assert(s<d && "s should be less than d");

  V.row(s) = p;
  V.row(d) = p;


  const auto & kill_edge = [&E,&EI,&EF](const int e)
  {
    E(e,0) = IGL_COLLAPSE_EDGE_NULL;
    E(e,1) = IGL_COLLAPSE_EDGE_NULL;
    EF(e,0) = IGL_COLLAPSE_EDGE_NULL;
    EF(e,1) = IGL_COLLAPSE_EDGE_NULL;
    EI(e,0) = IGL_COLLAPSE_EDGE_NULL;
    EI(e,1) = IGL_COLLAPSE_EDGE_NULL;
  };



  const int m = F.rows();
  for(int side = 0;side<2;side++)
  {
    const int f = EF(e,side);
    const int v = EI(e,side);
    const int sign = (eflip==0?1:-1)*(1-2*side);

    const int e1 = EMAP(f+m*((v+sign*1+3)%3));

    const int e2 = EMAP(f+m*((v+sign*2+3)%3));
    assert(E(e1,0) == d || E(e1,1) == d);
    assert(E(e2,0) == s || E(e2,1) == s);

    const bool flip1 = EF(e1,1)==f;
    const int f1 = flip1 ? EF(e1,0) : EF(e1,1);
    assert(f1!=f);
    assert(F(f1,0)==d || F(f1,1)==d || F(f1,2) == d);

    const int v1 = flip1 ? EI(e1,0) : EI(e1,1);

    kill_edge(e1);

    F(f,0) = IGL_COLLAPSE_EDGE_NULL;
    F(f,1) = IGL_COLLAPSE_EDGE_NULL;
    F(f,2) = IGL_COLLAPSE_EDGE_NULL;

    assert(EMAP(f1+m*v1) == e1);
    EMAP(f1+m*v1) = e2;

    const int opp2 = (EF(e2,0)==f?0:1);
    assert(EF(e2,opp2) == f);
    EF(e2,opp2) = f1;
    EI(e2,opp2) = v1;

    E(e2,0) = E(e2,0)==d ? s : E(e2,0);
    E(e2,1) = E(e2,1)==d ? s : E(e2,1);
    if(side==0)
    {
      a_e1 = e1;
      a_f1 = f;
    }else
    {
      a_e2 = e1;
      a_f2 = f;
    }
  }








  {
    int p1 = -1;
    for(auto f : nV2Fd)
    {
      for(int v = 0;v<3;v++)
      {
        if(F(f,v) == d)
        {
          const int e1 = EMAP(f+m*((v+1)%3));
          const int flip1 = (EF(e1,0)==f)?1:0;
          assert( E(e1,flip1) == d || E(e1,flip1) == s);
          E(e1,flip1) = s;
          const int e2 = EMAP(f+m*((v+2)%3));


          if(e2 != p1)
          {
            const int flip2 = (EF(e2,0)==f)?0:1;
            assert( E(e2,flip2) == d || E(e2,flip2) == s);
            E(e2,flip2) = s;
          }

          F(f,v) = s;
          p1 = e1;
          break;
        }
      }
    }
  }

  kill_edge(e);
  return true;
}


#ifdef IGL_STATIC_LIBRARY

template bool igl::collapse_edge<Eigen::Matrix<double, 1, -1, 1, 1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>>(int, Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>>&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>>&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>>&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 1, 0, -1, 1>>&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>>&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>>&, int&, int&, int&, int&);
template bool igl::collapse_edge<Eigen::Block<Eigen::Matrix<double, -1, -1, 0, -1, -1>, 1, -1, false>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>>(int, Eigen::MatrixBase<Eigen::Block<Eigen::Matrix<double, -1, -1, 0, -1, -1>, 1, -1, false>> const&, std::vector<int, std::allocator<int>>&, std::vector<int, std::allocator<int>> const&, std::vector<int, std::allocator<int>>&, std::vector<int, std::allocator<int>> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>>&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>>&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>>&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 1, 0, -1, 1>>&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>>&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>>&, int&, int&, int&, int&);
#endif
