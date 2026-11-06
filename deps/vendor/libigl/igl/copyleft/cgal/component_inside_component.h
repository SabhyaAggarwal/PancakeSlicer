// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_CGAL_COMONENT_INSIDE_COMPONENT
#define IGL_COPYLEFT_CGAL_COMONENT_INSIDE_COMPONENT

#include "../../igl_inline.h"
#include <Eigen/Core>
#include <vector>

namespace igl {
  namespace copyleft
  {
    namespace cgal
    {














      template<typename DerivedV, typename DerivedF, typename DerivedI>
          IGL_INLINE bool component_inside_component(
                  const Eigen::MatrixBase<DerivedV>& V1,
                  const Eigen::MatrixBase<DerivedF>& F1,
                  const Eigen::MatrixBase<DerivedI>& I1,
                  const Eigen::MatrixBase<DerivedV>& V2,
                  const Eigen::MatrixBase<DerivedF>& F2,
                  const Eigen::MatrixBase<DerivedI>& I2);

      template<typename DerivedV, typename DerivedF>
          IGL_INLINE bool component_inside_component(
                  const Eigen::MatrixBase<DerivedV>& V1,
                  const Eigen::MatrixBase<DerivedF>& F1,
                  const Eigen::MatrixBase<DerivedV>& V2,
                  const Eigen::MatrixBase<DerivedF>& F2);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#include "component_inside_component.cpp"
#endif
#endif
