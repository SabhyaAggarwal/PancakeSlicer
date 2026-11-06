#ifndef IGL_PSO_H
#define IGL_PSO_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <functional>

namespace igl
{
















  template <
    typename Scalar,
    typename DerivedX,
    typename DerivedLB,
    typename DerivedUB>
  IGL_INLINE Scalar pso(
    const std::function< Scalar (DerivedX &) > f,
    const Eigen::MatrixBase<DerivedLB> & LB,
    const Eigen::MatrixBase<DerivedUB> & UB,
    const int max_iters,
    const int population,
    DerivedX & X);




  template <
    typename Scalar,
    typename DerivedX,
    typename DerivedLB,
    typename DerivedUB,
    typename DerivedP>
  IGL_INLINE Scalar pso(
    const std::function< Scalar (DerivedX &) > f,
    const Eigen::MatrixBase<DerivedLB> & LB,
    const Eigen::MatrixBase<DerivedUB> & UB,
    const Eigen::DenseBase<DerivedP> & P,
    const int max_iters,
    const int population,
    DerivedX & X);
}

#ifndef IGL_STATIC_LIBRARY
#  include "pso.cpp"
#endif

#endif
