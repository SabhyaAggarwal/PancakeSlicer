// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
//
#ifndef IGL_COPYLEFT_CGAL_EXTRACT_CELLS_SINGLE_COMPONENT_H
#define IGL_COPYLEFT_CGAL_EXTRACT_CELLS_SINGLE_COMPONENT_H

#include "../../igl_inline.h"
#include <Eigen/Core>
#include <vector>

namespace igl {
  namespace copyleft
  {
    namespace cgal
    {














      template<
        typename DerivedV,
        typename DerivedF,
        typename DerivedP,
        typename DeriveduE,
        typename DeriveduEC,
        typename DeriveduEE,
        typename DerivedC >
      IGL_INLINE int extract_cells_single_component(
        const Eigen::MatrixBase<DerivedV>& V,
        const Eigen::MatrixBase<DerivedF>& F,
        const Eigen::MatrixBase<DerivedP>& P,
        const Eigen::MatrixBase<DeriveduE>& uE,
        const Eigen::MatrixBase<DeriveduEC>& uEC,
        const Eigen::MatrixBase<DeriveduEE>& uEE,
        Eigen::PlainObjectBase<DerivedC>& cells);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "extract_cells_single_component.cpp"
#endif
#endif
