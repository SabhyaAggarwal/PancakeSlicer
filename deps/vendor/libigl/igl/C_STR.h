// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_C_STR_H
#define IGL_C_STR_H
#include <sstream>
#include <string>
















#define C_STR(X) static_cast<std::ostringstream&>(std::ostringstream().flush() << X).str().c_str()
#endif
