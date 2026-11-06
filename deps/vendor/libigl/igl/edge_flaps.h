// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
// Copyright (C) 2020 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_EDGE_FLAPS_H
#define IGL_EDGE_FLAPS_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{























  template <
    typename DerivedF,
    typename DeriveduE,
    typename DerivedEMAP,
    typename DerivedEF,
    typename DerivedEI>
  IGL_INLINE void edge_flaps(
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<DeriveduE> & uE,
    const Eigen::MatrixBase<DerivedEMAP> & EMAP,
    Eigen::PlainObjectBase<DerivedEF> & EF,
    Eigen::PlainObjectBase<DerivedEI> & EI);

  template <
    typename DerivedF,
    typename DeriveduE,
    typename DerivedEMAP,
    typename DerivedEF,
    typename DerivedEI>
  IGL_INLINE void edge_flaps(
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DeriveduE> & uE,
    Eigen::PlainObjectBase<DerivedEMAP> & EMAP,
    Eigen::PlainObjectBase<DerivedEF> & EF,
    Eigen::PlainObjectBase<DerivedEI> & EI);
}
#ifndef IGL_STATIC_LIBRARY
#  include "edge_flaps.cpp"
#endif

#endif
