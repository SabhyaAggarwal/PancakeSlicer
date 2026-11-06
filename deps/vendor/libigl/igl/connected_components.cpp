// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2020 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#include "connected_components.h"
#include <queue>

template < typename Atype, typename DerivedC, typename DerivedK>
IGL_INLINE int igl::connected_components(
  const Eigen::SparseMatrix<Atype> & A,
  Eigen::PlainObjectBase<DerivedC> & C,
  Eigen::PlainObjectBase<DerivedK> & K)
{
  typedef typename Eigen::SparseMatrix<Atype>::Index Index;
  const auto m = A.rows();
  assert(A.cols() == A.rows() && "A should be square");


  C.setConstant(m,1,m);

  K.setZero(m,1);
  typename DerivedC::Scalar c = 0;
  for(Eigen::Index f = 0;f<m;f++)
  {

    if(C(f)<m) continue;

    std::queue<Index> Q;
    Q.push(f);
    while(!Q.empty())
    {
      const Index g = Q.front();
      Q.pop();

      if(C(g)<m) continue;

      C(g) = c;
      K(c)++;
      for(typename Eigen::SparseMatrix<Atype>::InnerIterator it (A,g); it; ++it)
      {
        const Index n = it.index();

        if(C(n)<m) continue;
        Q.push(n);
      }
    }
    c++;
  }
  K.conservativeResize(c,1);
  return c;
}

#ifdef IGL_STATIC_LIBRARY


template int igl::connected_components<bool, Eigen::Matrix<int, -1, 1, 0, -1, 1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::SparseMatrix<bool, 0, int> const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&);

template int igl::connected_components<int, Eigen::Matrix<int, -1, 1, 0, -1, 1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::SparseMatrix<int, 0, int> const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&);
#endif
