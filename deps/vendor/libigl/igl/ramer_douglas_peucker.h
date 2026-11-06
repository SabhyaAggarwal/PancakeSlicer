#ifndef IGL_RAMER_DOUGLAS_PEUCKER_H
#define IGL_RAMER_DOUGLAS_PEUCKER_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{







  template <typename DerivedP, typename DerivedS, typename DerivedJ>
  IGL_INLINE void ramer_douglas_peucker(
    const Eigen::MatrixBase<DerivedP> & P,
    const typename DerivedP::Scalar tol,
    Eigen::PlainObjectBase<DerivedS> & S,
    Eigen::PlainObjectBase<DerivedJ> & J);





  template <
    typename DerivedP,
    typename DerivedS,
    typename DerivedJ,
    typename DerivedQ>
  IGL_INLINE void ramer_douglas_peucker(
    const Eigen::MatrixBase<DerivedP> & P,
    const typename DerivedP::Scalar tol,
    Eigen::PlainObjectBase<DerivedS> & S,
    Eigen::PlainObjectBase<DerivedJ> & J,
    Eigen::PlainObjectBase<DerivedQ> & Q);

}
#ifndef IGL_STATIC_LIBRARY
#  include "ramer_douglas_peucker.cpp"
#endif
#endif
