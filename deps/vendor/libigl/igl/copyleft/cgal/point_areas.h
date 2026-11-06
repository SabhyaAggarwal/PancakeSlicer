// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Gavin Barill <gavinpcb@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/

#ifndef IGL_POINT_AREAS_H
#define IGL_POINT_AREAS_H
#include "../../igl_inline.h"
#include <Eigen/Core>
namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {























      template <typename DerivedP, typename DerivedI, typename DerivedN,
      typename DerivedA, typename DerivedT>
      IGL_INLINE void point_areas(
        const Eigen::MatrixBase<DerivedP>& P,
        const Eigen::MatrixBase<DerivedI>& I,
        const Eigen::MatrixBase<DerivedN>& N,
        Eigen::PlainObjectBase<DerivedA> & A,
        Eigen::PlainObjectBase<DerivedT> & T);

      template <typename DerivedP, typename DerivedI, typename DerivedN,
               typename DerivedA>
      IGL_INLINE void point_areas(
        const Eigen::MatrixBase<DerivedP>& P,
        const Eigen::MatrixBase<DerivedI>& I,
        const Eigen::MatrixBase<DerivedN>& N,
        Eigen::PlainObjectBase<DerivedA> & A);

    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "point_areas.cpp"
#endif

#endif
