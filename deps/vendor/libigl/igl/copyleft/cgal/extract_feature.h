// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_COPYLEFT_CGAL_EXTRACT_FEATURE_H
#define IGL_COPYLEFT_CGAL_EXTRACT_FEATURE_H
#include "../../igl_inline.h"
#include <Eigen/Core>
#include <vector>

namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {


















      template <
        typename DerivedV,
        typename DerivedF,
        typename Derivedfeature_edges>
      IGL_INLINE void extract_feature(
            const Eigen::MatrixBase<DerivedV>& V,
            const Eigen::MatrixBase<DerivedF>& F,
            const double tol,
            Eigen::PlainObjectBase<Derivedfeature_edges>& feature_edges);




      template <
        typename DerivedV,
        typename DerivedF,
        typename DeriveduE,
        typename Derivedfeature_edges
        >
      IGL_INLINE void extract_feature(
            const Eigen::MatrixBase<DerivedV>& V,
            const Eigen::MatrixBase<DerivedF>& F,
            const double tol,
            const Eigen::MatrixBase<DeriveduE>& uE,
            const std::vector<std::vector<typename DeriveduE::Scalar> >& uE2E,
            Eigen::PlainObjectBase<Derivedfeature_edges>& feature_edges);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "extract_feature.cpp"
#endif
#endif
