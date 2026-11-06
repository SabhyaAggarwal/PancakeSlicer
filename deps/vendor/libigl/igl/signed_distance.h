// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SIGNED_DISTANCE_H
#define IGL_SIGNED_DISTANCE_H

#include "igl_inline.h"
#include "AABB.h"
#include "WindingNumberAABB.h"
#include "fast_winding_number.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{

  enum SignedDistanceType
  {

    SIGNED_DISTANCE_TYPE_PSEUDONORMAL         = 0,

    SIGNED_DISTANCE_TYPE_WINDING_NUMBER       = 1,

    SIGNED_DISTANCE_TYPE_DEFAULT              = 2,

    SIGNED_DISTANCE_TYPE_UNSIGNED             = 3,

    SIGNED_DISTANCE_TYPE_FAST_WINDING_NUMBER  = 4,

    NUM_SIGNED_DISTANCE_TYPE                  = 5
  };


















  template <
    typename DerivedP,
    typename DerivedV,
    typename DerivedF,
    typename DerivedS,
    typename DerivedI,
    typename DerivedC,
    typename DerivedN>
  IGL_INLINE void signed_distance(
    const Eigen::MatrixBase<DerivedP> & P,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const SignedDistanceType sign_type,
    const typename DerivedV::Scalar lower_bound,
    const typename DerivedV::Scalar upper_bound,
    Eigen::PlainObjectBase<DerivedS> & S,
    Eigen::PlainObjectBase<DerivedI> & I,
    Eigen::PlainObjectBase<DerivedC> & C,
    Eigen::PlainObjectBase<DerivedN> & N);

  template <
    typename DerivedP,
    typename DerivedV,
    typename DerivedF,
    typename DerivedS,
    typename DerivedI,
    typename DerivedC,
    typename DerivedN>
  IGL_INLINE void signed_distance(
    const Eigen::MatrixBase<DerivedP> & P,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const SignedDistanceType sign_type,
    Eigen::PlainObjectBase<DerivedS> & S,
    Eigen::PlainObjectBase<DerivedI> & I,
    Eigen::PlainObjectBase<DerivedC> & C,
    Eigen::PlainObjectBase<DerivedN> & N);













  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedFN,
    typename DerivedVN,
    typename DerivedEN,
    typename DerivedEMAP,
    typename Derivedq>
  IGL_INLINE typename DerivedV::Scalar signed_distance_pseudonormal(
    const AABB<DerivedV,3> & tree,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<DerivedFN> & FN,
    const Eigen::MatrixBase<DerivedVN> & VN,
    const Eigen::MatrixBase<DerivedEN> & EN,
    const Eigen::MatrixBase<DerivedEMAP> & EMAP,
    const Eigen::MatrixBase<Derivedq> & q);



  template <
    typename DerivedP,
    typename DerivedV,
    typename DerivedF,
    typename DerivedFN,
    typename DerivedVN,
    typename DerivedEN,
    typename DerivedEMAP,
    typename DerivedS,
    typename DerivedI,
    typename DerivedC,
    typename DerivedN>
  IGL_INLINE void signed_distance_pseudonormal(
    const Eigen::MatrixBase<DerivedP> & P,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const AABB<DerivedV,3> & tree,
    const Eigen::MatrixBase<DerivedFN> & FN,
    const Eigen::MatrixBase<DerivedVN> & VN,
    const Eigen::MatrixBase<DerivedEN> & EN,
    const Eigen::MatrixBase<DerivedEMAP> & EMAP,
    Eigen::PlainObjectBase<DerivedS> & S,
    Eigen::PlainObjectBase<DerivedI> & I,
    Eigen::PlainObjectBase<DerivedC> & C,
    Eigen::PlainObjectBase<DerivedN> & N);









  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedFN,
    typename DerivedVN,
    typename DerivedEN,
    typename DerivedEMAP,
    typename Derivedq,
    typename Scalar,
    typename Derivedc,
    typename Derivedn>
  IGL_INLINE void signed_distance_pseudonormal(
    const AABB<DerivedV,3> & tree,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<DerivedFN> & FN,
    const Eigen::MatrixBase<DerivedVN> & VN,
    const Eigen::MatrixBase<DerivedEN> & EN,
    const Eigen::MatrixBase<DerivedEMAP> & EMAP,
    const Eigen::MatrixBase<Derivedq> & q,
    Scalar & s,
    Scalar & sqrd,
    int & i,
    Eigen::PlainObjectBase<Derivedc> & c,
    Eigen::PlainObjectBase<Derivedn> & n);

  template <
    typename DerivedV,
    typename DerivedE,
    typename DerivedEN,
    typename DerivedVN,
    typename Derivedq,
    typename Scalar,
    typename Derivedc,
    typename Derivedn>
  IGL_INLINE void signed_distance_pseudonormal(
    const AABB<DerivedV,2> & tree,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedE> & E,
    const Eigen::MatrixBase<DerivedEN> & EN,
    const Eigen::MatrixBase<DerivedVN> & VN,
    const Eigen::MatrixBase<Derivedq> & q,
    Scalar & s,
    Scalar & sqrd,
    int & i,
    Eigen::PlainObjectBase<Derivedc> & c,
    Eigen::PlainObjectBase<Derivedn> & n);








  template <
    typename DerivedV,
    typename DerivedF,
    typename Derivedq>
  IGL_INLINE typename DerivedV::Scalar signed_distance_winding_number(
    const AABB<DerivedV,3> & tree,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const igl::WindingNumberAABB<typename DerivedV::Scalar,typename DerivedF::Scalar> & hier,
    const Eigen::MatrixBase<Derivedq> & q);





  template <
    typename DerivedV,
    typename DerivedF,
    typename Derivedq,
    typename Scalar,
    typename Derivedc>
  IGL_INLINE void signed_distance_winding_number(
    const AABB<DerivedV,3> & tree,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const igl::WindingNumberAABB<typename DerivedV::Scalar,typename DerivedF::Scalar> & hier,
    const Eigen::MatrixBase<Derivedq> & q,
    Scalar & s,
    Scalar & sqrd,
    int & i,
    Eigen::PlainObjectBase<Derivedc> & c);

  template <
    typename DerivedV,
    typename DerivedF,
    typename Derivedq,
    typename Scalar,
    typename Derivedc>
  IGL_INLINE void signed_distance_winding_number(
    const AABB<DerivedV,2> & tree,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<Derivedq> & q,
    Scalar & s,
    Scalar & sqrd,
    int & i,
    Eigen::PlainObjectBase<Derivedc> & c);



















  template <
    typename DerivedP,
    typename DerivedV,
    typename DerivedF,
    typename DerivedS>
  IGL_INLINE void signed_distance_fast_winding_number(
    const Eigen::MatrixBase<DerivedP> & P,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const AABB<DerivedV,3> & tree,
    const igl::FastWindingNumberBVH & fwn_bvh,
    Eigen::PlainObjectBase<DerivedS> & S
  );











  template <
    typename Derivedq,
    typename DerivedV,
    typename DerivedF>
  IGL_INLINE typename DerivedV::Scalar signed_distance_fast_winding_number(
    const Eigen::MatrixBase<Derivedq> & q,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const AABB<DerivedV,3> & tree,
    const igl::FastWindingNumberBVH & fwn_bvh
  );
}

#ifndef IGL_STATIC_LIBRARY
#  include "signed_distance.cpp"
#endif

#endif
