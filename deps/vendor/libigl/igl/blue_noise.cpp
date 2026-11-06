// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2020 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "blue_noise.h"
#include "doublearea.h"
#include "random_points_on_mesh.h"
#include "sortrows.h"
#include "placeholders.h"
#include "PI.h"
#include "get_seconds.h"
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <random>
#include <cstdint>

namespace igl
{


  typedef std::int64_t BlueNoiseKeyType;
}


namespace igl
{











  inline BlueNoiseKeyType blue_noise_key(
    const BlueNoiseKeyType w,
    const BlueNoiseKeyType x,
    const BlueNoiseKeyType y,
    const BlueNoiseKeyType z)
  {
    return x+w*(y+w*z);
  }











  template <
    typename DerivedX,
    typename DerivedXs>
  inline bool blue_noise_far_enough(
    const Eigen::MatrixBase<DerivedX> & X,
    const Eigen::MatrixBase<DerivedXs> & Xs,
    const std::unordered_map<BlueNoiseKeyType,int> & S,
    const double & rr,
    const int & w,
    const int i)
  {
    const int xi = Xs(i,0);
    const int yi = Xs(i,1);
    const int zi = Xs(i,2);
    int g = 2;
    for(int x = std::max(xi-g,0);x<=std::min(xi+g,w-1);x++)
    for(int y = std::max(yi-g,0);y<=std::min(yi+g,w-1);y++)
    for(int z = std::max(zi-g,0);z<=std::min(zi+g,w-1);z++)
    {
      if(x!=xi || y!=yi || z!=zi)
      {
        const BlueNoiseKeyType nk = blue_noise_key(w,x,y,z);

        const auto Siter = S.find(nk);
        if(Siter !=S.end() && Siter->second >= 0)
        {
          const int ni = Siter->second;

          if( (X.row(i)-X.row(ni)).squaredNorm() < rr)
          {
            return false;
          }
        }
      }
    }
    return true;
  }


















  template <
    typename DerivedX,
    typename DerivedXs>
  inline bool activate(
    const Eigen::MatrixBase<DerivedX> & X,
    const Eigen::MatrixBase<DerivedXs> & Xs,
    const double & rr,
    const int & i,
    const int & w,
    const BlueNoiseKeyType & nk,
    std::unordered_map<BlueNoiseKeyType,std::vector<int> > & M,
    std::unordered_map<BlueNoiseKeyType,int> & S,
    std::vector<int> & active)
  {
    assert(M.count(nk));
    auto & Mvec = M.find(nk)->second;
    auto miter = Mvec.begin();
    while(miter != Mvec.end())
    {
      const int mi = *miter;


      if(i>=0 && (X.row(i)-X.row(mi)).squaredNorm() > 4.*rr)
      {

       miter++;
      } else if(blue_noise_far_enough(X,Xs,S,rr,w,mi))
      {
        active.push_back(mi);
        S.find(nk)->second = mi;

        return true;
      }else
      {



        *miter = Mvec.back();
        bool was_last = (std::next(miter) == Mvec.end());
        Mvec.pop_back();
        if (was_last) {



          miter = Mvec.end();
        }
      }
    }
    return false;
  }

  template <
    typename DerivedX,
    typename DerivedXs,
    typename URBG>
  inline bool step(
    const Eigen::MatrixBase<DerivedX> & X,
    const Eigen::MatrixBase<DerivedXs> & Xs,
    const double & rr,
    const int & w,
    URBG && urbg,
    std::unordered_map<BlueNoiseKeyType,std::vector<int> > & M,
    std::unordered_map<BlueNoiseKeyType,int> & S,
    std::vector<int> & active,
    std::vector<int> & collected
    )
  {

    if(active.size() == 0) return false;

    std::uniform_int_distribution<> dis(0, active.size()-1);
    const int e = dis(urbg);
    const int i = active[e];

    const int xi = Xs(i,0);
    const int yi = Xs(i,1);
    const int zi = Xs(i,2);


    int g = 4;
    std::vector<BlueNoiseKeyType> N;N.reserve((1+g*1)^3-1);
    for(int x = std::max(xi-g,0);x<=std::min(xi+g,w-1);x++)
    for(int y = std::max(yi-g,0);y<=std::min(yi+g,w-1);y++)
    for(int z = std::max(zi-g,0);z<=std::min(zi+g,w-1);z++)
    {
      if(x!=xi || y!=yi || z!=zi)
      {

        const BlueNoiseKeyType nk = blue_noise_key(w,x,y,z);

        const auto Siter = S.find(nk);
        if(Siter !=S.end() && Siter->second < 0)
        {
          assert(M.find(nk) != M.end());
          N.emplace_back(nk);
        }
      }
    }


    std::shuffle(std::begin(N), std::end(N), urbg);
    bool found = false;
    for(const BlueNoiseKeyType & nk : N)
    {
      assert(M.find(nk) != M.end());
      if(activate(X,Xs,rr,i,w,nk,M,S,active))
      {
        found = true;
        break;
      }
    }
    if(!found)
    {


      collected.push_back(i);

      std::swap(active[e], active.back());

      active.pop_back();

    }

    return true;
  }
}

template <
  typename DerivedV,
  typename DerivedF,
  typename DerivedB,
  typename DerivedFI,
  typename DerivedP,
  typename URBG>
IGL_INLINE void igl::blue_noise(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const typename DerivedV::Scalar r,
    Eigen::PlainObjectBase<DerivedB> & B,
    Eigen::PlainObjectBase<DerivedFI> & FI,
    Eigen::PlainObjectBase<DerivedP> & P,
    URBG && urbg)
{
  typedef typename DerivedV::Scalar Scalar;

  typedef Eigen::Matrix<Scalar,Eigen::Dynamic,3,Eigen::RowMajor> MatrixX3S;
  assert(V.cols() == 3 && "Only 3D embeddings allowed");

  const Scalar min_r = r;








  const Scalar s = r/sqrt(3.0);

  const double area =
    [&](){Eigen::VectorXd A;igl::doublearea(V,F,A);return A.array().sum()/2;}();

  const double expected_number_of_points =
    area * (igl::PI * sqrt(3.0) / 6.0) / (igl::PI * min_r * min_r / 4.0);


  const int nx = 30.0*expected_number_of_points;
  MatrixX3S X,XB;
  Eigen::VectorXi XFI;
  igl::random_points_on_mesh(nx,V,F,XB,XFI,X,urbg);


  Eigen::Matrix<int,Eigen::Dynamic,3,Eigen::RowMajor> Xs =
    ((X.rowwise()-X.colwise().minCoeff())/s).template cast<int>();
  const int w = Xs.maxCoeff()+1;
  {
    Eigen::VectorXi I;
    igl::sortrows(decltype(Xs)(Xs),true,Xs,I);
    X = X(I,igl::placeholders::all).eval();

    XB = XB(I,igl::placeholders::all).eval();
    XFI = XFI(I,igl::placeholders::all).eval();
  }

  std::unordered_map<BlueNoiseKeyType,std::vector<int> > M;
  std::unordered_map<BlueNoiseKeyType, int > S;

  std::unordered_map<BlueNoiseKeyType, int > A;


  M.reserve(Xs.rows());
  S.reserve(Xs.rows());
  for(int i = 0;i<Xs.rows();i++)
  {
    BlueNoiseKeyType k = blue_noise_key(w,Xs(i,0),Xs(i,1),Xs(i,2));
    const auto Miter = M.find(k);
    if(Miter  == M.end())
    {
      M.insert({k,{i}});
    }else
    {
      Miter->second.push_back(i);
    }
    S.emplace(k,-1);
    A.emplace(k,false);
  }

  std::vector<int> active;


  const double rr = r*r;
  std::vector<int> collected;
  collected.reserve(2.0*expected_number_of_points);

  auto Mouter = M.begin();

  const auto initialize = [&]()->bool
  {
    while(true)
    {
      if(Mouter == M.end())
      {
        return false;
      }
      const BlueNoiseKeyType k = Mouter->first;

      if(S[k]<0)
      {
        if(activate(X,Xs,rr,-1,w,k,M,S,active)) return true;
      }
      Mouter++;
    }
    assert(false && "should not be reachable.");
  };


  while(initialize())
  {
    while(active.size()>0)
    {
      step(X,Xs,rr,w,urbg,M,S,active,collected);
    }
  }
  {
    const int n = collected.size();
    P.resize(n,3);
    B.resize(n,3);
    FI.resize(n);
    for(int i = 0;i<n;i++)
    {
      const int c = collected[i];
      P.row(i) = X.row(c).template cast<typename DerivedP::Scalar>();
      B.row(i) = XB.row(c).template cast<typename DerivedB::Scalar>();
      FI(i) = XFI(c);
    }
  }
}

#ifdef IGL_STATIC_LIBRARY

template void igl::blue_noise<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, std::mt19937_64 >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, std::mt19937_64&&);
template void igl::blue_noise<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, std::mt19937 >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, std::mt19937&&);
#endif
