// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Hanxiao Shen <hanxiao@cs.nyu.edu>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_PREDICATES_SEGMENT_SEGMENT_INTERSECT_H
#define IGL_PREDICATES_SEGMENT_SEGMENT_INTERSECT_H

#include "../igl_inline.h"
#include "predicates.h"
#include <Eigen/Core>
namespace igl
{
  namespace predicates
  {








    template <typename DerivedP>
    IGL_INLINE bool segment_segment_intersect(
      const Eigen::MatrixBase<DerivedP>& A,
      const Eigen::MatrixBase<DerivedP>& B,
      const Eigen::MatrixBase<DerivedP>& C,
      const Eigen::MatrixBase<DerivedP>& D
    );
  }
}
#ifndef IGL_STATIC_LIBRARY
#   include "segment_segment_intersect.cpp"
#endif
#endif
