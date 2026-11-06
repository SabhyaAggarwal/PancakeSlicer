// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COLUMNIZE_H
#define IGL_COLUMNIZE_H
#include "igl_inline.h"

#include <Eigen/Core>
namespace igl
{













  template <typename DerivedA, typename DerivedB>
  IGL_INLINE void columnize(
    const Eigen::MatrixBase<DerivedA> & A,
    const int k,
    const int dim,
    Eigen::PlainObjectBase<DerivedB> & B);
}
#ifndef IGL_STATIC_LIBRARY
#  include "columnize.cpp"
#endif
#endif
