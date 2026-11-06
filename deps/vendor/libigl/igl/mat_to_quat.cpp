// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "mat_to_quat.h"
#include <cmath>


template <typename Q_type>
static inline Q_type ReciprocalSqrt( const Q_type x )
{
  return 1.0/sqrt(x);
}















template <typename Q_type>
IGL_INLINE void igl::mat4_to_quat(const Q_type * mat, Q_type * q)
{
  Q_type trace;
  Q_type s;
  Q_type t;
  int i;
  int j;
  int k;

  static int next[3] = { 1, 2, 0 };

  trace = mat[0 * 4 + 0] + mat[1 * 4 + 1] + mat[2 * 4 + 2];

  if ( trace > 0.0f ) {

    t = trace + 1.0f;
    s = ReciprocalSqrt( t ) * 0.5f;

    q[3] = s * t;
    q[0] = ( mat[1 * 4 + 2] - mat[2 * 4 + 1] ) * s;
    q[1] = ( mat[2 * 4 + 0] - mat[0 * 4 + 2] ) * s;
    q[2] = ( mat[0 * 4 + 1] - mat[1 * 4 + 0] ) * s;

  } else {

    i = 0;
    if ( mat[1 * 4 + 1] > mat[0 * 4 + 0] ) {
      i = 1;
    }
    if ( mat[2 * 4 + 2] > mat[i * 4 + i] ) {
      i = 2;
    }
    j = next[i];
    k = next[j];

    t = ( mat[i * 4 + i] - ( mat[j * 4 + j] + mat[k * 4 + k] ) ) + 1.0f;
    s = ReciprocalSqrt( t ) * 0.5f;

    q[i] = s * t;
    q[3] = ( mat[j * 4 + k] - mat[k * 4 + j] ) * s;
    q[j] = ( mat[i * 4 + j] + mat[j * 4 + i] ) * s;
    q[k] = ( mat[i * 4 + k] + mat[k * 4 + i] ) * s;
  }





}

template <typename Q_type>
IGL_INLINE void igl::mat3_to_quat(const Q_type * mat, Q_type * q)
{
  Q_type trace;
  Q_type s;
  Q_type t;
  int i;
  int j;
  int k;

  static int next[3] = { 1, 2, 0 };

  trace = mat[0 * 3 + 0] + mat[1 * 3 + 1] + mat[2 * 3 + 2];

  if ( trace > 0.0f ) {

    t = trace + 1.0f;
    s = ReciprocalSqrt( t ) * 0.5f;

    q[3] = s * t;
    q[0] = ( mat[1 * 3 + 2] - mat[2 * 3 + 1] ) * s;
    q[1] = ( mat[2 * 3 + 0] - mat[0 * 3 + 2] ) * s;
    q[2] = ( mat[0 * 3 + 1] - mat[1 * 3 + 0] ) * s;

  } else {

    i = 0;
    if ( mat[1 * 3 + 1] > mat[0 * 3 + 0] ) {
      i = 1;
    }
    if ( mat[2 * 3 + 2] > mat[i * 3 + i] ) {
      i = 2;
    }
    j = next[i];
    k = next[j];

    t = ( mat[i * 3 + i] - ( mat[j * 3 + j] + mat[k * 3 + k] ) ) + 1.0f;
    s = ReciprocalSqrt( t ) * 0.5f;

    q[i] = s * t;
    q[3] = ( mat[j * 3 + k] - mat[k * 3 + j] ) * s;
    q[j] = ( mat[i * 3 + j] + mat[j * 3 + i] ) * s;
    q[k] = ( mat[i * 3 + k] + mat[k * 3 + i] ) * s;
  }





}



#ifdef IGL_STATIC_LIBRARY

template void igl::mat4_to_quat<double>(double const*, double*);
template void igl::mat4_to_quat<float>(float const*, float*);
template void igl::mat3_to_quat<double>(double const*, double*);
#endif
