// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2021 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_MARCH_CUBE_H
#define IGL_MARCH_CUBE_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <unordered_map>
#include <cstdint>
namespace igl
{















  template <
    typename DerivedGV,
    typename Scalar,
    typename Index,
    typename DerivedV,
    typename DerivedF>
  IGL_INLINE void march_cube(
    const DerivedGV & GV,
    const Eigen::Matrix<Scalar,8,1> & cS,
    const Eigen::Matrix<Index,8,1> & cI,
    const Scalar & isovalue,
    Eigen::PlainObjectBase<DerivedV> &V,
    Index & n,
    Eigen::PlainObjectBase<DerivedF> &F,
    Index & m,
    std::unordered_map<std::int64_t,int> & E2V);
}

#ifndef IGL_STATIC_LIBRARY
#  include "march_cube.cpp"
#endif

#endif
