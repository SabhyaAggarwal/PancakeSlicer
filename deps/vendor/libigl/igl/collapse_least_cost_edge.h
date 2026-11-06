// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COLLAPSE_LEAST_COST_EDGE_H
#define IGL_COLLAPSE_LEAST_COST_EDGE_H
#include "igl_inline.h"
#include "min_heap.h"
#include "decimate_callback_types.h"
#include "COLLAPSE_EDGE_NULL.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{








































  IGL_INLINE bool collapse_least_cost_edge(
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
    int & f2);
}

#ifndef IGL_STATIC_LIBRARY
#  include "collapse_least_cost_edge.cpp"
#endif
#endif
