#ifndef IGL_BOX_SURFACE_AREA_H
#define IGL_BOX_SURFACE_AREA_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Geometry>

namespace igl
{





  template <typename DerivedCorner>
  IGL_INLINE typename DerivedCorner::Scalar box_surface_area(
    const Eigen::MatrixBase<DerivedCorner> & min_corner,
    const Eigen::MatrixBase<DerivedCorner> & max_corner);


  template <typename Scalar, int AmbientDim>
  IGL_INLINE Scalar box_surface_area(
    const Eigen::AlignedBox<Scalar,AmbientDim> & box);
}

#ifndef IGL_STATIC_LIBRARY
#  include "box_surface_area.cpp"
#endif

#endif
