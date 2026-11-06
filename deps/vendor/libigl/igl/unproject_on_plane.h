#ifndef IGL_UNPROJECT_ON_PLANE_H
#define IGL_UNPROJECT_ON_PLANE_H

#include <Eigen/Core>

namespace igl
{









  template <
    typename DerivedUV,
    typename DerivedM,
    typename DerivedVP,
    typename DerivedP,
    typename DerivedZ>
  void unproject_on_plane(
    const Eigen::MatrixBase<DerivedUV> & UV,
    const Eigen::MatrixBase<DerivedM> & M,
    const Eigen::MatrixBase<DerivedVP> & VP,
    const Eigen::MatrixBase<DerivedP> & P,
    Eigen::PlainObjectBase<DerivedZ> & Z);
}

#ifndef IGL_STATIC_LIBRARY
#  include "unproject_on_plane.cpp"
#endif

#endif
