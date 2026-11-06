// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Hanxiao Shen <hanxiao@cs.nyu.edu>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_PREDICATES_POINT_INSIDE_CONVEX_POLYGON_H
#define IGL_PREDICATES_POINT_INSIDE_CONVEX_POLYGON_H



#include "../igl_inline.h"
#include <Eigen/Core>
#include "predicates.h"

namespace igl
{
  namespace predicates
  {




    template <typename DerivedP, typename DerivedQ>
    IGL_INLINE bool point_inside_convex_polygon(
        const Eigen::MatrixBase<DerivedP>& P,
        const Eigen::MatrixBase<DerivedQ>& q
    );
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "point_inside_convex_polygon.cpp"
#endif

#endif
