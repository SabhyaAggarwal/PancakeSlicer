// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Qingan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_DELAUNAY_TRIANGULATION_H
#define IGL_DELAUNAY_TRIANGULATION_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{














  template<
    typename DerivedV,
    typename Orient2D,
    typename InCircle,
    typename DerivedF
    >
  IGL_INLINE void delaunay_triangulation(
      const Eigen::MatrixBase<DerivedV>& V,
      Orient2D orient2D,
      InCircle incircle,
      Eigen::PlainObjectBase<DerivedF>& F);
}

#ifndef IGL_STATIC_LIBRARY
#  include "delaunay_triangulation.cpp"
#endif
#endif
