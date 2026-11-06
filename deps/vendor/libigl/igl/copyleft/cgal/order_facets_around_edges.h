// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_CGAL_ORDER_FACETS_AROUND_EDGES_H
#define IGL_COPYLEFT_CGAL_ORDER_FACETS_AROUND_EDGES_H
#include "../../igl_inline.h"
#include <Eigen/Core>
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
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
          typename DerivedN,
          typename DeriveduE,
          typename uE2EType,
          typename uE2oEType,
          typename uE2CType >
      IGL_INLINE
      typename std::enable_if<!std::is_same<typename DerivedV::Scalar,
      typename CGAL::Exact_predicates_exact_constructions_kernel::FT>::value, void>::type
      order_facets_around_edges(
        const Eigen::MatrixBase<DerivedV>& V,
        const Eigen::MatrixBase<DerivedF>& F,
        const Eigen::MatrixBase<DerivedN>& N,
        const Eigen::MatrixBase<DeriveduE>& uE,
        const std::vector<std::vector<uE2EType> >& uE2E,
        std::vector<std::vector<uE2oEType> >& uE2oE,
        std::vector<std::vector<uE2CType > >& uE2C );

      template<
          typename DerivedV,
          typename DerivedF,
          typename DerivedN,
          typename DeriveduE,
          typename uE2EType,
          typename uE2oEType,
          typename uE2CType >
      IGL_INLINE
      typename std::enable_if<std::is_same<typename DerivedV::Scalar,
      typename CGAL::Exact_predicates_exact_constructions_kernel::FT>::value, void>::type
      order_facets_around_edges(
        const Eigen::MatrixBase<DerivedV>& V,
        const Eigen::MatrixBase<DerivedF>& F,
        const Eigen::MatrixBase<DerivedN>& N,
        const Eigen::MatrixBase<DeriveduE>& uE,
        const std::vector<std::vector<uE2EType> >& uE2E,
        std::vector<std::vector<uE2oEType> >& uE2oE,
        std::vector<std::vector<uE2CType > >& uE2C );



      template<
          typename DerivedV,
          typename DerivedF,
          typename DeriveduE,
          typename uE2EType,
          typename uE2oEType,
          typename uE2CType >
      IGL_INLINE void order_facets_around_edges(
        const Eigen::MatrixBase<DerivedV>& V,
        const Eigen::MatrixBase<DerivedF>& F,
        const Eigen::MatrixBase<DeriveduE>& uE,
        const std::vector<std::vector<uE2EType> >& uE2E,
        std::vector<std::vector<uE2oEType> >& uE2oE,
        std::vector<std::vector<uE2CType > >& uE2C );
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#include "order_facets_around_edges.cpp"
#endif

#endif
