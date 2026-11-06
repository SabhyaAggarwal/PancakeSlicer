// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//               2014 Christian Schüller <schuellchr@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_HIT_H
#define IGL_HIT_H

namespace igl
{


  template <typename Scalar>
  struct Hit
  {

    int id;

    int gid;


    Scalar u,v;


    Scalar t;
  };
}
#endif
