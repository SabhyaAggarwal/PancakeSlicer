#ifndef IGL_PROJECTION_CONSTRAINT_H
#define IGL_PROJECTION_CONSTRAINT_H

#include <Eigen/Core>

namespace igl
{














  template <
    typename DerivedUV,
    typename DerivedM,
    typename DerivedVP,
    typename DerivedA,
    typename DerivedB>
  void projection_constraint(
    const Eigen::MatrixBase<DerivedUV> & UV,
    const Eigen::MatrixBase<DerivedM> & M,
    const Eigen::MatrixBase<DerivedVP> & VP,
    Eigen::PlainObjectBase<DerivedA> & A,
    Eigen::PlainObjectBase<DerivedB> & B);
}

#ifndef IGL_STATIC_LIBRARY
#  include "projection_constraint.cpp"
#endif

#endif
