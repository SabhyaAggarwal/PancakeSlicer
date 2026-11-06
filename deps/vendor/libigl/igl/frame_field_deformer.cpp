// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Daniele Panozzo <daniele.panozzo@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "frame_field_deformer.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <vector>

#include "cotmatrix_entries.h"
#include "cotmatrix.h"
#include "vertex_triangle_adjacency.h"

namespace igl
{

class Frame_field_deformer
{
public:

  IGL_INLINE Frame_field_deformer();
  IGL_INLINE ~Frame_field_deformer();


  IGL_INLINE void init(const Eigen::MatrixXd& _V, const Eigen::MatrixXi& _F, const Eigen::MatrixXd& _D1, const Eigen::MatrixXd& _D2, double _Lambda, double _perturb_rotations, int _fixed = 1);


  IGL_INLINE void optimize(int N, bool reset = false);


  IGL_INLINE void reset_opt();


  IGL_INLINE void precompute_opt();


  IGL_INLINE void precompute_ARAP(Eigen::SparseMatrix<double> & Lff, Eigen::MatrixXd & LfcVc);


  IGL_INLINE void precompute_SMOOTH(Eigen::SparseMatrix<double> & MS, Eigen::MatrixXd & bS);



  IGL_INLINE void extractBlock(Eigen::SparseMatrix<double> & mat, int r0, int c0, int r, int c, Eigen::SparseMatrix<double> & m1);



  IGL_INLINE void compute_optimal_rotations();


  IGL_INLINE void compute_optimal_positions();


  IGL_INLINE void computeXField(std::vector< Eigen::Matrix<double,3,2> > & XF);


  IGL_INLINE void compute_idealWarp(std::vector< Eigen::Matrix<double,3,3> > & WW);





  Eigen::MatrixXd V;
  Eigen::MatrixXi F;

  std::vector<std::vector<int> > VT;
  std::vector<std::vector<int> > VTi;

  Eigen::MatrixXd V_w;

  std::vector< Eigen::Matrix<double,3,2> > FF;
  std::vector< Eigen::Matrix<double,3,3> > WW;
  std::vector< Eigen::Matrix<double,3,2> > XF;

  int fixed;

  double perturb_rotations;


  int nfree,nconst;
  Eigen::MatrixXd C;
  Eigen::SparseMatrix<double> L;

  Eigen::SparseMatrix<double> M;
  Eigen::MatrixXd RHS;
  std::vector< Eigen::Matrix<double,3,3> > RW;
  Eigen::SimplicialCholesky<Eigen::SparseMatrix<double> > solver;


private:
  double Lambda = 0.1;

};

  IGL_INLINE Frame_field_deformer::Frame_field_deformer() {}

  IGL_INLINE Frame_field_deformer::~Frame_field_deformer() {}

  IGL_INLINE void Frame_field_deformer::init(const Eigen::MatrixXd& _V,
                          const Eigen::MatrixXi& _F,
                          const Eigen::MatrixXd& _D1,
                          const Eigen::MatrixXd& _D2,
                          double _Lambda,
                          double _perturb_rotations,
                          int _fixed)
{
  V = _V;
  F = _F;

  assert(_D1.rows() == _D2.rows());

  FF.clear();
  for (unsigned i=0; i < _D1.rows(); ++i)
  {
    Eigen::Matrix<double,3,2> ff;
    ff.col(0) = _D1.row(i);
    ff.col(1) = _D2.row(i);
    FF.push_back(ff);
  }

  fixed = _fixed;
  Lambda = _Lambda;
  perturb_rotations = _perturb_rotations;

  reset_opt();
  precompute_opt();
}


IGL_INLINE void Frame_field_deformer::optimize(int N, bool reset)
{

	if (reset)
    reset_opt();


  for (int i=0; i<N;i++)
  {
    compute_optimal_rotations();
    compute_optimal_positions();
		computeXField(XF);
  }
}

IGL_INLINE void Frame_field_deformer::reset_opt()
{
  V_w = V;

  for (unsigned i=0; i<V_w.rows(); ++i)
    for (unsigned j=0; j<V_w.cols(); ++j)
      V_w(i,j) += (double(rand())/double(RAND_MAX))*10e-4*perturb_rotations;

}


IGL_INLINE void Frame_field_deformer::precompute_opt()
{
  using namespace Eigen;
	nfree = V.rows() - fixed;
  nconst = V.rows()-nfree;
  igl::vertex_triangle_adjacency(V,F,VT,VTi);

  igl::cotmatrix_entries(V,F,C);

  igl::cotmatrix(V,F,L);

	SparseMatrix<double> MA;
	MatrixXd LfcVc;
	SparseMatrix<double> MS;
	MatrixXd bS;

	precompute_ARAP(MA,LfcVc);
	precompute_SMOOTH(MS,bS);
	compute_idealWarp(WW);
  RW.resize(F.rows());

  M =	  (1-Lambda)*MA + Lambda*MS;

	RHS = (1-Lambda)*LfcVc + Lambda*bS;
  solver.compute(M);
  if (solver.info()!=Eigen::Success) {fprintf(stderr,"Decomposition failed in pre-conditioning!\n"); exit(-1);}

	fprintf(stdout,"Preconditioning done.\n");

}

IGL_INLINE void Frame_field_deformer::precompute_ARAP(Eigen::SparseMatrix<double> & Lff, Eigen::MatrixXd & LfcVc)
{
  using namespace Eigen;
	fprintf(stdout,"Precomputing ARAP terms\n");
	SparseMatrix<double> LL = -4*L;
	Lff = SparseMatrix<double>(nfree,nfree);
  extractBlock(LL,0,0,nfree,nfree,Lff);
	SparseMatrix<double> Lfc = SparseMatrix<double>(nfree,nconst);
  extractBlock(LL,0,nfree,nfree,nconst,Lfc);
	LfcVc = - Lfc * V_w.block(nfree,0,nconst,3);
}

IGL_INLINE void Frame_field_deformer::precompute_SMOOTH(Eigen::SparseMatrix<double> & MS, Eigen::MatrixXd & bS)
{
  using namespace Eigen;
	fprintf(stdout,"Precomputing SMOOTH terms\n");

	SparseMatrix<double> LL = 4*L*L;


	MS = SparseMatrix<double>(nfree,nfree);
  extractBlock(LL,0,0,nfree,nfree,MS);


	SparseMatrix<double> Mfc = SparseMatrix<double>(nfree,nconst);
  extractBlock(LL,0,nfree,nfree,nconst,Mfc);

	MatrixXd MfcVc = Mfc * V_w.block(nfree,0,nconst,3);
	bS = (LL*V).block(0,0,nfree,3)-MfcVc;

}

  IGL_INLINE void Frame_field_deformer::extractBlock(Eigen::SparseMatrix<double> & mat, int r0, int c0, int r, int c, Eigen::SparseMatrix<double> & m1)
{
  std::vector<Eigen::Triplet<double> > tripletList;
  for (int k=c0; k<c0+c; ++k)
    for (Eigen::SparseMatrix<double>::InnerIterator it(mat,k); it; ++it)
    {
      if (it.row()>=r0 && it.row()<r0+r)
        tripletList.push_back(Eigen::Triplet<double>(it.row()-r0,it.col()-c0,it.value()));
    }
  m1.setFromTriplets(tripletList.begin(), tripletList.end());
}

IGL_INLINE void Frame_field_deformer::compute_optimal_rotations()
{
  using namespace Eigen;
  Matrix<double,3,3> r,S,P,PP,D;

  for (int i=0;i<F.rows();i++)
	{

		P.col(0) = (V.row(F(i,1))-V.row(F(i,0))).transpose();
		P.col(1) = (V.row(F(i,2))-V.row(F(i,1))).transpose();
		P.col(2) = (V.row(F(i,0))-V.row(F(i,2))).transpose();

		P = WW[i] * P;


		PP.col(0) = (V_w.row(F(i,1))-V_w.row(F(i,0))).transpose();
		PP.col(1) = (V_w.row(F(i,2))-V_w.row(F(i,1))).transpose();
		PP.col(2) = (V_w.row(F(i,0))-V_w.row(F(i,2))).transpose();


		D <<    C(i,2), 0,      0,
    0,      C(i,0), 0,
    0,      0,      C(i,1);

		S = PP*D*P.transpose();
		Eigen::JacobiSVD<Matrix<double,3,3> > svd(S, Eigen::ComputeFullU | Eigen::ComputeFullV );
		Matrix<double,3,3>  su = svd.matrixU();
		Matrix<double,3,3>  sv = svd.matrixV();
		r = su*sv.transpose();

		if (r.determinant()<0)
		{
			su(0,2)=-su(0,2); su(1,2)=-su(1,2); su(2,2)=-su(2,2);
			r = su*sv.transpose();
		}
		RW[i] = r*WW[i];
	}
}

IGL_INLINE void Frame_field_deformer::compute_optimal_positions()
{
  using namespace Eigen;

  MatrixXd b(nfree,3);
	MatrixXd X;
  int t;
	int vi,i1,i2;

  for (int i=0;i<nfree;i++)
  {
    b.row(i) << 0.0, 0.0, 0.0;
    for (int k=0;k<(int)VT[i].size();k++)
    {
      t = VT[i][k];
			vi = (i==F(t,0))?0:(i==F(t,1))?1:(i==F(t,2))?2:3;
			assert(vi!=3);
			i1 = F(t,(vi+1)%3);
			i2 = F(t,(vi+2)%3);
			b.row(i)+=(C(t,(vi+2)%3)*RW[t]*(V.row(i1)-V.row(i)).transpose()).transpose();
			b.row(i)+=(C(t,(vi+1)%3)*RW[t]*(V.row(i2)-V.row(i)).transpose()).transpose();
    }
  }
  b/=2.0;
	b=-4*b;

	b*=(1-Lambda);

  b+=RHS;

	X = solver.solve(b);
	if (solver.info()!=Eigen::Success) {printf("Solving linear system failed!\n"); return;}


  for (int i=0;i<nfree;i++)
    V_w.row(i)=X.row(i);

}

  IGL_INLINE void Frame_field_deformer::computeXField(std::vector< Eigen::Matrix<double,3,2> > & XF)
{
  using namespace Eigen;
  Matrix<double,3,3> P,PP,DG;
	XF.resize(F.rows());

  for (int i=0;i<F.rows();i++)
	{
		int i0,i1,i2;

		i0 = F(i,0); i1 = F(i,1); i2 = F(i,2);


		P.col(0) = (V.row(i1)-V.row(i0)).transpose();
		P.col(1) = (V.row(i2)-V.row(i0)).transpose();
		P.col(2) = P.col(0).cross(P.col(1));


		PP.col(0) = (V_w.row(i1)-V_w.row(i0)).transpose();
		PP.col(1) = (V_w.row(i2)-V_w.row(i0)).transpose();
		PP.col(2) = PP.col(0).cross(PP.col(1));


		DG = PP * P.inverse();
		XF[i] = DG * FF[i];
	}
}


  IGL_INLINE void Frame_field_deformer::compute_idealWarp(std::vector< Eigen::Matrix<double,3,3> > & WW)
{
  using namespace Eigen;

  WW.resize(F.rows());
	for (int i=0;i<(int)FF.size();i++)
	{
		Vector3d v0,v1,v2;
		v0 = FF[i].col(0);
		v1 = FF[i].col(1);
		v2=v0.cross(v1); v2.normalize();

		Matrix3d A,AI;
		A <<    v0[0], v1[0], v2[0],
    v0[1], v1[1], v2[1],
    v0[2], v1[2], v2[2];
		AI = A.inverse();


		Eigen::JacobiSVD<Matrix<double,3,3> > svd(AI, Eigen::ComputeFullU | Eigen::ComputeFullV );

		Matrix<double,3,3>  av = svd.matrixV();
		DiagonalMatrix<double,3>	as(svd.singularValues());
		WW[i] = av*as*av.transpose();
	}
}

}


IGL_INLINE void igl::frame_field_deformer(
  const Eigen::MatrixXd& V,
  const Eigen::MatrixXi& F,
  const Eigen::MatrixXd& FF1,
  const Eigen::MatrixXd& FF2,
  Eigen::MatrixXd&       V_d,
  Eigen::MatrixXd&       FF1_d,
  Eigen::MatrixXd&       FF2_d,
  const int              iterations,
  const double           lambda,
  const bool             perturb_initial_guess)
{
  using namespace Eigen;

  Frame_field_deformer deformer;


  deformer.init(V, F, FF1, FF2, lambda, perturb_initial_guess ? 0.1 : 0);


  deformer.optimize(iterations,true);


  V_d = deformer.V_w;


  FF1_d.resize(F.rows(),3);
  FF2_d.resize(F.rows(),3);


  for(unsigned i=0; i<deformer.XF.size(); ++i)
  {
    FF1_d.row(i) = deformer.XF[i].col(0);
    FF2_d.row(i) = deformer.XF[i].col(1);
  }
}

#ifdef IGL_STATIC_LIBRARY

#endif
