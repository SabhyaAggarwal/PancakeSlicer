// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
//
#ifndef IGL_COPYLEFT_CGAL_REMESH_INTERSECTIONS_H
#define IGL_COPYLEFT_CGAL_REMESH_INTERSECTIONS_H

#include "../../igl_inline.h"
#include <Eigen/Dense>
#include "CGAL_includes.hpp"

namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {




















      template <
        typename DerivedV,
        typename DerivedF,
        typename Kernel,
        typename DerivedVV,
        typename DerivedFF,
        typename DerivedJ,
        typename DerivedIM>
      IGL_INLINE void remesh_intersections(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedF> & F,
        const std::vector<CGAL::Triangle_3<Kernel> > & T,
        const std::map<
          typename DerivedF::Index,
            std::vector<
            std::pair<typename DerivedF::Index, CGAL::Object> > > & offending,
        bool stitch_all,
        bool slow_and_more_precise_rounding,
        Eigen::PlainObjectBase<DerivedVV> & VV,
        Eigen::PlainObjectBase<DerivedFF> & FF,
        Eigen::PlainObjectBase<DerivedJ> & J,
        Eigen::PlainObjectBase<DerivedIM> & IM);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "remesh_intersections.cpp"
#endif

#endif
