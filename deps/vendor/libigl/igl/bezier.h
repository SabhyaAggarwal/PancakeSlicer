#ifndef BEZIER_H
#define BEZIER_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{





  template <typename DerivedV, typename DerivedP>
  IGL_INLINE void bezier(
    const Eigen::MatrixBase<DerivedV> & V,
    const typename DerivedV::Scalar t,
    Eigen::PlainObjectBase<DerivedP> & P);





  template <typename DerivedV, typename DerivedT, typename DerivedP>
  IGL_INLINE void bezier(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedT> & T,
    Eigen::PlainObjectBase<DerivedP> & P);









  template <typename VMat, typename DerivedT, typename DerivedP>
  IGL_INLINE void bezier(
    const std::vector<VMat> & spline,
    const Eigen::MatrixBase<DerivedT> & T,
    Eigen::PlainObjectBase<DerivedP> & P);
}

#ifndef IGL_STATIC_LIBRARY
#  include "bezier.cpp"
#endif

#endif
