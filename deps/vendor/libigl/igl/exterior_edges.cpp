// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "exterior_edges.h"
#include "oriented_facets.h"
#include "sort.h"
#include "unique_rows.h"
#include "PlainMatrix.h"

#include <cassert>
#include <unordered_map>
#include <utility>
#include <iostream>



























template <typename DerivedF, typename DerivedE>
IGL_INLINE void igl::exterior_edges(
  const Eigen::MatrixBase<DerivedF> & F,
  Eigen::PlainObjectBase<DerivedE> & E)
{
  using namespace Eigen;
  using namespace std;
  using Index = typename DerivedF::Scalar;
  using VectorXI = Eigen::Matrix<typename DerivedF::Scalar,Eigen::Dynamic,1>;
  using MatrixXI = Eigen::Matrix<typename DerivedF::Scalar,Eigen::Dynamic,Eigen::Dynamic>;
  assert(F.cols() == 3);
  const Index m = F.rows();
  MatrixXI all_E,sall_E,sort_order;

  oriented_facets(F,all_E);
  sort(all_E,2,true,sall_E,sort_order);

  PlainMatrix<DerivedF,Eigen::Dynamic> uE;
  VectorXI IA,EMAP;
  unique_rows(sall_E,uE,IA,EMAP);
  VectorXI counts = VectorXI::Zero(uE.rows());
  for(Index a = 0;a<3*m;a++)
  {
    counts(EMAP(a)) += (sort_order(a)==0?1:-1);
  }

  E.resize(all_E.rows(),2);
  {
    Index e = 0;
    const Index nue = uE.rows();

    for(Index ue = 0; ue<nue; ue++)
    {
      const Index count = counts(ue);
      Index i,j;
      if(count == 0)
      {
        continue;
      }else if(count < 0)
      {
        i = uE(ue,1);
        j = uE(ue,0);
      }else if(count > 0)
      {
        i = uE(ue,0);
        j = uE(ue,1);
      }

      const Index abs_count = abs(count);
      for(Index k = 0;k<abs_count;k++)
      {
        E(e,0) = i;
        E(e,1) = j;
        e++;
      }
    }
    E.conservativeResize(e,2);
  }
}

#ifdef IGL_STATIC_LIBRARY


template void igl::exterior_edges<Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 2, 0, -1, 2>>(Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>> const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 2, 0, -1, 2>>&);
template void igl::exterior_edges<Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>>(Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>> const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>>&);
#endif
