// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SVD3X3_AVX_H
#define IGL_SVD3X3_AVX_H
#include "igl_inline.h"
#include <Eigen/Dense>

namespace igl
{












  template<typename T>
  IGL_INLINE void svd3x3_avx(
    const Eigen::Matrix<T, 3*8, 3>& A,
    Eigen::Matrix<T, 3*8, 3> &U,
    Eigen::Matrix<T, 3*8, 1> &S,
    Eigen::Matrix<T, 3*8, 3>&V);
}
#ifndef IGL_STATIC_LIBRARY
#  include "svd3x3_avx.cpp"
#endif
#endif
