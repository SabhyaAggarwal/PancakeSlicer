// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_DECIMATE_H
#define IGL_DECIMATE_H
#include "igl_inline.h"
#include "decimate_callback_types.h"
#include "COLLAPSE_EDGE_NULL.h"
#include <Eigen/Core>






























































namespace igl
{
















  IGL_INLINE bool decimate(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi & F,
    const int max_m,
    const bool block_intersections,
    Eigen::MatrixXd & U,
    Eigen::MatrixXi & G,
    Eigen::VectorXi & J,
    Eigen::VectorXi & I);



































  IGL_INLINE bool decimate(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi & F,
    const decimate_cost_and_placement_callback & cost_and_placement,
    const decimate_stopping_condition_callback & stopping_condition,
    const decimate_pre_collapse_callback       & pre_collapse,
    const decimate_post_collapse_callback      & post_collapse,
    Eigen::MatrixXd & U,
    Eigen::MatrixXi & G,
    Eigen::VectorXi & J,
    Eigen::VectorXi & I);
}

#ifndef IGL_STATIC_LIBRARY
#  include "decimate.cpp"
#endif
#endif
