// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_CGAL_TRIM_WITH_SOLID_H
#define IGL_COPYLEFT_CGAL_TRIM_WITH_SOLID_H

#include "../../igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {
      enum TrimWithSolidMethod
      {


        CHECK_EACH_FACE = 1,






        CHECK_EACH_PATCH = 2,











        RESOLVE_BOTH_AND_RESTORE_THEN_CHECK_EACH_PATCH = 3,
      };
















      template <
        typename DerivedVA,
        typename DerivedFA,
        typename DerivedVB,
        typename DerivedFB,
        typename DerivedV,
        typename DerivedF,
        typename DerivedD,
        typename DerivedJ>
      IGL_INLINE void trim_with_solid(
        const Eigen::MatrixBase<DerivedVA> & VA,
        const Eigen::MatrixBase<DerivedFA> & FA,
        const Eigen::MatrixBase<DerivedVB> & VB,
        const Eigen::MatrixBase<DerivedFB> & FB,
        Eigen::PlainObjectBase<DerivedV> & Vd,
        Eigen::PlainObjectBase<DerivedF> & F,
        Eigen::PlainObjectBase<DerivedD> & D,
        Eigen::PlainObjectBase<DerivedJ> & J);




      template <
        typename DerivedVA,
        typename DerivedFA,
        typename DerivedVB,
        typename DerivedFB,
        typename DerivedV,
        typename DerivedF,
        typename DerivedD,
        typename DerivedJ>
      IGL_INLINE void trim_with_solid(
        const Eigen::MatrixBase<DerivedVA> & VA,
        const Eigen::MatrixBase<DerivedFA> & FA,
        const Eigen::MatrixBase<DerivedVB> & VB,
        const Eigen::MatrixBase<DerivedFB> & FB,
        const TrimWithSolidMethod method,
        Eigen::PlainObjectBase<DerivedV> & Vd,
        Eigen::PlainObjectBase<DerivedF> & F,
        Eigen::PlainObjectBase<DerivedD> & D,
        Eigen::PlainObjectBase<DerivedJ> & J);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "trim_with_solid.cpp"
#endif
#endif
