// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "planarize_quad_mesh.h"
#include "quad_planarity.h"
#include <Eigen/Sparse>
#include <Eigen/Eigenvalues>
#include <iostream>

namespace igl
{
  template <typename DerivedV, typename DerivedF>
  class PlanarizerShapeUp
  {
  protected:

    long numV, numF;

    const Eigen::MatrixBase<DerivedV> &Vin;
    const Eigen::MatrixBase<DerivedF> &Fin;




    Eigen::Matrix<typename DerivedV::Scalar, Eigen::Dynamic, 1> Vv, weightsSqrt, P;




    Eigen::SparseMatrix<typename DerivedV::Scalar > Q, Ni;
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<typename DerivedV::Scalar > > solver;

    int maxIter;
    double threshold;
    const int ni = 4;


    inline void assembleQ();
    inline void assembleP();
    inline void assembleNi();


    inline void assembleSelector(int fi,
                          Eigen::SparseMatrix<typename DerivedV::Scalar > &S);


  public:

    inline PlanarizerShapeUp(const Eigen::MatrixBase<DerivedV> &V_,
                             const Eigen::MatrixBase<DerivedF> &F_,
                             const int maxIter_,
                             const double &threshold_);

    inline void planarize(Eigen::PlainObjectBase<DerivedV> &Vout);
  };
}



template <typename DerivedV, typename DerivedF>
inline igl::PlanarizerShapeUp<DerivedV, DerivedF>::PlanarizerShapeUp(const Eigen::MatrixBase<DerivedV> &V_,
                                                                     const Eigen::MatrixBase<DerivedF> &F_,
                                                                     const int maxIter_,
                                                                     const double &threshold_):
numV(V_.rows()),
numF(F_.rows()),
Vin(V_),
Fin(F_),
weightsSqrt(Eigen::Matrix<typename DerivedV::Scalar, Eigen::Dynamic, 1>::Ones(numF,1)),
maxIter(maxIter_),
threshold(threshold_)
{

  Vv.setZero(3*numV,1);
  for (int i =0;i<numV;++i)
    Vv.segment(3*i,3) = Vin.row(i);

  assembleQ();
};

template <typename DerivedV, typename DerivedF>
inline void igl::PlanarizerShapeUp<DerivedV, DerivedF>::assembleQ()
{
  std::vector<Eigen::Triplet<typename DerivedV::Scalar> > tripletList;


  assembleNi();

  for (int fi = 0; fi< numF; fi++)
  {
    Eigen::SparseMatrix<typename DerivedV::Scalar > Sfi;
    assembleSelector(fi, Sfi);


    Eigen::SparseMatrix<typename DerivedV::Scalar > Qi = weightsSqrt(fi)*Ni*Sfi;


    for (int k=0; k<Qi.outerSize(); ++k)
      for (typename Eigen::SparseMatrix<typename DerivedV::Scalar >::InnerIterator it(Qi,k); it; ++it)
      {
        typename DerivedV::Scalar val = it.value();
        int row = it.row();
        int col = it.col();
        tripletList.push_back(Eigen::Triplet<typename DerivedV::Scalar>(row+3*ni*fi,col,val));
      }
  }

  Q.resize(3*ni*numF,3*numV);
  Q.setFromTriplets(tripletList.begin(), tripletList.end());


  solver.compute(Q.transpose()*Q);
  if(solver.info()!=Eigen::Success)
  {
    assert(false && "Cholesky failed");
  }
}

template <typename DerivedV, typename DerivedF>
inline void igl::PlanarizerShapeUp<DerivedV, DerivedF>::assembleNi()
{
  std::vector<Eigen::Triplet<typename DerivedV::Scalar>> tripletList;
  for (int ii = 0; ii< ni; ii++)
  {
    for (int jj = 0; jj< ni; jj++)
    {
      tripletList.push_back(Eigen::Triplet<typename DerivedV::Scalar>(3*ii+0,3*jj+0,-1./ni));
      tripletList.push_back(Eigen::Triplet<typename DerivedV::Scalar>(3*ii+1,3*jj+1,-1./ni));
      tripletList.push_back(Eigen::Triplet<typename DerivedV::Scalar>(3*ii+2,3*jj+2,-1./ni));
    }
    tripletList.push_back(Eigen::Triplet<typename DerivedV::Scalar>(3*ii+0,3*ii+0,1.));
    tripletList.push_back(Eigen::Triplet<typename DerivedV::Scalar>(3*ii+1,3*ii+1,1.));
    tripletList.push_back(Eigen::Triplet<typename DerivedV::Scalar>(3*ii+2,3*ii+2,1.));
  }
  Ni.resize(3*ni,3*ni);
  Ni.setFromTriplets(tripletList.begin(), tripletList.end());
}


template <typename DerivedV, typename DerivedF>
inline void igl::PlanarizerShapeUp<DerivedV, DerivedF>::assembleSelector(int fi,
                                                                            Eigen::SparseMatrix<typename DerivedV::Scalar > &S)
{

  std::vector<Eigen::Triplet<typename DerivedV::Scalar>> tripletList;
  for (int fvi = 0; fvi< ni; fvi++)
  {
    int vi = Fin(fi,fvi);
    tripletList.push_back(Eigen::Triplet<typename DerivedV::Scalar>(3*fvi+0,3*vi+0,1.));
    tripletList.push_back(Eigen::Triplet<typename DerivedV::Scalar>(3*fvi+1,3*vi+1,1.));
    tripletList.push_back(Eigen::Triplet<typename DerivedV::Scalar>(3*fvi+2,3*vi+2,1.));
  }

  S.resize(3*ni,3*numV);
  S.setFromTriplets(tripletList.begin(), tripletList.end());

}


template <typename DerivedV, typename DerivedF>
inline void igl::PlanarizerShapeUp<DerivedV, DerivedF>::assembleP()
{
  P.setZero(3*ni*numF);
  for (int fi = 0; fi< numF; fi++)
  {

    Eigen::SparseMatrix<typename DerivedV::Scalar > Sfi;
    assembleSelector(fi, Sfi);
    Eigen::SparseMatrix<typename DerivedV::Scalar > NSi = Ni*Sfi;

    Eigen::Matrix<typename DerivedV::Scalar, Eigen::Dynamic, 1> Vi = NSi*Vv;
    Eigen::Matrix<typename DerivedV::Scalar, Eigen::Dynamic, Eigen::Dynamic> CC(3,ni);
    for (int i = 0; i <ni; ++i)
      CC.col(i) = Vi.segment(3*i, 3);
    Eigen::Matrix<typename DerivedV::Scalar, 3, 3> C = CC*CC.transpose();


    Eigen::EigenSolver<Eigen::Matrix<typename DerivedV::Scalar, 3, 3>> es(C);

    Eigen::Matrix<typename DerivedV::Scalar, 3, 1> lambda = es.eigenvalues().real();
    Eigen::Matrix<typename DerivedV::Scalar, 3, 3> U = es.eigenvectors().real();
    int min_i;
    lambda.cwiseAbs().minCoeff(&min_i);
    U.col(min_i).setZero();
    Eigen::Matrix<typename DerivedV::Scalar, Eigen::Dynamic, Eigen::Dynamic> PP = U*U.transpose()*CC;
    for (int i = 0; i <ni; ++i)
     P.segment(3*ni*fi+3*i, 3) =  weightsSqrt[fi]*PP.col(i);

  }
}


template <typename DerivedV, typename DerivedF>
inline void igl::PlanarizerShapeUp<DerivedV, DerivedF>::planarize(Eigen::PlainObjectBase<DerivedV> &Vout)
{
  Eigen::Matrix<typename DerivedV::Scalar, Eigen::Dynamic, 1> planarity;
  Vout = Vin;

  for (int iter =0; iter<maxIter; ++iter)
  {
    igl::quad_planarity(Vout, Fin, planarity);
    typename DerivedV::Scalar nonPlanarity = planarity.cwiseAbs().maxCoeff();
    if (nonPlanarity<threshold)
      break;
    assembleP();
    Vv = solver.solve(Q.transpose()*P);
    if(solver.info()!=Eigen::Success)
    {
      assert(false && "Linear solve failed");
    }
    for (int i =0;i<numV;++i)
      Vout.row(i) << Vv.segment(3*i,3).transpose();
  }

  Eigen::Matrix<typename DerivedV::Scalar, 1, 3> oldMean, newMean;
  oldMean = Vin.colwise().mean();
  newMean = Vout.colwise().mean();
  Vout.rowwise() += (oldMean - newMean);

};



template <typename DerivedV, typename DerivedF>
IGL_INLINE void igl::planarize_quad_mesh(const Eigen::MatrixBase<DerivedV> &Vin,
                                    const Eigen::MatrixBase<DerivedF> &Fin,
                                    const int maxIter,
                                    const double &threshold,
                                    Eigen::PlainObjectBase<DerivedV> &Vout)
{
  PlanarizerShapeUp<DerivedV, DerivedF> planarizer(Vin, Fin, maxIter, threshold);
  planarizer.planarize(Vout);
}

#ifdef IGL_STATIC_LIBRARY

template void igl::planarize_quad_mesh<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, int, double const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&);
#endif
