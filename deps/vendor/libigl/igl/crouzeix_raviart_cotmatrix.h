// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2017 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_CROUZEIX_RAVIART_COTMATRIX
#define IGL_CROUZEIX_RAVIART_COTMATRIX
#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Sparse>
namespace igl
{












  template <typename DerivedV, typename DerivedF, typename LT, typename DerivedE, typename DerivedEMAP>
  void crouzeix_raviart_cotmatrix(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedF> & F,
      Eigen::SparseMatrix<LT> & L,
      Eigen::PlainObjectBase<DerivedE> & E,
      Eigen::PlainObjectBase<DerivedEMAP> & EMAP);












  template <typename DerivedV, typename DerivedF, typename DerivedE, typename DerivedEMAP, typename LT>
  void crouzeix_raviart_cotmatrix(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedF> & F,
      const Eigen::MatrixBase<DerivedE> & E,
      const Eigen::MatrixBase<DerivedEMAP> & EMAP,
      Eigen::SparseMatrix<LT> & L);
}
#ifndef IGL_STATIC_LIBRARY
#  include "crouzeix_raviart_cotmatrix.cpp"
#endif
#endif
