// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_HEAT_GEODESICS_H
#define IGL_HEAT_GEODESICS_H
#include "igl_inline.h"
#include "min_quad_with_fixed.h"
#include <Eigen/Sparse>
#include <Eigen/Sparse>
namespace igl
{



  template <typename Scalar>
  struct HeatGeodesicsData
  {

    Eigen::SparseMatrix<Scalar> Grad;

    Eigen::SparseMatrix<Scalar> Div;

    int ng;

    Eigen::VectorXi b;

    min_quad_with_fixed_data<Scalar> Dirichlet,Neumann,Poisson;

    bool use_intrinsic_delaunay = false;
  };








  template < typename DerivedV, typename DerivedF, typename Scalar >
  IGL_INLINE bool heat_geodesics_precompute(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    HeatGeodesicsData<Scalar> & data);




  template < typename DerivedV, typename DerivedF, typename Scalar >
  IGL_INLINE bool heat_geodesics_precompute(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Scalar t,
    HeatGeodesicsData<Scalar> & data);








  template < typename Scalar, typename Derivedgamma, typename DerivedD>
  IGL_INLINE void heat_geodesics_solve(
    const HeatGeodesicsData<Scalar> & data,
    const Eigen::MatrixBase<Derivedgamma> & gamma,
    Eigen::PlainObjectBase<DerivedD> & D);
}

#ifndef IGL_STATIC_LIBRARY
#include "heat_geodesics.cpp"
#endif

#endif
