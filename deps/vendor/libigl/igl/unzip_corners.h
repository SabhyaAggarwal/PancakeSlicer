// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_UNZIP_CORNERS_H
#define IGL_UNZIP_CORNERS_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>
#include <functional>

namespace igl
{



























  template < typename DerivedA, typename DerivedU, typename DerivedG, typename DerivedJ>
  IGL_INLINE void unzip_corners(
    const std::vector<std::reference_wrapper<DerivedA> > & A,
    Eigen::PlainObjectBase<DerivedU> & U,
    Eigen::PlainObjectBase<DerivedG> & G,
    Eigen::PlainObjectBase<DerivedJ> & J);
}

#ifndef IGL_STATIC_LIBRARY
#  include "unzip_corners.cpp"
#endif
#endif
