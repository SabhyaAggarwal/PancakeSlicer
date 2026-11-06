// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_NULL_H
#define IGL_NULL_H
#include "igl_inline.h"

#include <Eigen/Dense>

namespace igl
{







  template <typename DerivedA, typename DerivedN>
  IGL_INLINE void null(
    const Eigen::MatrixBase<DerivedA> & A,
    Eigen::PlainObjectBase<DerivedN> & N);
}

#ifndef IGL_STATIC_LIBRARY
#  include "null.cpp"
#endif

#endif
