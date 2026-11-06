// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_MATLAB_FORMAT_H
#define IGL_MATLAB_FORMAT_H

#include "igl_inline.h"

#include <Eigen/Core>
#include <Eigen/Sparse>
#include <string>

namespace igl
{
























  template <typename DerivedM>
  IGL_INLINE const Eigen::WithFormat< DerivedM > matlab_format(
    const Eigen::DenseBase<DerivedM> & M,
    const std::string name = "");



  template <typename DerivedM>
  IGL_INLINE std::string matlab_format_index(
    const Eigen::MatrixBase<DerivedM> & M,
    const std::string name = "");

















  template <typename DerivedS>
  IGL_INLINE const std::string matlab_format(
    const Eigen::SparseMatrix<DerivedS> & S,
    const std::string name = "");


  IGL_INLINE const std::string matlab_format(
    const double v,
    const std::string name = "");

  IGL_INLINE const std::string matlab_format(
    const float v,
    const std::string name = "");













  IGL_INLINE Eigen::IOFormat matlab_format();
}

#ifndef IGL_STATIC_LIBRARY
#  include "matlab_format.cpp"
#endif

#endif
