// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2021 Jérémie Dumas <jeremie.dumas@ens-lyon.org>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_DEFAULT_NUM_THREADS_H
#define IGL_DEFAULT_NUM_THREADS_H
#include "igl_inline.h"

namespace igl
{















  IGL_INLINE unsigned int default_num_threads(unsigned int force_num_threads = 0);
}

#ifndef IGL_STATIC_LIBRARY
#include "default_num_threads.cpp"
#endif

#endif
