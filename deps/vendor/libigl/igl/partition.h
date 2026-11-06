// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_PARTITION_H
#define IGL_PARTITION_H
#include "igl_inline.h"
#include <Eigen/Dense>

namespace igl
{










  IGL_INLINE void partition(
    const Eigen::MatrixXd & W,
    const int k,
    Eigen::Matrix<int,Eigen::Dynamic,1> & G,
    Eigen::Matrix<int,Eigen::Dynamic,1> & S,
    Eigen::Matrix<double,Eigen::Dynamic,1> & D);
}

#ifndef IGL_STATIC_LIBRARY
#include "partition.cpp"
#endif
#endif
