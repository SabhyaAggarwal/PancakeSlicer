// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_READ_TRIANGLE_MESH_H
#define IGL_READ_TRIANGLE_MESH_H
#include "igl_inline.h"

#include <Eigen/Core>
#include <string>
#include <cstdio>
#include <vector>




namespace igl
{










  template <typename DerivedV, typename DerivedF>
  IGL_INLINE bool read_triangle_mesh(
    const std::string str,
    Eigen::PlainObjectBase<DerivedV>& V,
    Eigen::PlainObjectBase<DerivedF>& F);


  template <typename Scalar, typename Index>
  IGL_INLINE bool read_triangle_mesh(
    const std::string str,
    std::vector<std::vector<Scalar> > & V,
    std::vector<std::vector<Index> > & F);





  template <typename DerivedV, typename DerivedF>
  IGL_INLINE bool read_triangle_mesh(
    const std::string str,
    Eigen::PlainObjectBase<DerivedV>& V,
    Eigen::PlainObjectBase<DerivedF>& F,
    std::string & dir,
    std::string & base,
    std::string & ext,
    std::string & name);



  template <typename DerivedV, typename DerivedF>
  IGL_INLINE bool read_triangle_mesh(
    const std::string & ext,
    FILE * fp,
    Eigen::PlainObjectBase<DerivedV>& V,
    Eigen::PlainObjectBase<DerivedF>& F);
}

#ifndef IGL_STATIC_LIBRARY
#  include "read_triangle_mesh.cpp"
#endif

#endif
