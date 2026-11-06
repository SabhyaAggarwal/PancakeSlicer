// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_CYLINDER_H
#define IGL_CYLINDER_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{







  template <typename DerivedV, typename DerivedF>
  IGL_INLINE void cylinder(
    const int axis_devisions,
    const int height_devisions,
    Eigen::PlainObjectBase<DerivedV> & V,
    Eigen::PlainObjectBase<DerivedF> & F);
}
#ifndef IGL_STATIC_LIBRARY
#  include "cylinder.cpp"
#endif
#endif
