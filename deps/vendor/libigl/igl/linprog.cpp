// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "linprog.h"
#include "find.h"
#include "colon.h"
#include "placeholders.h"


IGL_INLINE bool igl::linprog(
  const Eigen::VectorXd & c,
  const Eigen::MatrixXd & _A,
  const Eigen::VectorXd & b,
  const int k,
  Eigen::VectorXd & x)
{


  using namespace Eigen;
  using namespace std;
  bool success = true;

  const int m = _A.rows();

  const int n = _A.cols();

  int it = 0;


  const int MAXIT = 100*m;

  const double tol = 1e-10;
  const auto & sign = [](const Eigen::VectorXd & B) -> Eigen::VectorXd
  {
    Eigen::VectorXd Bsign(B.size());
    for(int i = 0;i<B.size();i++)
    {
      Bsign(i) = B(i)>0?1:(B(i)<0?-1:0);
    }
    return Bsign;
  };

  VectorXd Dv = sign(sign(b).array()+0.5);
  Dv.head(k).setConstant(1.);
  MatrixXd D = Dv.asDiagonal();

  MatrixXd A(_A.rows(),_A.cols()+D.cols());
  A<<_A,D;

  VectorXi B = igl::colon<int>(n,n+m-1);

  VectorXi N = igl::colon<int>(0,n-1);
  int j;
  double bmin = b.minCoeff(&j);
  int phase;
  VectorXd xb;
  VectorXd s;
  VectorXi J;
  if(k>0 && bmin<0)
  {
    phase = 1;
    xb = VectorXd::Ones(m);

    s.resize(n+m+1);
    s<<VectorXd::Zero(n+k),VectorXd::Ones(m-k+1);
    N.resize(n+1);
    N<<igl::colon<int>(0,n-1),B(j);
    J.resize(B.size()-1);




    J.head(j) = B.head(j);
    J.tail(B.size()-j-1) = B.tail(B.size()-j-1);
    B(j) = n+m;
    MatrixXd AJ = A(igl::placeholders::all,J);
    const VectorXd a = b - AJ.rowwise().sum();
    {
      MatrixXd old_A = A;
      A.resize(A.rows(),A.cols()+a.cols());
      A<<old_A,a;
    }
    D.col(j) = -a/a(j);
    D(j,j) = 1./a(j);
  }else if(k==m)
  {
    phase = 2;
    xb = b;
    s.resize(c.size()+m);

    s<<c,VectorXd::Zero(m);
  }else
  {
    phase = 1;
    xb = b.array().abs();
    s.resize(n+m);

    s<<VectorXd::Zero(n+k),VectorXd::Ones(m-k);
  }
  while(phase<3)
  {
    double df = -1;
    int t = std::numeric_limits<int>::max();

    VectorXd yb = D.transpose() * s(B);
    while(true)
    {
      if(MAXIT>0 && it>=MAXIT)
      {
#ifdef IGL_LINPROG_VERBOSE
        cerr<<"linprog: warning! maximum iterations without convergence."<<endl;
#endif
        success = false;
        break;
      }

      if(N.size() == 0)
      {
        break;
      }

      VectorXd sN = s(N);
      MatrixXd AN = A(igl::placeholders::all,N);
      VectorXd r = sN - AN.transpose() * yb;
      int q;

      double rmin = r.minCoeff(&q);

      if(rmin>=-tol*(sN.array().abs().maxCoeff()+1))
      {
        break;
      }

      it++;

      if(df>=0)
      {
        if(MAXIT == -1)
        {
#ifdef IGL_LINPROG_VERBOSE
          cerr<<"linprog: warning! degenerate vertex"<<endl;
#endif
          success = false;
        }
        igl::find((r.array()<0).eval(),J);
        double Nq = N(J).minCoeff();


        (N.array()==Nq).cast<int>().maxCoeff(&q);
      }
      VectorXd d = D*A.col(N(q));
      VectorXi I;
      igl::find((d.array()>tol).eval(),I);
      if(I.size() == 0)
      {
#ifdef IGL_LINPROG_VERBOSE
        cerr<<"linprog: warning! solution is unbounded"<<endl;
#endif

        it=-it;
        success = false;
        break;
      }
      VectorXd xbd = xb(I).array()/d(I).array();

      int p;
      {
        double r;
        r = xbd.minCoeff(&p);
        p = I(p);

        if(df>=0)
        {
          igl::find((xbd.array()==r).eval(),J);
          double Bp = B(I(J)).minCoeff();



          (B.array()==Bp).cast<int>().maxCoeff(&p);
        }

        xb -= r*d;
        xb(p) = r;

        df = r*rmin;
      }

      RowVectorXd v = D.row(p)/d(p);
      yb += v.transpose() * (s(N(q)) - d.transpose()*s(B));
      d(p)-=1;

      D = D - d*v;
      t = B(p);
      B(p) = N(q);
      if(t>(n+k-1))
      {

        VectorXi old_N = N;
        N.resize(N.size()-1);
        N.head(q) = old_N.head(q);
        N.head(q) = old_N.head(q);
        N.tail(old_N.size()-q-1) = old_N.tail(old_N.size()-q-1);
      }else
      {
        N(q) = t;
      }
    }

    xb = (xb+D*(b-A(igl::placeholders::all,B)*xb)).eval();

    VectorXi I;
    igl::find((xb.array()<0).eval(),I);
    if(I.size()>0)
    {

      xb(I) = VectorXd::Zero(I.size(),1);
    }

    if(phase == 2 || it<0)
    {
      break;
    }
    if(xb.transpose()*s(B) > tol)
    {
      it = -it;
#ifdef IGL_LINPROG_VERBOSE
      cerr<<"linprog: warning, no feasible solution"<<endl;
#endif
      success = false;
      break;
    }

    phase = phase+1;
    s*=1e6*c.array().abs().maxCoeff();
    s.head(n) = c;
  }
  x.setZero(std::max(B.maxCoeff()+1,n));
  x(B) = xb;
  x = x.head(n).eval();
  return success;
}

IGL_INLINE bool igl::linprog(
  const Eigen::VectorXd & f,
  const Eigen::MatrixXd & A,
  const Eigen::VectorXd & b,
  const Eigen::MatrixXd & B,
  const Eigen::VectorXd & c,
  Eigen::VectorXd & x)
{
  using namespace Eigen;
  using namespace std;
  const int m = A.rows();
  const int n = A.cols();
  const int p = B.rows();
  MatrixXd Im = MatrixXd::Identity(m,m);
  MatrixXd AS(m,n+m);
  AS<<A,Im;
  MatrixXd bS = b.array().abs();
  for(int i = 0;i<m;i++)
  {
    const auto & sign = [](double x)->double
    {
      return (x<0?-1:(x>0?1:0));
    };
    AS.row(i) *= sign(b(i));
  }
  MatrixXd In = MatrixXd::Identity(n,n);
  MatrixXd P(n+m,2*n+m);
  P<<              In, -In, MatrixXd::Zero(n,m),
     MatrixXd::Zero(m,2*n), Im;
  MatrixXd ASP = AS*P;
  MatrixXd BSP(0,2*n+m);
  if(p>0)
  {

    MatrixXd BS(p,n+m);
    BS<<B,MatrixXd::Zero(p,m);

    BSP = BS*P;

  }

  VectorXd fSP = VectorXd::Ones(2*n+m);
  fSP.head(2*n) = P.block(0,0,n,2*n).transpose()*f;
  const VectorXd & cc = fSP;

  MatrixXd AA(m+p,2*n+m);
  AA<<ASP,BSP;
  VectorXd bb(m+p);
  bb<<bS,c;

  VectorXd xxs;
















  bool ret = linprog(cc,AA,bb,0,xxs);

  x = P.block(0,0,n,2*n+m)*xxs;
  return ret;
}
