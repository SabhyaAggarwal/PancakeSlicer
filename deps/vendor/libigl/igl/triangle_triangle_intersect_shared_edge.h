#ifndef IGL_TRIANGLE_TRIANGLE_INTERSECT_SHARED_EDGE_H
#define IGL_TRIANGLE_TRIANGLE_INTERSECT_SHARED_EDGE_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{

















  template <
    typename DerivedV,
    typename DerivedF,
    typename Derivedp>
  IGL_INLINE bool triangle_triangle_intersect_shared_edge(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const int f,
    const int c,
    const Eigen::MatrixBase<Derivedp> & p,
    const int g,
    const typename DerivedV::Scalar epsilon);
}

#ifndef IGL_STATIC_LIBRARY
#  include "triangle_triangle_intersect_shared_edge.cpp"
#endif

#endif
