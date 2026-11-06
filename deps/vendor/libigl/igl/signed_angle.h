#ifndef IGL_SIGNED_ANGLE_H
#define IGL_SIGNED_ANGLE_H
#include "igl_inline.h"
#include <Eigen/Dense>
namespace igl
{






  template <
    typename DerivedA,
    typename DerivedB,
    typename DerivedP>
  IGL_INLINE typename DerivedA::Scalar signed_angle(
    const Eigen::MatrixBase<DerivedA> & A,
    const Eigen::MatrixBase<DerivedB> & B,
    const Eigen::MatrixBase<DerivedP> & P);
}
#ifndef IGL_STATIC_LIBRARY
#  include "signed_angle.cpp"
#endif
#endif
