// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_TETGEN_TETRAHEDRALIZE_H
#define IGL_COPYLEFT_TETGEN_TETRAHEDRALIZE_H
#include "../../igl_inline.h"

#include <vector>
#include <string>
#include <Eigen/Core>
#ifndef TETLIBRARY
#define TETLIBRARY
#endif
#include <tetgen.h>

namespace igl
{
  namespace copyleft
  {
    namespace tetgen
    {



















































      template <
        typename DerivedV,
        typename DerivedF,
        typename DerivedH,
        typename DerivedVM,
        typename DerivedFM,
        typename DerivedR,
        typename DerivedTV,
        typename DerivedTT,
        typename DerivedTF,
        typename DerivedTM,
        typename DerivedTR,
        typename DerivedTN,
        typename DerivedPT,
        typename DerivedFT>
      IGL_INLINE int tetrahedralize(
        const Eigen::MatrixBase<DerivedV>& V,
        const Eigen::MatrixBase<DerivedF>& F,
        const Eigen::MatrixBase<DerivedH>& H,
        const Eigen::MatrixBase<DerivedVM>& VM,
        const Eigen::MatrixBase<DerivedFM>& FM,
        const Eigen::MatrixBase<DerivedR>& R,
        const std::string switches,
        Eigen::PlainObjectBase<DerivedTV>& TV,
        Eigen::PlainObjectBase<DerivedTT>& TT,
        Eigen::PlainObjectBase<DerivedTF>& TF,
        Eigen::PlainObjectBase<DerivedTM>& TM,
        Eigen::PlainObjectBase<DerivedTR>& TR,
        Eigen::PlainObjectBase<DerivedTN>& TN,
        Eigen::PlainObjectBase<DerivedPT>& PT,
        Eigen::PlainObjectBase<DerivedFT>& FT,
        int & num_regions);

      template <
        typename DerivedV,
        typename DerivedF,
        typename DerivedTV,
        typename DerivedTT,
        typename DerivedTF>
      IGL_INLINE int tetrahedralize(
        const Eigen::MatrixBase<DerivedV>& V,
        const Eigen::MatrixBase<DerivedF>& F,
        const std::string switches,
        Eigen::PlainObjectBase<DerivedTV>& TV,
        Eigen::PlainObjectBase<DerivedTT>& TT,
        Eigen::PlainObjectBase<DerivedTF>& TF);
   }
  }
}


#ifndef IGL_STATIC_LIBRARY
#  include "tetrahedralize.cpp"
#endif

#endif
