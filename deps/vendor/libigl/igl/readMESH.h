// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_READMESH_H
#define IGL_READMESH_H
#include "igl_inline.h"

#include <Eigen/Core>
#include <string>
#include <vector>
#include <cstdio>

namespace igl
{











  template <typename DerivedV, typename DerivedF, typename DerivedT>
  IGL_INLINE bool readMESH(
    const std::string mesh_file_name,
    Eigen::PlainObjectBase<DerivedV>& V,
    Eigen::PlainObjectBase<DerivedT>& T,
    Eigen::PlainObjectBase<DerivedF>& F);

  template <typename Scalar, typename Index>
  IGL_INLINE bool readMESH(
    const std::string mesh_file_name,
    std::vector<std::vector<Scalar > > & V,
    std::vector<std::vector<Index > > & T,
    std::vector<std::vector<Index > > & F);


  template <typename DerivedV, typename DerivedF, typename DerivedT>
  IGL_INLINE bool readMESH(
    FILE * mesh_file,
    Eigen::PlainObjectBase<DerivedV>& V,
    Eigen::PlainObjectBase<DerivedT>& T,
    Eigen::PlainObjectBase<DerivedF>& F);

  template <typename Scalar, typename Index>
  IGL_INLINE bool readMESH(
    FILE * mesh_file,
    std::vector<std::vector<Scalar > > & V,
    std::vector<std::vector<Index > > & T,
    std::vector<std::vector<Index > > & F);
}

#ifndef IGL_STATIC_LIBRARY
#  include "readMESH.cpp"
#endif

#endif
