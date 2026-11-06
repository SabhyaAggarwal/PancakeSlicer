// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2020 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_DECIMATE_CALLBACK_TYPES_H
#define IGL_DECIMATE_CALLBACK_TYPES_H
#include <Eigen/Core>
#include "min_heap.h"



namespace igl
{


















  using decimate_cost_and_placement_callback =
    std::function<void(
      const int                                           ,
      const Eigen::MatrixXd &                             ,
      const Eigen::MatrixXi &                             ,
      const Eigen::MatrixXi &                             ,
      const Eigen::VectorXi &                             ,
      const Eigen::MatrixXi &                             ,
      const Eigen::MatrixXi &                             ,
      double &                                            ,
      Eigen::RowVectorXd &
      )>;

























  using decimate_stopping_condition_callback =
    std::function<bool(
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
      const int
      )>;





















  using decimate_pre_collapse_callback =
    std::function<bool(
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
      )>;
























  using decimate_post_collapse_callback =
    std::function<void(
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
      )>;
}
#endif
