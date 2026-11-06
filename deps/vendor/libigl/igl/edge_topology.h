// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_EDGE_TOPOLOGY_H
#define IGL_EDGE_TOPOLOGY_H

#include "igl_inline.h"

#include <Eigen/Core>
#include <vector>

namespace igl
{


















  template <typename DerivedV, typename DerivedF, typename DerivedE>
    IGL_INLINE void edge_topology(
      const Eigen::MatrixBase<DerivedV>& V,
      const Eigen::MatrixBase<DerivedF>& F,
      Eigen::PlainObjectBase<DerivedE>& EV,
      Eigen::PlainObjectBase<DerivedE>& FE,
      Eigen::PlainObjectBase<DerivedE>& EF);
}

#ifndef IGL_STATIC_LIBRARY
#  include "edge_topology.cpp"
#endif

#endif
