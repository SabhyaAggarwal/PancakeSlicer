// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Oded Stein <oded.stein@columbia.edu>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_TET_TET_ADJACENCY_H
#define IGL_TET_TET_ADJACENCY_H

#include <Eigen/Core>

#include "igl_inline.h"

namespace igl
{










  template <typename DerivedT, typename DerivedTT, typename DerivedTTi>
  IGL_INLINE void tet_tet_adjacency(
    const Eigen::MatrixBase<DerivedT>& T,
    Eigen::PlainObjectBase<DerivedTT>& TT,
    Eigen::PlainObjectBase<DerivedTTi>& TTi);

  template <typename DerivedT, typename DerivedTT>
  IGL_INLINE void tet_tet_adjacency(
    const Eigen::MatrixBase<DerivedT>& T,
    Eigen::PlainObjectBase<DerivedTT>& TT);
}

#ifndef IGL_STATIC_LIBRARY
#  include "tet_tet_adjacency.cpp"
#endif


#endif
