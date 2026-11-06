// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "intersect.h"
template <class M>
IGL_INLINE void igl::intersect(const M & A, const M & B, M & C)
{


  M dyn_C(A.size() > B.size() ? A.size() : B.size(),1);

  int c = 0;

  for(int i = 0;i<A.size();i++)
  {

    for(int j = 0;j<B.size();j++)
    {
      if(A(i) == B(j))
      {
        dyn_C(c) = A(i);
        c++;
      }
    }
  }


  C.resize(c,1);

  for(int i = 0;i<c;i++)
  {
    C(i) = dyn_C(i);
  }
}

template <class M>
IGL_INLINE M igl::intersect(const M & A, const M & B)
{
  M C;
  intersect(A,B,C);
  return C;
}
#ifdef IGL_STATIC_LIBRARY

template Eigen::Matrix<int, -1, 1, 0, -1, 1> igl::intersect<Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&);
#endif
