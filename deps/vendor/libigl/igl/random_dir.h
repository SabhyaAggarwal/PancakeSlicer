// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_RANDOM_DIR_H
#define IGL_RANDOM_DIR_H
#include "igl_inline.h"

#include <Eigen/Core>

namespace igl
{


  IGL_INLINE Eigen::Vector3d random_dir();





  IGL_INLINE Eigen::MatrixXd random_dir_stratified(const int n);
}

#ifndef IGL_STATIC_LIBRARY
#  include "random_dir.cpp"
#endif

#endif
