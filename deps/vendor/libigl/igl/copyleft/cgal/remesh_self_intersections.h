// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_CGAL_REMESH_SELF_INTERSECTIONS_H
#define IGL_COPYLEFT_CGAL_REMESH_SELF_INTERSECTIONS_H
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
        typename DerivedV,
        typename DerivedF,
        typename DerivedVV,
        typename DerivedFF,
        typename DerivedIF,
        typename DerivedJ,
        typename DerivedIM>
      IGL_INLINE void remesh_self_intersections(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedF> & F,
        const RemeshSelfIntersectionsParam & params,
        Eigen::PlainObjectBase<DerivedVV> & VV,
        Eigen::PlainObjectBase<DerivedFF> & FF,
        Eigen::PlainObjectBase<DerivedIF> & IF,
        Eigen::PlainObjectBase<DerivedJ> & J,
        Eigen::PlainObjectBase<DerivedIM> & IM);



      template <
        typename DerivedV,
        typename DerivedF,
        typename DerivedVV,
        typename DerivedFF,
        typename DerivedIF,
        typename DerivedJ>
      IGL_INLINE void remesh_self_intersections(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedF> & F,
        const RemeshSelfIntersectionsParam & params,
        Eigen::PlainObjectBase<DerivedVV> & VV,
        Eigen::PlainObjectBase<DerivedFF> & FF,
        Eigen::PlainObjectBase<DerivedIF> & IF,
        Eigen::PlainObjectBase<DerivedJ> & J);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "remesh_self_intersections.cpp"
#endif

#endif
