// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COLLAPSE_SMALL_TRIANGLES_H
#define IGL_COLLAPSE_SMALL_TRIANGLES_H
#include <Eigen/Dense>
namespace igl
{














  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedFF>
  void collapse_small_triangles(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const double eps,
    Eigen::PlainObjectBase<DerivedFF> & FF);
}

#ifndef IGL_STATIC_LIBRARY
#  include "collapse_small_triangles.cpp"
#endif

#endif
