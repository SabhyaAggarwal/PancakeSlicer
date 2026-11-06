// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_FIT_ROTATIONS_H
#define IGL_FIT_ROTATIONS_H
#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{








  template <typename DerivedS, typename DerivedD>
  IGL_INLINE void fit_rotations(
    const Eigen::MatrixBase<DerivedS> & S,
    const bool single_precision,
          Eigen::PlainObjectBase<DerivedD> & R);
#ifdef __SSE__



  IGL_INLINE void fit_rotations_SSE( const Eigen::MatrixXf & S, Eigen::MatrixXf & R);



  IGL_INLINE void fit_rotations_SSE( const Eigen::MatrixXd & S, Eigen::MatrixXd & R);
#endif
#ifdef __AVX__



  IGL_INLINE void fit_rotations_AVX( const Eigen::MatrixXf & S, Eigen::MatrixXf & R);
#endif










  template <typename DerivedS, typename DerivedD>
  IGL_INLINE void fit_rotations_planar(
    const Eigen::MatrixBase<DerivedS> & S,
          Eigen::PlainObjectBase<DerivedD> & R);
}

#ifndef IGL_STATIC_LIBRARY
#  include "fit_rotations.cpp"
#endif

#endif
