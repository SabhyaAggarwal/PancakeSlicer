// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_EMBREE_BONE_VISIBLE_H
#define IGL_EMBREE_BONE_VISIBLE_H
#include "../igl_inline.h"
#include <Eigen/Core>
#include "EmbreeIntersector.h"
namespace igl
{
  namespace embree
  {















    template <
      typename DerivedV,
      typename DerivedF,
      typename DerivedSD,
      typename Derivedflag>
    IGL_INLINE void bone_visible(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedF> & F,
      const Eigen::MatrixBase<DerivedSD> & s,
      const Eigen::MatrixBase<DerivedSD> & d,
      Eigen::PlainObjectBase<Derivedflag>  & flag);


    template <
      typename DerivedV,
      typename DerivedF,
      typename DerivedSD,
      typename Derivedflag>
    IGL_INLINE void bone_visible(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedF> & F,
      const EmbreeIntersector & ei,
      const Eigen::MatrixBase<DerivedSD> & s,
      const Eigen::MatrixBase<DerivedSD> & d,
      Eigen::PlainObjectBase<Derivedflag>  & flag);
  }
}
#ifndef IGL_STATIC_LIBRARY
#  include "bone_visible.cpp"
#endif
#endif
