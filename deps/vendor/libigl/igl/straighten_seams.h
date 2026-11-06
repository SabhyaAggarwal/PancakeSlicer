#ifndef IGL_STRAIGHTEN_SEAMS_H
#define IGL_STRAIGHTEN_SEAMS_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{




















  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedVT,
    typename DerivedFT,
    typename Scalar,
    typename DerivedUE,
    typename DerivedUT,
    typename DerivedOT>
  IGL_INLINE void straighten_seams(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<DerivedVT> & VT,
    const Eigen::MatrixBase<DerivedFT> & FT,
    const Scalar tol,
    Eigen::PlainObjectBase<DerivedUE> & UE,
    Eigen::PlainObjectBase<DerivedUT> & UT,
    Eigen::PlainObjectBase<DerivedOT> & OT);
}

#ifndef IGL_STATIC_LIBRARY
#  include "straighten_seams.cpp"
#endif

#endif
