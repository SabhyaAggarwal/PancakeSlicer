// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SETDIFF_H
#define IGL_SETDIFF_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{






  template <
    typename DerivedA,
    typename DerivedB,
    typename DerivedC,
    typename DerivedIA>
  IGL_INLINE void setdiff(
    const Eigen::MatrixBase<DerivedA> & A,
    const Eigen::MatrixBase<DerivedB> & B,
    Eigen::PlainObjectBase<DerivedC> & C,
    Eigen::PlainObjectBase<DerivedIA> & IA);
}

#ifndef IGL_STATIC_LIBRARY
#  include "setdiff.cpp"
#endif
#endif
