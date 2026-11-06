// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_PROGRESSIVE_HULLS_COST_AND_PLACEMENT_H
#define IGL_COPYLEFT_PROGRESSIVE_HULLS_COST_AND_PLACEMENT_H
#include <Eigen/Core>
#include "../igl_inline.h"
namespace igl
{
  namespace copyleft
  {






















    IGL_INLINE void progressive_hulls_cost_and_placement(
      const int e,
      const Eigen::MatrixXd & V,
      const Eigen::MatrixXi & F,
      const Eigen::MatrixXi & E,
      const Eigen::VectorXi & EMAP,
      const Eigen::MatrixXi & EF,
      const Eigen::MatrixXi & EI,
      double & cost,
      Eigen::RowVectorXd & p);
  }
}
#ifndef IGL_STATIC_LIBRARY
#  include "progressive_hulls_cost_and_placement.cpp"
#endif
#endif
