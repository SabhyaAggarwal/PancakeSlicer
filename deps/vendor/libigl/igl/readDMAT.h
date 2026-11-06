// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_READDMAT_H
#define IGL_READDMAT_H
#include "igl_inline.h"









































#include <string>
#include <vector>
#include <Eigen/Core>
namespace igl
{






  template <typename DerivedW>
  IGL_INLINE bool readDMAT(const std::string file_name,
    Eigen::PlainObjectBase<DerivedW> & W);

  template <typename Scalar>
  IGL_INLINE bool readDMAT(
    const std::string file_name,
    std::vector<std::vector<Scalar> > & W);
}

#ifndef IGL_STATIC_LIBRARY
#  include "readDMAT.cpp"
#endif

#endif
