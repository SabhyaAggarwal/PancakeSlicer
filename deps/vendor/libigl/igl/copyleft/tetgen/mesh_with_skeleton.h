// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_TETGEN_MESH_WITH_SKELETON_H
#define IGL_COPYLEFT_TETGEN_MESH_WITH_SKELETON_H
#include "../../igl_inline.h"
#include <Eigen/Dense>
#include <string>

namespace igl
{
  namespace copyleft
  {
    namespace tetgen
    {

















      IGL_INLINE bool mesh_with_skeleton(
        const Eigen::MatrixXd & V,
        const Eigen::MatrixXi & F,
        const Eigen::MatrixXd & C,
        const Eigen::VectorXi &  ,
        const Eigen::MatrixXi & BE,
        const Eigen::MatrixXi & CE,
        const int samples_per_bone,
        const std::string & tetgen_flags,
        Eigen::MatrixXd & VV,
        Eigen::MatrixXi & TT,
        Eigen::MatrixXi & FF);

      IGL_INLINE bool mesh_with_skeleton(
        const Eigen::MatrixXd & V,
        const Eigen::MatrixXi & F,
        const Eigen::MatrixXd & C,
        const Eigen::VectorXi &  ,
        const Eigen::MatrixXi & BE,
        const Eigen::MatrixXi & CE,
        const int samples_per_bone,
        Eigen::MatrixXd & VV,
        Eigen::MatrixXi & TT,
        Eigen::MatrixXi & FF);
    }
  }
}


#ifndef IGL_STATIC_LIBRARY
#  include "mesh_with_skeleton.cpp"
#endif

#endif
