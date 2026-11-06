// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COLON_H
#define IGL_COLON_H
#include "igl_inline.h"
#include <Eigen/Dense>
namespace igl
{



























  template <typename L,typename S,typename H,typename T>
  IGL_INLINE void colon(
    const L low,
    const S step,
    const H hi,
    Eigen::Matrix<T,Eigen::Dynamic,1> & I);
















  template <typename L,typename H,typename T>
  IGL_INLINE void colon(
    const L low,
    const H hi,
    Eigen::Matrix<T,Eigen::Dynamic,1> & I);




















  template <typename T,typename L,typename H>
  IGL_INLINE Eigen::Matrix<T,Eigen::Dynamic,1> colon(
    const L low,
    const H hi);
}

#ifndef IGL_STATIC_LIBRARY
#  include "colon.cpp"
#endif

#endif
