// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Alec Jacobson
// Copyright (C) 2016 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#include "delaunay_triangulation.h"
#include "../../delaunay_triangulation.h"
#include "orient2D.h"
#include "incircle.h"

template<
  typename DerivedV,
  typename DerivedF>
IGL_INLINE void igl::copyleft::cgal::delaunay_triangulation(
    const Eigen::MatrixBase<DerivedV>& V,
    Eigen::PlainObjectBase<DerivedF>& F)
{
  typedef typename DerivedV::Scalar Scalar;
  igl::delaunay_triangulation(V, orient2D<Scalar>, incircle<Scalar>, F);





































}

#ifdef IGL_STATIC_LIBRARY


template void igl::copyleft::cgal::delaunay_triangulation<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
#endif
