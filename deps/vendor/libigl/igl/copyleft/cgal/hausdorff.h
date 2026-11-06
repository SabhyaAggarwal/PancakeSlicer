// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_CGAL_HAUSDORFF_H
#define IGL_COPYLEFT_CGAL_HAUSDORFF_H
#include "../../igl_inline.h"

#include <Eigen/Dense>
#include "CGAL_includes.hpp"
#include <vector>

namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {












      template <
        typename DerivedV,
        typename Kernel,
        typename Scalar>
      IGL_INLINE void hausdorff(
        const Eigen::MatrixBase<DerivedV>& V,
        const CGAL::AABB_tree<
          CGAL::AABB_traits_3<Kernel,
            CGAL::AABB_triangle_primitive_3<Kernel,
              typename std::vector<CGAL::Triangle_3<Kernel> >::iterator
            >
          >
        > & treeB,
        const std::vector<CGAL::Triangle_3<Kernel> > & TB,
        Scalar & l,
        Scalar & u);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "hausdorff.cpp"
#endif

#endif
