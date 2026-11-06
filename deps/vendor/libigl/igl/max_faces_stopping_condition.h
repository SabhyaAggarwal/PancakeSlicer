// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_MAX_FACES_STOPPING_CONDITION_H
#define IGL_MAX_FACES_STOPPING_CONDITION_H
#include "igl_inline.h"
#include "decimate_callback_types.h"
#include <Eigen/Core>
#include <vector>
#include <set>
#include <functional>
namespace igl
{












  IGL_INLINE void max_faces_stopping_condition(
    int & m,
    const int orig_m,
    const int max_m,
    decimate_stopping_condition_callback & stopping_condition);

  IGL_INLINE decimate_stopping_condition_callback
    max_faces_stopping_condition(
      int & m,
      const int orign_m,
      const int max_m);
}

#ifndef IGL_STATIC_LIBRARY
#  include "max_faces_stopping_condition.cpp"
#endif
#endif
