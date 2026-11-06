#ifndef IGL_TURNING_NUMBER_H
#define IGL_TURNING_NUMBER_H
#include <Eigen/Core>
#include "igl_inline.h"

namespace igl
{





  template <typename DerivedV>
  IGL_INLINE typename DerivedV::Scalar turning_number(
    const Eigen::MatrixBase<DerivedV> & V);
}

#ifndef IGL_STATIC_LIBRARY
#  include "turning_number.cpp"
#endif

#endif
