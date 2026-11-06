// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "bone_visible.h"
#include "../project_to_line.h"
#include "../EPS.h"
#include "../Hit.h"
#include "../parallel_for.h"
#include "../Timer.h"
#include <iostream>

template <
  typename DerivedV,
  typename DerivedF,
  typename DerivedSD,
  typename Derivedflag>
IGL_INLINE void igl::embree::bone_visible(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedF> & F,
  const Eigen::MatrixBase<DerivedSD> & s,
  const Eigen::MatrixBase<DerivedSD> & d,
  Eigen::PlainObjectBase<Derivedflag>  & flag)
{

  Eigen::Matrix<typename DerivedF::Scalar,Eigen::Dynamic,Eigen::Dynamic> FF;
  FF.resize(F.rows()*2,F.cols());
  FF << F, F.rowwise().reverse();

  EmbreeIntersector ei;
  ei.init(V.template cast<float>(),FF.template cast<int>());
  return bone_visible(V,F,ei,s,d,flag);
}

template <
  typename DerivedV,
  typename DerivedF,
  typename DerivedSD,
  typename Derivedflag>
IGL_INLINE void igl::embree::bone_visible(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedF> & F,
  const EmbreeIntersector & ei,
  const Eigen::MatrixBase<DerivedSD> & s,
  const Eigen::MatrixBase<DerivedSD> & d,
  Eigen::PlainObjectBase<Derivedflag>  & flag)
{
  using namespace std;
  using namespace Eigen;
  flag.resize(V.rows());
  const double sd_norm = (s-d).norm();


  parallel_for(V.rows(),[&](const int v)
  {
    const Vector3d Vv = V.row(v);


    double t,sqrd;
    Vector3d projv;

    if(sd_norm < DOUBLE_EPS)
    {
      t = 0;
      sqrd = (Vv-s).array().pow(2).sum();
      projv = s;
    }else
    {

      project_to_line(
        Vv(0),Vv(1),Vv(2),s(0),s(1),s(2),d(0),d(1),d(2),
        projv(0),projv(1),projv(2),t,sqrd);

      if(t<0)
      {
        t = 0;
        sqrd = (Vv-s).array().pow(2).sum();
        projv = s;
      } else if(t>1)
      {
        t = 1;
        sqrd = (Vv-d).array().pow(2).sum();
        projv = d;
      }
    }


    igl::Hit<float> hit;


    const Vector3d dir = (Vv-projv)*1.0;
    if(ei.intersectSegment(
       projv.template cast<float>(),
       dir.template cast<float>(),
       hit))
    {

      const int fi = hit.id % F.rows();

















      flag(v) = false;

      for(int c = 0;c<F.cols();c++)
      {
        if(F(fi,c) == v)
        {

          flag(v) = true;
          break;
        }
      }

      if(!flag(v) && (hit.t*hit.t*dir.squaredNorm())>sqrd)
      {
        flag(v) = true;
      }
    }else
    {

      flag(v) = true;
    }
  },10000);
}

#ifdef IGL_STATIC_LIBRARY

template void igl::embree::bone_visible<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, 3, 1, 0, 3, 1>, Eigen::Matrix<bool, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, 3, 1, 0, 3, 1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, 3, 1, 0, 3, 1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<bool, -1, 1, 0, -1, 1> >&);
template void igl::embree::bone_visible<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<bool, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<bool, -1, 1, 0, -1, 1> >&);
#endif
