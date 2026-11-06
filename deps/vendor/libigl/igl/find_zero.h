#ifndef IGL_FIND_ZERO_H
#define IGL_FIND_ZERO_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Sparse>
namespace igl
{








  template <typename AType, typename DerivedI>
  IGL_INLINE void find_zero(
    const Eigen::SparseMatrix<AType> & A,
    const int dim,
    Eigen::PlainObjectBase<DerivedI> & I);
}
#ifndef IGL_STATIC_LIBRARY
#  include "find_zero.cpp"
#endif
#endif
