#ifndef IGL_OFFSET_SURFACE_H
#define IGL_OFFSET_SURFACE_H
#include "igl_inline.h"
#include "signed_distance.h"
#include <Eigen/Core>

namespace igl
{
















  template <
    typename DerivedV,
    typename DerivedF,
    typename isolevelType,
    typename DerivedSV,
    typename DerivedSF,
    typename DerivedGV,
    typename Derivedside,
    typename DerivedS>
  void offset_surface(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const isolevelType isolevel,
    const typename Derivedside::Scalar s,
    const SignedDistanceType & signed_distance_type,
    Eigen::PlainObjectBase<DerivedSV> & SV,
    Eigen::PlainObjectBase<DerivedSF> & SF,
    Eigen::PlainObjectBase<DerivedGV> & GV,
    Eigen::PlainObjectBase<Derivedside> & side,
    Eigen::PlainObjectBase<DerivedS> & S);

}
#ifndef IGL_STATIC_LIBRARY
#  include "offset_surface.cpp"
#endif
#endif
