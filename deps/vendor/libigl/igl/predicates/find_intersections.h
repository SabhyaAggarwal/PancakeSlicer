// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2024 Alec Jacobson alecjacobson@gmail.com
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/
#ifndef IGL_FAST_FIND_INTERSECTIONS_H
#define IGL_FAST_FIND_INTERSECTIONS_H

#include "../igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  template <typename DerivedV, int DIM> class AABB;
  namespace predicates
  {













    template <
      typename DerivedV1,
      typename DerivedF1,
      typename DerivedV2,
      typename DerivedF2,
      typename DerivedIF,
      typename DerivedCP>
    IGL_INLINE bool find_intersections(
      const AABB<DerivedV1,3> & tree1,
      const Eigen::MatrixBase<DerivedV1> & V1,
      const Eigen::MatrixBase<DerivedF1> & F1,
      const Eigen::MatrixBase<DerivedV2> & V2,
      const Eigen::MatrixBase<DerivedF2> & F2,
      const bool first_only,
      Eigen::PlainObjectBase<DerivedIF> & IF,
      Eigen::PlainObjectBase<DerivedCP> & CP);


    template <
      typename DerivedV1,
      typename DerivedF1,
      typename DerivedV2,
      typename DerivedF2,
      typename DerivedIF,
      typename DerivedCP>
    IGL_INLINE bool find_intersections(
      const Eigen::MatrixBase<DerivedV1> & V1,
      const Eigen::MatrixBase<DerivedF1> & F1,
      const Eigen::MatrixBase<DerivedV2> & V2,
      const Eigen::MatrixBase<DerivedF2> & F2,
      const bool first_only,
      Eigen::PlainObjectBase<DerivedIF> & IF,
      Eigen::PlainObjectBase<DerivedCP> & CP);




    template <
      typename DerivedV1,
      typename DerivedF1,
      typename DerivedV2,
      typename DerivedF2,
      typename DerivedIF,
      typename DerivedCP,
      typename DerivedEV,
      typename DerivedEE,
      typename DerivedEI>
    IGL_INLINE bool find_intersections(
      const Eigen::MatrixBase<DerivedV1> & V1,
      const Eigen::MatrixBase<DerivedF1> & F1,
      const Eigen::MatrixBase<DerivedV2> & V2,
      const Eigen::MatrixBase<DerivedF2> & F2,
      Eigen::PlainObjectBase<DerivedIF> & IF,
      Eigen::PlainObjectBase<DerivedCP> & CP,
      Eigen::PlainObjectBase<DerivedEV> & EV,
      Eigen::PlainObjectBase<DerivedEE> & EE,
      Eigen::PlainObjectBase<DerivedEI> & EI);
  template <
    typename DerivedV1,
    typename DerivedF1,
    typename DerivedV2,
    typename DerivedF2,
    typename DerivedIF,
    typename DerivedCP,
    typename DerivedEV,
    typename DerivedEE,
    typename DerivedEI>
  IGL_INLINE bool find_intersections(
    const igl::AABB<DerivedV1,3> & tree1,
    const Eigen::MatrixBase<DerivedV1> & V1,
    const Eigen::MatrixBase<DerivedF1> & F1,
    const Eigen::MatrixBase<DerivedV2> & V2,
    const Eigen::MatrixBase<DerivedF2> & F2,
    Eigen::PlainObjectBase<DerivedIF> & IF,
    Eigen::PlainObjectBase<DerivedCP> & CP,
    Eigen::PlainObjectBase<DerivedEV> & EV,
    Eigen::PlainObjectBase<DerivedEE> & EE,
    Eigen::PlainObjectBase<DerivedEI> & EI);
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "find_intersections.cpp"
#endif

#endif
