// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_READOBJ_H
#define IGL_READOBJ_H
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
  IGL_INLINE bool readOBJ(
    const std::string obj_file_name,
    std::vector<std::vector<Scalar > > & V,
    std::vector<std::vector<Scalar > > & TC,
    std::vector<std::vector<Scalar > > & N,
    std::vector<std::vector<Index > > & F,
    std::vector<std::vector<Index > > & FTC,
    std::vector<std::vector<Index > > & FN,
    std::vector<std::tuple<std::string, Index, Index >> &FM
    );

  template <typename Scalar, typename Index>
  IGL_INLINE bool readOBJ(
    const std::string obj_file_name,
    std::vector<std::vector<Scalar > > & V,
    std::vector<std::vector<Scalar > > & TC,
    std::vector<std::vector<Scalar > > & N,
    std::vector<std::vector<Index > > & F,
    std::vector<std::vector<Index > > & FTC,
    std::vector<std::vector<Index > > & FN);


  template <typename Scalar, typename Index>
  IGL_INLINE bool readOBJ(
    FILE * obj_file,
    std::vector<std::vector<Scalar > > & V,
    std::vector<std::vector<Scalar > > & TC,
    std::vector<std::vector<Scalar > > & N,
    std::vector<std::vector<Index > > & F,
    std::vector<std::vector<Index > > & FTC,
    std::vector<std::vector<Index > > & FN,
    std::vector<std::tuple<std::string, Index, Index >> &FM);

  template <typename Scalar, typename Index>
  IGL_INLINE bool readOBJ(
    const std::string obj_file_name,
    std::vector<std::vector<Scalar > > & V,
    std::vector<std::vector<Index > > & F);




  template <
    typename DerivedV,
    typename DerivedTC,
    typename DerivedCN,
    typename DerivedF,
    typename DerivedFTC,
    typename DerivedFN>
  IGL_INLINE bool readOBJ(
    const std::string str,
    Eigen::PlainObjectBase<DerivedV>& V,
    Eigen::PlainObjectBase<DerivedTC>& TC,
    Eigen::PlainObjectBase<DerivedCN>& CN,
    Eigen::PlainObjectBase<DerivedF>& F,
    Eigen::PlainObjectBase<DerivedFTC>& FTC,
    Eigen::PlainObjectBase<DerivedFN>& FN);

  template <typename DerivedV, typename DerivedF>
  IGL_INLINE bool readOBJ(
    const std::string str,
    Eigen::PlainObjectBase<DerivedV>& V,
    Eigen::PlainObjectBase<DerivedF>& F);






  template <typename DerivedV, typename DerivedI, typename DerivedC>
  IGL_INLINE bool readOBJ(
    const std::string str,
    Eigen::PlainObjectBase<DerivedV>& V,
    Eigen::PlainObjectBase<DerivedI>& I,
    Eigen::PlainObjectBase<DerivedC>& C);

}

#ifndef IGL_STATIC_LIBRARY
#  include "readOBJ.cpp"
#endif

#endif
