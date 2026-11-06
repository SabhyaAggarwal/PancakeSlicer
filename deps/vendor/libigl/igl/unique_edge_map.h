// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_UNIQUE_EDGE_MAP_H
#define IGL_UNIQUE_EDGE_MAP_H
#include "igl_inline.h"
#include <Eigen/Dense>
#include <vector>
namespace igl
{













  template <
    typename DerivedF,
    typename DerivedE,
    typename DeriveduE,
    typename DerivedEMAP,
    typename uE2EType>
  IGL_INLINE void unique_edge_map(
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedE> & E,
    Eigen::PlainObjectBase<DeriveduE> & uE,
    Eigen::PlainObjectBase<DerivedEMAP> & EMAP,
    std::vector<std::vector<uE2EType> > & uE2E);

  template <
    typename DerivedF,
    typename DerivedE,
    typename DeriveduE,
    typename DerivedEMAP>
  IGL_INLINE void unique_edge_map(
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedE> & E,
    Eigen::PlainObjectBase<DeriveduE> & uE,
    Eigen::PlainObjectBase<DerivedEMAP> & EMAP);




































  template <
    typename DerivedF,
    typename DerivedE,
    typename DeriveduE,
    typename DerivedEMAP,
    typename DeriveduEC,
    typename DeriveduEE>
  IGL_INLINE void unique_edge_map(
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedE> & E,
    Eigen::PlainObjectBase<DeriveduE> & uE,
    Eigen::PlainObjectBase<DerivedEMAP> & EMAP,
    Eigen::PlainObjectBase<DeriveduEC> & uEC,
    Eigen::PlainObjectBase<DeriveduEE> & uEE);

}
#ifndef IGL_STATIC_LIBRARY
#  include "unique_edge_map.cpp"
#endif

#endif
