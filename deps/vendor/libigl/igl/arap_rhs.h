// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_ARAP_RHS_H
#define IGL_ARAP_RHS_H
#include "igl_inline.h"
#include "ARAPEnergyType.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>

namespace igl
{













  template<typename DerivedV, typename DerivedF, typename DerivedK>
  IGL_INLINE void arap_rhs(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const int dim,
    const igl::ARAPEnergyType energy,
    Eigen::SparseCompressedBase<DerivedK>& K);
}
#ifndef IGL_STATIC_LIBRARY
#include "arap_rhs.cpp"
#endif
#endif
