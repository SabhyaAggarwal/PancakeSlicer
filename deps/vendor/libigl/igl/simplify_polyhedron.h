// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SIMPLIFY_POLYHEDRON_H
#define IGL_SIMPLIFY_POLYHEDRON_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{












  IGL_INLINE void simplify_polyhedron(
    const Eigen::MatrixXd & OV,
    const Eigen::MatrixXi & OF,
    Eigen::MatrixXd & V,
    Eigen::MatrixXi & F,
    Eigen::VectorXi & J);
}
#ifndef IGL_STATIC_LIBRARY
#  include "simplify_polyhedron.cpp"
#endif
#endif
