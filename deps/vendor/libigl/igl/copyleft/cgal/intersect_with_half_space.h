#ifndef IGL_COPYLEFT_CGAL_INTERSECT_WITH_HALF_SPACE_H
#define IGL_COPYLEFT_CGAL_INTERSECT_WITH_HALF_SPACE_H
#include "../../igl_inline.h"
#include <Eigen/Core>
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Plane_3.h>

namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {











      template <
        typename DerivedV,
        typename DerivedF,
        typename Derivedp,
        typename Derivedn,
        typename DerivedVC,
        typename DerivedFC,
        typename DerivedJ>
      IGL_INLINE bool intersect_with_half_space(
        const Eigen::MatrixBase<DerivedV > & V,
        const Eigen::MatrixBase<DerivedF > & F,
        const Eigen::MatrixBase<Derivedp > & p,
        const Eigen::MatrixBase<Derivedn > & n,
        Eigen::PlainObjectBase<DerivedVC > & VC,
        Eigen::PlainObjectBase<DerivedFC > & FC,
        Eigen::PlainObjectBase<DerivedJ > & J);



      template <
        typename DerivedV,
        typename DerivedF,
        typename Derivedequ,
        typename DerivedVC,
        typename DerivedFC,
        typename DerivedJ>
      IGL_INLINE bool intersect_with_half_space(
        const Eigen::MatrixBase<DerivedV > & V,
        const Eigen::MatrixBase<DerivedF > & F,
        const Eigen::MatrixBase<Derivedequ > & equ,
        Eigen::PlainObjectBase<DerivedVC > & VC,
        Eigen::PlainObjectBase<DerivedFC > & FC,
        Eigen::PlainObjectBase<DerivedJ > & J);


      template <
        typename DerivedV,
        typename DerivedF,
        typename DerivedVC,
        typename DerivedFC,
        typename DerivedJ>
      IGL_INLINE bool intersect_with_half_space(
        const Eigen::MatrixBase<DerivedV > & V,
        const Eigen::MatrixBase<DerivedF > & F,
        const CGAL::Plane_3<CGAL::Epeck> & P,
        Eigen::PlainObjectBase<DerivedVC > & VC,
        Eigen::PlainObjectBase<DerivedFC > & FC,
        Eigen::PlainObjectBase<DerivedJ > & J);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "intersect_with_half_space.cpp"
#endif

#endif
