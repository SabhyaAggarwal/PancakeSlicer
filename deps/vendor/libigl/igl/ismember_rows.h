// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_ISMEMBER_ROWS_H
#define IGL_ISMEMBER_ROWS_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{








  template <
    typename DerivedA,
    typename DerivedB,
    typename DerivedIA,
    typename DerivedLOCB>
  IGL_INLINE void ismember_rows(
    const Eigen::MatrixBase<DerivedA> & A,
    const Eigen::MatrixBase<DerivedB> & B,
    Eigen::PlainObjectBase<DerivedIA> & IA,
    Eigen::PlainObjectBase<DerivedLOCB> & LOCB);

}

#ifndef IGL_STATIC_LIBRARY
#  include "ismember_rows.cpp"
#endif
#endif
