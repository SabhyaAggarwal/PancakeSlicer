// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SWEPT_VOLUME_SIGNED_DISTANCE_H
#define IGL_SWEPT_VOLUME_SIGNED_DISTANCE_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <functional>
namespace igl
{



















  IGL_INLINE void swept_volume_signed_distance(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi & F,
    const std::function<Eigen::Affine3d(const double t)> & transform,
    const size_t & steps,
    const Eigen::MatrixXd & GV,
    const Eigen::RowVector3i & res,
    const double h,
    const double isolevel,
    const Eigen::VectorXd & S0,
    Eigen::VectorXd & S);

  IGL_INLINE void swept_volume_signed_distance(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi & F,
    const std::function<Eigen::Affine3d(const double t)> & transform,
    const size_t & steps,
    const Eigen::MatrixXd & GV,
    const Eigen::RowVector3i & res,
    const double h,
    const double isolevel,
    Eigen::VectorXd & S);
  }

#ifndef IGL_STATIC_LIBRARY
#  include "swept_volume_signed_distance.cpp"
#endif

#endif
