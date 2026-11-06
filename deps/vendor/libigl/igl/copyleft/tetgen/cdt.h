// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_TETGEN_CDT_H
#define IGL_COPYLEFT_TETGEN_CDT_H
#include "../../igl_inline.h"

#include <Eigen/Core>
#include <string>
#ifndef TETLIBRARY
#  define TETLIBRARY
#endif
#include <tetgen.h>

namespace igl
{
  namespace copyleft
  {
    namespace tetgen
    {

      struct CDTParam
      {



        bool use_bounding_box = false;


        double bounding_box_scale = 1.01;

        std::string flags = "Y";
      };










      template <
        typename DerivedV,
        typename DerivedF,
        typename DerivedTV,
        typename DerivedTT,
        typename DerivedTF>
      IGL_INLINE bool cdt(
        const Eigen::MatrixBase<DerivedV>& V,
        const Eigen::MatrixBase<DerivedF>& F,
        const CDTParam & param,
        Eigen::PlainObjectBase<DerivedTV>& TV,
        Eigen::PlainObjectBase<DerivedTT>& TT,
        Eigen::PlainObjectBase<DerivedTF>& TF);
    }
  }
}


#ifndef IGL_STATIC_LIBRARY
#  include "cdt.cpp"
#endif

#endif
