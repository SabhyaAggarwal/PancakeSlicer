// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_ADJACENCY_LIST_H
#define IGL_ADJACENCY_LIST_H
#include "igl_inline.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <vector>
namespace igl
{



















  template <typename Index, typename IndexVector>
  IGL_INLINE void adjacency_list(
    const Eigen::MatrixBase<Index>  & F,
    std::vector<std::vector<IndexVector> >& A,
    bool sorted = false);





  template <typename Index>
  IGL_INLINE void adjacency_list(
    const std::vector<std::vector<Index> > & F,
    std::vector<std::vector<Index> >& A);

}

#ifndef IGL_STATIC_LIBRARY
#  include "adjacency_list.cpp"
#endif

#endif
