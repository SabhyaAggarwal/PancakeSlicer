// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "arap_dof.h"
#include "IGL_ASSERT.h"

#include "cotmatrix.h"
#include "massmatrix.h"
#include "speye.h"
#include "repdiag.h"
#include "repmat.h"
#include "slice.h"
#include "colon.h"
#include "is_sparse.h"
#include "mode.h"
#include "is_symmetric.h"
#include "group_sum_matrix.h"
#include "arap_rhs.h"
#include "covariance_scatter_matrix.h"
#include "fit_rotations.h"

#include "verbose.h"
#include "print_ijv.h"


#include "kkt_inverse.h"
#include "get_seconds.h"
#include "columnize.h"
#include <type_traits>


#define IGL_ARAP_DOF_FIXED_ITERATIONS_COUNT



#if __cplusplus < 201703L
template <typename Mat, bool IsSparse> struct arap_dof_slice_helper;
template <typename Mat> struct arap_dof_slice_helper<Mat,true>
{
  static void slice(const Mat & A, const Eigen::VectorXi & I, const Eigen::VectorXi & J, Mat & B)
  {
    static_assert(std::is_base_of<Eigen::SparseMatrixBase<Mat>, Mat>::value, "Mat must be sparse");
    igl::slice(A,I,J,B);
  }
};
template <typename Mat> struct arap_dof_slice_helper<Mat,false>
{
  static void slice(const Mat & A, const Eigen::VectorXi & I, const Eigen::VectorXi & J, Mat & B)
  {
    B = A(I,J);
  }
};
#endif



template <typename LbsMatrixType, typename SSCALAR>
IGL_INLINE bool igl::arap_dof_precomputation(
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  const LbsMatrixType & M,
  const Eigen::Matrix<int,Eigen::Dynamic,1> & G,
  ArapDOFData<LbsMatrixType, SSCALAR> & data)
{
  using namespace Eigen;
  typedef Matrix<SSCALAR, Dynamic, Dynamic> MatrixXS;

  int n = V.rows();

  data.n = n;

  data.dim = V.cols();
  assert(data.dim == M.rows()/n);
  assert(data.dim*n == M.rows());
  if(data.dim == 3)
  {

    if(V.col(2).minCoeff() == 0 && V.col(2).maxCoeff() == 0)
    {
      data.effective_dim = 2;
    }
  }else
  {
    data.effective_dim = data.dim;
  }

  data.m = M.cols()/data.dim/(data.dim+1);
  assert(data.m*data.dim*(data.dim+1) == M.cols());





  SparseMatrix<double> Lcot;

  cotmatrix(V,F,Lcot);

  SparseMatrix<double> Lapl = -2.0*Lcot;
#ifdef EXTREME_VERBOSE
  cout<<"LaplIJV=["<<endl;print_ijv(Lapl,1);cout<<endl<<"];"<<
    endl<<"Lapl=sparse(LaplIJV(:,1),LaplIJV(:,2),LaplIJV(:,3),"<<
    Lapl.rows()<<","<<Lapl.cols()<<");"<<endl;
#endif



  SparseMatrix<double> G_sum;
  if(G.size() == 0)
  {
    speye(n,G_sum);
  }else
  {

    Eigen::Matrix<int,Eigen::Dynamic,1> GG;
    if(data.energy == ARAP_ENERGY_TYPE_ELEMENTS)
    {
      MatrixXi GF(F.rows(),F.cols());
      for(int j = 0;j<F.cols();j++)
      {
        GF.col(j) = G(F.col(j));
      }
      mode<int>(GF,2,GG);
    }else
    {
      GG=G;
    }

    group_sum_matrix(GG,G_sum);
  }

#ifdef EXTREME_VERBOSE
  cout<<"G_sumIJV=["<<endl;print_ijv(G_sum,1);cout<<endl<<"];"<<
    endl<<"G_sum=sparse(G_sumIJV(:,1),G_sumIJV(:,2),G_sumIJV(:,3),"<<
    G_sum.rows()<<","<<G_sum.cols()<<");"<<endl;
#endif



  SparseMatrix<double> CSM;

  covariance_scatter_matrix(V,F,data.energy,CSM);
#ifdef EXTREME_VERBOSE
  cout<<"CSMIJV=["<<endl;print_ijv(CSM,1);cout<<endl<<"];"<<
    endl<<"CSM=sparse(CSMIJV(:,1),CSMIJV(:,2),CSMIJV(:,3),"<<
    CSM.rows()<<","<<CSM.cols()<<");"<<endl;
#endif



















  SparseMatrix<double> G_sum_dim;
  repdiag(G_sum,data.dim,G_sum_dim);
  CSM = (G_sum_dim * CSM).eval();
#ifdef EXTREME_VERBOSE
  cout<<"CSMIJV=["<<endl;print_ijv(CSM,1);cout<<endl<<"];"<<
    endl<<"CSM=sparse(CSMIJV(:,1),CSMIJV(:,2),CSMIJV(:,3),"<<
    CSM.rows()<<","<<CSM.cols()<<");"<<endl;
#endif



  data.CSM_M.resize(data.dim);
#ifdef EXTREME_VERBOSE
  cout<<"data.CSM_M = cell("<<data.dim<<",1);"<<endl;
#endif

  Eigen::Matrix<int,Eigen::Dynamic,1> span_n(n);
  for(int i = 0;i<n;i++)
  {
    span_n(i) = i;
  }


  Eigen::Matrix<int,Eigen::Dynamic,1> span_mlbs_cols(M.cols());
  for(int i = 0;i<M.cols();i++)
  {
    span_mlbs_cols(i) = i;
  }


  int k = CSM.rows()/data.dim;
  for(int i = 0;i<data.dim;i++)
  {

    LbsMatrixType M_i;

#if __cplusplus >= 201703L

    if constexpr (std::is_base_of<SparseMatrixBase<LbsMatrixType>, LbsMatrixType>::value)
    {
      slice(M,(span_n.array()+i*n).matrix().eval(),span_mlbs_cols,M_i);
    }
    else
    {
      M_i = M((span_n.array()+i*n).eval(),span_mlbs_cols);
    }
#else
    constexpr bool LbsMatrixTypeIsSparse = std::is_base_of<SparseMatrixBase<LbsMatrixType>, LbsMatrixType>::value;
    arap_dof_slice_helper<LbsMatrixType,LbsMatrixTypeIsSparse>::slice(M,(span_n.array()+i*n).matrix().eval(),span_mlbs_cols,M_i);
#endif
    LbsMatrixType M_i_dim;
    data.CSM_M[i].resize(k*data.dim,data.m*data.dim*(data.dim+1));
    assert(data.CSM_M[i].cols() == M.cols());
    for(int j = 0;j<data.dim;j++)
    {
      SparseMatrix<double> CSMj;

      slice(
        CSM,
        colon<int>(j*k,(j+1)*k-1),
        colon<int>(j*n,(j+1)*n-1),
        CSMj);
      assert(CSMj.rows() == k);
      assert(CSMj.cols() == n);
      LbsMatrixType CSMjM_i = CSMj * M_i;
      if(is_sparse(CSMjM_i))
      {


        MatrixXd CSMjM_ifull(CSMjM_i);






        data.CSM_M[i].block(j*k,0,CSMjM_i.rows(),CSMjM_i.cols()) = CSMjM_ifull;
      }else
      {
        data.CSM_M[i].block(j*k,0,CSMjM_i.rows(),CSMjM_i.cols()) = CSMjM_i;
      }
    }
#ifdef EXTREME_VERBOSE
    cout<<"CSM_Mi=["<<endl<<data.CSM_M[i]<<endl<<"];"<<endl;
#endif
  }



  SparseMatrix<double> K;
  arap_rhs(V,F,V.cols(),data.energy,K);






  SparseMatrix<double> G_sumT = G_sum.transpose();
  SparseMatrix<double> G_sumT_dim_dim;
  repdiag(G_sumT,data.dim*data.dim,G_sumT_dim_dim);
  LbsMatrixType MT = M.transpose();

  data.M_KG = -4.0 * (MT * (K * G_sumT_dim_dim));








  SparseMatrix<double> A;
  repdiag(Lapl,data.dim,A);
  data.Q = MT * (A * M);










    SparseMatrix<double> Mass;

    massmatrix(V,F,(F.cols()>3?MASSMATRIX_TYPE_BARYCENTRIC:MASSMATRIX_TYPE_VORONOI),Mass);




    SparseMatrix<double> Mass_rep;
    repdiag(Mass,data.dim,Mass_rep);


    data.Mass_tilde = MT * Mass_rep * M;
    MatrixXd ones(data.dim*data.n,data.dim);
    for(int i = 0;i<data.n;i++)
    {
      for(int d = 0;d<data.dim;d++)
      {
        ones(i+d*data.n,d) = 1;
      }
    }
    data.fgrav = MT * (Mass_rep * ones);
    data.fext = MatrixXS::Zero(MT.rows(),1);






  if(!is_symmetric(data.Q))
  {


    LbsMatrixType QT = data.Q.transpose();
    LbsMatrixType Q_copy = data.Q;
    data.Q = 0.5*(Q_copy+QT);


  }


  verbose("Number of handles: %i\n", data.m);
  return true;
}






namespace igl
{

  template <typename SSCALAR>
  inline static SSCALAR maxBlokErr(const Eigen::Matrix3f &blok)
  {
    SSCALAR mD;
    SSCALAR value = blok(0,0);
    SSCALAR diff1 = fabs(blok(1,1) - value);
    SSCALAR diff2 = fabs(blok(2,2) - value);
    if (diff1 > diff2) mD = diff1;
    else mD = diff2;

    for (int v=0; v<3; v++)
    {
      for (int w=0; w<3; w++)
      {
        if (v == w)
        {
          continue;
        }
        if (mD < fabs(blok(v, w)))
        {
          mD = fabs(blok(v, w));
        }
      }
    }

    return mD;
  }





  template <typename MatrixXS>
  static typename MatrixXS::Scalar condense_CSM(
    const std::vector<MatrixXS> &CSM_M_SSCALAR,
    int numBones,
    int dim,
    MatrixXS &CSM)
  {
    const int numRows = CSM_M_SSCALAR[0].rows();
    assert(CSM_M_SSCALAR[0].cols() == dim*(dim+1)*numBones);
    assert(CSM_M_SSCALAR[1].cols() == dim*(dim+1)*numBones);
    assert(CSM_M_SSCALAR[2].cols() == dim*(dim+1)*numBones);
    assert(CSM_M_SSCALAR[1].rows() == numRows);
    assert(CSM_M_SSCALAR[2].rows() == numRows);

    const int numCols = (dim + 1)*numBones;
    CSM.resize(numRows, numCols);

    typedef typename MatrixXS::Scalar SSCALAR;
    SSCALAR maxDiff = 0.0f;

    for (int r=0; r<numRows; r++)
    {
      for (int coord=0; coord<dim+1; coord++)
      {
        for (int b=0; b<numBones; b++)
        {

          Eigen::Matrix3f blok;
          for (int v=0; v<3; v++)
          {
            for (int w=0; w<3; w++)
            {
              blok(v,w) = CSM_M_SSCALAR[v](r, coord*(numBones*dim) + b + w*numBones);
            }
          }





          SSCALAR mD = maxBlokErr<SSCALAR>(blok);
          if (mD > maxDiff) maxDiff = mD;


          CSM(r, coord*numBones + b) = blok(0,0);
        }
      }
    }

    return maxDiff;
  }





  template <typename MatL, typename MatLsep>
  static void splitColumns(
   const MatL &L,
   int numBones,
   int dim,
   int dimp1,
   MatLsep &Lsep)
  {
    assert(L.cols() == 1);
    assert(L.rows() == dim*(dimp1)*numBones);

    assert(Lsep.rows() == (dimp1)*numBones && Lsep.cols() == dim);

    for (int b=0; b<numBones; b++)
    {
      for (int coord=0; coord<dimp1; coord++)
      {
        for (int c=0; c<dim; c++)
        {
          Lsep(coord*numBones + b, c) = L(coord*numBones*dim + c*numBones + b, 0);
        }
      }
    }
  }






  template <typename MatrixXS>
  static void mergeColumns(const MatrixXS &Lsep, int numBones, int dim, int dimp1, MatrixXS &L)
  {
    assert(L.cols() == 1);
    assert(L.rows() == dim*(dimp1)*numBones);

    assert(Lsep.rows() == (dimp1)*numBones && Lsep.cols() == dim);

    for (int b=0; b<numBones; b++)
    {
      for (int coord=0; coord<dimp1; coord++)
      {
        for (int c=0; c<dim; c++)
        {
          L(coord*numBones*dim + c*numBones + b, 0) = Lsep(coord*numBones + b, c);
        }
      }
    }
  }





  template <typename MatrixXS>
  static typename MatrixXS::Scalar condense_Solve1(MatrixXS &Solve1, int numBones, int numGroups, int dim, MatrixXS &CSolve1)
  {
    assert(Solve1.rows() == dim*(dim + 1)*numBones);
    assert(Solve1.cols() == dim*dim*numGroups);

    typedef typename MatrixXS::Scalar SSCALAR;
    SSCALAR maxDiff = 0.0f;

    CSolve1.resize((dim + 1)*numBones, dim*numGroups);
    for (int rowCoord=0; rowCoord<dim+1; rowCoord++)
    {
      for (int b=0; b<numBones; b++)
      {
        for (int colCoord=0; colCoord<dim; colCoord++)
        {
          for (int g=0; g<numGroups; g++)
          {
            Eigen::Matrix3f blok;
            for (int r=0; r<3; r++)
            {
              for (int c=0; c<3; c++)
              {
                blok(r, c) = Solve1(rowCoord*numBones*dim + r*numBones + b, colCoord*numGroups*dim + c*numGroups + g);
              }
            }

            SSCALAR mD = maxBlokErr<SSCALAR>(blok);
            if (mD > maxDiff) maxDiff = mD;

            CSolve1(rowCoord*numBones + b, colCoord*numGroups + g) = blok(0,0);
          }
        }
      }
    }

    return maxDiff;
  }
}

template <typename LbsMatrixType, typename SSCALAR>
IGL_INLINE bool igl::arap_dof_recomputation(
  const Eigen::Matrix<int,Eigen::Dynamic,1> & fixed_dim,
  const Eigen::SparseMatrix<double> & A_eq,
  ArapDOFData<LbsMatrixType, SSCALAR> & data)
{
  using namespace Eigen;
  typedef Matrix<SSCALAR, Dynamic, Dynamic> MatrixXS;

  LbsMatrixType * Q;
  LbsMatrixType Qdyn;
  if(data.with_dynamics)
  {


    LbsMatrixType Q_copy = data.Q;
    Qdyn = Q_copy + (1.0/(data.h*data.h))*data.Mass_tilde;
    Q = &Qdyn;



    if(!is_symmetric(*Q))
    {


      LbsMatrixType QT = (*Q).transpose();
      LbsMatrixType Q_copy = *Q;
      *Q = 0.5*(Q_copy+QT);


    }
  }else
  {
    Q = &data.Q;
  }

  assert((int)data.CSM_M.size() == data.dim);
  assert(A_eq.cols() == data.m*data.dim*(data.dim+1));
  data.fixed_dim = fixed_dim;

  if(fixed_dim.size() > 0)
  {
    assert(fixed_dim.maxCoeff() < data.m*data.dim*(data.dim+1));
    assert(fixed_dim.minCoeff() >= 0);
  }

#ifdef EXTREME_VERBOSE
  cout<<"data.fixed_dim=["<<endl<<data.fixed_dim<<endl<<"]+1;"<<endl;
#endif



  MatrixXd Qfull(*Q);
  MatrixXd A_eqfull(A_eq);
  MatrixXd M_Solve;

  double timer0_start = get_seconds();
  bool use_lu = data.effective_dim != 2;


  kkt_inverse(Qfull, A_eqfull, use_lu,M_Solve);
  double timer0_end = get_seconds();
  verbose("Bob timing: %.20f\n", (timer0_end - timer0_start)*1000.0);


  const int fsRows = data.m * data.dim * (data.dim + 1);
  const int fsCols1 = data.M_KG.cols();
  const int fsCols2 = A_eq.rows();
  data.M_FullSolve.resize(fsRows, fsCols1 + fsCols2);


  data.M_FullSolve <<
    (-0.5 * M_Solve.block(0, 0, fsRows, fsRows) * data.M_KG).template cast<SSCALAR>(),
    M_Solve.block(0, fsRows, fsRows, fsCols2).template cast<SSCALAR>();

  if(data.with_dynamics)
  {
    printf(
      "---------------------------------------------------------------------\n"
      "\n\n\nWITH DYNAMICS recomputation\n\n\n"
      "---------------------------------------------------------------------\n"
      );

    data.Pi_1 = M_Solve.block(0, 0, fsRows, fsRows).template cast<SSCALAR>();
  }



  std::vector<MatrixXS> CSM_M_SSCALAR;
  CSM_M_SSCALAR.resize(data.dim);
  for (int i=0; i<data.dim; i++) CSM_M_SSCALAR[i] = data.CSM_M[i].template cast<SSCALAR>();
  SSCALAR maxErr1 = condense_CSM(CSM_M_SSCALAR, data.m, data.dim, data.CSM);
  verbose("condense_CSM maxErr = %.15f (this should be close to zero)\n", maxErr1);
  assert(fabs(maxErr1) < 1e-5);



  const int k = data.CSM_M[0].rows()/data.dim;
  MatrixXS SolveBlock1 = data.M_FullSolve.block(0, 0, data.M_FullSolve.rows(), data.dim * data.dim * k);
  SSCALAR maxErr2 = condense_Solve1(SolveBlock1, data.m, k, data.dim, data.CSolveBlock1);
  verbose("condense_Solve1 maxErr = %.15f (this should be close to zero)\n", maxErr2);
  assert(fabs(maxErr2) < 1e-5);

  return true;
}

template <typename LbsMatrixType, typename SSCALAR>
IGL_INLINE bool igl::arap_dof_update(
  const ArapDOFData<LbsMatrixType, SSCALAR> & data,
  const Eigen::Matrix<double,Eigen::Dynamic,1> & B_eq,
  const Eigen::MatrixXd & L0,
  const int max_iters,
  const double
#ifdef IGL_ARAP_DOF_FIXED_ITERATIONS_COUNT
  tol,
#else
   ,
#endif
  Eigen::MatrixXd & L
  )
{
  using namespace Eigen;
  typedef Matrix<SSCALAR, Dynamic, Dynamic> MatrixXS;
#ifdef ARAP_GLOBAL_TIMING
  double timer_start = get_seconds();
#endif


  IGL_ASSERT((int)data.CSM_M.size() == data.dim);
  IGL_ASSERT((int)L0.size() == (data.m)*data.dim*(data.dim+1));
  IGL_ASSERT(max_iters >= 0);
  IGL_ASSERT(tol >= 0);


  double
    sec_start,
    sec_covGather,
    sec_fitRotations,

    sec_prepMult,
    sec_solve, sec_end;

  assert(L0.cols() == 1);
#ifdef EXTREME_VERBOSE
  cout<<"dim="<<data.dim<<";"<<endl;
  cout<<"m="<<data.m<<";"<<endl;
#endif


  const int k = data.CSM_M[0].rows()/data.dim;
  for(int i = 0;i<data.dim;i++)
  {
    assert(data.CSM_M[i].rows()/data.dim == k);
  }
#ifdef EXTREME_VERBOSE
  cout<<"k="<<k<<";"<<endl;
#endif


  L = L0;
#ifndef IGL_ARAP_DOF_FIXED_ITERATIONS_COUNT

  MatrixXS L_prev;
#endif

  MatrixXS L_SSCALAR = L.cast<SSCALAR>();

  int iters = 0;
#ifndef IGL_ARAP_DOF_FIXED_ITERATIONS_COUNT
  double max_diff = tol+1;
#endif

  MatrixXS S(k*data.dim,data.dim);
  MatrixXS R(data.dim,data.dim*k);
  Eigen::Matrix<SSCALAR,Eigen::Dynamic,1> Rcol(data.dim * data.dim * k);
  Matrix<SSCALAR,Dynamic,1> B_eq_SSCALAR = B_eq.cast<SSCALAR>();
  Matrix<SSCALAR,Dynamic,1> L0SSCALAR = L0.cast<SSCALAR>();
  Matrix<SSCALAR,Dynamic,1> B_eq_fix_SSCALAR = L0SSCALAR(data.fixed_dim);


  MatrixXS Lsep(data.m*(data.dim + 1), 3);
  const MatrixXS L_part2 =
    data.M_FullSolve.block(0, Rcol.rows(), data.M_FullSolve.rows(), B_eq_SSCALAR.rows()) * B_eq_SSCALAR;
  const MatrixXS L_part3 =
    data.M_FullSolve.block(0, Rcol.rows() + B_eq_SSCALAR.rows(), data.M_FullSolve.rows(), B_eq_fix_SSCALAR.rows()) * B_eq_fix_SSCALAR;
  MatrixXS L_part2and3 = L_part2 + L_part3;


  MatrixXS Rxyz(k*data.dim, data.dim);
  MatrixXS L_part1xyz((data.dim + 1) * data.m, data.dim);
  MatrixXS L_part1(data.dim * (data.dim + 1) * data.m, 1);

#ifdef ARAP_GLOBAL_TIMING
    double timer_prepFinished = get_seconds();
#endif

#ifdef IGL_ARAP_DOF_FIXED_ITERATIONS_COUNT
  while(iters < max_iters)
#else
  while(iters < max_iters && max_diff > tol)
#endif
  {
    if(data.print_timings)
    {
      sec_start = get_seconds();
    }

#ifndef IGL_ARAP_DOF_FIXED_ITERATIONS_COUNT
    L_prev = L_SSCALAR;
#endif






    splitColumns(L_SSCALAR, data.m, data.dim, data.dim + 1, Lsep);

    S = data.CSM * Lsep;








    if(data.print_timings)
    {
      sec_covGather = get_seconds();
    }

#ifdef EXTREME_VERBOSE
    cout<<"S=["<<endl<<S<<endl<<"];"<<endl;
#endif

    if(data.effective_dim == 2)
    {
      fit_rotations_planar(S,R);
    }else
    {
#ifdef __SSE__
      fit_rotations_SSE(S,R);
#else
      fit_rotations(S,false,R);
#endif
    }

#ifdef EXTREME_VERBOSE
    cout<<"R=["<<endl<<R<<endl<<"];"<<endl;
#endif

    if(data.print_timings)
    {
      sec_fitRotations = get_seconds();
    }









    columnize(R, k, 2, Rcol);
#ifdef EXTREME_VERBOSE
    cout<<"Rcol=["<<endl<<Rcol<<endl<<"];"<<endl;
#endif
    splitColumns(Rcol, k, data.dim, data.dim, Rxyz);

    if(data.print_timings)
    {
      sec_prepMult = get_seconds();
    }

    L_part1xyz = data.CSolveBlock1 * Rxyz;





    mergeColumns(L_part1xyz, data.m, data.dim, data.dim + 1, L_part1);

    if(data.with_dynamics)
    {

      MatrixXS L_part1_dyn(data.dim * (data.dim + 1) * data.m, 1);














      MatrixXS temp = -1.0 *
          ( (-1.0/(data.h*data.h)) * data.L0.array() +
            (1.0/(data.h)) * data.Lvel0.array()
            ).matrix();
      MatrixXd temp_d = temp.template cast<double>();

      MatrixXd temp_g = data.fgrav*(data.grav_mag*data.grav_dir);

      assert(data.fext.rows() == temp_g.rows());
      assert(data.fext.cols() == temp_g.cols());
      MatrixXd temp2 = data.Mass_tilde * temp_d + temp_g + data.fext.template cast<double>();
      MatrixXS temp2_f = temp2.template cast<SSCALAR>();
      L_part1_dyn = data.Pi_1 * temp2_f;
      L_part1.array() = L_part1.array() + L_part1_dyn.array();
    }


    assert(L_SSCALAR.rows() == L_part1.rows() && L_SSCALAR.rows() == L_part2and3.rows());
    for (int i=0; i<L_SSCALAR.rows(); i++)
    {
      L_SSCALAR(i, 0) = L_part1(i, 0) + L_part2and3(i, 0);
    }

#ifdef EXTREME_VERBOSE
    cout<<"L=["<<endl<<L<<endl<<"];"<<endl;
#endif

    if(data.print_timings)
    {
      sec_solve = get_seconds();
    }

#ifndef IGL_ARAP_DOF_FIXED_ITERATIONS_COUNT

    max_diff = (L_SSCALAR-L_prev).eval().array().abs().matrix().maxCoeff();
#endif
    iters++;

    if(data.print_timings)
    {
      sec_end = get_seconds();
#ifndef WIN32

      if(false)
#endif
      printf(
        "\ntotal iteration time = %f "
        "[local: covGather = %f, "
        "fitRotations = %f, "
        "global: prep = %f, "
        "solve = %f, "
        "error = %f [ms]]\n",
        (sec_end - sec_start)*1000.0,
        (sec_covGather - sec_start)*1000.0,
        (sec_fitRotations - sec_covGather)*1000.0,
        (sec_prepMult - sec_fitRotations)*1000.0,
        (sec_solve - sec_prepMult)*1000.0,
        (sec_end - sec_solve)*1000.0 );
    }
  }


  L = L_SSCALAR.template cast<double>();
  assert(L.cols() == 1);

#ifdef ARAP_GLOBAL_TIMING
  double timer_finito = get_seconds();
  printf(
    "ARAP preparation = %f, "
    "all %i iterations = %f [ms]\n",
    (timer_prepFinished - timer_start)*1000.0,
    max_iters,
    (timer_finito - timer_prepFinished)*1000.0);
#endif

  return true;
}

#ifdef IGL_STATIC_LIBRARY

template bool igl::arap_dof_update<Eigen::Matrix<double, -1, -1, 0, -1, -1>, double>(ArapDOFData<Eigen::Matrix<double, -1, -1, 0, -1, -1>, double> const&, Eigen::Matrix<double, -1, 1, 0, -1, 1> const&, Eigen::Matrix<double, -1, -1, 0, -1, -1> const&, int, double, Eigen::Matrix<double, -1, -1, 0, -1, -1>&);
template bool igl::arap_dof_recomputation<Eigen::Matrix<double, -1, -1, 0, -1, -1>, double>(Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, Eigen::SparseMatrix<double, 0, int> const&, ArapDOFData<Eigen::Matrix<double, -1, -1, 0, -1, -1>, double>&);
template bool igl::arap_dof_precomputation<Eigen::Matrix<double, -1, -1, 0, -1, -1>, double>(Eigen::Matrix<double, -1, -1, 0, -1, -1> const&, Eigen::Matrix<int, -1, -1, 0, -1, -1> const&, Eigen::Matrix<double, -1, -1, 0, -1, -1> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, ArapDOFData<Eigen::Matrix<double, -1, -1, 0, -1, -1>, double>&);
template bool igl::arap_dof_update<Eigen::Matrix<double, -1, -1, 0, -1, -1>, float>(igl::ArapDOFData<Eigen::Matrix<double, -1, -1, 0, -1, -1>, float> const&, Eigen::Matrix<double, -1, 1, 0, -1, 1> const&, Eigen::Matrix<double, -1, -1, 0, -1, -1> const&, int, double, Eigen::Matrix<double, -1, -1, 0, -1, -1>&);
template bool igl::arap_dof_recomputation<Eigen::Matrix<double, -1, -1, 0, -1, -1>, float>(Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, Eigen::SparseMatrix<double, 0, int> const&, igl::ArapDOFData<Eigen::Matrix<double, -1, -1, 0, -1, -1>, float>&);
template bool igl::arap_dof_precomputation<Eigen::Matrix<double, -1, -1, 0, -1, -1>, float>(Eigen::Matrix<double, -1, -1, 0, -1, -1> const&, Eigen::Matrix<int, -1, -1, 0, -1, -1> const&, Eigen::Matrix<double, -1, -1, 0, -1, -1> const&, Eigen::Matrix<int, -1, 1, 0, -1, 1> const&, igl::ArapDOFData<Eigen::Matrix<double, -1, -1, 0, -1, -1>, float>&);
#endif
