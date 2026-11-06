// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SLICE_MASK_H
#define IGL_SLICE_MASK_H
#include "igl_inline.h"

#include <Eigen/Sparse>
#include <Eigen/Core>
namespace igl
{









  template <typename XType, typename YType>
  IGL_INLINE void slice_mask(
    const Eigen::SparseMatrix<XType> & X,
    const Eigen::Array<bool,Eigen::Dynamic,1> & R,
    const Eigen::Array<bool,Eigen::Dynamic,1> & C,
    Eigen::SparseMatrix<YType> & Y);





  template <typename XType, typename YType>
  IGL_INLINE void slice_mask(
    const Eigen::SparseMatrix<XType> & X,
    const Eigen::Array<bool,Eigen::Dynamic,1> & R,
    const int dim,
    Eigen::SparseMatrix<YType> & Y);





  template <typename DerivedX,typename DerivedY>
  IGL_INLINE void slice_mask(
    const Eigen::DenseBase<DerivedX> & X,
    const Eigen::Array<bool,Eigen::Dynamic,1> & R,
    const Eigen::Array<bool,Eigen::Dynamic,1> & C,
    Eigen::PlainObjectBase<DerivedY> & Y);
  template <typename DerivedX,typename DerivedY>
  IGL_INLINE void slice_mask(
    const Eigen::DenseBase<DerivedX> & X,
    const Eigen::Array<bool,Eigen::Dynamic,1> & R,
    const int dim,
    Eigen::PlainObjectBase<DerivedY> & Y);





  template <typename DerivedX>
  IGL_INLINE DerivedX slice_mask(
    const Eigen::DenseBase<DerivedX> & X,
    const Eigen::Array<bool,Eigen::Dynamic,1> & R,
    const Eigen::Array<bool,Eigen::Dynamic,1> & C);

  template <typename DerivedX>
  IGL_INLINE DerivedX slice_mask(
    const Eigen::DenseBase<DerivedX> & X,
    const Eigen::Array<bool,Eigen::Dynamic,1> & R,
    const int dim);
}


#ifndef IGL_STATIC_LIBRARY
#  include "slice_mask.cpp"
#endif

#endif
