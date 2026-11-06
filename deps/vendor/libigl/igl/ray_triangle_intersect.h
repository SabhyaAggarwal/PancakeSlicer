#ifndef IGL_RAY_TRIANGLE_INTERSECT_H
#define IGL_RAY_TRIANGLE_INTERSECT_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{


















  template <
    typename DerivedO,
    typename DerivedD,
    typename DerivedV0,
    typename DerivedV1,
    typename DerivedV2>
  IGL_INLINE bool ray_triangle_intersect(
     const Eigen::MatrixBase<DerivedO> & O,
     const Eigen::MatrixBase<DerivedD> & D,
     const Eigen::MatrixBase<DerivedV0> & V0,
     const Eigen::MatrixBase<DerivedV1> & V1,
     const Eigen::MatrixBase<DerivedV2> & V2,
     const typename DerivedO::Scalar epsilon,
     typename DerivedO::Scalar & t,
     typename DerivedO::Scalar & u,
     typename DerivedO::Scalar & v,
     bool & parallel);
}

#ifndef IGL_STATIC_LIBRARY
#  include "ray_triangle_intersect.cpp"
#endif

#endif
