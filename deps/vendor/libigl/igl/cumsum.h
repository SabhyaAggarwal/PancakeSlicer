// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_CUMSUM_H
#define IGL_CUMSUM_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{








  template <typename DerivedX, typename DerivedY>
  IGL_INLINE void cumsum(
    const Eigen::MatrixBase<DerivedX > & X,
    const int dim,
    Eigen::PlainObjectBase<DerivedY > & Y);











  template <typename DerivedX, typename DerivedY>
  IGL_INLINE void cumsum(
    const Eigen::MatrixBase<DerivedX > & X,
    const int dim,
    const bool zero_prefix,
    Eigen::PlainObjectBase<DerivedY > & Y);




}

#ifndef IGL_STATIC_LIBRARY
#  include "cumsum.cpp"
#endif

#endif
