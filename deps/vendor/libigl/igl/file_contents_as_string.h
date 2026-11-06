// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_FILE_CONTENTS_AS_STRING_H
#define IGL_FILE_CONTENTS_AS_STRING_H
#include "igl_inline.h"

#include <string>
namespace igl
{





  IGL_INLINE bool file_contents_as_string(
    const std::string file_name,
    std::string & content);

  IGL_INLINE std::string file_contents_as_string(
    const std::string file_name);
}

#ifndef IGL_STATIC_LIBRARY
#  include "file_contents_as_string.cpp"
#endif

#endif
