#ifndef IGL_FAST_WINDING_NUMBER
#define IGL_FAST_WINDING_NUMBER
#include "igl_inline.h"
#include "FastWindingNumberForSoups.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{


























  template <
    typename DerivedP,
    typename DerivedA,
    typename DerivedN,
    typename Index,
    typename DerivedCH,
    typename DerivedCM,
    typename DerivedR,
    typename DerivedEC>
  IGL_INLINE void fast_winding_number(
    const Eigen::MatrixBase<DerivedP>& P,
    const Eigen::MatrixBase<DerivedN>& N,
    const Eigen::MatrixBase<DerivedA>& A,
    const std::vector<std::vector<Index> > & point_indices,
    const Eigen::MatrixBase<DerivedCH>& CH,
    const int expansion_order,
    Eigen::PlainObjectBase<DerivedCM>& CM,
    Eigen::PlainObjectBase<DerivedR>& R,
    Eigen::PlainObjectBase<DerivedEC>& EC);






















  template <
    typename DerivedP,
    typename DerivedA,
    typename DerivedN,
    typename Index,
    typename DerivedCH,
    typename DerivedCM,
    typename DerivedR,
    typename DerivedEC,
    typename DerivedQ,
    typename BetaType,
    typename DerivedWN>
  IGL_INLINE void fast_winding_number(
    const Eigen::MatrixBase<DerivedP>& P,
    const Eigen::MatrixBase<DerivedN>& N,
    const Eigen::MatrixBase<DerivedA>& A,
    const std::vector<std::vector<Index> > & point_indices,
    const Eigen::MatrixBase<DerivedCH>& CH,
    const Eigen::MatrixBase<DerivedCM>& CM,
    const Eigen::MatrixBase<DerivedR>& R,
    const Eigen::MatrixBase<DerivedEC>& EC,
    const Eigen::MatrixBase<DerivedQ>& Q,
    const BetaType beta,
    Eigen::PlainObjectBase<DerivedWN>& WN);




  template <
    typename DerivedP,
    typename DerivedA,
    typename DerivedN,
    typename DerivedQ,
    typename BetaType,
    typename DerivedWN>
  IGL_INLINE void fast_winding_number(
    const Eigen::MatrixBase<DerivedP>& P,
    const Eigen::MatrixBase<DerivedN>& N,
    const Eigen::MatrixBase<DerivedA>& A,
    const Eigen::MatrixBase<DerivedQ>& Q,
    const int expansion_order,
    const BetaType beta,
    Eigen::PlainObjectBase<DerivedWN>& WN);

  template <
    typename DerivedP,
    typename DerivedA,
    typename DerivedN,
    typename DerivedQ,
    typename DerivedWN>
  IGL_INLINE void fast_winding_number(
    const Eigen::MatrixBase<DerivedP>& P,
    const Eigen::MatrixBase<DerivedN>& N,
    const Eigen::MatrixBase<DerivedA>& A,
    const Eigen::MatrixBase<DerivedQ>& Q,
    Eigen::PlainObjectBase<DerivedWN>& WN);

  namespace FastWindingNumber {

    namespace HDK_Sample{

      template <typename T1, typename T2> class UT_SolidAngle;} }


  struct FastWindingNumberBVH {

    FastWindingNumber::HDK_Sample::UT_SolidAngle<float,float> ut_solid_angle;


    std::vector<FastWindingNumber::HDK_Sample::UT_Vector3T<float> > U;
    std::vector<int> F;
  };







  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedQ,
    typename DerivedW>
  IGL_INLINE void fast_winding_number(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<DerivedQ> & Q,
    Eigen::PlainObjectBase<DerivedW> & W);








  template <
    typename DerivedV,
    typename DerivedF>
  IGL_INLINE void fast_winding_number(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const int order,
    FastWindingNumberBVH & fwn_bvh);







  template <
    typename DerivedQ,
    typename DerivedW>
  IGL_INLINE void fast_winding_number(
    const FastWindingNumberBVH & fwn_bvh,
    const float accuracy_scale,
    const Eigen::MatrixBase<DerivedQ> & Q,
    Eigen::PlainObjectBase<DerivedW> & W);






  template <typename Derivedp>
  IGL_INLINE typename Derivedp::Scalar fast_winding_number(
    const FastWindingNumberBVH & fwn_bvh,
    const float accuracy_scale,
    const Eigen::MatrixBase<Derivedp> & p);
}
#ifndef IGL_STATIC_LIBRARY
#  include "fast_winding_number.cpp"
#endif

#endif
