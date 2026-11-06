// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Olga Diamanti <olga.diam@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_PARALLEL_TRANSPORT_ANGLE
#define IGL_PARALLEL_TRANSPORT_ANGLE
#include "igl_inline.h"

#include <Eigen/Core>
#include <vector>

namespace igl {














  template <typename DerivedV, typename DerivedF, typename DerivedK>
  IGL_INLINE void parallel_transport_angles(
    const Eigen::MatrixBase<DerivedV>&V,
    const Eigen::MatrixBase<DerivedF>&F,
    const Eigen::MatrixBase<DerivedV>&FN,
    const Eigen::MatrixXi &E2F,
    const Eigen::MatrixXi &F2E,
    Eigen::PlainObjectBase<DerivedK>&K);

};


#ifndef IGL_STATIC_LIBRARY
#include "parallel_transport_angles.cpp"
#endif


#endif
