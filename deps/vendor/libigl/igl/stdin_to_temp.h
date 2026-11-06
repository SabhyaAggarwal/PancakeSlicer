// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_STDIN_TO_TEMP_H
#define IGL_STDIN_TO_TEMP_H
#include "igl_inline.h"
#include <cstdio>
namespace igl
{














  IGL_INLINE bool stdin_to_temp(FILE ** temp_file);
}

#ifndef IGL_STATIC_LIBRARY
#  include "stdin_to_temp.cpp"
#endif

#endif
