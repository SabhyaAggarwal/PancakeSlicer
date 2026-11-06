// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_CIRCULATION_H
#define IGL_CIRCULATION_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>

namespace igl
{















  template <typename DerivedEMAP, typename DerivedEF, typename DerivedEI>
  IGL_INLINE std::vector<int> circulation(
    const int e,
    const bool ccw,
    const Eigen::MatrixBase<DerivedEMAP> & EMAP,
    const Eigen::MatrixBase<DerivedEF> & EF,
    const Eigen::MatrixBase<DerivedEI> & EI);















  template <typename DerivedEMAP, typename DerivedEF, typename DerivedEI, typename DerivedvN>
  IGL_INLINE void circulation(
    const int e,
    const bool ccw,
    const Eigen::MatrixBase<DerivedEMAP> & EMAP,
    const Eigen::MatrixBase<DerivedEF> & EF,
    const Eigen::MatrixBase<DerivedEI> & EI,
    Eigen::PlainObjectBase<DerivedvN> & vN);
















  template <typename DerivedF, typename DerivedEMAP, typename DerivedEF, typename DerivedEI, typename Nv_type>
  IGL_INLINE void circulation(
    const int e,
    const bool ccw,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<DerivedEMAP> & EMAP,
    const Eigen::MatrixBase<DerivedEF> & EF,
    const Eigen::MatrixBase<DerivedEI> & EI,

    std::vector<Nv_type> & Nv,
    std::vector<Nv_type> & Nf);
}

#ifndef IGL_STATIC_LIBRARY
#  include "circulation.cpp"
#endif
#endif
