#ifndef IGL_COPYLEFT_CGAL_WIRE_MESH_H
#define IGL_COPYLEFT_CGAL_WIRE_MESH_H
#include "../../igl_inline.h"
#include <Eigen/Core>
namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {
















      template <
        typename DerivedWV,
        typename DerivedWE,
        typename Derivedth,
        typename DerivedV,
        typename DerivedF,
        typename DerivedJ>
      IGL_INLINE void wire_mesh(
        const Eigen::MatrixBase<DerivedWV> & WV,
        const Eigen::MatrixBase<DerivedWE> & WE,
        const Eigen::MatrixBase<Derivedth> & th,
        const int poly_size,
        const bool solid,
        Eigen::PlainObjectBase<DerivedV> & V,
        Eigen::PlainObjectBase<DerivedF> & F,
        Eigen::PlainObjectBase<DerivedJ> & J);


      template <
        typename DerivedWV,
        typename DerivedWE,
        typename DerivedV,
        typename DerivedF,
        typename DerivedJ>
      IGL_INLINE void wire_mesh(
        const Eigen::MatrixBase<DerivedWV> & WV,
        const Eigen::MatrixBase<DerivedWE> & WE,
        const double th,
        const int poly_size,
        const bool solid,
        Eigen::PlainObjectBase<DerivedV> & V,
        Eigen::PlainObjectBase<DerivedF> & F,
        Eigen::PlainObjectBase<DerivedJ> & J);


      template <
        typename DerivedWV,
        typename DerivedWE,
        typename DerivedV,
        typename DerivedF,
        typename DerivedJ>
      IGL_INLINE void wire_mesh(
        const Eigen::MatrixBase<DerivedWV> & WV,
        const Eigen::MatrixBase<DerivedWE> & WE,
        const double th,
        const int poly_size,
        Eigen::PlainObjectBase<DerivedV> & V,
        Eigen::PlainObjectBase<DerivedF> & F,
        Eigen::PlainObjectBase<DerivedJ> & J);

    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "wire_mesh.cpp"
#endif
#endif
