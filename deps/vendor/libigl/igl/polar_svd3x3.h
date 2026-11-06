// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_POLAR_SVD3X3_H
#define IGL_POLAR_SVD3X3_H
#include <Eigen/Core>
#include "igl_inline.h"
namespace igl
{












  template<typename Mat>
  IGL_INLINE void polar_svd3x3(const Mat& A, Mat& R);
  #ifdef __SSE__

  template<typename T>
  IGL_INLINE void polar_svd3x3_sse(const Eigen::Matrix<T, 3*4, 3>& A, Eigen::Matrix<T, 3*4, 3> &R);
  #endif
  #ifdef __AVX__

  template<typename T>
  IGL_INLINE void polar_svd3x3_avx(const Eigen::Matrix<T, 3*8, 3>& A, Eigen::Matrix<T, 3*8, 3> &R);
  #endif
}
#ifndef IGL_STATIC_LIBRARY
#  include "polar_svd3x3.cpp"
#endif
#endif
