// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2018 Alec Jacobson <alecjacobson@gmail.com>
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "dirname.h"

#include <algorithm>
#include "verbose.h"

IGL_INLINE std::string igl::dirname(const std::string & path)
{
  if(path == "")
  {
    return std::string("");
  }

  size_t found = path.find_last_of("/\\");
  if(found == std::string::npos)
  {

    return std::string(".");
  }else if(found == 0)
  {

    return std::string(path.begin(),path.begin()+1);
  }else if(found == path.length()-1)
  {

    std::string redo = std::string(path.begin(),path.end()-1);
    return igl::dirname(redo);
  }

  return std::string(path.begin(),path.begin()+found);
}
