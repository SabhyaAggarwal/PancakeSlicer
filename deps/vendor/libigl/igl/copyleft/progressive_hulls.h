// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_PROGRESSIVE_HULLS_H
#define IGL_COPYLEFT_PROGRESSIVE_HULLS_H
#include "../igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  namespace copyleft
  {













    IGL_INLINE bool progressive_hulls(
      const Eigen::MatrixXd & V,
      const Eigen::MatrixXi & F,
      const size_t max_m,
      Eigen::MatrixXd & U,
      Eigen::MatrixXi & G,
      Eigen::VectorXi & J);
  }
}
#ifndef IGL_STATIC_LIBRARY
#  include "progressive_hulls.cpp"
#endif
#endif
