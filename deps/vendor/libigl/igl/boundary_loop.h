// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Stefan Brugger <stefanbrugger@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_BOUNDARY_LOOP_H
#define IGL_BOUNDARY_LOOP_H
#include "igl_inline.h"

#include <Eigen/Dense>
#include <vector>

namespace igl
{






  template <typename DerivedF, typename Index>
  IGL_INLINE void boundary_loop(
    const Eigen::MatrixBase<DerivedF>& F,
    std::vector<std::vector<Index> >& L);









  template <typename DerivedF, typename Index>
  IGL_INLINE void boundary_loop(
    const Eigen::MatrixBase<DerivedF>& F,
    std::vector<Index>& L);







  template <typename DerivedF, typename DerivedL>
  IGL_INLINE void boundary_loop(
    const Eigen::MatrixBase<DerivedF>& F,
    Eigen::PlainObjectBase<DerivedL>& L);
}

#ifndef IGL_STATIC_LIBRARY
#  include "boundary_loop.cpp"
#endif
#endif
