// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_KKT_INVERSE_H
#define IGL_KKT_INVERSE_H
#include "igl_inline.h"

#include <Eigen/Dense>






namespace igl
{
































  template <typename T>
  IGL_INLINE void kkt_inverse(
    const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& A,
    const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& Aeq,
    const bool use_lu_decomposition,
    Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& S);
}

#ifndef IGL_STATIC_LIBRARY
#  include "kkt_inverse.cpp"
#endif

#endif
