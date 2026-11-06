// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_CGAL_POINT_SOLID_SIGNED_SQUARED_DISTANCE_H
#define IGL_COPYLEFT_CGAL_POINT_SOLID_SIGNED_SQUARED_DISTANCE_H
#include "../../igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {









      template <
        typename DerivedQ,
        typename DerivedVB,
        typename DerivedFB,
        typename DerivedD>
      IGL_INLINE void point_solid_signed_squared_distance(
        const Eigen::MatrixBase<DerivedQ> & Q,
        const Eigen::MatrixBase<DerivedVB> & VB,
        const Eigen::MatrixBase<DerivedFB> & FB,
        Eigen::PlainObjectBase<DerivedD> & D);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "point_solid_signed_squared_distance.cpp"
#endif

#endif
