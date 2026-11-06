// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Daniele Panozzo <daniele.panozzo@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_STB_READ_IMAGE_H
#define IGL_STB_READ_IMAGE_H
#include "../igl_inline.h"
#include <Eigen/Core>
#include <string>

namespace igl
{
  namespace stb
  {





















    IGL_INLINE bool read_image(const std::string image_file,
    Eigen::Matrix<unsigned char,Eigen::Dynamic,Eigen::Dynamic>& R,
    Eigen::Matrix<unsigned char,Eigen::Dynamic,Eigen::Dynamic>& G,
    Eigen::Matrix<unsigned char,Eigen::Dynamic,Eigen::Dynamic>& B,
    Eigen::Matrix<unsigned char,Eigen::Dynamic,Eigen::Dynamic>& A
    );
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "read_image.cpp"
#endif

#endif
