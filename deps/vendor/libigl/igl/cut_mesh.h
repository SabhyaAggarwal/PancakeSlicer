// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Hanxiao Shen <hanxiao@cims.nyu.edu>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_CUT_MESH_H
#define IGL_CUT_MESH_H
#include "igl_inline.h"

#include <Eigen/Core>

namespace igl
{




















  template <typename DerivedV, typename DerivedF, typename DerivedC, typename DerivedI>
  IGL_INLINE void cut_mesh(
    Eigen::PlainObjectBase<DerivedV>& V,
    Eigen::PlainObjectBase<DerivedF>& F,
    const Eigen::MatrixBase<DerivedC>& cuts,
    Eigen::PlainObjectBase<DerivedI>& I
  );












  template <typename DerivedV, typename DerivedF, typename DerivedFF, typename DerivedFFi, typename DerivedC, typename DerivedI>
  IGL_INLINE void cut_mesh(
    Eigen::PlainObjectBase<DerivedV>& V,
    Eigen::PlainObjectBase<DerivedF>& F,
    Eigen::MatrixBase<DerivedFF>& FF,
    Eigen::MatrixBase<DerivedFFi>& FFi,
    const Eigen::MatrixBase<DerivedC>& C,
    Eigen::PlainObjectBase<DerivedI>& I
  );












  template <typename DerivedV, typename DerivedF, typename DerivedC>
  IGL_INLINE void cut_mesh(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    const Eigen::MatrixBase<DerivedC>& cuts,
    Eigen::PlainObjectBase<DerivedV>& Vn,
    Eigen::PlainObjectBase<DerivedF>& Fn
  );

















  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedC,
    typename DerivedVn,
    typename DerivedFn,
    typename DerivedI>
  IGL_INLINE void cut_mesh(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    const Eigen::MatrixBase<DerivedC>& cuts,
    Eigen::PlainObjectBase<DerivedVn>& Vn,
    Eigen::PlainObjectBase<DerivedFn>& Fn,
    Eigen::PlainObjectBase<DerivedI>& I);



}


#ifndef IGL_STATIC_LIBRARY
#include "cut_mesh.cpp"
#endif


#endif
