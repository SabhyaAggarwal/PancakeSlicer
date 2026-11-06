// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
//
#ifndef IGL_COPYLEFT_CGAL_PROPAGATE_WINDING_NUMBERS_H
#define IGL_COPYLEFT_CGAL_PROPAGATE_WINDING_NUMBERS_H
#include "../../igl_inline.h"
#include <Eigen/Core>
#include <vector>








namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {



















      template<
        typename DerivedV,
        typename DerivedF,
        typename DerivedL,
        typename DerivedW>
      IGL_INLINE bool propagate_winding_numbers(
        const Eigen::MatrixBase<DerivedV>& V,
        const Eigen::MatrixBase<DerivedF>& F,
        const Eigen::MatrixBase<DerivedL>& labels,
        Eigen::PlainObjectBase<DerivedW>& W);









      template<
        typename DerivedV,
        typename DerivedF,
        typename DeriveduE,
        typename DeriveduEC,
        typename DeriveduEE,
        typename DerivedP,
        typename DerivedC,
        typename DerivedL,
        typename DerivedW>
      IGL_INLINE bool propagate_winding_numbers(
        const Eigen::MatrixBase<DerivedV>& V,
        const Eigen::MatrixBase<DerivedF>& F,
        const Eigen::MatrixBase<DeriveduE>& uE,
        const Eigen::MatrixBase<DeriveduEC>& uEC,
        const Eigen::MatrixBase<DeriveduEE>& uEE,
        const size_t num_patches,
        const Eigen::MatrixBase<DerivedP>& P,
        const size_t num_cells,
        const Eigen::MatrixBase<DerivedC>& C,
        const Eigen::MatrixBase<DerivedL>& labels,
        Eigen::PlainObjectBase<DerivedW>& W);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "propagate_winding_numbers.cpp"
#endif
#endif
