// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2020 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "decimate_trivial_callbacks.h"

IGL_INLINE void igl::decimate_trivial_callbacks(
    decimate_pre_collapse_callback  & always_try,
    decimate_post_collapse_callback & never_care)
{
  always_try = [](
    const Eigen::MatrixXd &                             ,
    const Eigen::MatrixXi &                             ,
    const Eigen::MatrixXi &                             ,
    const Eigen::VectorXi &                             ,
    const Eigen::MatrixXi &                             ,
    const Eigen::MatrixXi &                             ,
    const igl::min_heap< std::tuple<double,int,int> > & ,
    const Eigen::VectorXi &                             ,
    const Eigen::MatrixXd &                             ,
    const int
    ) -> bool { return true;};
  never_care = [](
    const Eigen::MatrixXd &                             ,
    const Eigen::MatrixXi &                             ,
    const Eigen::MatrixXi &                             ,
    const Eigen::VectorXi &                             ,
    const Eigen::MatrixXi &                             ,
    const Eigen::MatrixXi &                             ,
    const igl::min_heap< std::tuple<double,int,int> > & ,
    const Eigen::VectorXi &                             ,
    const Eigen::MatrixXd &                             ,
    const int                                           ,
    const int                                           ,
    const int                                           ,
    const int                                           ,
    const int                                           ,
    const bool
    )-> void { };
}
