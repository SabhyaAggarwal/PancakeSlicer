#ifndef IGL_COPYLEFT_CGAL_HALF_SPACE_BOX_H
#define IGL_COPYLEFT_CGAL_HALF_SPACE_BOX_H
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








      template <typename DerivedV, typename Index>
      IGL_INLINE void half_space_box(
        const CGAL::Plane_3<CGAL::Epeck> & P,
        const Eigen::MatrixBase<DerivedV> & V,
        Eigen::Matrix<CGAL::Epeck::FT,8,3> & BV,
        Eigen::Matrix<Index,12,3> & BF);



      template <typename Derivedp, typename Derivedn, typename DerivedV, typename Index>
      IGL_INLINE void half_space_box(
        const Eigen::MatrixBase<Derivedp> & p,
        const Eigen::MatrixBase<Derivedn> & n,
        const Eigen::MatrixBase<DerivedV> & V,
        Eigen::Matrix<CGAL::Epeck::FT,8,3> & BV,
        Eigen::Matrix<Index,12,3> & BF);


      template <typename Derivedequ, typename DerivedV, typename Index>
      IGL_INLINE void half_space_box(
        const Eigen::MatrixBase<Derivedequ> & equ,
        const Eigen::MatrixBase<DerivedV> & V,
        Eigen::Matrix<CGAL::Epeck::FT,8,3> & BV,
        Eigen::Matrix<Index,12,3> & BF);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "half_space_box.cpp"
#endif
#endif
