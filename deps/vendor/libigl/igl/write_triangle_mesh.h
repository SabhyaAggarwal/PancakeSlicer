// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_WRITE_TRIANGLE_MESH_H
#define IGL_WRITE_TRIANGLE_MESH_H
#include "igl_inline.h"
#include "FileEncoding.h"

#include <Eigen/Core>
#include <string>

namespace igl
{











  template <typename DerivedV, typename DerivedF>
  IGL_INLINE bool write_triangle_mesh(
    const std::string str,
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    FileEncoding encoding = FileEncoding::Ascii);
}

#ifndef IGL_STATIC_LIBRARY
#  include "write_triangle_mesh.cpp"
#endif

#endif
