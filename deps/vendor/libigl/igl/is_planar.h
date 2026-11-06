// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_IS_PLANAR_H
#define IGL_IS_PLANAR_H
#include "igl_inline.h"

#include <Eigen/Dense>

namespace igl
{







  IGL_INLINE bool is_planar(const Eigen::MatrixXd & V);
}

#ifndef IGL_STATIC_LIBRARY
#  include "is_planar.cpp"
#endif
#endif
