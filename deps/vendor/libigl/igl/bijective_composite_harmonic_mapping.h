// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2017 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_BIJECTIVE_COMPOSITE_HARMONIC_MAPPING_H
#define IGL_BIJECTIVE_COMPOSITE_HARMONIC_MAPPING_H
#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{


















  template <
    typename DerivedV,
    typename DerivedF,
    typename Derivedb,
    typename Derivedbc,
    typename DerivedU>
  IGL_INLINE bool bijective_composite_harmonic_mapping(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<Derivedb> & b,
    const Eigen::MatrixBase<Derivedbc> & bc,
    Eigen::PlainObjectBase<DerivedU> & U);









  template <
    typename DerivedV,
    typename DerivedF,
    typename Derivedb,
    typename Derivedbc,
    typename DerivedU>
  IGL_INLINE bool bijective_composite_harmonic_mapping(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<Derivedb> & b,
    const Eigen::MatrixBase<Derivedbc> & bc,
    const int min_steps,
    const int max_steps,
    const int num_inner_iters,
    const bool test_for_flips,
    Eigen::PlainObjectBase<DerivedU> & U);
}

#ifndef IGL_STATIC_LIBRARY
#  include "bijective_composite_harmonic_mapping.cpp"
#endif
#endif
