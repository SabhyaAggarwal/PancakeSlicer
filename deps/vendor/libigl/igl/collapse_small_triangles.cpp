// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "collapse_small_triangles.h"

#include "bounding_box_diagonal.h"
#include "doublearea.h"
#include "edge_lengths.h"
#include "colon.h"
#include "faces_first.h"

#include <limits>

#include <iostream>

template <
  typename DerivedV,
  typename DerivedF,
  typename DerivedFF>
void igl::collapse_small_triangles(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedF> & F,
  const double eps,
  Eigen::PlainObjectBase<DerivedFF> & FF)
{
  using namespace Eigen;
  using namespace std;


  double bbd = bounding_box_diagonal(V);
  MatrixXd l;
  edge_lengths(V,F,l);
  VectorXd dblA;
  doublearea(l,0.,dblA);


  const double min_dblarea = 2.0*eps*bbd*bbd;

  Eigen::VectorXi FIM = colon<int>(0,V.rows()-1);
  int num_edge_collapses = 0;

  for(int f = 0;f<F.rows();f++)
  {
    if(dblA(f) < min_dblarea)
    {
      double minl = 0;
      int minli = -1;

      for(int e = 0;e<3;e++)
      {
        if(minli==-1 || l(f,e)<minl)
        {
          minli = e;
          minl = l(f,e);
        }
      }
      double maxl = 0;
      int maxli = -1;

      for(int e = 0;e<3;e++)
      {
        if(maxli==-1 || l(f,e)>maxl)
        {
          maxli = e;
          maxl = l(f,e);
        }
      }

      maxli = (minli==maxli?(minli+1)%3:maxli);



      int i = maxli;
      int j = ((minli+1)%3 == maxli ? (minli+2)%3: (minli+1)%3);
      assert(i != minli);
      assert(j != minli);
      assert(i != j);
      FIM(F(f,i)) = FIM(F(f,j));
      num_edge_collapses++;
    }
  }


  MatrixXi rF = F;

  for(int f = 0;f<rF.rows();f++)
  {
    for(int i = 0;i<rF.cols();i++)
    {
      rF(f,i) = FIM(rF(f,i));
    }
  }

  FF.resizeLike(rF);
#ifndef NDEBUG
  int num_face_collapses=0;
#endif

  {
    int ff = 0;

    for(int f = 0;f<rF.rows();f++)
    {
      bool collapsed = false;

      for(int i = 0;i<rF.cols();i++)
      {
        for(int j = i+1;j<rF.cols();j++)
        {
          if(rF(f,i)==rF(f,j))
          {
            collapsed = true;
#ifndef NDEBUG
            num_face_collapses++;
#endif
            break;
          }
        }
      }
      if(!collapsed)
      {
        FF.row(ff++) = rF.row(f);
      }
    }

    FF.conservativeResize(ff,FF.cols());
  }


  if(num_edge_collapses == 0)
  {

    assert(num_face_collapses==0);

    return;
  }




  MatrixXi recFF = FF;
  return collapse_small_triangles(V,recFF,eps,FF);
}

#ifdef IGL_STATIC_LIBRARY

template void igl::collapse_small_triangles<Eigen::MatrixXd, Eigen::MatrixXi, Eigen::MatrixXi> ( const Eigen::MatrixBase<Eigen::MatrixXd> &, const Eigen::MatrixBase<Eigen::MatrixXi> &, const double , Eigen::PlainObjectBase<Eigen::MatrixXi> & );
#endif
