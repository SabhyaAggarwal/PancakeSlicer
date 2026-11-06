// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_FACES_FIRST_H
#define IGL_FACES_FIRST_H
#include "igl_inline.h"
namespace igl
{


























  template <typename MatV, typename MatF, typename VecI>
  IGL_INLINE void faces_first(
    const MatV & V,
    const MatF & F,
    MatV & RV,
    MatF & RF,
    VecI & IM);

  template <typename MatV, typename MatF, typename VecI>
  IGL_INLINE void faces_first(
    MatV & V,
    MatF & F,
    VecI & IM);
}

#ifndef IGL_STATIC_LIBRARY
#  include "faces_first.cpp"
#endif

#endif
