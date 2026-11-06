// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SLICE_H
#define IGL_SLICE_H
#include "igl_inline.h"
#include "placeholders.h"























#include <Eigen/Sparse>
#include <vector>
namespace igl
{











  template <
    typename TX,
    typename TY,
    typename DerivedR,
    typename DerivedC>
  IGL_INLINE void slice(
    const Eigen::SparseMatrix<TX>& X,
    const Eigen::DenseBase<DerivedR> & R,
    const Eigen::DenseBase<DerivedC> & C,
    Eigen::SparseMatrix<TY>& Y);






  template <
    typename MatX,
    typename DerivedR,
    typename MatY>
  IGL_INLINE void slice(
    const MatX& X,
    const Eigen::DenseBase<DerivedR> & R,
    const int dim,
    MatY& Y);

  template< class T >
  IGL_INLINE void slice(
    const std::vector<T> & X,
    std::vector<size_t> const & R,
    std::vector<T> & Y);



  template <typename DerivedX, typename DerivedY, typename DerivedR>
  IGL_INLINE void slice(
    const Eigen::DenseBase<DerivedX> & X,
    const Eigen::DenseBase<DerivedR> & R,
    Eigen::PlainObjectBase<DerivedY> & Y);





  template <
    typename DerivedX,
    typename DerivedR,
    typename DerivedC,
    typename DerivedY>
  IGL_INLINE void slice(
    const Eigen::DenseBase<DerivedX> & X,
    const Eigen::DenseBase<DerivedR> & R,
    const Eigen::DenseBase<DerivedC> & C,
    Eigen::PlainObjectBase<DerivedY> & Y);






  template <typename DerivedX, typename DerivedR>
  IGL_INLINE DerivedX slice(
    const Eigen::DenseBase<DerivedX> & X,
    const Eigen::DenseBase<DerivedR> & R);

  template <typename DerivedX, typename DerivedR>
  IGL_INLINE DerivedX slice(
    const Eigen::DenseBase<DerivedX>& X,
    const Eigen::DenseBase<DerivedR> & R,
    const int dim);
}

#ifndef IGL_STATIC_LIBRARY
#  include "slice.cpp"
#endif

#endif
