// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Hanxiao Shen <hanxiao@cs.nyu.edu>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_PREDICATES_EAR_CLIPPING_H
#define IGL_PREDICATES_EAR_CLIPPING_H

#include <Eigen/Core>
#include "../igl_inline.h"

namespace igl
{
  namespace predicates
  {

















    template <
      typename DerivedP,
      typename DerivedRT,
      typename DerivedF,
      typename DerivedI>
    IGL_INLINE void ear_clipping(
      const Eigen::MatrixBase<DerivedP>& P,
      const Eigen::MatrixBase<DerivedRT>& RT,
      Eigen::PlainObjectBase<DerivedF>& eF,
      Eigen::PlainObjectBase<DerivedI>& I);




    template <typename DerivedP, typename DerivedF>
    IGL_INLINE bool ear_clipping(
      const Eigen::MatrixBase<DerivedP>& P,
      Eigen::PlainObjectBase<DerivedF>& eF);

  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "ear_clipping.cpp"
#endif


#endif
