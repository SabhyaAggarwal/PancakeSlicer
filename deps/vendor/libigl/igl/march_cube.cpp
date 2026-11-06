// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2021 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "march_cube.h"
#include <cstdint>












template <
  typename DerivedGV,
  typename Scalar,
  typename Index,
  typename DerivedV,
  typename DerivedF>
IGL_INLINE void igl::march_cube(
  const DerivedGV & GV,
  const Eigen::Matrix<Scalar,8,1> & cS,
  const Eigen::Matrix<Index,8,1> & cI,
  const Scalar & isovalue,
  Eigen::PlainObjectBase<DerivedV> &V,
  Index & n,
  Eigen::PlainObjectBase<DerivedF> &F,
  Index & m,
  std::unordered_map<std::int64_t,int> & E2V)
{


#include "marching_cubes_tables.h"




  const auto ij2vertex =
    [&E2V,&V,&n]
    (const Index & i, const Index & j, Index & v)->bool
    {

      const auto ij2key = [](std::int32_t i,std::int32_t j)
      {
        if(i>j){ std::swap(i,j); }
        std::int64_t ret = 0;
        ret |= i;
        ret |= static_cast<std::int64_t>(j) << 32;
        return ret;
      };
      const auto key = ij2key(i,j);
      const auto it = E2V.find(key);
      if(it == E2V.end())
      {

        if(n==V.rows()){ V.conservativeResize(V.rows()*2+1,V.cols()); }
        v = n;
        E2V[key] = v;
        n++;
        return true;
      }else
      {
        v = it->second;
        return false;
      }

    };

  int c_flags = 0;
  for(int c = 0; c < 8; c++)
  {
    if(cS(c) > isovalue){ c_flags |= 1<<c; }
  }

  int e_flags = aiCubeEdgeFlags[c_flags];

  if(e_flags == 0) { return; }


  Eigen::Matrix<Index,12,1> edge_vertices;
  for(int e = 0; e < 12; e++)
  {
#ifndef NDEBUG
    edge_vertices[e] = -1;
#endif

    if(e_flags & (1<<e))
    {

      const Index & i = cI(a2eConnection[e][0]);
      const Index & j = cI(a2eConnection[e][1]);
      Index & v = edge_vertices[e];
      if(ij2vertex(i,j,v))
      {

        const Scalar & a = cS(a2eConnection[e][0]);
        const Scalar & b = cS(a2eConnection[e][1]);

        Scalar t;
        const Scalar delta = b-a;
        if(delta == 0) { t = 0.5; }
        t = (isovalue - a)/delta;
        V.row(v) = GV.row(i) + t*(GV.row(j) - GV.row(i));
      }
      assert(v >= 0);
      assert(v < n);
    }
  }

  for(int f = 0; f < 5; f++)
  {
    if(a2fConnectionTable[c_flags][3*f] < 0) break;
    if(m==F.rows()){ F.conservativeResize(F.rows()*2+1,F.cols()); }
    assert(edge_vertices[a2fConnectionTable[c_flags][3*f+0]]>=0);
    assert(edge_vertices[a2fConnectionTable[c_flags][3*f+1]]>=0);
    assert(edge_vertices[a2fConnectionTable[c_flags][3*f+2]]>=0);
    F.row(m) <<
      edge_vertices[a2fConnectionTable[c_flags][3*f+0]],
      edge_vertices[a2fConnectionTable[c_flags][3*f+1]],
      edge_vertices[a2fConnectionTable[c_flags][3*f+2]];
      m++;
  }
}


#ifdef IGL_STATIC_LIBRARY

template void igl::march_cube<Eigen::MatrixBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> >, float, unsigned int, Eigen::Matrix<float, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 3, 1, -1, 3> >(Eigen::MatrixBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> > const&, Eigen::Matrix<float, 8, 1, 0, 8, 1> const&, Eigen::Matrix<unsigned int, 8, 1, 0, 8, 1> const&, float const&, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, 3, 1, -1, 3> >&, unsigned int&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 3, 1, -1, 3> >&, unsigned int&, std::unordered_map<std::int64_t, int, std::hash<std::int64_t>, std::equal_to<std::int64_t>, std::allocator<std::pair<std::int64_t const, int> > >&);
template void igl::march_cube<Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >, double, unsigned int, Eigen::Matrix<double, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 3, 1, -1, 3> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::Matrix<double, 8, 1, 0, 8, 1> const&, Eigen::Matrix<unsigned int, 8, 1, 0, 8, 1> const&, double const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 3, 1, -1, 3> >&, unsigned int&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 3, 1, -1, 3> >&, unsigned int&, std::unordered_map<std::int64_t, int, std::hash<std::int64_t>, std::equal_to<std::int64_t>, std::allocator<std::pair<std::int64_t const, int> > >&);
template void igl::march_cube<Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >, double, long, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::Matrix<double, 8, 1, 0, 8, 1> const&, Eigen::Matrix<long, 8, 1, 0, 8, 1> const&, double const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, long&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, long&, std::unordered_map<std::int64_t, int, std::hash<std::int64_t>, std::equal_to<std::int64_t>, std::allocator<std::pair<std::int64_t const, int> > >&);
template void igl::march_cube<Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >, double, unsigned int, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::Matrix<double, 8, 1, 0, 8, 1> const&, Eigen::Matrix<unsigned int, 8, 1, 0, 8, 1> const&, double const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, unsigned int&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, unsigned int&, std::unordered_map<std::int64_t, int, std::hash<std::int64_t>, std::equal_to<std::int64_t>, std::allocator<std::pair<std::int64_t const, int> > >&);
#ifdef WIN32
template void __cdecl igl::march_cube<class Eigen::MatrixBase<class Eigen::Matrix<double,-1,-1,0,-1,-1> >,double,__int64,class Eigen::Matrix<double,-1,-1,0,-1,-1>,class Eigen::Matrix<int,-1,-1,0,-1,-1> >(class Eigen::MatrixBase<class Eigen::Matrix<double,-1,-1,0,-1,-1> > const &,class Eigen::Matrix<double,8,1,0,8,1> const &,class Eigen::Matrix<__int64,8,1,0,8,1> const &,double const &,class Eigen::PlainObjectBase<class Eigen::Matrix<double,-1,-1,0,-1,-1> > &,__int64 &,class Eigen::PlainObjectBase<class Eigen::Matrix<int,-1,-1,0,-1,-1> > &,__int64 &,class std::unordered_map<__int64,int,struct std::hash<__int64>,struct std::equal_to<__int64>,class std::allocator<struct std::pair<__int64 const ,int> > > &);
#endif
#endif
