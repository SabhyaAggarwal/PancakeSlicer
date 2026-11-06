// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2020 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef FIT_CUBIC_BEZIER_H
#define FIT_CUBIC_BEZIER_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{









  IGL_INLINE void fit_cubic_bezier(
    const Eigen::MatrixXd & d,
    const double error,
    std::vector<Eigen::MatrixXd> & cubics);












  IGL_INLINE void fit_cubic_bezier_substring(
    const Eigen::MatrixXd & d,
    const int first,
    const int last,
    const Eigen::RowVectorXd & tHat1,
    const Eigen::RowVectorXd & tHat2,
    const double error,
    const bool force_split,
    std::vector<Eigen::MatrixXd> & cubics);
}

#ifndef IGL_STATIC_LIBRARY
#include "fit_cubic_bezier.cpp"
#endif

#endif
