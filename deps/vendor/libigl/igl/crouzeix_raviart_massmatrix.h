// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef CROUZEIX_RAVIART_MASSMATRIX_H
#define CROUZEIX_RAVIART_MASSMATRIX_H
#include <Eigen/Dense>
#include <Eigen/Sparse>

namespace igl
{














  template <typename MT, typename DerivedV, typename DerivedF, typename DerivedE, typename DerivedEMAP>
  void crouzeix_raviart_massmatrix(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedF> & F,
      Eigen::SparseMatrix<MT> & M,
      Eigen::PlainObjectBase<DerivedE> & E,
      Eigen::PlainObjectBase<DerivedEMAP> & EMAP);














  template <typename MT, typename DerivedV, typename DerivedF, typename DerivedE, typename DerivedEMAP>
  void crouzeix_raviart_massmatrix(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedF> & F,
      const Eigen::MatrixBase<DerivedE> & E,
      const Eigen::MatrixBase<DerivedEMAP> & EMAP,
      Eigen::SparseMatrix<MT> & M);
}
#ifndef IGL_STATIC_LIBRARY
#  include "crouzeix_raviart_massmatrix.cpp"
#endif

#endif
