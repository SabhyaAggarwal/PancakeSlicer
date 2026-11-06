// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_IN_ELEMENT_H
#define IGL_IN_ELEMENT_H

#include "igl_inline.h"
#include "AABB.h"
#include <Eigen/Core>
#include <Eigen/Sparse>

namespace igl
{










  template <
    typename DerivedV,
    typename DerivedEle,
    typename DerivedQ,
    int DIM,
    typename DerivedI>
  IGL_INLINE void in_element(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedEle> & Ele,
    const Eigen::MatrixBase<DerivedQ> & Q,
    const AABB<DerivedV,DIM> & aabb,
    Eigen::PlainObjectBase<DerivedI> & I);




  template <
    typename DerivedV,
    typename DerivedEle,
    typename DerivedQ,
    int DIM,
    typename Scalar>
  IGL_INLINE void in_element(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedEle> & Ele,
    const Eigen::MatrixBase<DerivedQ> & Q,
    const AABB<DerivedV,DIM> & aabb,
    Eigen::SparseMatrix<Scalar> & I);
};

#ifndef IGL_STATIC_LIBRARY
#include "in_element.cpp"
#endif

#endif
