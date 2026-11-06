// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_ALL_PAIRS_DISTANCES_H
#define IGL_ALL_PAIRS_DISTANCES_H
#include "igl_inline.h"

namespace igl
{










  template <typename Mat>
  IGL_INLINE void all_pairs_distances(
    const Mat & V,
    const Mat & U,
    const bool squared,
    Mat & D);
}

#ifndef IGL_STATIC_LIBRARY
#  include "all_pairs_distances.cpp"
#endif

#endif
