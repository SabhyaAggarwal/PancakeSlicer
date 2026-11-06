// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_READOFF_H
#define IGL_READOFF_H
#include "igl_inline.h"



#ifndef IGL_NO_EIGEN
#  include <Eigen/Core>
#endif
#include <string>
#include <vector>
#include <cstdio>

namespace igl
{












  template <typename Scalar, typename Index>
  IGL_INLINE bool readOFF(
    const std::string off_file_name,
    std::vector<std::vector<Scalar > > & V,
    std::vector<std::vector<Index > > & F,
    std::vector<std::vector<Scalar > > & N,
    std::vector<std::vector<Scalar > > & C);

  template <typename DerivedV, typename DerivedF>
  IGL_INLINE bool readOFF(
    const std::string str,
    Eigen::PlainObjectBase<DerivedV>& V,
    Eigen::PlainObjectBase<DerivedF>& F);

  template <typename DerivedV, typename DerivedF>
  IGL_INLINE bool readOFF(
    const std::string str,
    Eigen::PlainObjectBase<DerivedV>& V,
    Eigen::PlainObjectBase<DerivedF>& F,
    Eigen::PlainObjectBase<DerivedV>& N);


  template <typename Scalar, typename Index>
  IGL_INLINE bool readOFF(
    FILE * off_file,
    std::vector<std::vector<Scalar > > & V,
    std::vector<std::vector<Index > > & F,
    std::vector<std::vector<Scalar > > & N,
    std::vector<std::vector<Scalar > > & C);

}

#ifndef IGL_STATIC_LIBRARY
#  include "readOFF.cpp"
#endif

#endif
