// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "snap_to_canonical_view_quat.h"

#include "canonical_quaternions.h"
#include "normalize_quat.h"

#include <cstdio>
#include <cassert>








template <typename Q_type>
IGL_INLINE bool igl::snap_to_canonical_view_quat(
  const Q_type* q,
  const Q_type threshold,
  Q_type* s)
{



  s[0] = q[0];
  s[1] = q[1];
  s[2] = q[2];
  s[3] = q[3];


  Q_type qn[4];
  bool valid_len =
    igl::normalize_quat(q,qn);

  if(!valid_len)
  {
    return false;
  }


  const Q_type MAX_DISTANCE = 0.4;
  Q_type min_distance = 2*MAX_DISTANCE;
  int min_index = -1;
  double min_sign = 0;

  for(double sign = -1;sign<=1;sign+=2)
  {
    for(int i = 0; i<NUM_CANONICAL_VIEW_QUAT; i++)
    {
      Q_type distance = 0.0;

      for(int j = 0;j<4;j++)
      {

        distance +=
          (qn[j]-sign*igl::CANONICAL_VIEW_QUAT<Q_type>(i,j))*
          (qn[j]-sign*igl::CANONICAL_VIEW_QUAT<Q_type>(i,j));
      }
      if(min_distance > distance)
      {
        min_distance = distance;
        min_index = i;
        min_sign = sign;
      }
    }
  }

  if(MAX_DISTANCE < min_distance)
  {
    fprintf(
      stderr,
      "ERROR: found new max MIN_DISTANCE: %g\n"
      "PLEASE update snap_to_canonical_quat()",
      min_distance);
  }

  assert(min_distance < MAX_DISTANCE);
  assert(min_index >= 0);

  if( min_distance/MAX_DISTANCE <= threshold)
  {

    for(int j = 0;j<4;j++)
    {
      s[j] = min_sign*igl::CANONICAL_VIEW_QUAT<Q_type>(min_index,j);
    }
    return true;
  }
  return false;
}

template <typename Scalarq, typename Scalars>
IGL_INLINE bool igl::snap_to_canonical_view_quat(
  const Eigen::Quaternion<Scalarq> & q,
  const double threshold,
  Eigen::Quaternion<Scalars> & s)
{
  return snap_to_canonical_view_quat<Scalars>(
    q.coeffs().data(),threshold,s.coeffs().data());
}

#ifdef IGL_STATIC_LIBRARY


template bool igl::snap_to_canonical_view_quat<double>(const double*, double, double*);

template bool igl::snap_to_canonical_view_quat<float>(const float*, float, float*);
template bool igl::snap_to_canonical_view_quat<float, float>(Eigen::Quaternion<float, 0> const&, double, Eigen::Quaternion<float, 0>&);
template bool igl::snap_to_canonical_view_quat<double, double>(Eigen::Quaternion<double, 0> const&, double, Eigen::Quaternion<double, 0>&);
#endif
