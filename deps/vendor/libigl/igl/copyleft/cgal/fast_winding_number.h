#ifndef IGL_COPYLEFT_CGAL_FAST_WINDING_NUMBER
#define IGL_COPYLEFT_CGAL_FAST_WINDING_NUMBER
#include "../../igl_inline.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {

















      template <
        typename DerivedP,
        typename DerivedN,
        typename DerivedQ,
        typename BetaType,
        typename DerivedWN>
      IGL_INLINE void fast_winding_number(
        const Eigen::MatrixBase<DerivedP>& P,
        const Eigen::MatrixBase<DerivedN>& N,
        const Eigen::MatrixBase<DerivedQ>& Q,
        const int expansion_order,
        const BetaType beta,
        Eigen::PlainObjectBase<DerivedWN>& WN);

      template <
        typename DerivedP,
        typename DerivedN,
        typename DerivedQ,
        typename DerivedWN>
      IGL_INLINE void fast_winding_number(
        const Eigen::MatrixBase<DerivedP>& P,
        const Eigen::MatrixBase<DerivedN>& N,
        const Eigen::MatrixBase<DerivedQ>& Q,
        Eigen::PlainObjectBase<DerivedWN>& WN);
    }
  }
}
#ifndef IGL_STATIC_LIBRARY
#  include "fast_winding_number.cpp"
#endif

#endif
