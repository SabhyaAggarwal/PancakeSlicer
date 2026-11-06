#ifndef IGL_RANDOM_SEARCH_H
#define IGL_RANDOM_SEARCH_H
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
  IGL_INLINE Scalar random_search(
    const std::function< Scalar (DerivedX &) > f,
    const Eigen::MatrixBase<DerivedLB> & LB,
    const Eigen::MatrixBase<DerivedUB> & UB,
    const int iters,
    DerivedX & X);
}

#ifndef IGL_STATIC_LIBRARY
#  include "random_search.cpp"
#endif

#endif
