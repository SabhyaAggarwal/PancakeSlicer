// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_WRITEMESH_H
#define IGL_WRITEMESH_H
#include "igl_inline.h"

#include <string>
#include <vector>
#include <Eigen/Core>

namespace igl
{











  template <typename Scalar, typename Index>
  IGL_INLINE bool writeMESH(
    const std::string mesh_file_name,
    const std::vector<std::vector<Scalar > > & V,
    const std::vector<std::vector<Index > > & T,
    const std::vector<std::vector<Index > > & F);










  template <typename DerivedV, typename DerivedT, typename DerivedF>
  IGL_INLINE bool writeMESH(
    const std::string str,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedT> & T,
    const Eigen::MatrixBase<DerivedF> & F);
}

#ifndef IGL_STATIC_LIBRARY
#  include "writeMESH.cpp"
#endif

#endif
