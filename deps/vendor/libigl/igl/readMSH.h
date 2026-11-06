// high level interface for MshLoader.h/.cpp

// Copyright (C) 2020 Vladimir Fonov <vladimir.fonov@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distribute
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_READ_MSH_H
#define IGL_READ_MSH_H
#include "igl_inline.h"

#include <Eigen/Core>
#include <string>
#include <vector>


namespace igl
{





















    template <
      typename DerivedX,
      typename DerivedTri,
      typename DerivedTet,
      typename DerivedTriTag,
      typename DerivedTetTag,
      typename MatrixXF,
      typename MatrixTriF,
      typename MatrixTetF
      >
    IGL_INLINE bool readMSH(
      const std::string &msh,
      Eigen::PlainObjectBase<DerivedX> &X,
      Eigen::PlainObjectBase<DerivedTri> &Tri,
      Eigen::PlainObjectBase<DerivedTet> &Tet,
      Eigen::PlainObjectBase<DerivedTriTag> &TriTag,
      Eigen::PlainObjectBase<DerivedTetTag> &TetTag,
      std::vector<std::string>     &XFields,
      std::vector<MatrixXF> &XF,
      std::vector<std::string>     &EFields,
      std::vector<MatrixTriF> &TriF,
      std::vector<MatrixTetF> &TetF);

    template <int EigenMatrixOptions>
    IGL_INLINE bool readMSH(
      const std::string &msh,
      Eigen::Matrix<double,Eigen::Dynamic,Eigen::Dynamic,EigenMatrixOptions> &X,
      Eigen::Matrix<int,Eigen::Dynamic,Eigen::Dynamic,EigenMatrixOptions> &Tri,
      Eigen::Matrix<int,Eigen::Dynamic,Eigen::Dynamic,EigenMatrixOptions> &Tet,
      Eigen::VectorXi &TriTag,
      Eigen::VectorXi &TetTag);

    template <int EigenMatrixOptions>
    IGL_INLINE bool readMSH(
      const std::string &msh,
      Eigen::Matrix<double,Eigen::Dynamic,Eigen::Dynamic,EigenMatrixOptions> &X,
      Eigen::Matrix<int,Eigen::Dynamic,Eigen::Dynamic,EigenMatrixOptions> &Tri,
      Eigen::VectorXi &TriTag);

    template <int EigenMatrixOptions>
    IGL_INLINE bool readMSH(
      const std::string &msh,
      Eigen::Matrix<double,Eigen::Dynamic,Eigen::Dynamic,EigenMatrixOptions> &X,
      Eigen::Matrix<int,Eigen::Dynamic,Eigen::Dynamic,EigenMatrixOptions> &Tri);

}


#ifndef IGL_STATIC_LIBRARY
#  include "readMSH.cpp"
#endif

#endif
