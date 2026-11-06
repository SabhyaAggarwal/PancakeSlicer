// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_MOSEK_BBW_H
#define IGL_MOSEK_BBW_H
#include "../igl_inline.h"
#include "mosek_quadprog.h"
#include "../bbw.h"
#include <Eigen/Dense>

namespace igl
{
  namespace mosek
  {

















    template <
      typename DerivedV,
      typename DerivedEle,
      typename Derivedb,
      typename Derivedbc,
      typename DerivedW>
    IGL_INLINE bool bbw(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedEle> & Ele,
      const Eigen::MatrixBase<Derivedb> & b,
      const Eigen::MatrixBase<Derivedbc> & bc,
      igl::BBWData & data,
      igl::mosek::MosekData & mosek_data,
      Eigen::PlainObjectBase<DerivedW> & W);
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "bbw.cpp"
#endif

#endif
