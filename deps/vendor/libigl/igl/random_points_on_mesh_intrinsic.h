// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_RANDOM_POINTS_ON_MESH_INTRINSIC_H
#define IGL_RANDOM_POINTS_ON_MESH_INTRINSIC_H

#include "igl_inline.h"
#include "generate_default_urbg.h"
#include <Eigen/Core>
#include <Eigen/Sparse>

namespace igl
{








  template <
    typename DeriveddblA,
    typename DerivedB,
    typename DerivedFI,
    typename URBG = DEFAULT_URBG>
  IGL_INLINE void random_points_on_mesh_intrinsic(
    const int n,
    const Eigen::MatrixBase<DeriveddblA > & dblA,
    Eigen::PlainObjectBase<DerivedB > & B,
    Eigen::PlainObjectBase<DerivedFI > & FI,
    URBG && urbg = igl::generate_default_urbg());







  template <
    typename DeriveddblA,
    typename DerivedF,
    typename ScalarB,
    typename DerivedFI,
    typename URBG>
  IGL_INLINE void random_points_on_mesh_intrinsic(
    const int n,
    const Eigen::MatrixBase<DeriveddblA > & dblA,
    const int num_vertices,
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::SparseMatrix<ScalarB > & B,
    Eigen::PlainObjectBase<DerivedFI > & FI,
    URBG && urbg);
}

#ifndef IGL_STATIC_LIBRARY
#  include "random_points_on_mesh_intrinsic.cpp"
#endif

#endif
