// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_HARMONIC_H
#define IGL_HARMONIC_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Sparse>
namespace igl
{









  template <
    typename DerivedV,
    typename DerivedF,
    typename Derivedb,
    typename Derivedbc,
    typename DerivedW>
  IGL_INLINE bool harmonic(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<Derivedb> & b,
    const Eigen::MatrixBase<Derivedbc> & bc,
    const int k,
    Eigen::PlainObjectBase<DerivedW> & W);


  template <
    typename DerivedF,
    typename Derivedb,
    typename Derivedbc,
    typename DerivedW>
  IGL_INLINE bool harmonic(
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<Derivedb> & b,
    const Eigen::MatrixBase<Derivedbc> & bc,
    const int k,
    Eigen::PlainObjectBase<DerivedW> & W);





  template <
    typename DerivedL,
    typename DerivedM,
    typename Derivedb,
    typename Derivedbc,
    typename DerivedW>
  IGL_INLINE bool harmonic(
    const Eigen::SparseCompressedBase<DerivedL> & L,
    const Eigen::SparseCompressedBase<DerivedM> & M,
    const Eigen::MatrixBase<Derivedb> & b,
    const Eigen::MatrixBase<Derivedbc> & bc,
    const int k,
    Eigen::PlainObjectBase<DerivedW> & W);







  template <
    typename DerivedL,
    typename DerivedM,
    typename DerivedQ>
  IGL_INLINE void harmonic(
    const Eigen::SparseCompressedBase<DerivedL> & L,
    const Eigen::SparseCompressedBase<DerivedM> & M,
    const int k,
    DerivedQ & Q);



  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedQ>
  IGL_INLINE void harmonic(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const int k,
    DerivedQ & Q);
};

#ifndef IGL_STATIC_LIBRARY
#include "harmonic.cpp"
#endif
#endif
