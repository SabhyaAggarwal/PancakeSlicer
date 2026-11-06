#ifndef IGL_TRIANGLE_CDT_H
#define IGL_TRIANGLE_CDT_H

#include "../igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  namespace triangle
  {
















    template <
      typename DerivedV,
      typename DerivedE,
      typename DerivedWV,
      typename DerivedWF,
      typename DerivedWE,
      typename DerivedJ>
    IGL_INLINE void cdt(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedE> & E,
      const std::string & flags,
      Eigen::PlainObjectBase<DerivedWV> & WV,
      Eigen::PlainObjectBase<DerivedWF> & WF,
      Eigen::PlainObjectBase<DerivedWE> & WE,
      Eigen::PlainObjectBase<DerivedJ> & J);
  }
}

#ifndef IGL_STATIC_LIBRARY
#include "cdt.cpp"
#endif
#endif
