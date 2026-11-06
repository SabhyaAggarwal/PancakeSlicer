#ifndef IGL_DUAL_CONTOURING_H
#define IGL_DUAL_CONTOURING_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Dense>
#include <functional>
namespace igl
{




















  template <
    typename DerivedV,
    typename DerivedQ>
  IGL_INLINE void dual_contouring(
    const std::function<
      typename DerivedV::Scalar(const Eigen::Matrix<typename DerivedV::Scalar,1,3> &)> & f,
    const std::function<
      Eigen::Matrix<typename DerivedV::Scalar,1,3>(
        const Eigen::Matrix<typename DerivedV::Scalar,1,3> &)> & f_grad,
    const Eigen::Matrix<typename DerivedV::Scalar,1,3> & min_corner,
    const Eigen::Matrix<typename DerivedV::Scalar,1,3> & max_corner,
    const int nx,
    const int ny,
    const int nz,
    const bool constrained,
    const bool triangles,
    const bool root_finding,
    Eigen::PlainObjectBase<DerivedV> & V,
    Eigen::PlainObjectBase<DerivedQ> & Q);






  template <
    typename DerivedGf,
    typename DerivedGV,
    typename DerivedV,
    typename DerivedQ>
  IGL_INLINE void dual_contouring(
    const std::function<
      typename DerivedV::Scalar(const Eigen::Matrix<typename DerivedV::Scalar,1,3> &)> & f,
    const std::function<
      Eigen::Matrix<typename DerivedV::Scalar,1,3>(
        const Eigen::Matrix<typename DerivedV::Scalar,1,3> &)> & f_grad,
    const Eigen::MatrixBase<DerivedGf> & Gf,
    const Eigen::MatrixBase<DerivedGV> & GV,
    const int nx,
    const int ny,
    const int nz,
    const bool constrained,
    const bool triangles,
    const bool root_finding,
    Eigen::PlainObjectBase<DerivedV> & V,
    Eigen::PlainObjectBase<DerivedQ> & Q);







  template <
    typename DerivedGf,
    typename DerivedGV,
    typename DerivedGI,
    typename DerivedV,
    typename DerivedQ>
  IGL_INLINE void dual_contouring(
    const std::function<typename DerivedV::Scalar(const Eigen::Matrix<typename DerivedV::Scalar,1,3> &)> & f,
    const std::function<Eigen::Matrix<typename DerivedV::Scalar,1,3>(const Eigen::Matrix<typename DerivedV::Scalar,1,3> &)> & f_grad,
    const Eigen::Matrix<typename DerivedV::Scalar,1,3> & step,
    const Eigen::MatrixBase<DerivedGf> & Gf,
    const Eigen::MatrixBase<DerivedGV> & GV,
    const Eigen::MatrixBase<DerivedGI> & GI,
    const bool constrained,
    const bool triangles,
    const bool root_finding,
    Eigen::PlainObjectBase<DerivedV> & V,
    Eigen::PlainObjectBase<DerivedQ> & Q);
}

#ifndef IGL_STATIC_LIBRARY
#  include "dual_contouring.cpp"
#endif
#endif
