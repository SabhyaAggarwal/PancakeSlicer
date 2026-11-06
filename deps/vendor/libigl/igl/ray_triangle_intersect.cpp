#include "ray_triangle_intersect.h"
#include <Eigen/Geometry>
#include <type_traits>

namespace
{

  template <typename Derived>
  void assert_3_vector(const Eigen::MatrixBase<Derived>& mat) {
      static_assert(
          (Derived::RowsAtCompileTime == 3 && Derived::ColsAtCompileTime == 1) ||
          (Derived::RowsAtCompileTime == 1 && Derived::ColsAtCompileTime == 3) ||
          (Derived::RowsAtCompileTime == Eigen::Dynamic && Derived::ColsAtCompileTime == 1) ||
          (Derived::RowsAtCompileTime == 1 && Derived::ColsAtCompileTime == Eigen::Dynamic),
          "Matrix must be 3x1, 1x3, Eigen::Dynamic×1, or 1×Eigen::Dynamic.");


      assert((mat.rows() == 3 && mat.cols() == 1) ||
             (mat.rows() == 1 && mat.cols() == 3));
  }
}

template <
  typename DerivedO,
  typename DerivedD,
  typename DerivedV0,
  typename DerivedV1,
  typename DerivedV2>
IGL_INLINE bool igl::ray_triangle_intersect(
   const Eigen::MatrixBase<DerivedO>  & _O,
   const Eigen::MatrixBase<DerivedD>  & _D,
   const Eigen::MatrixBase<DerivedV0> & _V0,
   const Eigen::MatrixBase<DerivedV1> & _V1,
   const Eigen::MatrixBase<DerivedV2> & _V2,
   const typename DerivedO::Scalar epsilon,
   typename DerivedO::Scalar & t,
   typename DerivedO::Scalar & u,
   typename DerivedO::Scalar & v,
   bool & parallel)
{

  assert_3_vector(_O);
  assert_3_vector(_D);
  assert_3_vector(_V0);
  assert_3_vector(_V1);
  assert_3_vector(_V2);
  using Scalar = typename DerivedO::Scalar;
  static_assert(std::is_same<Scalar, typename DerivedD::Scalar>::value,
    "O and D must have the same scalar type");
  static_assert(std::is_same<Scalar, typename DerivedV0::Scalar>::value,
    "O and V0 must have the same scalar type");
  static_assert(std::is_same<Scalar, typename DerivedV1::Scalar>::value,
    "O and V1 must have the same scalar type");
  static_assert(std::is_same<Scalar, typename DerivedV2::Scalar>::value,
    "O and V2 must have the same scalar type");
  const auto  O = _O.template head<3>();
  const auto  D = _D.template head<3>();
  const auto V0 = _V0.template head<3>();
  const auto V1 = _V1.template head<3>();
  const auto V2 = _V2.template head<3>();





  const auto edge1 = (V1-V0).eval();
  const auto edge2 = (V2-V0).eval();
  const auto pvec = D.cross(edge2).eval();
  const Scalar det = edge1.dot(pvec);
  if( det > -epsilon && det < epsilon )
  {
    parallel = true;
    return false;
  }
  parallel = false;
  const Scalar inv_det = 1.0 / det;
  const auto tvec = (O - V0).eval();
  u = tvec.dot(pvec) * inv_det;
  if( u < 0.0-epsilon || u > 1.0+epsilon )
  {
    return false;
  }
  const auto qvec = tvec.cross(edge1).eval();
  v = D.dot(qvec) * inv_det;
  if( v < 0.0-epsilon || u + v > 1.0+epsilon )
  {
    return false;
  }
  t = edge2.dot(qvec) * inv_det;
  return true;
}

#ifdef IGL_STATIC_LIBRARY


template bool igl::ray_triangle_intersect<Eigen::Matrix<double, 1, 3, 1, 1, 3>, Eigen::Matrix<double, 1, 3, 1, 1, 3>, Eigen::Matrix<double, 1, 3, 1, 1, 3>, Eigen::Matrix<double, 1, 3, 1, 1, 3>, Eigen::Matrix<double, 1, 3, 1, 1, 3> >(Eigen::MatrixBase<Eigen::Matrix<double, 1, 3, 1, 1, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 3, 1, 1, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 3, 1, 1, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 3, 1, 1, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 3, 1, 1, 3> > const&, Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar, Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar&, Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar&, Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar&, bool&);
#endif
