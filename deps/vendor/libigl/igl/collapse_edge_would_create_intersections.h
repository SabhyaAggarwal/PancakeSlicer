#ifndef IGL_COLLAPSE_EDGE_WOULD_CREATE_INTERSECTIONS_H
#define IGL_COLLAPSE_EDGE_WOULD_CREATE_INTERSECTIONS_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{





















  template <typename DerivedV, int DIM> class AABB;

  template <
    typename Derivedp,
    typename DerivedV,
    typename DerivedF,
    typename DerivedE,
    typename DerivedEMAP,
    typename DerivedEF,
    typename DerivedEI>
  IGL_INLINE bool collapse_edge_would_create_intersections(
    const int e,
    const Eigen::MatrixBase<Derivedp> & p,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<DerivedE> & E,
    const Eigen::MatrixBase<DerivedEMAP> & EMAP,
    const Eigen::MatrixBase<DerivedEF> & EF,
    const Eigen::MatrixBase<DerivedEI> & EI,
    const igl::AABB<DerivedV,3> & tree,
    const int inf_face_id = -1);
}
#ifndef IGL_STATIC_LIBRARY
#  include "collapse_edge_would_create_intersections.cpp"
#endif
#endif
