// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_TRANSPOSE_BLOCKS_H
#define IGL_TRANSPOSE_BLOCKS_H
#include "igl_inline.h"

#include <Eigen/Core>

namespace igl
{
































  template <typename T>
  IGL_INLINE void transpose_blocks(
    const Eigen::Matrix<T,Eigen::Dynamic,Eigen::Dynamic> & A,
    const size_t k,
    const size_t dim,
    Eigen::Matrix<T,Eigen::Dynamic,Eigen::Dynamic> & B);
}

#ifndef IGL_STATIC_LIBRARY
#  include "transpose_blocks.cpp"
#endif

#endif
