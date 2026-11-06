// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SHORTEST_EDGE_AND_MIDPOINT_H
#define IGL_SHORTEST_EDGE_AND_MIDPOINT_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{














  IGL_INLINE void shortest_edge_and_midpoint(
    const int e,
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi &  ,
    const Eigen::MatrixXi & E,
    const Eigen::VectorXi &  ,
    const Eigen::MatrixXi &  ,
    const Eigen::MatrixXi &  ,
    double & cost,
    Eigen::RowVectorXd & p);
}

#ifndef IGL_STATIC_LIBRARY
#  include "shortest_edge_and_midpoint.cpp"
#endif
#endif
