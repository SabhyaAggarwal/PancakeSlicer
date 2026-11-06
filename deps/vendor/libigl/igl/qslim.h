// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_QSLIM_H
#define IGL_QSLIM_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{


















  IGL_INLINE bool qslim(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi & F,
    const int max_m,
    const bool block_intersections,
    Eigen::MatrixXd & U,
    Eigen::MatrixXi & G,
    Eigen::VectorXi & J,
    Eigen::VectorXi & I);
}
#ifndef IGL_STATIC_LIBRARY
#  include "qslim.cpp"
#endif
#endif
