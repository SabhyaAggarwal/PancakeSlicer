// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_CGAL_INTERSECT_OTHER_H
#define IGL_COPYLEFT_CGAL_INTERSECT_OTHER_H
#include "../../igl_inline.h"
#include "RemeshSelfIntersectionsParam.h"

#include <Eigen/Dense>

namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {

















      template <
        typename DerivedVA,
        typename DerivedFA,
        typename DerivedVB,
        typename DerivedFB,
        typename DerivedIF,
        typename DerivedVVAB,
        typename DerivedFFAB,
        typename DerivedJAB,
        typename DerivedIMAB>
      IGL_INLINE bool intersect_other(
        const Eigen::MatrixBase<DerivedVA> & VA,
        const Eigen::MatrixBase<DerivedFA> & FA,
        const Eigen::MatrixBase<DerivedVB> & VB,
        const Eigen::MatrixBase<DerivedFB> & FB,
        const RemeshSelfIntersectionsParam & params,
        Eigen::PlainObjectBase<DerivedIF> & IF,
        Eigen::PlainObjectBase<DerivedVVAB> & VVAB,
        Eigen::PlainObjectBase<DerivedFFAB> & FFAB,
        Eigen::PlainObjectBase<DerivedJAB>  & JAB,
        Eigen::PlainObjectBase<DerivedIMAB> & IMAB);


      template <
        typename DerivedVA,
        typename DerivedFA,
        typename DerivedVB,
        typename DerivedFB,
        typename DerivedIF>
      IGL_INLINE bool intersect_other(
        const Eigen::MatrixBase<DerivedVA> & VA,
        const Eigen::MatrixBase<DerivedFA> & FA,
        const Eigen::MatrixBase<DerivedVB> & VB,
        const Eigen::MatrixBase<DerivedFB> & FB,
        const bool first_only,
        Eigen::PlainObjectBase<DerivedIF> & IF);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "intersect_other.cpp"
#endif

#endif
