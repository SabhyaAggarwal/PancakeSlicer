#ifndef IGL_QUADPROG_H
#define IGL_QUADPROG_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{



















  template <typename Scalar, int n, int ni>
  IGL_INLINE Eigen::Matrix<Scalar,n,1> quadprog(
    const Eigen::Matrix<Scalar,n,n> & H,
    const Eigen::Matrix<Scalar,n,1> & f,
    const Eigen::Matrix<Scalar,ni,n> & Ai,
    const Eigen::Matrix<Scalar,ni,1> & lbi,
    const Eigen::Matrix<Scalar,ni,1> & ubi,
    const Eigen::Matrix<Scalar,n,1> & lb,
    const Eigen::Matrix<Scalar,n,1> & ub);


















  template <typename Scalar, int n, int m>
  IGL_INLINE Eigen::Matrix<Scalar,n,1> quadprog(
    const Eigen::Matrix<Scalar,n,n> & H,
    const Eigen::Matrix<Scalar,n,1> & f,
    const Eigen::Matrix<Scalar,m,n> & A,
    const Eigen::Matrix<Scalar,m,1> & b,
    const Eigen::Matrix<Scalar,n,1> & lb,
    const Eigen::Matrix<Scalar,n,1> & ub);














  template <typename Scalar, int n>
  IGL_INLINE Eigen::Matrix<Scalar,n,1> quadprog(
    const Eigen::Matrix<Scalar,n,n> & H,
    const Eigen::Matrix<Scalar,n,1> & f,
    const Eigen::Matrix<Scalar,n,1> & lb,
    const Eigen::Matrix<Scalar,n,1> & ub);
}

#ifndef IGL_STATIC_LIBRARY
#  include "quadprog.cpp"
#endif

#endif
