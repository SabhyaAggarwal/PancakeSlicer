// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_CANONICAL_QUATERNIONS_H
#define IGL_CANONICAL_QUATERNIONS_H
#include "igl_inline.h"







namespace igl
{


#define SQRT_2_OVER_2 0.707106781f

  const float IDENTITY_QUAT_F[4] = {0,0,0,1};


  const float XY_PLANE_QUAT_F[4] = {0,0,0,1};

  const float XZ_PLANE_QUAT_F[4] = {-SQRT_2_OVER_2,0,0,SQRT_2_OVER_2};

  const float YZ_PLANE_QUAT_F[4] = {-0.5,-0.5,-0.5,0.5};
  const float CANONICAL_VIEW_QUAT_F[][4] =
    {
      {             0,             0,             0,             1},
      {             0,             0, SQRT_2_OVER_2, SQRT_2_OVER_2},
      {             0,             0,             1,             0},
      {             0,             0, SQRT_2_OVER_2,-SQRT_2_OVER_2},

      {             0,            -1,             0,             0},
      {-SQRT_2_OVER_2, SQRT_2_OVER_2,             0,             0},
      {            -1,             0,             0,             0},
      {-SQRT_2_OVER_2,-SQRT_2_OVER_2,             0,             0},

      {          -0.5,          -0.5,          -0.5,           0.5},
      {             0,-SQRT_2_OVER_2,             0, SQRT_2_OVER_2},
      {           0.5,          -0.5,           0.5,           0.5},
      { SQRT_2_OVER_2,             0, SQRT_2_OVER_2,             0},

      { SQRT_2_OVER_2,             0,-SQRT_2_OVER_2,             0},
      {           0.5,           0.5,          -0.5,           0.5},
      {             0, SQRT_2_OVER_2,             0, SQRT_2_OVER_2},
      {          -0.5,           0.5,           0.5,           0.5},

      {             0, SQRT_2_OVER_2, SQRT_2_OVER_2,             0},
      {          -0.5,           0.5,           0.5,          -0.5},
      {-SQRT_2_OVER_2,             0,             0,-SQRT_2_OVER_2},
      {          -0.5,          -0.5,          -0.5,          -0.5},

      {-SQRT_2_OVER_2,             0,             0, SQRT_2_OVER_2},
      {          -0.5,          -0.5,           0.5,           0.5},
      {             0,-SQRT_2_OVER_2, SQRT_2_OVER_2,             0},
      {           0.5,          -0.5,           0.5,          -0.5}
    };
#undef SQRT_2_OVER_2

#define SQRT_2_OVER_2 0.70710678118654757

  const double IDENTITY_QUAT_D[4] = {0,0,0,1};


  const double XY_PLANE_QUAT_D[4] = {0,0,0,1};

  const double XZ_PLANE_QUAT_D[4] = {-SQRT_2_OVER_2,0,0,SQRT_2_OVER_2};

  const double YZ_PLANE_QUAT_D[4] = {-0.5,-0.5,-0.5,0.5};
  const double CANONICAL_VIEW_QUAT_D[][4] =
    {
      {             0,             0,             0,             1},
      {             0,             0, SQRT_2_OVER_2, SQRT_2_OVER_2},
      {             0,             0,             1,             0},
      {             0,             0, SQRT_2_OVER_2,-SQRT_2_OVER_2},

      {             0,            -1,             0,             0},
      {-SQRT_2_OVER_2, SQRT_2_OVER_2,             0,             0},
      {            -1,             0,             0,             0},
      {-SQRT_2_OVER_2,-SQRT_2_OVER_2,             0,             0},

      {          -0.5,          -0.5,          -0.5,           0.5},
      {             0,-SQRT_2_OVER_2,             0, SQRT_2_OVER_2},
      {           0.5,          -0.5,           0.5,           0.5},
      { SQRT_2_OVER_2,             0, SQRT_2_OVER_2,             0},

      { SQRT_2_OVER_2,             0,-SQRT_2_OVER_2,             0},
      {           0.5,           0.5,          -0.5,           0.5},
      {             0, SQRT_2_OVER_2,             0, SQRT_2_OVER_2},
      {          -0.5,           0.5,           0.5,           0.5},

      {             0, SQRT_2_OVER_2, SQRT_2_OVER_2,             0},
      {          -0.5,           0.5,           0.5,          -0.5},
      {-SQRT_2_OVER_2,             0,             0,-SQRT_2_OVER_2},
      {          -0.5,          -0.5,          -0.5,          -0.5},

      {-SQRT_2_OVER_2,             0,             0, SQRT_2_OVER_2},
      {          -0.5,          -0.5,           0.5,           0.5},
      {             0,-SQRT_2_OVER_2, SQRT_2_OVER_2,             0},
      {           0.5,          -0.5,           0.5,          -0.5}
    };
#undef SQRT_2_OVER_2
#define NUM_CANONICAL_VIEW_QUAT 24










  template <typename Q_type>
  IGL_INLINE Q_type CANONICAL_VIEW_QUAT(int i, int j);

  template <>
  IGL_INLINE float CANONICAL_VIEW_QUAT<float>(int i, int j);
  template <>
  IGL_INLINE double CANONICAL_VIEW_QUAT<double>(int i, int j);

}

#ifndef IGL_STATIC_LIBRARY
#  include "canonical_quaternions.cpp"
#endif

#endif
