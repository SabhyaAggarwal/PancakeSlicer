// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_FIND_H
#define IGL_FIND_H
#include "igl_inline.h"
#define EIGEN_YES_I_KNOW_SPARSE_MODULE_IS_NOT_STABLE_YET
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <vector>
namespace igl
{









  template <
    typename T,
    typename DerivedI,
    typename DerivedJ,
    typename DerivedV>
  IGL_INLINE void find(
    const Eigen::SparseMatrix<T>& X,
    Eigen::DenseBase<DerivedI> & I,
    Eigen::DenseBase<DerivedJ> & J,
    Eigen::DenseBase<DerivedV> & V);

  template < typename T>
  IGL_INLINE std::vector<Eigen::Triplet<T> > find(
    const Eigen::SparseMatrix<T>& X);

  template <
    typename DerivedX,
    typename DerivedI,
    typename DerivedJ,
    typename DerivedV>
  IGL_INLINE void find(
    const Eigen::DenseBase<DerivedX>& X,
    Eigen::PlainObjectBase<DerivedI> & I,
    Eigen::PlainObjectBase<DerivedJ> & J,
    Eigen::PlainObjectBase<DerivedV> & V);



  template <
    typename DerivedX,
    typename DerivedI>
  IGL_INLINE void find(
    const Eigen::DenseBase<DerivedX>& X,
    Eigen::PlainObjectBase<DerivedI> & I);

  template <typename T>
  IGL_INLINE void find(
    const Eigen::SparseVector<T>& X,
    Eigen::Matrix<int,Eigen::Dynamic,1> & I,
    Eigen::Matrix<T,Eigen::Dynamic,1> & V);














  template <int RowsAtCompileTime, int MaxRowsAtCompileTime>
  IGL_INLINE std::vector<int> find(
    const Eigen::Array<bool,RowsAtCompileTime,1,0,MaxRowsAtCompileTime,1> & M);
}

#ifndef IGL_STATIC_LIBRARY
#  include "find.cpp"
#endif

#endif
