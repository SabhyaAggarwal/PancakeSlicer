// high level interface for MshSaver
//
// Copyright (C) 2020 Vladimir Fonov <vladimir.fonov@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_WRITE_MSH_H
#define IGL_WRITE_MSH_H
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
  IGL_INLINE bool writeMSH(
    const std::string   &msh,
    const Eigen::MatrixBase<DerivedX> &X,
    const Eigen::MatrixBase<DerivedTri> &Tri,
    const Eigen::MatrixBase<DerivedTet> &Tet,
    const Eigen::MatrixBase<DerivedTriTag> &TriTag,
    const Eigen::MatrixBase<DerivedTetTag> &TetTag,
    const std::vector<std::string> &XFields,
    const std::vector<MatrixXF> &XF,
    const std::vector<std::string>  &EFields,
    const std::vector<MatrixTriF> &TriF,
    const std::vector<MatrixTetF> &TetF);
}

#ifndef IGL_STATIC_LIBRARY
#  include "writeMSH.cpp"
#endif

#endif
