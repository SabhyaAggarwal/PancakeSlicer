// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_BOUNDARY_CONDITIONS_H
#define IGL_BOUNDARY_CONDITIONS_H
#include "igl_inline.h"
#include <Eigen/Dense>

namespace igl
{
























  template <
    typename DerivedV,
    typename DerivedEle,
    typename DerivedC,
    typename DerivedP,
    typename DerivedBE,
    typename DerivedCE,
    typename DerivedCF,
    typename Derivedb,
    typename Derivedbc>
  IGL_INLINE bool boundary_conditions(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedEle> & Ele,
    const Eigen::MatrixBase<DerivedC> & C,
    const Eigen::MatrixBase<DerivedP> & P,
    const Eigen::MatrixBase<DerivedBE> & BE,
    const Eigen::MatrixBase<DerivedCE> & CE,
    const Eigen::MatrixBase<DerivedCF> & CF,
    Eigen::PlainObjectBase<Derivedb> & b,
    Eigen::PlainObjectBase<Derivedbc> & bc);
}

#ifndef IGL_STATIC_LIBRARY
#  include "boundary_conditions.cpp"
#endif

#endif
