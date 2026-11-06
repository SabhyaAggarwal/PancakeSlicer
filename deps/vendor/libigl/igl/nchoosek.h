// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Olga Diamanti, Alec Jacobson
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_NCHOOSEK
#define IGL_NCHOOSEK
#include "igl_inline.h"
#include <vector>

#include <Eigen/Core>

namespace igl
{





  IGL_INLINE double nchoosek(const int n, const int k);






  template < typename DerivedV, typename DerivedU>
  IGL_INLINE void nchoosek(
    const Eigen::MatrixBase<DerivedV> & V,
    const int k,
    Eigen::PlainObjectBase<DerivedU> & U);
}


#ifndef IGL_STATIC_LIBRARY
#include "nchoosek.cpp"
#endif


#endif
