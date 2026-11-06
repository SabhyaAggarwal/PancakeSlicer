// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Daniele Panozzo <daniele.panozzo@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_HALFEDGEITERATOR_H
#define IGL_HALFEDGEITERATOR_H

#include <Eigen/Core>

#include <vector>
#include "igl_inline.h"


namespace igl
{













  template <
    typename DerivedF,
    typename DerivedFF,
    typename DerivedFFi>
  class HalfEdgeIterator
  {
  public:









    IGL_INLINE HalfEdgeIterator(
        const Eigen::MatrixBase<DerivedF>& _F,
        const Eigen::MatrixBase<DerivedFF>& _FF,
        const Eigen::MatrixBase<DerivedFFi>& _FFi,
        int _fi,
        int _ei,
        bool _reverse = false
        );


    IGL_INLINE void flipF();


    IGL_INLINE void flipE();


    IGL_INLINE void flipV();



    IGL_INLINE bool isBorder();














    IGL_INLINE bool NextFE();



    IGL_INLINE int Vi();



    IGL_INLINE int Fi();



    IGL_INLINE int Ei();



    IGL_INLINE bool operator==(HalfEdgeIterator& p2);

  private:
    int fi;
    int ei;
    bool reverse;


    const Eigen::MatrixBase<DerivedF> & F;
    const Eigen::MatrixBase<DerivedFF> & FF;
    const Eigen::MatrixBase<DerivedFFi> & FFi;
  };

}

#ifndef IGL_STATIC_LIBRARY
#  include "HalfEdgeIterator.cpp"
#endif

#endif
