// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_JET_H
#define IGL_JET_H
#include "igl_inline.h"

#  include <Eigen/Dense>

namespace igl
{













  template <typename T>
  IGL_INLINE void jet(const T f, T * rgb);

  template <typename T>
  IGL_INLINE void jet(const T f, T & r, T & g, T & b);




  template <typename DerivedZ, typename DerivedC>
  IGL_INLINE void jet(
    const Eigen::MatrixBase<DerivedZ> & Z,
    const bool normalize,
    Eigen::PlainObjectBase<DerivedC> & C);



  template <typename DerivedZ, typename DerivedC>
  IGL_INLINE void jet(
    const Eigen::MatrixBase<DerivedZ> & Z,
    const double min_Z,
    const double max_Z,
    Eigen::PlainObjectBase<DerivedC> & C);
};

#ifndef IGL_STATIC_LIBRARY
#  include "jet.cpp"
#endif

#endif
