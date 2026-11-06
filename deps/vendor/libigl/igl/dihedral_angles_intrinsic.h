// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_DIHEDRAL_ANGLES_INTRINSIC_H
#define IGL_DIHEDRAL_ANGLES_INTRINSIC_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{






  template <
    typename DerivedL,
    typename DerivedA,
    typename Derivedtheta,
    typename Derivedcos_theta>
  IGL_INLINE void dihedral_angles_intrinsic(
    const Eigen::MatrixBase<DerivedL>& L,
    const Eigen::MatrixBase<DerivedA>& A,
    Eigen::PlainObjectBase<Derivedtheta>& theta,
    Eigen::PlainObjectBase<Derivedcos_theta>& cos_theta);
}

#ifndef IGL_STATIC_LIBRARY
#  include "dihedral_angles_intrinsic.cpp"
#endif

#endif
