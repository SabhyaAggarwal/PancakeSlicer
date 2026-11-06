// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_TWO_AXIS_VALUATOR_FIXED_AXIS_UP_H
#define IGL_TWO_AXIS_VALUATOR_FIXED_AXIS_UP_H

#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Geometry>

namespace igl
{















  template <typename Scalardown_quat, typename Scalarquat>
  IGL_INLINE void two_axis_valuator_fixed_up(
    const int w,
    const int h,
    const double speed,
    const Eigen::Quaternion<Scalardown_quat> & down_quat,
    const int down_x,
    const int down_y,
    const int mouse_x,
    const int mouse_y,
    Eigen::Quaternion<Scalarquat> & quat);
}

#ifndef IGL_STATIC_LIBRARY
#  include "two_axis_valuator_fixed_up.cpp"
#endif

#endif
