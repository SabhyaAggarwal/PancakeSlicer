// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2024 Alec Jacobson alecjacobson@gmail.com
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/
#ifndef IGL_PREDICATES_FIND_SELF_INTERSECTIONS_H
#define IGL_PREDICATES_FIND_SELF_INTERSECTIONS_H

#include "../igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  namespace predicates
  {










    template <
      typename DerivedV,
      typename DerivedF,
      typename DerivedIF,
      typename DerivedCP>
    IGL_INLINE bool find_self_intersections(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedF> & F,
      const bool first_only,
      Eigen::PlainObjectBase<DerivedIF> & IF,
      Eigen::PlainObjectBase<DerivedCP> & CP);




    template <
      typename DerivedV,
      typename DerivedF,
      typename DerivedIF,
      typename DerivedCP,
      typename DerivedEV,
      typename DerivedEE,
      typename DerivedEI>
    IGL_INLINE bool find_self_intersections(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedF> & F,
      Eigen::PlainObjectBase<DerivedIF> & IF,
      Eigen::PlainObjectBase<DerivedCP> & CP,
      Eigen::PlainObjectBase<DerivedEV> & EV,
      Eigen::PlainObjectBase<DerivedEE> & EE,
      Eigen::PlainObjectBase<DerivedEI> & EI);
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "find_self_intersections.cpp"
#endif

#endif
