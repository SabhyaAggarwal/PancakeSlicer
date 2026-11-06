// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_INTERNAL_ANGLES_INTRINSIC_H
#define IGL_INTERNAL_ANGLES_INTRINSIC_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{






  template <typename DerivedL, typename DerivedK>
  IGL_INLINE void internal_angles_intrinsic(
    const Eigen::MatrixBase<DerivedL>& L_sq,
    Eigen::PlainObjectBase<DerivedK> & K);
}

#ifndef IGL_STATIC_LIBRARY
#  include "internal_angles_intrinsic.cpp"
#endif

#endif
