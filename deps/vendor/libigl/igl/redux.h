// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2023 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_REDUX_H
#define IGL_REDUX_H
#include <Eigen/Core>
#include <Eigen/Sparse>
namespace igl
{



















  template <typename AType, typename Func, typename DerivedB>
  inline void redux(
    const Eigen::SparseMatrix<AType> & A,
    const int dim,
    const Func & func,
    Eigen::PlainObjectBase<DerivedB> & B);
}



#include "for_each.h"

template <typename AType, typename Func, typename DerivedB>
inline void igl::redux(
  const Eigen::SparseMatrix<AType> & A,
  const int dim,
  const Func & func,
  Eigen::PlainObjectBase<DerivedB> & B)
{
  typedef typename Eigen::SparseMatrix<AType>::StorageIndex Index;
  assert((dim == 1 || dim == 2) && "dim must be 2 or 1");

  int m = A.rows();
  int n = A.cols();

  B = DerivedB::Zero(dim==1?n:m);
  const auto func_wrap = [&func,&B,&dim](const Index i, const Index j, const AType v)
  {
    if(dim == 1)
    {
      B(j) = i == 0? v : func(B(j),v);
    }else
    {
      B(i) = j == 0? v : func(B(i),v);
    }
  };
  for_each(A,func_wrap);
}





#endif
