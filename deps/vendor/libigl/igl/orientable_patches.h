// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_ORIENTABLE_PATCHES_H
#define IGL_ORIENTABLE_PATCHES_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Sparse>
namespace igl
{









  template <typename DerivedF, typename DerivedC, typename AScalar>
  IGL_INLINE void orientable_patches(
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedC> & C,
    Eigen::SparseMatrix<AScalar> & A);

  template <typename DerivedF, typename DerivedC>
  IGL_INLINE void orientable_patches(
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedC> & C);
};
#ifndef IGL_STATIC_LIBRARY
#  include "orientable_patches.cpp"
#endif
#endif
