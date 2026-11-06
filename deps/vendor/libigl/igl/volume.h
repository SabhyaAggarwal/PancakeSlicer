// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_VOLUME_H
#define IGL_VOLUME_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{






  template <
    typename DerivedV,
    typename DerivedT,
    typename Derivedvol>
  IGL_INLINE void volume(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedT>& T,
    Eigen::PlainObjectBase<Derivedvol>& vol);





  template <
    typename DerivedA,
    typename DerivedB,
    typename DerivedC,
    typename DerivedD,
    typename Derivedvol>
  IGL_INLINE void volume(
    const Eigen::MatrixBase<DerivedA> & A,
    const Eigen::MatrixBase<DerivedB> & B,
    const Eigen::MatrixBase<DerivedC> & C,
    const Eigen::MatrixBase<DerivedD> & D,
    Eigen::PlainObjectBase<Derivedvol> & vol);


  template <
    typename VecA,
    typename VecB,
    typename VecC,
    typename VecD>
  IGL_INLINE typename VecA::Scalar volume_single(
    const VecA & a,
    const VecB & b,
    const VecC & c,
    const VecD & d);




  template <
    typename DerivedL,
    typename Derivedvol>
  IGL_INLINE void volume(
    const Eigen::MatrixBase<DerivedL>& L,
    Eigen::PlainObjectBase<Derivedvol>& vol);
}

#ifndef IGL_STATIC_LIBRARY
#  include "volume.cpp"
#endif

#endif
