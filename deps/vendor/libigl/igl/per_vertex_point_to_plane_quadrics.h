// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_PER_VERTEX_POINT_TO_PLANE_QUADRICS_H
#define IGL_PER_VERTEX_POINT_TO_PLANE_QUADRICS_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>
#include <tuple>
namespace igl
{























  IGL_INLINE void per_vertex_point_to_plane_quadrics(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi & F,
    const Eigen::MatrixXi & EMAP,
    const Eigen::MatrixXi & EF,
    const Eigen::MatrixXi & EI,
    std::vector<
      std::tuple<
        Eigen::MatrixXd,
        Eigen::RowVectorXd,
        double> > & quadrics);
}
#ifndef IGL_STATIC_LIBRARY
#  include "per_vertex_point_to_plane_quadrics.cpp"
#endif
#endif
