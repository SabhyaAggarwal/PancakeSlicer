#ifndef IGL_EARS_H
#define IGL_EARS_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{








  template <
    typename DerivedF,
    typename Derivedear,
    typename Derivedear_opp>
  IGL_INLINE void ears(
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<Derivedear> & ear,
    Eigen::PlainObjectBase<Derivedear_opp> & ear_opp);
}
#ifndef IGL_STATIC_LIBRARY
#  include "ears.cpp"
#endif
#endif
