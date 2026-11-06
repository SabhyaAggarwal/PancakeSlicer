// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "per_vertex_normals.h"

#include "get_seconds.h"
#include "per_face_normals.h"
#include "doublearea.h"
#include "parallel_for.h"
#include "internal_angles.h"

template <
  typename DerivedV,
  typename DerivedF,
  typename DerivedN>
IGL_INLINE void igl::per_vertex_normals(
  const Eigen::MatrixBase<DerivedV>& V,
  const Eigen::MatrixBase<DerivedF>& F,
  const igl::PerVertexNormalsWeightingType weighting,
  Eigen::PlainObjectBase<DerivedN> & N)
{
  Eigen::Matrix<typename DerivedV::Scalar,Eigen::Dynamic,3> PFN;
  igl::per_face_normals(V,F,PFN);
  return per_vertex_normals(V,F,weighting,PFN,N);
}

template <typename DerivedV, typename DerivedF, typename DerivedN>
IGL_INLINE void igl::per_vertex_normals(
  const Eigen::MatrixBase<DerivedV>& V,
  const Eigen::MatrixBase<DerivedF>& F,
  Eigen::PlainObjectBase<DerivedN> & N)
{
  return per_vertex_normals(V,F,PER_VERTEX_NORMALS_WEIGHTING_TYPE_DEFAULT,N);
}

template <typename DerivedV, typename DerivedF, typename DerivedFN, typename DerivedN>
IGL_INLINE void igl::per_vertex_normals(
  const Eigen::MatrixBase<DerivedV>& V,
  const Eigen::MatrixBase<DerivedF>& F,
  const igl::PerVertexNormalsWeightingType weighting,
  const Eigen::MatrixBase<DerivedFN>& FN,
  Eigen::PlainObjectBase<DerivedN> & N)
{
  using namespace std;

  N.setZero(V.rows(),3);

  Eigen::Matrix<typename DerivedN::Scalar,DerivedF::RowsAtCompileTime,3>
    W(F.rows(),3);
  switch(weighting)
  {
    case PER_VERTEX_NORMALS_WEIGHTING_TYPE_UNIFORM:
      W.setConstant(1.);
      break;
    default:
      assert(false && "Unknown weighting type");
    case PER_VERTEX_NORMALS_WEIGHTING_TYPE_DEFAULT:
    case PER_VERTEX_NORMALS_WEIGHTING_TYPE_AREA:
    {
      Eigen::Matrix<typename DerivedN::Scalar,DerivedF::RowsAtCompileTime,1> A;
      doublearea(V,F,A);
      W = A.replicate(1,3);
      break;
    }
    case PER_VERTEX_NORMALS_WEIGHTING_TYPE_ANGLE:
      internal_angles(V,F,W);
      break;
  }


  for(int i = 0;i<F.rows();i++)
  {

    for(int j = 0; j < 3;j++)
    {
      N.row(F(i,j)) += W(i,j) * FN.row(i);
    }
  }





















  N.rowwise().normalize();
}

template <
  typename DerivedV,
  typename DerivedF,
  typename DerivedFN,
  typename DerivedN>
IGL_INLINE void igl::per_vertex_normals(
  const Eigen::MatrixBase<DerivedV>& V,
  const Eigen::MatrixBase<DerivedF>& F,
  const Eigen::MatrixBase<DerivedFN>& FN,
  Eigen::PlainObjectBase<DerivedN> & N)
{
  return
    per_vertex_normals(V,F,PER_VERTEX_NORMALS_WEIGHTING_TYPE_DEFAULT,FN,N);
}

#ifdef IGL_STATIC_LIBRARY


template void igl::per_vertex_normals<Eigen::Matrix<double, -1, 3, 0, -1, 3>, Eigen::Matrix<int, -1, 3, 0, -1, 3>, Eigen::Matrix<double, -1, 3, 0, -1, 3>, Eigen::Matrix<double, -1, 3, 0, -1, 3> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, 3, 0, -1, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 3, 0, -1, 3> > const&, igl::PerVertexNormalsWeightingType, Eigen::MatrixBase<Eigen::Matrix<double, -1, 3, 0, -1, 3> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 3, 0, -1, 3> >&);
template void igl::per_vertex_normals<Eigen::Matrix<float, -1, 3, 1, -1, 3>, Eigen::Matrix<unsigned int, -1, 3, 1, -1, 3>, Eigen::Matrix<float, -1, 3, 1, -1, 3> >(Eigen::MatrixBase<Eigen::Matrix<float, -1, 3, 1, -1, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<unsigned int, -1, 3, 1, -1, 3> > const&, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, 3, 1, -1, 3> >&);

template void igl::per_vertex_normals<Eigen::Matrix<float, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 3, 1, -1, 3>, Eigen::Matrix<float, -1, -1, 0, -1, -1>, Eigen::Matrix<float, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<float, -1, 3, 1, -1, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 3, 1, -1, 3> > const&, igl::PerVertexNormalsWeightingType, Eigen::MatrixBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> >&);

template void igl::per_vertex_normals<Eigen::Matrix<float, -1, 3, 0, -1, 3>, Eigen::Matrix<int, -1, 3, 0, -1, 3>, Eigen::Matrix<float, -1, -1, 0, -1, -1>, Eigen::Matrix<float, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<float, -1, 3, 0, -1, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 3, 0, -1, 3> > const&, igl::PerVertexNormalsWeightingType, Eigen::MatrixBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> >&);

template void igl::per_vertex_normals<Eigen::Matrix<double, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 3, 1, -1, 3>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, 3, 1, -1, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 3, 1, -1, 3> > const&, igl::PerVertexNormalsWeightingType, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&);

template void igl::per_vertex_normals<Eigen::Matrix<double, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 3, 1, -1, 3>, Eigen::Matrix<double, -1, 3, 0, -1, 3>, Eigen::Matrix<double, -1, 3, 0, -1, 3> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, 3, 1, -1, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 3, 1, -1, 3> > const&, igl::PerVertexNormalsWeightingType, Eigen::MatrixBase<Eigen::Matrix<double, -1, 3, 0, -1, 3> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 3, 0, -1, 3> >&);
template void igl::per_vertex_normals<Eigen::Matrix<float, -1, 3, 0, -1, 3>, Eigen::Matrix<int, -1, 3, 0, -1, 3>, Eigen::Matrix<float, -1, 3, 0, -1, 3>, Eigen::Matrix<float, -1, 3, 0, -1, 3> >(Eigen::MatrixBase<Eigen::Matrix<float, -1, 3, 0, -1, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 3, 0, -1, 3> > const&, igl::PerVertexNormalsWeightingType, Eigen::MatrixBase<Eigen::Matrix<float, -1, 3, 0, -1, 3> > const&, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, 3, 0, -1, 3> >&);
template void igl::per_vertex_normals<Eigen::Matrix<float, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 3, 1, -1, 3>, Eigen::Matrix<float, -1, 3, 0, -1, 3>, Eigen::Matrix<float, -1, 3, 0, -1, 3> >(Eigen::MatrixBase<Eigen::Matrix<float, -1, 3, 1, -1, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 3, 1, -1, 3> > const&, igl::PerVertexNormalsWeightingType, Eigen::MatrixBase<Eigen::Matrix<float, -1, 3, 0, -1, 3> > const&, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, 3, 0, -1, 3> >&);
template void igl::per_vertex_normals<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, igl::PerVertexNormalsWeightingType, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&);
template void igl::per_vertex_normals<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&);
template void igl::per_vertex_normals<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, 3, 0, -1, 3>, Eigen::Matrix<double, -1, 3, 0, -1, 3> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, igl::PerVertexNormalsWeightingType, Eigen::MatrixBase<Eigen::Matrix<double, -1, 3, 0, -1, 3> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 3, 0, -1, 3> >&);
template void igl::per_vertex_normals<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, igl::PerVertexNormalsWeightingType, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&);
template void igl::per_vertex_normals<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&);
#endif
