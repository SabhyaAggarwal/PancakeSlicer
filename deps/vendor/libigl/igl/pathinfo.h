// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_PATHINFO_H
#define IGL_PATHINFO_H
#include "igl_inline.h"

#include <string>

namespace igl
{


































  IGL_INLINE void pathinfo(
    const std::string & path,
    std::string & dirname,
    std::string & basename,
    std::string & extension,
    std::string & filename);

}

#ifndef IGL_STATIC_LIBRARY
#  include "pathinfo.cpp"
#endif

#endif
