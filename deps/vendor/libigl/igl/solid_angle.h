#ifndef IGL_SOLID_ANGLE_H
#define IGL_SOLID_ANGLE_H
#include "igl_inline.h"
#include <Eigen/Dense>
namespace igl
{







  template <
    typename DerivedA,
    typename DerivedB,
    typename DerivedC,
    typename DerivedP>
  IGL_INLINE typename DerivedA::Scalar solid_angle(
    const Eigen::MatrixBase<DerivedA> & A,
    const Eigen::MatrixBase<DerivedB> & B,
    const Eigen::MatrixBase<DerivedC> & C,
    const Eigen::MatrixBase<DerivedP> & P);
}
#ifndef IGL_STATIC_LIBRARY
#  include "solid_angle.cpp"
#endif
#endif
