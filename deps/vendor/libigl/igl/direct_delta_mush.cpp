// This file is part of libigl, a simple C++ geometry processing library.
//
// Copyright (C) 2020 Xiangyu Kong <xiangyu.kong@mail.utoronto.ca>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "direct_delta_mush.h"
#include "cotmatrix.h"
#include "PlainMatrix.h"

template <
  typename DerivedV,
  typename DerivedOmega,
  typename DerivedU>
IGL_INLINE void igl::direct_delta_mush(
  const Eigen::MatrixBase<DerivedV> & V,
  const std::vector<Eigen::Affine3d, Eigen::aligned_allocator<Eigen::Affine3d> > & T,
  const Eigen::MatrixBase<DerivedOmega> & Omega,
  Eigen::PlainObjectBase<DerivedU> & U)
{
  using namespace Eigen;


  assert(V.cols() == 3 && "V should contain 3D positions.");
  assert(Omega.rows() == V.rows() && "Omega contain the same number of rows as V.");
  assert(Omega.cols() == T.size() * 10 && "Omega should have #T*10 columns.");

  typedef typename DerivedV::Scalar Scalar;

  int n = V.rows();
  int m = T.size();





  Matrix<Scalar, Dynamic, 4> V_homogeneous(n, 4);
  V_homogeneous << V, Matrix<Scalar, Dynamic, 1>::Ones(n, 1);
  U.resize(n, 3);

  for (int i = 0; i < n; ++i)
  {

    Matrix<Scalar, 4, 4> Q_mat(4, 4);
    Q_mat = Matrix<Scalar, 4, 4>::Zero(4, 4);
    for (int j = 0; j < m; ++j)
    {
      Matrix<typename DerivedOmega::Scalar, 4, 4> Omega_curr(4, 4);
      Matrix<typename DerivedOmega::Scalar, 10, 1> curr = Omega.block(i, j * 10, 1, 10).transpose();
      Omega_curr << curr(0), curr(1), curr(2), curr(3),
        curr(1), curr(4), curr(5), curr(6),
        curr(2), curr(5), curr(7), curr(8),
        curr(3), curr(6), curr(8), curr(9);

      Affine3d M_curr = T[j];
      Q_mat += M_curr.matrix() * Omega_curr;
    }

    Q_mat /= Q_mat(Q_mat.rows() - 1, Q_mat.cols() - 1);

    Matrix<Scalar, 3, 3> Q_i = Q_mat.block(0, 0, 3, 3);
    Matrix<Scalar, 3, 1> q_i = Q_mat.block(0, 3, 3, 1);
    Matrix<Scalar, 3, 1> p_i = Q_mat.block(3, 0, 1, 3).transpose();


    Matrix<Scalar, 3, 3> SVD_i = Q_i - q_i * p_i.transpose();
    JacobiSVD<Matrix<Scalar, 3, 3>> svd;
    svd.compute(SVD_i, ComputeFullU | ComputeFullV);
    Matrix<Scalar, 3, 3> R_i = svd.matrixU() * svd.matrixV().transpose();
    Matrix<Scalar, 3, 1> t_i = q_i - R_i * p_i;


    Matrix<Scalar, 3, 4> Gamma_i(3, 4);
    Gamma_i.block(0, 0, 3, 3) = R_i;
    Gamma_i.block(0, 3, 3, 1) = t_i;


    Matrix<Scalar, 4, 1> v_i = V_homogeneous.row(i);
    U.row(i) = Gamma_i * v_i;
  }
}

template <
  typename DerivedV,
  typename DerivedF,
  typename DerivedW,
  typename DerivedOmega>
IGL_INLINE void igl::direct_delta_mush_precomputation(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedF> & F,
  const Eigen::MatrixBase<DerivedW> & W,
  const int p,
  const typename DerivedV::Scalar lambda,
  const typename DerivedV::Scalar kappa,
  const typename DerivedV::Scalar alpha,
  Eigen::PlainObjectBase<DerivedOmega> & Omega)
{
  using namespace Eigen;


  assert(V.cols() == 3 && "V should contain 3D positions.");
  assert(F.cols() == 3 && "F should contain triangles.");
  assert(W.rows() == V.rows() && "W.rows() should be equal to V.rows().");


  assert(p > 0 && "Laplacian iteration p should be positive.");
  assert(lambda > 0 && "lambda should be positive.");
  assert(kappa > 0 && kappa < lambda && "kappa should be positive and less than lambda.");
  assert(alpha >= 0 && alpha < 1 && "alpha should be non-negative and less than 1.");

  typedef typename DerivedV::Scalar Scalar;







  auto extract_upper_triangle = [](
    const Matrix<Scalar, Dynamic, Dynamic> & full) -> Matrix<Scalar, Dynamic, 1>
  {
    int dims = full.rows();
    Matrix<Scalar, Dynamic, 1> upper_triangle((dims * (dims + 1)) / 2);
    int vector_idx = 0;
    for (int i = 0; i < dims; ++i)
    {
      for (int j = i; j < dims; ++j)
      {
        upper_triangle(vector_idx) = full(i, j);
        vector_idx++;
      }
    }
    return upper_triangle;
  };

  const int n = V.rows();
  const int m = W.cols();





  Matrix<Scalar, Dynamic, 4> V_homogeneous(n, 4);
  V_homogeneous << V, Matrix<Scalar, Dynamic, 1>::Ones(n);


  SparseMatrix<Scalar> I(n, n);
  I.setIdentity();



  SparseMatrix<Scalar> L;
  igl::cotmatrix(V, F, L);
  L = -L;

  Matrix<Scalar, Dynamic, 1> D_L = L.diagonal();

  for (int i = 0; i < D_L.size(); ++i)
  {
    if (D_L(i) != 0)
    {
      D_L(i) = 1 / D_L(i);
    }
  }
  SparseMatrix<Scalar> D_L_inv = D_L.asDiagonal().toDenseMatrix().sparseView();
  SparseMatrix<Scalar> L_bar = L * D_L_inv;





  SimplicialLDLT<SparseMatrix<Scalar>> ldlt_W_prime;
  SparseMatrix<Scalar> c(I + kappa * L_bar);

  PlainMatrix<DerivedW,Dynamic,Dynamic> W_prime(W);
  ldlt_W_prime.compute(c.transpose());
  for (int iter = 0; iter < p; ++iter)
  {
    W_prime = ldlt_W_prime.solve(W_prime);
  }



  Matrix<Scalar, Dynamic, 10> U_precomputed(n, 10);
  for (int k = 0; k < n; ++k)
  {
    Matrix<Scalar, 4, 4> u_full = V_homogeneous.row(k).transpose() * V_homogeneous.row(k);
    U_precomputed.row(k) = extract_upper_triangle(u_full);
  }




  Matrix<Scalar, Dynamic, Dynamic> U_prime(n, m * 10);
  for (int j = 0; j < m; ++j)
  {
    Matrix<Scalar, Dynamic, 1> w_j = W.col(j);
    for (int x = 0; x < 10; ++x)
    {
      Matrix<Scalar, Dynamic, 1> u_x = U_precomputed.col(x);
      U_prime.col(10 * j + x) = w_j.array() * u_x.array();
    }
  }











  Matrix<Scalar, Dynamic, Dynamic> Psi = U_prime.sparseView();
  SparseMatrix<Scalar> b = (I + lambda * L_bar).transpose();
  SimplicialLDLT<SparseMatrix<Scalar>> ldlt_Psi;
  ldlt_Psi.compute(b);
  for (int iter = 0; iter < p; ++iter)
  {
    Psi = ldlt_Psi.solve(Psi);
  }





  Matrix<Scalar, Dynamic, 10> P(n, 10);
  for (int i = 0; i < n; ++i)
  {
    Matrix<Scalar, 3, 1> p_i = Matrix<Scalar, 3, 1>::Zero(3);
    Scalar last = 0;
    for (int j = 0; j < m; ++j)
    {
      Matrix<Scalar, 3, 1> p_i_curr(3);
      p_i_curr << Psi(i, j * 10 + 3), Psi(i, j * 10 + 6), Psi(i, j * 10 + 8);
      p_i += p_i_curr;
      last += Psi(i, j * 10 + 9);
    }
    p_i /= last;
    Matrix<Scalar, 4, 4> p_matrix(4, 4);
    p_matrix.block(0, 0, 3, 3) = p_i * p_i.transpose();
    p_matrix.block(0, 3, 3, 1) = p_i;
    p_matrix.block(3, 0, 1, 3) = p_i.transpose();
    p_matrix(3, 3) = 1;
    P.row(i) = extract_upper_triangle(p_matrix);
  }


  Omega.resize(n, m * 10);
  for (int i = 0; i < n; ++i)
  {
    Matrix<Scalar, 10, 1> p_vector = P.row(i);
    for (int j = 0; j < m; ++j)
    {
      Matrix<Scalar, 10, 1> Omega_curr(10);
      Matrix<Scalar, 10, 1> Psi_curr = Psi.block(i, j * 10, 1, 10).transpose();
      Omega_curr = (1. - alpha) * Psi_curr + alpha * W_prime(i, j) * p_vector;
      Omega.block(i, j * 10, 1, 10) = Omega_curr.transpose();
    }
  }
}

#ifdef IGL_STATIC_LIBRARY


template void igl::direct_delta_mush<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, std::vector<Eigen::Transform<double, 3, 2, 0>, Eigen::aligned_allocator<Eigen::Transform<double, 3, 2, 0> > > const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&); template void igl::direct_delta_mush_precomputation<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, int, Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&);
#endif
