// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_WRITEOBJ_H
#define IGL_WRITEOBJ_H
#include "igl_inline.h"



#include <Eigen/Core>
#include <string>
#include <vector>

namespace igl
{















  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedCN,
    typename DerivedFN,
    typename DerivedTC,
    typename DerivedFTC>
  IGL_INLINE bool writeOBJ(
    const std::string str,
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F,
    const Eigen::MatrixBase<DerivedCN>& CN,
    const Eigen::MatrixBase<DerivedFN>& FN,
    const Eigen::MatrixBase<DerivedTC>& TC,
    const Eigen::MatrixBase<DerivedFTC>& FTC);

  template <typename DerivedV, typename DerivedF>
  IGL_INLINE bool writeOBJ(
    const std::string str,
    const Eigen::MatrixBase<DerivedV>& V,
    const Eigen::MatrixBase<DerivedF>& F);






  template <typename DerivedV, typename T>
  IGL_INLINE bool writeOBJ(
    const std::string &str,
    const Eigen::MatrixBase<DerivedV>& V,
    const std::vector<std::vector<T> >& F);

}

#ifndef IGL_STATIC_LIBRARY
#  include "writeOBJ.cpp"
#endif

#endif
