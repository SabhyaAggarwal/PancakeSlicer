// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_ARAP_LINEAR_BLOCK_H
#define IGL_ARAP_LINEAR_BLOCK_H
#include "igl_inline.h"

#include <Eigen/Sparse>
#include "ARAPEnergyType.h"

namespace igl
{





























  template <typename MatV, typename MatF, typename MatK>
  IGL_INLINE void arap_linear_block(
    const MatV & V,
    const MatF & F,
    const int d,
    const igl::ARAPEnergyType energy,
    MatK & Kd);












  template <typename MatV, typename MatF, typename MatK>
  IGL_INLINE void arap_linear_block_spokes(
    const MatV & V,
    const MatF & F,
    const int d,
    MatK & Kd);












  template <typename MatV, typename MatF, typename MatK>
  IGL_INLINE void arap_linear_block_spokes_and_rims(
    const MatV & V,
    const MatF & F,
    const int d,
    MatK & Kd);












  template <typename MatV, typename MatF, typename MatK>
  IGL_INLINE void arap_linear_block_elements(
    const MatV & V,
    const MatF & F,
    const int d,
    MatK & Kd);
}

#ifndef IGL_STATIC_LIBRARY
#  include "arap_linear_block.cpp"
#endif

#endif
