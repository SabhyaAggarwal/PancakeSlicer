// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_LIST_TO_MATRIX_H
#define IGL_LIST_TO_MATRIX_H
#include "igl_inline.h"
#include <vector>
#include <array>
#include <Eigen/Core>

namespace igl
{









  template <typename T, typename Derived>
  IGL_INLINE bool list_to_matrix(
    const std::vector<std::vector<T > > & V,
    Eigen::PlainObjectBase<Derived>& M);

  template <typename T, size_t N, typename Derived>
  IGL_INLINE bool list_to_matrix(
    const std::vector<std::array<T, N> > & V,
    Eigen::PlainObjectBase<Derived>& M);


  template <typename T, typename Derived>
  IGL_INLINE bool list_to_matrix(const std::vector<T > & V,Eigen::PlainObjectBase<Derived>& M);







  template <typename T, typename Derived>
  IGL_INLINE bool list_to_matrix(
    const std::vector<std::vector<T > > & V,
    const int n,
    const T & padding,
    Eigen::PlainObjectBase<Derived>& M);
}

#ifndef IGL_STATIC_LIBRARY
#  include "list_to_matrix.cpp"
#endif

#endif
