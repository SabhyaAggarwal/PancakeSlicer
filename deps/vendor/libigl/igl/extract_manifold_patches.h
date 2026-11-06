// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_EXTRACT_MANIFOLD_PATCHES
#define IGL_EXTRACT_MANIFOLD_PATCHES

#include "igl_inline.h"
#include <Eigen/Dense>
#include <vector>

namespace igl {










  template <
    typename DerivedF,
    typename DerivedEMAP,
    typename DeriveduEC,
    typename DeriveduEE,
    typename DerivedP>
  IGL_INLINE int extract_manifold_patches(
    const Eigen::MatrixBase<DerivedF>& F,
    const Eigen::MatrixBase<DerivedEMAP>& EMAP,
    const Eigen::MatrixBase<DeriveduEC>& uEC,
    const Eigen::MatrixBase<DeriveduEE>& uEE,
    Eigen::PlainObjectBase<DerivedP>& P);


  template <
    typename DerivedF,
    typename DerivedEMAP,
    typename uE2EType,
    typename DerivedP>
  IGL_INLINE int extract_manifold_patches(
    const Eigen::MatrixBase<DerivedF>& F,
    const Eigen::MatrixBase<DerivedEMAP>& EMAP,
    const std::vector<std::vector<uE2EType> >& uE2E,
    Eigen::PlainObjectBase<DerivedP>& P);

  template <
      typename DerivedF,
      typename DerivedP>
  IGL_INLINE int extract_manifold_patches(
      const Eigen::MatrixBase<DerivedF> &F,
      Eigen::PlainObjectBase<DerivedP> &P);
}
#ifndef IGL_STATIC_LIBRARY
#  include "extract_manifold_patches.cpp"
#endif

#endif
