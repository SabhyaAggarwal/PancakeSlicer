// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "bfs_orient.h"
#include "orientable_patches.h"
#include "parallel_for.h"
#include <Eigen/Sparse>
#include <queue>

template <typename DerivedF, typename DerivedFF, typename DerivedC>
IGL_INLINE void igl::bfs_orient(
  const Eigen::MatrixBase<DerivedF> & F,
  Eigen::PlainObjectBase<DerivedFF> & FF,
  Eigen::PlainObjectBase<DerivedC> & C)
{
  using namespace Eigen;
  using namespace std;
  SparseMatrix<typename DerivedF::Scalar> A;
  orientable_patches(F,C,A);


  const int m = F.rows();

  const int num_cc = C.maxCoeff()+1;
  VectorXi seen = VectorXi::Zero(m);


  const int ES[3][2] = {{1,2},{2,0},{0,1}};

  if(((void*)&FF) != ((void*)&F))
  {
    FF = F;
  }

  parallel_for(num_cc,[&](const int c)
  {
    queue<typename DerivedF::Scalar> Q;

    for(int f = 0;f<FF.rows();f++)
    {
      if(C(f) == c)
      {
        Q.push(f);
        break;
      }
    }
    assert(!Q.empty());
    while(!Q.empty())
    {
      const typename DerivedF::Scalar f = Q.front();
      Q.pop();
      if(seen(f) > 0)
      {
        continue;
      }
      seen(f)++;

      for(typename SparseMatrix<typename DerivedF::Scalar>::InnerIterator it (A,f); it; ++it)
      {

        if(it.value() != 0 && it.row() != f)
        {
          const int n = it.row();
          assert(n != f);

          for(int efi = 0;efi<3;efi++)
          {

            Vector2i ef(FF(f,ES[efi][0]),FF(f,ES[efi][1]));

            for(int eni = 0;eni<3;eni++)
            {

              Vector2i en(FF(n,ES[eni][0]),FF(n,ES[eni][1]));

              if(ef(0) == en(0) && ef(1) == en(1))
              {

                FF.row(n) = FF.row(n).reverse().eval();
              }
            }
          }

          Q.push(n);
        }
      }
    }
  },1000);


}

#ifdef IGL_STATIC_LIBRARY

template void igl::bfs_orient<Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&);
#endif
