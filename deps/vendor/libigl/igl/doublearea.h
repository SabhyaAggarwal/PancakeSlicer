// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_DOUBLEAREA_H
#define IGL_DOUBLEAREA_H
#include "igl_inline.h"
#include <Eigen/Dense>
namespace igl
{














  template <typename DerivedV, typename DerivedF, typename DeriveddblA>
  IGL_INLINE void doublearea(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DeriveddblA> & dblA);






  template <
    typename DerivedA,
    typename DerivedB,
    typename DerivedC,
    typename DerivedD>
  IGL_INLINE void doublearea(
    const Eigen::MatrixBase<DerivedA> & A,
    const Eigen::MatrixBase<DerivedB> & B,
    const Eigen::MatrixBase<DerivedC> & C,
    Eigen::PlainObjectBase<DerivedD> & D);








  template <
    typename DerivedA,
    typename DerivedB,
    typename DerivedC>
  IGL_INLINE typename DerivedA::Scalar doublearea_single(
    const Eigen::MatrixBase<DerivedA> & A,
    const Eigen::MatrixBase<DerivedB> & B,
    const Eigen::MatrixBase<DerivedC> & C);














  template <typename Derivedl, typename DeriveddblA>
  IGL_INLINE void doublearea(
    const Eigen::MatrixBase<Derivedl> & l,
    const typename Derivedl::Scalar nan_replacement,
    Eigen::PlainObjectBase<DeriveddblA> & dblA);



  template <typename Derivedl, typename DeriveddblA>
  IGL_INLINE void doublearea(
    const Eigen::MatrixBase<Derivedl> & l,
    Eigen::PlainObjectBase<DeriveddblA> & dblA);







  template <typename DerivedV, typename DerivedF, typename DeriveddblA>
  IGL_INLINE void doublearea_quad(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DeriveddblA> & dblA);
}

#ifndef IGL_STATIC_LIBRARY
#  include "doublearea.cpp"
#endif

#endif
