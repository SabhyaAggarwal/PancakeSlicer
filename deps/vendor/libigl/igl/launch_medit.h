// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_LAUNCH_MEDIT_H
#define IGL_LAUNCH_MEDIT_H
#include "igl_inline.h"

#include <Eigen/Core>

namespace igl
{












  template <typename DerivedV, typename DerivedT, typename DerivedF>
  IGL_INLINE int launch_medit(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedT> & T,
    const Eigen::MatrixBase<DerivedF> & F,
    const bool wait);
}

#ifndef IGL_STATIC_LIBRARY
#  include "launch_medit.cpp"
#endif

#endif
