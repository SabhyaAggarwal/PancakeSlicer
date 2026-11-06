// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_PLANARIZE_QUAD_MESH_H
#define IGL_PLANARIZE_QUAD_MESH_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{

















  template <typename DerivedV, typename DerivedF>
  IGL_INLINE void planarize_quad_mesh(
    const Eigen::MatrixBase<DerivedV> &Vin,
    const Eigen::MatrixBase<DerivedF> &F,
    const int maxIter,
    const double &threshold,
    Eigen::PlainObjectBase<DerivedV> &Vout);
}
#ifndef IGL_STATIC_LIBRARY
#  include "planarize_quad_mesh.cpp"
#endif

#endif
