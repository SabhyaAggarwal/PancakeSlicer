// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Daniele Panozzo <daniele.panozzo@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_TRIANGLE_TRIANGLE_ADJACENCY_H
#define IGL_TRIANGLE_TRIANGLE_ADJACENCY_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>

namespace igl
{


























  template <typename DerivedF, typename DerivedTT, typename DerivedTTi>
  IGL_INLINE void triangle_triangle_adjacency(
    const Eigen::MatrixBase<DerivedF>& F,
    Eigen::PlainObjectBase<DerivedTT>& TT,
    Eigen::PlainObjectBase<DerivedTTi>& TTi);

  template <typename DerivedF, typename DerivedTT>
  IGL_INLINE void triangle_triangle_adjacency(
    const Eigen::MatrixBase<DerivedF>& F,
    Eigen::PlainObjectBase<DerivedTT>& TT);






  template <typename DerivedF, typename TTT_type>
  IGL_INLINE void triangle_triangle_adjacency_preprocess(
    const Eigen::MatrixBase<DerivedF>& F,
    std::vector<std::vector<TTT_type> >& TTT);








  template <typename DerivedF, typename TTT_type, typename DerivedTT>
  IGL_INLINE void triangle_triangle_adjacency_extractTT(
    const Eigen::MatrixBase<DerivedF>& F,
    std::vector<std::vector<TTT_type> >& TTT,
    Eigen::PlainObjectBase<DerivedTT>& TT);








  template <typename DerivedF, typename TTT_type, typename DerivedTTi>
  IGL_INLINE void triangle_triangle_adjacency_extractTTi(
    const Eigen::MatrixBase<DerivedF>& F,
    std::vector<std::vector<TTT_type> >& TTT,
    Eigen::PlainObjectBase<DerivedTTi>& TTi);









  template <
    typename DerivedF,
    typename TTIndex,
    typename TTiIndex>
    IGL_INLINE void triangle_triangle_adjacency(
      const Eigen::MatrixBase<DerivedF> & F,
      std::vector<std::vector<std::vector<TTIndex> > > & TT,
      std::vector<std::vector<std::vector<TTiIndex> > > & TTi);

  template < typename DerivedF, typename TTIndex>
    IGL_INLINE void triangle_triangle_adjacency(
      const Eigen::MatrixBase<DerivedF> & F,
      std::vector<std::vector<std::vector<TTIndex> > > & TT);




  template <
    typename DerivedF,
    typename TTIndex,
    typename TTiIndex>
    IGL_INLINE void triangle_triangle_adjacency(
      const Eigen::MatrixBase<DerivedF> & F,
      const bool construct_TTi,
      std::vector<std::vector<std::vector<TTIndex> > > & TT,
      std::vector<std::vector<std::vector<TTiIndex> > > & TTi);










  template <
    typename DerivedE,
    typename DerivedEMAP,
    typename uE2EType,
    typename TTIndex,
    typename TTiIndex>
    IGL_INLINE void triangle_triangle_adjacency(
      const Eigen::MatrixBase<DerivedE> & E,
      const Eigen::MatrixBase<DerivedEMAP> & EMAP,
      const std::vector<std::vector<uE2EType > > & uE2E,
      const bool construct_TTi,
      std::vector<std::vector<std::vector<TTIndex> > > & TT,
      std::vector<std::vector<std::vector<TTiIndex> > > & TTi);







  template <
    typename DerivedEMAP,
    typename DeriveduEC,
    typename DeriveduEE,
    typename TTIndex,
    typename TTiIndex>
  IGL_INLINE void triangle_triangle_adjacency(
    const Eigen::MatrixBase<DerivedEMAP> & EMAP,
    const Eigen::MatrixBase<DeriveduEC> & uEC,
    const Eigen::MatrixBase<DeriveduEE> & uEE,
    const bool construct_TTi,
    std::vector<std::vector<std::vector<TTIndex> > > & TT,
    std::vector<std::vector<std::vector<TTiIndex> > > & TTi);
}

#ifndef IGL_STATIC_LIBRARY
#  include "triangle_triangle_adjacency.cpp"
#endif

#endif
