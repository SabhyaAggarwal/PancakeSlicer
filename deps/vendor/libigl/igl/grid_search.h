#ifndef IGL_GRID_SEARCH_H
#define IGL_GRID_SEARCH_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <functional>
namespace igl
{














  template <
    typename Scalar,
    typename DerivedX,
    typename DerivedLB,
    typename DerivedUB,
    typename DerivedI>
  IGL_INLINE Scalar grid_search(
    const std::function< Scalar (DerivedX &) > f,
    const Eigen::MatrixBase<DerivedLB> & LB,
    const Eigen::MatrixBase<DerivedUB> & UB,
    const Eigen::MatrixBase<DerivedI> & I,
    DerivedX & X);
}

#ifndef IGL_STATIC_LIBRARY
#  include "grid_search.cpp"
#endif

#endif
