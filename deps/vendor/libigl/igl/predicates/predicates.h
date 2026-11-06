// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#pragma once
#ifndef IGL_PREDICATES_PREDICATES_H
#define IGL_PREDICATES_PREDICATES_H

#include "../igl_inline.h"
#include <Eigen/Core>

namespace igl {
  namespace predicates {



    enum class Orientation {
      POSITIVE=1, INSIDE=1,
      NEGATIVE=-1, OUTSIDE=-1,
      COLLINEAR=0, COPLANAR=0, COCIRCULAR=0, COSPHERICAL=0, DEGENERATE=0
    };






    IGL_INLINE void exactinit();











    template<typename Vector2D>
    IGL_INLINE Orientation orient2d(
        const Eigen::MatrixBase<Vector2D>& pa,
        const Eigen::MatrixBase<Vector2D>& pb,
        const Eigen::MatrixBase<Vector2D>& pc);











    template<typename Vector3D>
    IGL_INLINE Orientation orient3d(
        const Eigen::MatrixBase<Vector3D>& pa,
        const Eigen::MatrixBase<Vector3D>& pb,
        const Eigen::MatrixBase<Vector3D>& pc,
        const Eigen::MatrixBase<Vector3D>& pd);











    template<typename Vector2D>
    IGL_INLINE Orientation incircle(
        const Eigen::MatrixBase<Vector2D>& pa,
        const Eigen::MatrixBase<Vector2D>& pb,
        const Eigen::MatrixBase<Vector2D>& pc,
        const Eigen::MatrixBase<Vector2D>& pd);












    template<typename Vector3D>
    IGL_INLINE Orientation insphere(
        const Eigen::MatrixBase<Vector3D>& pa,
        const Eigen::MatrixBase<Vector3D>& pb,
        const Eigen::MatrixBase<Vector3D>& pc,
        const Eigen::MatrixBase<Vector3D>& pd,
        const Eigen::MatrixBase<Vector3D>& pe);
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "predicates.cpp"
#endif

#endif
