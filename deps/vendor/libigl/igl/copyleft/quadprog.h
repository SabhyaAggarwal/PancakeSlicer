// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_QUADPROG_H
#define IGL_COPYLEFT_QUADPROG_H

#include "../igl_inline.h"
#include <Eigen/Dense>
namespace igl
{
  namespace copyleft
  {














    IGL_INLINE bool quadprog(
      const Eigen::MatrixXd & G,
      const Eigen::VectorXd & g0,
      const Eigen::MatrixXd & CE,
      const Eigen::VectorXd & ce0,
      const Eigen::MatrixXd & CI,
      const Eigen::VectorXd & ci0,
      Eigen::VectorXd& x);
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "quadprog.cpp"
#endif

#endif
