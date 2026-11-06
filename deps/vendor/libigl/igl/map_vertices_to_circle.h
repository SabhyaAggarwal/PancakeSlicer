// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Stefan Brugger <stefanbrugger@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_MAP_VERTICES_TO_CIRCLE_H
#define IGL_MAP_VERTICES_TO_CIRCLE_H
#include "igl_inline.h"
#include "PI.h"

#include <Eigen/Dense>
#include <vector>

namespace igl
{







  IGL_INLINE void map_vertices_to_circle(
    const Eigen::MatrixXd& V,
    const Eigen::VectorXi& bnd,
    Eigen::MatrixXd& UV);
}

#ifndef IGL_STATIC_LIBRARY
#  include "map_vertices_to_circle.cpp"
#endif

#endif
