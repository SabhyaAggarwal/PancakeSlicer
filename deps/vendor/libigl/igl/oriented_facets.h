// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2017 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_ORIENTED_FACETS_H
#define IGL_ORIENTED_FACETS_H
#include "igl_inline.h"
#include <Eigen/Dense>
namespace igl
{













  template <typename DerivedF, typename DerivedE>
  IGL_INLINE void oriented_facets(
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedE> & E);
}

#ifndef IGL_STATIC_LIBRARY
#  include "oriented_facets.cpp"
#endif

#endif
