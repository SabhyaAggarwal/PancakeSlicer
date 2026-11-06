// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_HARWELL_BOEING_H
#define IGL_HARWELL_BOEING_H
#include "igl_inline.h"

#include <Eigen/Sparse>
#include <vector>


namespace igl
{














  template <typename Scalar, typename Index>
  IGL_INLINE void harwell_boeing(
    const Eigen::SparseMatrix<Scalar> & A,
    int & num_rows,
    std::vector<Scalar> & V,
    std::vector<Index> & R,
    std::vector<Index> & C);
}

#ifndef IGL_STATIC_LIBRARY
#  include "harwell_boeing.cpp"
#endif

#endif
