// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2022 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "split_nonmanifold.h"
#include "unique_edge_map.h"
#include "connected_components.h"
#include "unique.h"
#include "sort.h"
#include "triangle_triangle_adjacency.h"
#include "placeholders.h"
#include "is_edge_manifold.h"
#include <unordered_map>
#include <cassert>
#include <type_traits>

#include "is_vertex_manifold.h"
#include "matlab_format.h"
#include <iostream>
#include <unordered_set>
#include <utility>

template <
  typename DerivedF,
  typename DerivedSF,
  typename DerivedSVI
  >
IGL_INLINE void igl::split_nonmanifold(
  const Eigen::MatrixBase<DerivedF> & F,
  Eigen::PlainObjectBase <DerivedSF> & SF,
  Eigen::PlainObjectBase <DerivedSVI> & SVI)
{
  using Scalar = typename DerivedSF::Scalar;

  static_assert(std::is_signed<Scalar>::value,"Scalar must be signed");
  using MatrixX2I = Eigen::Matrix<Scalar,Eigen::Dynamic,2>;
  using MatrixX3I = Eigen::Matrix<Scalar,Eigen::Dynamic,3>;
  using VectorXI = Eigen::Matrix< Scalar,Eigen::Dynamic,1>;
  MatrixX2I E,uE;
  VectorXI EMAP,uEC,uEE;
  igl::unique_edge_map(F,E,uE,EMAP,uEC,uEE);








  Eigen::VectorXi V = Eigen::VectorXi::LinSpaced(F.size(),0,F.size()-1);


  Eigen::Map<Eigen::MatrixXi> CF = Eigen::Map<Eigen::MatrixXi>(V.data(),F.rows(),F.cols());



  std::vector<std::vector<int> > C(F.size());
  for(int i = 0;i<F.size();i++) { C[i] = {i}; }

  const int m = F.rows();




  const auto star = [&](const int v)->std::vector<int>
  {
    std::vector<int> faces(C[v].size());
    for(int i = 0;i<C[v].size();i++)
    {
      faces[i] = C[v][i]%m;
    }
    return faces;
  };




  const auto nonmanifold_edge_star = [&](const int v)->std::vector<int>
  {
    std::vector<int> edges;

    for(int e : C[v])
    {
      const int f = e%m;
      for(int j = 1;j<3;j++)
      {

        const int e1 = (e+j*m)%(3*m);
        const int u1 = EMAP(e1);

        if(uEC(u1+1)-uEC(u1) > 2)
        {
          edges.push_back(e1);
        }
      }
    }
    return edges;
  };



  const std::function<void(
    Eigen::VectorXi &,
    std::vector<std::vector<int> > &,
      const int, const int)> merge_vertex =
    [&merge_vertex](Eigen::VectorXi & V,
      std::vector<std::vector<int> > & C,
        const int u, const int v)
  {
    if(u == v) { return; }
    if(u > v) { merge_vertex(V,C,v,u); return; }
    assert(u < v);

    for(const int c : C[v])
    {
      V[c] = u;
    }

    C[u].insert(C[u].end(),C[v].begin(),C[v].end());
    C[v].clear();
  };



  const auto is_boundary = [&](const int e)->bool
  {





    const int s = (e+1*m)%(3*m);
    const int d = (e+2*m)%(3*m);
    const int f = e%m;
    const int vs = V[s];
    const int vd = V[d];

    for(const int g : star(vs))
    {
      if(g == f) { continue; }

      for(int i = 0;i<3;i++)
      {
        const int a = (g+(i+1)*m)%(3*m);
        const int b = (g+(i+2)*m)%(3*m);

        if(V[a] == vd && V[b] == vs) { return false; }
        if(V[a] == vs && V[b] == vd) { return false; }
      }
    }
    return true;
  };








  const auto is_manifold = [](Eigen::MatrixXi F)->bool
  {
    Eigen::Array<bool,Eigen::Dynamic,1> referenced =
      Eigen::Array<bool,Eigen::Dynamic,1>::Zero(F.maxCoeff()+1,1);
    for(int i = 0;i<F.size();i++)
    {
      referenced(F(i)) = true;
    }
    Eigen::Array<bool,Eigen::Dynamic,1> VM;
    igl::is_vertex_manifold(F,VM);
    for(int i = 0;i<VM.size();i++)
    {
      if(referenced(i) && !VM(i))
      {
        return false;
      }
    }
    return igl::is_edge_manifold(F);
  };

















  const auto simulated_merge_is_manifold =
    [&](
        const int vs1, const int vd2,
        const int vd1, const int vs2)->bool
  {

    std::vector<int> all_faces;
    for(int v : {vs1,vd2,vd1,vs2})
    {
      std::vector<int> star_v = star(v);
      all_faces.insert(all_faces.end(),star_v.begin(),star_v.end());
    }


    std::vector<int> unique_faces;
    std::vector<size_t> _, local;
    igl::unique(all_faces,unique_faces,_,local);
    Eigen::MatrixXi L(unique_faces.size(),3);

    for(int l = 0;l<unique_faces.size();l++)
    {
      L.row(l) = CF.row(unique_faces[l]);
    }
    {
      int f = 0;
      const auto merge_local = [&](const int v1, const int v2)
      {
        const int u = std::min(v1,v2);
        for(const int v : {v1,v2})
        {
          for(const int c : C[v])
          {
            const int i = c/m;
            L(local[f++],i) = u;
          }
        }
      };

      merge_local(vs1,vd2);
      merge_local(vd1,vs2);
    }



    std::unordered_map<int,int> M;
    for(int & i : L.reshaped())
    {
      if(M.find(i) == M.end())
      {
        M[i] = M.size();
      }
      i = M[i];
    }

    Eigen::Array<bool,Eigen::Dynamic,1> VM;
    const int vs = std::min(vs1,vd2);
    const int vd = std::min(vd1,vs2);
    igl::is_vertex_manifold(L,VM);
    if(!VM(M[vs])) {
      return false;
    }
    if(!VM(M[vd])) {
      return false;
    }


    return igl::is_edge_manifold(L);
  };

  const auto merge_edge = [&](const int e1, const int e2)
  {





    if(!is_boundary(e1) || !is_boundary(e2)) { return false; }
    assert(e1 != e2);

    if(EMAP(e1) != EMAP(e2)) { return false; }

    assert(EMAP(e1) == EMAP(e2));
    const int u = EMAP(e1);

    const bool consistent = E(e1,0) == E(e2,1);

    if(!consistent) { return false; }

    if(E(e1,1) != E(e2,0))
    {
    }
    assert(E(e1,1) == E(e2,0));













    const int s1 = (e1+1*m)%(3*m);
    const int d1 = (e1+2*m)%(3*m);
#ifndef NDEBUG
    {
      const int f1 = e1 % m;
      const int i1 = e1 / m;
      const int s1_test = f1 + ((i1+1)%3)*m;
      const int d1_test = f1 + ((i1+2)%3)*m;
      assert(s1 == s1_test);
      assert(d1 == d1_test);
    }
#endif
    int s2 = (e2+1*m)%(3*m);
    int d2 = (e2+2*m)%(3*m);
    const int vs1 = V[s1];
    const int vd2 = V[d2];
    const int vd1 = V[d1];
    const int vs2 = V[s2];

#ifdef IGL_SPLIT_NONMANIFOLD_DEBUG
    const auto simulated_merge_is_manifold_old = [&]()->bool
    {
      Eigen::VectorXi V_copy = V;
      std::vector<std::vector<int> > C_copy = C;
      merge_vertex(V_copy,C_copy,vs1,vd2);
      merge_vertex(V_copy,C_copy,vd1,vs2);
      Eigen::Map<Eigen::MatrixXi> CF_copy =
        Eigen::Map<Eigen::MatrixXi>(V_copy.data(),CF.rows(),CF.cols());
      if(!is_manifold(CF_copy)) { return false; }
      return true;
    };
    const bool ret_old = simulated_merge_is_manifold_old();
    const bool ret = simulated_merge_is_manifold(vs1,vd2,vd1,vs2);
    if(ret != ret_old)
    {
      assert(false);
    }
#endif







    const int edge_valence = uEC(u+1)-uEC(u);
    assert(edge_valence >= 2);
    if(edge_valence>2 && !simulated_merge_is_manifold(vs1,vd2,vd1,vs2))
    {
      return false;
    }


    merge_vertex(V,C,vs1,vd2);
    merge_vertex(V,C,vd1,vs2);
    return true;
  };




  VectorXI D = uEC.tail(uEC.rows()-1)-uEC.head(uEC.rows()-1);
  VectorXI uI;
  {
    VectorXI sD;
    igl::sort(D,1,true,sD,uI);
  }


  const std::function<void(const int)> dfs = [&](const int e)
  {


    const int s = (e+1*m)%(3*m);
    const int d = (e+2*m)%(3*m);
    for(const int c : {s,d})
    {
      const int v = V[c];
      std::vector<int> nme = nonmanifold_edge_star(v);



      for(int i = 0;i<nme.size();i++)
      {
        const int e1 = nme[i];
        for(int j = i+1;j<nme.size();j++)
        {
          const int e2 = nme[j];
          if(merge_edge(e1,e2))
          {
            dfs(e2);
          }
        }
      }
    }
  };


  for(auto u : uI)
  {

    if(uEC(u+1)-uEC(u) == 1) { continue; }
    for(int j = uEC(u);j<uEC(u+1);j++)
    {
      const int e1 = uEE(j);
      for(int k = j+1;k<uEC(u+1);k++)
      {
        const int e2 = uEE(k);
        if(merge_edge(e1,e2))
        {

          if(uEC(u+1)-uEC(u) > 2)
          {
            dfs(e1);
          }
          break;
        }
      }
    }
  }





  {
    SVI.resize(F.size());
    std::vector<bool> marked(F.size());
    VectorXI J = VectorXI::Constant(F.size(),-1);
    SF.resize(F.rows(),F.cols());
    {
      int nv = 0;
      for(int f = 0;f<m;f++)
      {
        for(int i = 0;i<3;i++)
        {
          const int c = CF(f,i);
          if(J(c) == -1)
          {
            J(c) = nv;
            SVI(nv) = F(f,i);
            nv++;
          }
          SF(f,i) = J(c);
        }
      }
      SVI.conservativeResize(nv);
    }
  }

}

template <
  typename DerivedV,
  typename DerivedF,
  typename DerivedSV,
  typename DerivedSF,
  typename DerivedSVI
  >
IGL_INLINE void igl::split_nonmanifold(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedF> & F,
  Eigen::PlainObjectBase <DerivedSV> & SV,
  Eigen::PlainObjectBase <DerivedSF> & SF,
  Eigen::PlainObjectBase <DerivedSVI> & SVI)
{
  igl::split_nonmanifold(F,SF,SVI);
  SV = V(SVI.derived(),igl::placeholders::all);
}

#ifdef IGL_STATIC_LIBRARY
template void igl::split_nonmanifold<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&);
#endif
