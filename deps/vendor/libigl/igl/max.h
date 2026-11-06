#ifndef IGL_MAX_H
#define IGL_MAX_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Sparse>
namespace igl
{










  template <typename AType, typename DerivedB, typename DerivedI>
  IGL_INLINE void max(
    const Eigen::SparseMatrix<AType> & A,
    const int dim,
    Eigen::PlainObjectBase<DerivedB> & B,
    Eigen::PlainObjectBase<DerivedI> & I);

  template <typename DerivedX, typename DerivedY, typename DerivedI>
  IGL_INLINE void max(
    const Eigen::DenseBase<DerivedX> & X,
    const int dim,
    Eigen::PlainObjectBase<DerivedY> & Y,
    Eigen::PlainObjectBase<DerivedI> & I);
}
#ifndef IGL_STATIC_LIBRARY
#  include "max.cpp"
#endif
#endif
