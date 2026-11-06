#ifndef IGL_TRIANGLE_TRIANGLE_INTERSECT_H
#define IGL_TRIANGLE_TRIANGLE_INTERSECT_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{





































  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedE,
    typename DerivedEMAP,
    typename DerivedEF,
    typename Derivedp>
  IGL_INLINE bool triangle_triangle_intersect(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<DerivedE> & E,
    const Eigen::MatrixBase<DerivedEMAP> & EMAP,
    const Eigen::MatrixBase<DerivedEF> & EF,
    const int f,
    const int c,
    const Eigen::MatrixBase<Derivedp> & p,
    const int g);












  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedIF,
    typename DerivedEV,
    typename DerivedEE>
  IGL_INLINE void triangle_triangle_intersect(
      const Eigen::MatrixBase<DerivedV> & V1,
      const Eigen::MatrixBase<DerivedF> & F1,
      const Eigen::MatrixBase<DerivedV> & V2,
      const Eigen::MatrixBase<DerivedF> & F2,
      const Eigen::MatrixBase<DerivedIF> & IF,
      Eigen::PlainObjectBase<DerivedEV> & EV,
      Eigen::PlainObjectBase<DerivedEE> & EE);


  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedIF,
    typename DerivedEV,
    typename DerivedEE>
  IGL_INLINE void triangle_triangle_intersect(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedF> & F,
      const Eigen::MatrixBase<DerivedIF> & IF,
      Eigen::PlainObjectBase<DerivedEV> & EV,
      Eigen::PlainObjectBase<DerivedEE> & EE);
}

#ifndef IGL_STATIC_LIBRARY
#  include "triangle_triangle_intersect.cpp"
#endif

#endif
