// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
//
#ifndef IGL_COPYLET_CGAL_CLOSEST_FACET_H
#define IGL_COPYLET_CGAL_CLOSEST_FACET_H

#include "../../igl_inline.h"
#include <Eigen/Core>
#include <vector>

#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits_3.h>
#include <CGAL/AABB_triangle_primitive_3.h>
#include <CGAL/intersections.h>
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>

namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {




















      template<
        typename DerivedV,
        typename DerivedF,
        typename DerivedI,
        typename DerivedP,
        typename DerivedEMAP,
        typename DeriveduEC,
        typename DeriveduEE,
        typename Kernel,
        typename DerivedR,
        typename DerivedS >
      IGL_INLINE void closest_facet(
          const Eigen::MatrixBase<DerivedV>& V,
          const Eigen::MatrixBase<DerivedF>& F,
          const Eigen::MatrixBase<DerivedI>& I,
          const Eigen::MatrixBase<DerivedP>& P,
          const Eigen::MatrixBase<DerivedEMAP>& EMAP,
          const Eigen::MatrixBase<DeriveduEC>& uEC,
          const Eigen::MatrixBase<DeriveduEE>& uEE,
          const std::vector<std::vector<size_t> > & VF,
          const std::vector<std::vector<size_t> > & VFi,
          const CGAL::AABB_tree<
            CGAL::AABB_traits_3<
              Kernel,
              CGAL::AABB_triangle_primitive_3<
                Kernel, typename std::vector<
                  typename Kernel::Triangle_3 >::iterator > > > & tree,
          const std::vector<typename Kernel::Triangle_3 > & triangles,
          const std::vector<bool> & in_I,
          Eigen::PlainObjectBase<DerivedR>& R,
          Eigen::PlainObjectBase<DerivedS>& S);

      template<
        typename DerivedV,
        typename DerivedF,
        typename DerivedI,
        typename DerivedP,
        typename DerivedEMAP,
        typename DeriveduEC,
        typename DeriveduEE,
        typename DerivedR,
        typename DerivedS >
      IGL_INLINE void closest_facet(
        const Eigen::MatrixBase<DerivedV>& V,
        const Eigen::MatrixBase<DerivedF>& F,
        const Eigen::MatrixBase<DerivedI>& I,
        const Eigen::MatrixBase<DerivedP>& P,
        const Eigen::MatrixBase<DerivedEMAP>& EMAP,
        const Eigen::MatrixBase<DeriveduEC>& uEC,
        const Eigen::MatrixBase<DeriveduEE>& uEE,
              Eigen::PlainObjectBase<DerivedR>& R,
              Eigen::PlainObjectBase<DerivedS>& S);

      template<
        typename DerivedV,
        typename DerivedF,
        typename DerivedP,
        typename DerivedEMAP,
        typename DeriveduEC,
        typename DeriveduEE,
        typename DerivedR,
        typename DerivedS >
      IGL_INLINE void closest_facet(
        const Eigen::MatrixBase<DerivedV>& V,
        const Eigen::MatrixBase<DerivedF>& F,
        const Eigen::MatrixBase<DerivedP>& P,
        const Eigen::MatrixBase<DerivedEMAP>& EMAP,
        const Eigen::MatrixBase<DeriveduEC>& uEC,
        const Eigen::MatrixBase<DeriveduEE>& uEE,
        Eigen::PlainObjectBase<DerivedR>& R,
        Eigen::PlainObjectBase<DerivedS>& S);

      template<
        typename DerivedV,
        typename DerivedF,
        typename DerivedI,
        typename DerivedP,
        typename DerivedEMAP,
        typename DeriveduEC,
        typename DeriveduEE,
        typename Kernel,
        typename DerivedR,
        typename DerivedS >
      IGL_INLINE void closest_facet(
          const Eigen::MatrixBase<DerivedV>& V,
          const Eigen::MatrixBase<DerivedF>& F,
          const Eigen::MatrixBase<DerivedI>& I,
          const Eigen::MatrixBase<DerivedP>& P,
          const Eigen::MatrixBase<DerivedEMAP>& EMAP,
          const Eigen::MatrixBase<DeriveduEC>& uEC,
          const Eigen::MatrixBase<DeriveduEE>& uEE,
          const std::vector<std::vector<size_t> > & VF,
          const std::vector<std::vector<size_t> > & VFi,
          const CGAL::AABB_tree<
            CGAL::AABB_traits_3<
              Kernel,
              CGAL::AABB_triangle_primitive_3<
                Kernel, typename std::vector<
                  typename Kernel::Triangle_3 >::iterator > > > & tree,
          const std::vector<typename Kernel::Triangle_3 > & triangles,
          const std::vector<bool> & in_I,
          Eigen::PlainObjectBase<DerivedR>& R,
          Eigen::PlainObjectBase<DerivedS>& S);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#include "closest_facet.cpp"
#endif
#endif
