// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "mosek_quadprog.h"
#include "mosek_guarded.h"
#include <cstdio>
#include "../find.h"
#include "../verbose.h"
#include "../speye.h"
#include "../matrix_to_list.h"
#include "../list_to_matrix.h"
#include "../harwell_boeing.h"
#include "../EPS.h"


igl::mosek::MosekData::MosekData()
{








  douparam[MSK_DPAR_INTPNT_TOL_REL_GAP]=1e-8;
#if MSK_VERSION_MAJOR >= 8
  douparam[MSK_DPAR_INTPNT_QO_TOL_REL_GAP]=1e-12;
#endif


#if MSK_VERSION_MAJOR >= 7
  intparam[MSK_IPAR_NUM_THREADS] = 6;
#elif MSK_VERSION_MAJOR == 6
  intparam[MSK_IPAR_INTPNT_NUM_THREADS] = 6;
#endif
#if MSK_VERSION_MAJOR == 6

  intparam[MSK_IPAR_DATA_CHECK]=MSK_OFF;
#endif










  intparam[MSK_IPAR_INTPNT_ORDER_METHOD] = MSK_ORDER_METHOD_NONE;

  douparam[MSK_DPAR_INTPNT_TOL_INFEAS] = 1e-8;

  douparam[MSK_DPAR_INTPNT_TOL_PSAFE]= 1e2;

  intparam[MSK_IPAR_CHECK_CONVEXITY] = MSK_CHECK_CONVEXITY_NONE;
}

template <typename Index, typename Scalar>
IGL_INLINE bool igl::mosek::mosek_quadprog(
  const Index n,
  std::vector<Index> & Qi,
  std::vector<Index> & Qj,
  std::vector<Scalar> & Qv,
  const std::vector<Scalar> & c,
  const Scalar cf,
  const Index m,
  std::vector<Scalar> & Av,
  std::vector<Index> & Ari,
  const std::vector<Index> & Acp,
  const std::vector<Scalar> & lc,
  const std::vector<Scalar> & uc,
  const std::vector<Scalar> & lx,
  const std::vector<Scalar> & ux,
  MosekData & mosek_data,
  std::vector<Scalar> & x)
{

  assert(Qv.size() == Qi.size());
  assert(Qv.size() == Qj.size());


  assert( (int)Acp.size() == (n+1));

  assert( ((int)lc.size() == m) || ((int)lc.size() == 0));
  assert( ((int)uc.size() == m) || ((int)uc.size() == 0));

  assert( ((int)lx.size() == n) || ((int)lx.size() == 0));
  assert( ((int)ux.size() == n) || ((int)ux.size() == 0));


  x.resize(n);


  MSKenv_t env;
  MSKtask_t task;


#if MSK_VERSION_MAJOR >= 7
  mosek_guarded(MSK_makeenv(&env,NULL));
#elif MSK_VERSION_MAJOR == 6
  mosek_guarded(MSK_makeenv(&env,NULL,NULL,NULL,NULL));
#endif








#if MSK_VERSION_MAJOR <= 7
  mosek_guarded(MSK_initenv(env));
#endif

  mosek_guarded(MSK_maketask(env,m,n,&task));
  verbose("Creating task with %ld linear constraints and %ld variables...\n",m,n);



  mosek_guarded(MSK_putmaxnumvar(task,n));
  if(m>0)
  {

    mosek_guarded(MSK_putmaxnumcon(task,m));

    mosek_guarded(MSK_putmaxnumanz(task,Av.size()));
  }

  mosek_guarded(MSK_putmaxnumqnz(task,Qv.size()));
  if(m>0)
  {


#if MSK_VERSION_MAJOR >= 7
    mosek_guarded(MSK_appendcons(task,m));
#elif MSK_VERSION_MAJOR == 6
    mosek_guarded(MSK_append(task,MSK_ACC_CON,m));
#endif
  }

#if MSK_VERSION_MAJOR >= 7
  mosek_guarded(MSK_appendvars(task,n));
#elif MSK_VERSION_MAJOR == 6
  mosek_guarded(MSK_append(task,MSK_ACC_VAR,n));
#endif

  mosek_guarded(MSK_putcfix(task,cf));


  for(int j = 0;j<n;j++)
  {
    if(c.size() > 0)
    {

      mosek_guarded(MSK_putcj(task,j,c[j]));
    }


    if(lx[j] == ux[j])
    {
#if MSK_VERSION_MAJOR <=8
      mosek_guarded(MSK_putbound(task,MSK_ACC_VAR,j,MSK_BK_FX,lx[j],ux[j]));
#else
      mosek_guarded(MSK_putvarbound(task,j,MSK_BK_FX,lx[j],ux[j]));
#endif
    }else
    {
#if MSK_VERSION_MAJOR <=8
      mosek_guarded(MSK_putbound(task,MSK_ACC_VAR,j,MSK_BK_RA,lx[j],ux[j]));
#else
      mosek_guarded(MSK_putvarbound(task,j,MSK_BK_RA,lx[j],ux[j]));
#endif
    }

    if(m>0)
    {

#if MSK_VERSION_MAJOR >= 7
      mosek_guarded( MSK_putacol( task, j, Acp[j+1]-Acp[j], &Ari[Acp[j]], &Av[Acp[j]]));
#elif MSK_VERSION_MAJOR == 6
      mosek_guarded( MSK_putavec( task, MSK_ACC_VAR, j, Acp[j+1]-Acp[j], &Ari[Acp[j]], &Av[Acp[j]]));
#endif
    }
  }


  for(int i = 0;i<m;i++)
  {

#if MSK_VERSION_MAJOR <=8
    mosek_guarded(MSK_putbound(task,MSK_ACC_CON,i,MSK_BK_RA,lc[i],uc[i]));
#else
    mosek_guarded(MSK_putconbound(task,i,MSK_BK_RA,lc[i],uc[i]));
#endif
  }


  mosek_guarded(MSK_putqobj(task,Qv.size(),&Qi[0],&Qj[0],&Qv[0]));


  for(
    std::map<MSKiparame,int>::iterator pit = mosek_data.intparam.begin();
    pit != mosek_data.intparam.end();
    pit++)
  {
    mosek_guarded(MSK_putintparam(task,pit->first,pit->second));
  }
  for(
    std::map<MSKdparame,double>::iterator pit = mosek_data.douparam.begin();
    pit != mosek_data.douparam.end();
    pit++)
  {
    mosek_guarded(MSK_putdouparam(task,pit->first,pit->second));
  }


  MSKrescodee trmcode;

  mosek_guarded(MSK_optimizetrm(task,&trmcode));






  MSKsolstae solsta;
#if MSK_VERSION_MAJOR >= 7
  MSK_getsolsta (task,MSK_SOL_ITR,&solsta);
#elif MSK_VERSION_MAJOR == 6
  MSK_getsolutionstatus(task,MSK_SOL_ITR,NULL,&solsta);
#endif

  bool success = false;
  switch(solsta)
  {
    case MSK_SOL_STA_OPTIMAL:
#if MSK_VERSION_MAJOR <= 8
    case MSK_SOL_STA_NEAR_OPTIMAL:
#endif
      MSK_getsolutionslice(task,MSK_SOL_ITR,MSK_SOL_ITEM_XX,0,n,&x[0]);





      success = true;
      break;
    case MSK_SOL_STA_DUAL_INFEAS_CER:
    case MSK_SOL_STA_PRIM_INFEAS_CER:
#if MSK_VERSION_MAJOR <= 8
    case MSK_SOL_STA_NEAR_DUAL_INFEAS_CER:
    case MSK_SOL_STA_NEAR_PRIM_INFEAS_CER:
#endif

      break;
    case MSK_SOL_STA_UNKNOWN:

      break;
    default:

      break;
  }

  MSK_deletetask(&task);
  MSK_deleteenv(&env);

  return success;
}

IGL_INLINE bool igl::mosek::mosek_quadprog(
  const Eigen::SparseMatrix<double> & Q,
  const Eigen::VectorXd & c,
  const double cf,
  const Eigen::SparseMatrix<double> & A,
  const Eigen::VectorXd & lc,
  const Eigen::VectorXd & uc,
  const Eigen::VectorXd & lx,
  const Eigen::VectorXd & ux,
  MosekData & mosek_data,
  Eigen::VectorXd & x)
{
  using namespace Eigen;
  using namespace std;

  typedef int Index;
  typedef double Scalar;

  assert(Q.rows() == Q.cols());

#ifdef EIGEN_HAS_A_BUG_AND_FAILS_TO_LET_ME_COMPUTE_Q_MINUS_Q_TRANSPOSE
  assert( (Q-Q.transpose()).sum() < FLOAT_EPS);
#endif

  SparseMatrix<Scalar> QL;

  QL = Q.triangularView<Lower>();
  VectorXi Qi,Qj;
  VectorXd Qv;
  find(QL,Qi,Qj,Qv);
  vector<Index> vQi = matrix_to_list(Qi);
  vector<Index> vQj = matrix_to_list(Qj);
  vector<Scalar> vQv = matrix_to_list(Qv);


  vector<Scalar> vc = matrix_to_list(c);

  assert(lc.size() == A.rows());
  assert(uc.size() == A.rows());

  vector<Scalar> vAv;
  vector<Index> vAr,vAc;
  Index nr;
  harwell_boeing(A,nr,vAv,vAr,vAc);

  assert(lx.size() == Q.rows());
  assert(ux.size() == Q.rows());
  vector<Scalar> vlc = matrix_to_list(lc);
  vector<Scalar> vuc = matrix_to_list(uc);
  vector<Scalar> vlx = matrix_to_list(lx);
  vector<Scalar> vux = matrix_to_list(ux);

  vector<Scalar> vx;
  bool ret = mosek_quadprog<Index,Scalar>(
    Q.rows(),vQi,vQj,vQv,
    vc,
    cf,
    nr,
    vAv, vAr, vAc,
    vlc,vuc,
    vlx,vux,
    mosek_data,
    vx);
  list_to_matrix(vx,x);
  return ret;
}
#ifdef IGL_STATIC_LIBRARY

#endif
