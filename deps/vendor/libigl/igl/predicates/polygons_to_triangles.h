// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2020 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_POLYGONS_TO_TRIANGLES_H
#define IGL_POLYGONS_TO_TRIANGLES_H

#include "../igl_inline.h"
#include <Eigen/Core>
#include <vector>

namespace igl
{
  namespace predicates
  {












    template <
      typename DerivedV,
      typename DerivedI,
      typename DerivedC,
      typename DerivedF,
      typename DerivedJ>
    IGL_INLINE void polygons_to_triangles(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedI> & I,
      const Eigen::MatrixBase<DerivedC> & C,
      Eigen::PlainObjectBase<DerivedF> & F,
      Eigen::PlainObjectBase<DerivedJ> & J);
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "polygons_to_triangles.cpp"
#endif

#endif
