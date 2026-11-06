// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
//
#ifndef IGL_COPYLEFT_CGAL_ORDER_FACETS_AROUND_EDGE_H
#define IGL_COPYLEFT_CGAL_ORDER_FACETS_AROUND_EDGE_H
#include "../../igl_inline.h"
#include <Eigen/Core>
#include <vector>

namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {
















      template<
        typename DerivedV,
        typename DerivedF,
        typename DerivedI >
      IGL_INLINE
      void order_facets_around_edge(
          const Eigen::MatrixBase<DerivedV>& V,
          const Eigen::MatrixBase<DerivedF>& F,
          size_t s,
          size_t d,
          const std::vector<int>& adj_faces,
          Eigen::PlainObjectBase<DerivedI>& order,
          bool debug=false);







      template<
        typename DerivedV,
        typename DerivedF,
        typename Derivedpivot_point,
        typename DerivedI>
      IGL_INLINE
      void order_facets_around_edge(
        const Eigen::MatrixBase<DerivedV>& V,
        const Eigen::MatrixBase<DerivedF>& F,
        size_t s,
        size_t d,
        const std::vector<int>& adj_faces,
        const Eigen::MatrixBase<Derivedpivot_point>& pivot_point,
        Eigen::PlainObjectBase<DerivedI>& order);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#include "order_facets_around_edge.cpp"
#endif
#endif
