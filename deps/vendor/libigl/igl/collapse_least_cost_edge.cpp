// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "collapse_least_cost_edge.h"
#include "collapse_edge.h"
#include "circulation.h"

IGL_INLINE bool igl::collapse_least_cost_edge(
  const decimate_cost_and_placement_callback & cost_and_placement,
  const decimate_pre_collapse_callback       & pre_collapse,
  const decimate_post_collapse_callback      & post_collapse,
  Eigen::MatrixXd & V,
  Eigen::MatrixXi & F,
  Eigen::MatrixXi & E,
  Eigen::VectorXi & EMAP,
  Eigen::MatrixXi & EF,
  Eigen::MatrixXi & EI,
  igl::min_heap< std::tuple<double,int,int> > & Q,
  Eigen::VectorXi & EQ,
  Eigen::MatrixXd & C,
  int & e,
  int & e1,
  int & e2,
  int & f1,
  int & f2)
{
  using namespace Eigen;
  using namespace igl;
  std::tuple<double,int,int> p;
  while(true)
  {

    if(Q.empty())
    {

      e = -1;
      return false;
    }

    p = Q.top();
    if(std::get<0>(p) == std::numeric_limits<double>::infinity())
    {
      e = -1;

      return false;
    }
    Q.pop();
    e = std::get<1>(p);

    if(std::get<2>(p) == EQ(e))
    {
      break;
    }

    assert(std::get<2>(p)  < EQ(e) || EQ(e) == -1);

  }




  std::vector<int>  Nsf,Nsv;
  circulation(e, true,F,EMAP,EF,EI, Nsv,Nsf);
  std::vector<int>  Ndf,Ndv;
  circulation(e, false,F,EMAP,EF,EI, Ndv,Ndf);


  bool collapsed = true;
  if(pre_collapse(V,F,E,EMAP,EF,EI,Q,EQ,C,e))
  {
    collapsed = collapse_edge(
      e,C.row(e),
      Nsv,Nsf,Ndv,Ndf,
      V,F,E,EMAP,EF,EI,e1,e2,f1,f2);
  }else
  {

    collapsed = false;
  }
  post_collapse(V,F,E,EMAP,EF,EI,Q,EQ,C,e,e1,e2,f1,f2,collapsed);
  if(collapsed)
  {

    EQ(e) = -1;

    EQ(e1) = -1;
    EQ(e2) = -1;







    std::vector<int> Nf;
    Nf.reserve( Nsf.size() + Ndf.size() );
    Nf.insert( Nf.end(), Nsf.begin(), Nsf.end() );
    Nf.insert( Nf.end(), Ndf.begin(), Ndf.end() );

    std::sort( Nf.begin(), Nf.end() );
    Nf.erase( std::unique( Nf.begin(), Nf.end() ), Nf.end() );

    std::vector<int> Ne;
    Ne.reserve(3*Nf.size());
    for(auto & n : Nf)
    {
      if(F(n,0) != IGL_COLLAPSE_EDGE_NULL ||
          F(n,1) != IGL_COLLAPSE_EDGE_NULL ||
          F(n,2) != IGL_COLLAPSE_EDGE_NULL)
      {
        for(int v = 0;v<3;v++)
        {

          const int ei = EMAP(v*F.rows()+n);
          Ne.push_back(ei);
        }
      }
    }

    std::sort( Ne.begin(), Ne.end() );
    Ne.erase( std::unique( Ne.begin(), Ne.end() ), Ne.end() );
    for(auto & ei : Ne)
    {

       double cost;
       RowVectorXd place;
       cost_and_placement(ei,V,F,E,EMAP,EF,EI,cost,place);

       EQ(ei)++;

       Q.emplace(cost,ei,EQ(ei));
       C.row(ei) = place;
    }
  }else
  {



    EQ(e)++;

    Q.emplace(std::numeric_limits<double>::infinity(),e,EQ(e));
  }
  return collapsed;
}
