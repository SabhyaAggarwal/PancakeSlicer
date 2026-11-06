// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_CGAL_PROJECTED_CDT_H
#define IGL_COPYLEFT_CGAL_PROJECTED_CDT_H
#include "../../igl_inline.h"
#include <Eigen/Core>
#include <CGAL/Plane_3.h>
#include <CGAL/Point_3.h>
#include <CGAL/Object.h>
#include <vector>
namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {













      template <typename Kernel, typename Index>
      IGL_INLINE void projected_cdt(
        const std::vector<CGAL::Object> & objects,
        const CGAL::Plane_3<Kernel> & P,
        std::vector<CGAL::Point_3<Kernel> >& vertices,
        std::vector<std::vector<Index> >& faces);






      template < typename Kernel, typename DerivedV, typename DerivedF>
      IGL_INLINE void projected_cdt(
        const std::vector<CGAL::Object> & objects,
        const CGAL::Plane_3<Kernel> & P,
        Eigen::PlainObjectBase<DerivedV> & V,
        Eigen::PlainObjectBase<DerivedF> & F);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "projected_cdt.cpp"
#endif
#endif
