// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_EMBREE_REORIENT_FACETS_RAYCAST_H
#define IGL_EMBREE_REORIENT_FACETS_RAYCAST_H
#include "../igl_inline.h"
#include <Eigen/Core>
namespace igl
{
  namespace embree
  {















    template <
      typename DerivedV,
      typename DerivedF,
      typename DerivedI,
      typename DerivedC>
    IGL_INLINE void reorient_facets_raycast(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedF> & F,
      int rays_total,
      int rays_minimum,
      bool facet_wise,
      bool use_parity,
      bool is_verbose,
      Eigen::PlainObjectBase<DerivedI> & I,
      Eigen::PlainObjectBase<DerivedC> & C);








    template <
      typename DerivedV,
      typename DerivedF,
      typename DerivedFF,
      typename DerivedI>
    IGL_INLINE void reorient_facets_raycast(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedF> & F,
      Eigen::PlainObjectBase<DerivedFF> & FF,
      Eigen::PlainObjectBase<DerivedI> & I);
  }
};

#ifndef IGL_STATIC_LIBRARY
#  include "reorient_facets_raycast.cpp"
#endif

#endif
