#include "find_zero.h"
#include "for_each.h"
#include "any.h"

template <typename AType, typename DerivedI>
IGL_INLINE void igl::find_zero(
  const Eigen::SparseMatrix<AType> & A,
  const int dim,
  Eigen::PlainObjectBase<DerivedI> & I)
{
  assert((dim == 1 || dim == 2) && "dim must be 2 or 1");

  int m = A.rows();
  int n = A.cols();

  I = DerivedI::Zero(dim==1?n:m);
  Eigen::Array<bool,Eigen::Dynamic,1> found =
    Eigen::Array<bool,Eigen::Dynamic,1>::Zero(dim==1?n:m);
  const auto func = [&I,&found,&dim](int i, int j, const int v)
  {
    if(dim == 2)
    {
      std::swap(i,j);
    }


    if(!found(j))
    {
      if(I(j) != i || v == 0)
      {


        found(j) = true;
      }else
      {

        I(j) = I(j)+1;
      }
    }
  };
  for_each(A,func);
}

#ifdef IGL_STATIC_LIBRARY


template void igl::find_zero<bool, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::SparseMatrix<bool, 0, int> const&, int, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&);
#endif
