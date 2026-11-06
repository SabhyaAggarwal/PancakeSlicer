#include "cdt.h"
#include "../bounding_box.h"
#include "../triangle/triangulate.h"
#include "../remove_duplicate_vertices.h"
#include "../remove_unreferenced.h"
#include "../placeholders.h"

template <
  typename DerivedV,
  typename DerivedE,
  typename DerivedWV,
  typename DerivedWF,
  typename DerivedWE,
  typename DerivedJ>
IGL_INLINE void igl::triangle::cdt(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedE> & E,
  const std::string & flags,
  Eigen::PlainObjectBase<DerivedWV> & WV,
  Eigen::PlainObjectBase<DerivedWF> & WF,
  Eigen::PlainObjectBase<DerivedWE> & WE,
  Eigen::PlainObjectBase<DerivedJ> & J)
{
  assert(V.cols() == 2);
  assert(E.cols() == 2);







  WV = V;
  WE = E;
  Eigen::VectorXi _;
  igl::remove_duplicate_vertices(DerivedWV(WV),DerivedWE(WE),1e-10,WV,_,J,WE);

  const Eigen::Array<bool,Eigen::Dynamic,1> keep = (WE.array().col(0) != WE.array().col(1));
  WE = WE(keep,igl::placeholders::all).eval();

  igl::triangle::triangulate(DerivedWV(WV),WE,DerivedWV(),flags,WV,WF);
  Eigen::VectorXi UJ;
  igl::remove_unreferenced(DerivedV(WV),Eigen::MatrixXi(WF),WV,WF,UJ);
  for(int i=0;i<WE.rows();i++) for(int j=0;j<WE.cols();j++) WE(i,j)=UJ(WE(i,j));


  for(int i=0;i<J.size();i++) J(i)=UJ(J(i));

}

#ifdef IGL_STATIC_LIBRARY


template void igl::triangle::cdt<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 1, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, std::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 1, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&);

template void igl::triangle::cdt<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, std::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&);
#endif
