// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Francis Williams <francis@fwilliams.info>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_MARCHING_TETS_H
#define IGL_MARCHING_TETS_H

#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Sparse>

namespace igl {













  template <typename DerivedTV,
            typename DerivedTT,
            typename DerivedS,
            typename DerivedSV,
            typename DerivedSF,
            typename DerivedJ,
            typename BCType>
  IGL_INLINE void marching_tets(
      const Eigen::MatrixBase<DerivedTV>& TV,
      const Eigen::MatrixBase<DerivedTT>& TT,
      const Eigen::MatrixBase<DerivedS>& S,
      const typename DerivedS::Scalar isovalue,
      Eigen::PlainObjectBase<DerivedSV>& SV,
      Eigen::PlainObjectBase<DerivedSF>& SF,
      Eigen::PlainObjectBase<DerivedJ>& J,
      Eigen::SparseMatrix<BCType>& BC);


  template <typename DerivedTV,
            typename DerivedTT,
            typename DerivedS,
            typename DerivedSV,
            typename DerivedSF,
            typename DerivedJ,
            typename BCType>
  IGL_INLINE void marching_tets(
      const Eigen::MatrixBase<DerivedTV>& TV,
      const Eigen::MatrixBase<DerivedTT>& TT,
      const Eigen::MatrixBase<DerivedS>& S,
      Eigen::PlainObjectBase<DerivedSV>& SV,
      Eigen::PlainObjectBase<DerivedSF>& SF,
      Eigen::PlainObjectBase<DerivedJ>& J,
      Eigen::SparseMatrix<BCType>& BC) {
    return igl::marching_tets(TV, TT, S, 0.0, SV, SF, J, BC);
  }

  template <typename DerivedTV,
            typename DerivedTT,
            typename DerivedS,
            typename DerivedSV,
            typename DerivedSF,
            typename DerivedJ>
  IGL_INLINE void marching_tets(
      const Eigen::MatrixBase<DerivedTV>& TV,
      const Eigen::MatrixBase<DerivedTT>& TT,
      const Eigen::MatrixBase<DerivedS>& S,
      const typename DerivedS::Scalar isovalue,
      Eigen::PlainObjectBase<DerivedSV>& SV,
      Eigen::PlainObjectBase<DerivedSF>& SF,
      Eigen::PlainObjectBase<DerivedJ>& J) {
    Eigen::SparseMatrix<typename DerivedSV::Scalar> _BC;
    return igl::marching_tets(TV, TT, S, isovalue, SV, SF, J, _BC);
  }

  template <typename DerivedTV,
            typename DerivedTT,
            typename DerivedS,
            typename DerivedSV,
            typename DerivedSF,
            typename BCType>
  IGL_INLINE void marching_tets(
      const Eigen::MatrixBase<DerivedTV>& TV,
      const Eigen::MatrixBase<DerivedTT>& TT,
      const Eigen::MatrixBase<DerivedS>& S,
      const typename DerivedS::Scalar isovalue,
      Eigen::PlainObjectBase<DerivedSV>& SV,
      Eigen::PlainObjectBase<DerivedSF>& SF,
      Eigen::SparseMatrix<BCType>& BC) {
    Eigen::VectorXi _J;
    return igl::marching_tets(TV, TT, S, isovalue, SV, SF, _J, BC);
  }

  template <typename DerivedTV,
            typename DerivedTT,
            typename DerivedS,
            typename DerivedSV,
            typename DerivedSF>
  IGL_INLINE void marching_tets(
      const Eigen::MatrixBase<DerivedTV>& TV,
      const Eigen::MatrixBase<DerivedTT>& TT,
      const Eigen::MatrixBase<DerivedS>& S,
      const typename DerivedS::Scalar isovalue,
      Eigen::PlainObjectBase<DerivedSV>& SV,
      Eigen::PlainObjectBase<DerivedSF>& SF) {
    Eigen::VectorXi _J;
    Eigen::SparseMatrix<typename DerivedSV::Scalar> _BC;
    return igl::marching_tets(TV, TT, S, isovalue, SV, SF, _J, _BC);
  }

}

#ifndef IGL_STATIC_LIBRARY
#  include "marching_tets.cpp"
#endif

#endif
