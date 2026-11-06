// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_INFINITE_COST_STOPPING_CONDITION_H
#define IGL_INFINITE_COST_STOPPING_CONDITION_H
#include "igl_inline.h"
#include "decimate_callback_types.h"
#include <Eigen/Core>
#include <vector>
#include <set>
#include <functional>
namespace igl
{






  IGL_INLINE void infinite_cost_stopping_condition(
    const decimate_cost_and_placement_callback & cost_and_placement,
    decimate_stopping_condition_callback & stopping_condition);

  IGL_INLINE decimate_stopping_condition_callback
    infinite_cost_stopping_condition(
    const decimate_cost_and_placement_callback & cost_and_placement);
}

#ifndef IGL_STATIC_LIBRARY
#  include "infinite_cost_stopping_condition.cpp"
#endif
#endif
