#ifndef IGL_FOR_EACH_H
#define IGL_FOR_EACH_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Sparse>
namespace igl
{









  template <typename AType, typename Func>
  inline void for_each(
    const Eigen::SparseMatrix<AType> & A,
    const Func & func);

  template <typename DerivedA, typename Func>
  inline void for_each(
    const Eigen::DenseBase<DerivedA> & A,
    const Func & func);
}



template <typename AType, typename Func>
inline void igl::for_each(
  const Eigen::SparseMatrix<AType> & A,
  const Func & func)
{


  for(int k=0; k<A.outerSize(); ++k)
  {

    for(typename Eigen::SparseMatrix<AType>::InnerIterator it (A,k); it; ++it)
    {
      func(it.row(),it.col(),it.value());
    }
  }
}

template <typename DerivedA, typename Func>
inline void igl::for_each(
  const Eigen::DenseBase<DerivedA> & A,
  const Func & func)
{

  if(A.IsRowMajor)
  {
    for(typename DerivedA::Index i = 0;i<A.rows();i++)
    {
      for(typename DerivedA::Index j = 0;j<A.cols();j++)
      {
        func(i,j,A(i,j));
      }
    }
  }else
  {
    for(typename DerivedA::Index j = 0;j<A.cols();j++)
    {
      for(typename DerivedA::Index i = 0;i<A.rows();i++)
      {
        func(i,j,A(i,j));
      }
    }
  }

}




#endif
