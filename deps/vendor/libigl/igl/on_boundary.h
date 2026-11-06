// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_ON_BOUNDARY_H
#define IGL_ON_BOUNDARY_H
#include "igl_inline.h"
#include <Eigen/Dense>

#include <vector>

namespace igl
{










  template <typename IntegerT>
  IGL_INLINE void on_boundary(
    const std::vector<std::vector<IntegerT> > & T,
    std::vector<bool> & I,
    std::vector<std::vector<bool> > & C);

  template <typename DerivedT, typename DerivedI, typename DerivedC>
  IGL_INLINE void on_boundary(
    const Eigen::MatrixBase<DerivedT>& T,
    Eigen::PlainObjectBase<DerivedI>& I,
    Eigen::PlainObjectBase<DerivedC>& C);
}

#ifndef IGL_STATIC_LIBRARY
#  include "on_boundary.cpp"
#endif

#endif
