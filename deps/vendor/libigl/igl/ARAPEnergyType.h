// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_ARAPENERGYTYPE_H
#define IGL_ARAPENERGYTYPE_H
namespace igl
{

  enum ARAPEnergyType
  {


    ARAP_ENERGY_TYPE_SPOKES = 0,




    ARAP_ENERGY_TYPE_SPOKES_AND_RIMS = 1,


    ARAP_ENERGY_TYPE_ELEMENTS = 2,


    ARAP_ENERGY_TYPE_DEFAULT = 3,

    NUM_ARAP_ENERGY_TYPES = 4
  };
}
#endif
