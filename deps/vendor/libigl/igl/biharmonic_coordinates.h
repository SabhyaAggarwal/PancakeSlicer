// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_BIHARMONIC_COORDINATES_H
#define IGL_BIHARMONIC_COORDINATES_H
#include "igl_inline.h"
#include <Eigen/Dense>
#include <vector>
namespace igl
{















































  template <
    typename DerivedV,
    typename DerivedT,
    typename SType,
    typename DerivedW>
  IGL_INLINE bool biharmonic_coordinates(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedT> & T,
    const std::vector<std::vector<SType> > & S,
    Eigen::PlainObjectBase<DerivedW> & W);


  template <
    typename DerivedV,
    typename DerivedT,
    typename SType,
    typename DerivedW>
  IGL_INLINE bool biharmonic_coordinates(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedT> & T,
    const std::vector<std::vector<SType> > & S,
    const int k,
    Eigen::PlainObjectBase<DerivedW> & W);

};
#  ifndef IGL_STATIC_LIBRARY
#    include "biharmonic_coordinates.cpp"
#  endif
#endif
