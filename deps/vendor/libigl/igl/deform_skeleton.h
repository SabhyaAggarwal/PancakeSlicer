// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_DEFORM_SKELETON_H
#define IGL_DEFORM_SKELETON_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <Eigen/StdVector>
#include <vector>
namespace igl
{








  IGL_INLINE void deform_skeleton(
    const Eigen::MatrixXd & C,
    const Eigen::MatrixXi & BE,
    const std::vector<
      Eigen::Affine3d,Eigen::aligned_allocator<Eigen::Affine3d> > & vA,
    Eigen::MatrixXd & CT,
    Eigen::MatrixXi & BET);



  IGL_INLINE void deform_skeleton(
    const Eigen::MatrixXd & C,
    const Eigen::MatrixXi & BE,
    const Eigen::MatrixXd & T,
    Eigen::MatrixXd & CT,
    Eigen::MatrixXi & BET);
}

#ifndef IGL_STATIC_LIBRARY
#  include "deform_skeleton.cpp"
#endif
#endif
