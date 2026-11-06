#include "internal_angles_intrinsic.h"
#include "parallel_for.h"

template <typename DerivedL, typename DerivedK>
IGL_INLINE void igl::internal_angles_intrinsic(
  const Eigen::MatrixBase<DerivedL>& L_sq,
  Eigen::PlainObjectBase<DerivedK> & K)
{
  typedef typename DerivedL::Index Index;
  assert(L_sq.cols() == 3 && "Edge-lengths should come from triangles");
  const Index m = L_sq.rows();
  K.resize(m,3);
  parallel_for(
    m,
    [&L_sq,&K](const Index f)
    {
      for(size_t d = 0;d<3;d++)
      {
        const auto & s1 = L_sq(f,d);
        const auto & s2 = L_sq(f,(d+1)%3);
        const auto & s3 = L_sq(f,(d+2)%3);
        using Scalar = typename DerivedK::Scalar;
        K(f,d) = std::acos((s3 + s2 - s1)/(Scalar(2)*std::sqrt(s3*s2)));



























      }
    },
    1000l);
}

#ifdef IGL_STATIC_LIBRARY


template void igl::internal_angles_intrinsic<Eigen::Matrix<float, -1, 1, 0, -1, 1>, Eigen::Matrix<float, -1, -1, 0, -1, -1>>(Eigen::MatrixBase<Eigen::Matrix<float, -1, 1, 0, -1, 1>> const&, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, -1, 0, -1, -1>>&);

template void igl::internal_angles_intrinsic<Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>>(Eigen::MatrixBase<Eigen::Matrix<double, -1, 1, 0, -1, 1>> const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>>&);

template void igl::internal_angles_intrinsic<Eigen::Matrix<float, -1, -1, 0, -1, -1>, Eigen::Matrix<float, -1, -1, 0, -1, -1>>(Eigen::MatrixBase<Eigen::Matrix<float, -1, -1, 0, -1, -1>> const&, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, -1, 0, -1, -1>>&);

template void igl::internal_angles_intrinsic<Eigen::Matrix<float, -1, 3, 0, -1, 3>, Eigen::Matrix<float, -1, 3, 0, -1, 3>>(Eigen::MatrixBase<Eigen::Matrix<float, -1, 3, 0, -1, 3>> const&, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, 3, 0, -1, 3>>&);

template void igl::internal_angles_intrinsic<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, 3, 0, -1, 3>>(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>> const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 3, 0, -1, 3>>&);

template void igl::internal_angles_intrinsic<Eigen::Matrix<double, -1, 3, 0, -1, 3>, Eigen::Matrix<double, -1, 3, 0, -1, 3>>(Eigen::MatrixBase<Eigen::Matrix<double, -1, 3, 0, -1, 3>> const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 3, 0, -1, 3>>&);

template void igl::internal_angles_intrinsic<Eigen::Matrix<double, -1, 3, 0, -1, 3>, Eigen::Matrix<double, -1, -1, 0, -1, -1>>(Eigen::MatrixBase<Eigen::Matrix<double, -1, 3, 0, -1, 3>> const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>>&);

template void igl::internal_angles_intrinsic<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&);
template void igl::internal_angles_intrinsic<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> >&);
#endif
