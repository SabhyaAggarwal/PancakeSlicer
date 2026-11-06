// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Daniele Panozzo <daniele.panozzo@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_FILE_DIALOG_SAVE_H
#define IGL_FILE_DIALOG_SAVE_H
#include "igl_inline.h"

#include <string>

namespace igl
{





  IGL_INLINE std::string file_dialog_save();
}

#ifndef IGL_STATIC_LIBRARY
#  include "file_dialog_save.cpp"
#endif

#endif
