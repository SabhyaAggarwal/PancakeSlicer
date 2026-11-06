// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_UNIFORMLY_SAMPLE_TWO_MANIFOLD_H
#define IGL_UNIFORMLY_SAMPLE_TWO_MANIFOLD_H
#include "igl_inline.h"
#include <Eigen/Dense>
namespace igl
{



















  IGL_INLINE void uniformly_sample_two_manifold(
    const Eigen::MatrixXd & W,
    const Eigen::MatrixXi & F,
    const int k,
    const double push,
    Eigen::MatrixXd & WS);



  IGL_INLINE void uniformly_sample_two_manifold_at_vertices(
    const Eigen::MatrixXd & W,
    const int k,
    const double push,
    Eigen::VectorXi & S);
}
#ifndef IGL_STATIC_LIBRARY
#  include "uniformly_sample_two_manifold.cpp"
#endif
#endif
