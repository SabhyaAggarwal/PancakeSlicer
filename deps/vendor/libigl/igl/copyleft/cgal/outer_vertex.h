// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Qingan Zhou <qnzhou@gmail.com>
// Copyright (C) 2021 Alec Jacobson <jacobson@cs.toronto.edu>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_CGAL_OUTER_VERTEX_H
#define IGL_COPYLEFT_CGAL_OUTER_VERTEX_H
#include "../../igl_inline.h"
#include <Eigen/Core>
namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {












      template <
          typename DerivedV,
          typename DerivedF,
          typename DerivedI,
          typename IndexType,
          typename DerivedA
          >
      IGL_INLINE void outer_vertex(
              const Eigen::MatrixBase<DerivedV> & V,
              const Eigen::MatrixBase<DerivedF> & F,
              const Eigen::MatrixBase<DerivedI> & I,
              IndexType & v_index,
              Eigen::PlainObjectBase<DerivedA> & A);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "outer_vertex.cpp"
#endif
#endif
