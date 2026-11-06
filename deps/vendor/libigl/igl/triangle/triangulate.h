// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Daniele Panozzo <daniele.panozzo@gmail.com>
// Copyright (C) 2017 Alec Jacobson
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_TRIANGLE_TRIANGULATE_H
#define IGL_TRIANGLE_TRIANGULATE_H
#include "../igl_inline.h"
#include <string>
#include <Eigen/Core>

namespace igl
{
  namespace triangle
  {













    template <
      typename DerivedV,
      typename DerivedE,
      typename DerivedH,
      typename DerivedVM,
      typename DerivedEM,
      typename DerivedV2,
      typename DerivedF2,
      typename DerivedVM2,
      typename DerivedE2,
      typename DerivedEM2>
    IGL_INLINE void triangulate(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedE> & E,
      const Eigen::MatrixBase<DerivedH> & H,
      const Eigen::MatrixBase<DerivedVM> & VM,
      const Eigen::MatrixBase<DerivedEM> & EM,
      const std::string flags,
      Eigen::PlainObjectBase<DerivedV2> & V2,
      Eigen::PlainObjectBase<DerivedF2> & F2,
      Eigen::PlainObjectBase<DerivedVM2> & VM2,
      Eigen::PlainObjectBase<DerivedE2>  & E2,
      Eigen::PlainObjectBase<DerivedEM2> & EM2);

    template <
      typename DerivedV,
      typename DerivedE,
      typename DerivedH,
      typename DerivedV2,
      typename DerivedF2>
    IGL_INLINE void triangulate(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedE> & E,
      const Eigen::MatrixBase<DerivedH> & H,
      const std::string flags,
      Eigen::PlainObjectBase<DerivedV2> & V2,
      Eigen::PlainObjectBase<DerivedF2> & F2);
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "triangulate.cpp"
#endif

#endif
