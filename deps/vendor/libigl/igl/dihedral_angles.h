// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_DIHEDRAL_ANGLES_H
#define IGL_DIHEDRAL_ANGLES_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{







  template <
    typename DerivedV,
    typename DerivedT,
    typename Derivedtheta,
    typename Derivedcos_theta>
  IGL_INLINE void dihedral_angles(
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedT>& T,
    Eigen::PlainObjectBase<Derivedtheta>& theta,
    Eigen::PlainObjectBase<Derivedcos_theta>& cos_theta);
}

#ifndef IGL_STATIC_LIBRARY
#  include "dihedral_angles.cpp"
#endif

#endif
