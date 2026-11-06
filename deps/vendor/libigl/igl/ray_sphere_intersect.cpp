// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "ray_sphere_intersect.h"

template <
  typename Derivedo,
  typename Derivedd,
  typename Derivedc,
  typename r_type,
  typename t_type>
IGL_INLINE int igl::ray_sphere_intersect(
  const Eigen::MatrixBase<Derivedo> & ao,
  const Eigen::MatrixBase<Derivedd> & d,
  const Eigen::MatrixBase<Derivedc> & ac,
  r_type r,
  t_type & t0,
  t_type & t1)
{
  Eigen::Vector3d o = ao-ac;


  double a = d.dot(d);
  double b = 2 * d.dot(o);
  double c = o.dot(o) - (r * r);


  double disc = b * b - 4 * a * c;



  if (disc < 0)
  {
    return 0;
  }


  double distSqrt = sqrt(disc);
  double q;
  if (b < 0)
  {
    q = (-b - distSqrt)/2.0;
  } else
  {
    q = (-b + distSqrt)/2.0;
  }


  t0 = q / a;
  double _t1 = c/q;
  if(_t1 == t0)
  {
    return 1;
  }
  t1 = _t1;

  if (t0 > t1)
  {

    double temp = t0;
    t0 = t1;
    t1 = temp;
  }
  return 2;
}

#ifdef IGL_STATIC_LIBRARY

template int igl::ray_sphere_intersect<Eigen::Matrix<double, 3, 1, 0, 3, 1>, Eigen::Matrix<double, 3, 1, 0, 3, 1>, Eigen::Matrix<double, 3, 1, 0, 3, 1>, double, double>(Eigen::MatrixBase<Eigen::Matrix<double, 3, 1, 0, 3, 1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, 3, 1, 0, 3, 1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, 3, 1, 0, 3, 1> > const&, double, double&, double&);
#endif
