// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_EMBREE_BONE_HEAT_H
#define IGL_EMBREE_BONE_HEAT_H
#include "../igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  namespace embree
  {













    IGL_INLINE bool bone_heat(
      const Eigen::MatrixXd & V,
      const Eigen::MatrixXi & F,
      const Eigen::MatrixXd & C,
      const Eigen::VectorXi & P,
      const Eigen::MatrixXi & BE,
      const Eigen::MatrixXi & CE,
      Eigen::MatrixXd & W);
  }
};

#ifndef IGL_STATIC_LIBRARY
#  include "bone_heat.cpp"
#endif

#endif
