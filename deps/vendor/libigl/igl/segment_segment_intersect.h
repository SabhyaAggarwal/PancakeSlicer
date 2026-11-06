// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Francisca Gil Ureta <gilureta@cs.nyu.edu>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SEGMENT_SEGMENT_INTERSECT_H
#define IGL_SEGMENT_SEGMENT_INTERSECT_H

#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{













  template<typename DerivedSource, typename DerivedDir>
  IGL_INLINE bool segment_segment_intersect(
    const Eigen::MatrixBase <DerivedSource> &p,
    const Eigen::MatrixBase <DerivedDir> &r,
    const Eigen::MatrixBase <DerivedSource> &q,
    const Eigen::MatrixBase <DerivedDir> &s,
    double &t,
    double &u,
    double eps = 1e-6);
}

#ifndef IGL_STATIC_LIBRARY
#   include "segment_segment_intersect.cpp"
#endif
#endif
