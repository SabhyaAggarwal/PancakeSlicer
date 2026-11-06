#ifndef IGL_UNPROJECT_ON_LINE_H
#define IGL_UNPROJECT_ON_LINE_H

#include <Eigen/Dense>

namespace igl
{












  template <
    typename DerivedUV,
    typename DerivedM,
    typename DerivedVP,
    typename Derivedorigin,
    typename Deriveddir>
  void unproject_on_line(
    const Eigen::MatrixBase<DerivedUV> & UV,
    const Eigen::MatrixBase<DerivedM> & M,
    const Eigen::MatrixBase<DerivedVP> & VP,
    const Eigen::MatrixBase<Derivedorigin> & origin,
    const Eigen::MatrixBase<Deriveddir> & dir,
    typename DerivedUV::Scalar & t);


  template <
    typename DerivedUV,
    typename DerivedM,
    typename DerivedVP,
    typename Derivedorigin,
    typename Deriveddir,
    typename DerivedZ>
  void unproject_on_line(
    const Eigen::MatrixBase<DerivedUV> & UV,
    const Eigen::MatrixBase<DerivedM> & M,
    const Eigen::MatrixBase<DerivedVP> & VP,
    const Eigen::MatrixBase<Derivedorigin> & origin,
    const Eigen::MatrixBase<Deriveddir> & dir,
    Eigen::PlainObjectBase<DerivedZ> & Z);
}

#ifndef IGL_STATIC_LIBRARY
#  include "unproject_on_line.cpp"
#endif

#endif
