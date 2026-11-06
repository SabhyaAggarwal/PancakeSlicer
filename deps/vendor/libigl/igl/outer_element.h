// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Qingan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_OUTER_ELEMENT_H
#define IGL_OUTER_ELEMENT_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
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
















  template<
      typename DerivedV,
      typename DerivedF,
      typename DerivedI,
      typename IndexType,
      typename DerivedA
      >
  IGL_INLINE void outer_edge(
          const Eigen::MatrixBase<DerivedV> & V,
          const Eigen::MatrixBase<DerivedF> & F,
          const Eigen::MatrixBase<DerivedI> & I,
          IndexType & v1,
          IndexType & v2,
          Eigen::PlainObjectBase<DerivedA> & A);
















  template<
      typename DerivedV,
      typename DerivedF,
      typename DerivedN,
      typename DerivedI,
      typename IndexType
      >
  IGL_INLINE void outer_facet(
          const Eigen::MatrixBase<DerivedV> & V,
          const Eigen::MatrixBase<DerivedF> & F,
          const Eigen::MatrixBase<DerivedN> & N,
          const Eigen::MatrixBase<DerivedI> & I,
          IndexType & f,
          bool & flipped);
}

#ifndef IGL_STATIC_LIBRARY
#  include "outer_element.cpp"
#endif
#endif
