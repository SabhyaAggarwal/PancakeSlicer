// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_NON_MANIFOLD_EDGE_CURVES
#define IGL_NON_MANIFOLD_EDGE_CURVES

#include "igl_inline.h"
#include <Eigen/Dense>
#include <vector>

namespace igl {













  template<
      typename DerivedF,
      typename DerivedEMAP,
      typename uE2EType>
  IGL_INLINE void extract_non_manifold_edge_curves(
          const Eigen::MatrixBase<DerivedF>& F,
          const Eigen::MatrixBase<DerivedEMAP>& EMAP,
          const std::vector<std::vector<uE2EType> >& uE2E,
          std::vector<std::vector<size_t> >& curves);
}

#ifndef IGL_STATIC_LIBRARY
#  include "extract_non_manifold_edge_curves.cpp"
#endif

#endif
