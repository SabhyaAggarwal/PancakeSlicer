#ifndef IGL_EDGES_TO_PATH_H
#define IGL_EDGES_TO_PATH_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{













  template <
    typename DerivedE,
    typename DerivedI,
    typename DerivedJ,
    typename DerivedK>
  IGL_INLINE void edges_to_path(
    const Eigen::MatrixBase<DerivedE> & E,
    Eigen::PlainObjectBase<DerivedI> & I,
    Eigen::PlainObjectBase<DerivedJ> & J,
    Eigen::PlainObjectBase<DerivedK> & K);
}
#ifndef IGL_STATIC_LIBRARY
#  include "edges_to_path.cpp"
#endif
#endif
