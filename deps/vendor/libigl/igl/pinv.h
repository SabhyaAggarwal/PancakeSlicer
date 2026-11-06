#ifndef IGL_PINV_H
#define IGL_PINV_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{









  template <typename DerivedA, typename DerivedX>
  void pinv(
    const Eigen::MatrixBase<DerivedA> & A,
    typename DerivedA::Scalar tol,
    Eigen::PlainObjectBase<DerivedX> & X);

  template <typename DerivedA, typename DerivedX>
  void pinv(
    const Eigen::MatrixBase<DerivedA> & A,
    Eigen::PlainObjectBase<DerivedX> & X);
}
#ifndef IGL_STATIC_LIBRARY
#  include "pinv.cpp"
#endif
#endif
