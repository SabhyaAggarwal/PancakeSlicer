// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Daniele Panozzo <daniele.panozzo@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_MATLAB_MATLAB_INTERFACE_H
#define IGL_MATLAB_MATLAB_INTERFACE_H
#include "../igl_inline.h"












#include <Eigen/Core>
#include <Eigen/Sparse>
#include <string>

#include <complex>
#include <cassert>
#include <map>
#include <string>
#include <vector>

#include <engine.h>

namespace igl
{
  namespace matlab
  {




    IGL_INLINE void mlinit(Engine** engine);




    IGL_INLINE void mlclose(Engine** engine);






    IGL_INLINE void mlsetmatrix(Engine** engine, std::string name, const Eigen::MatrixXd& M);


    IGL_INLINE void mlsetmatrix(Engine** engine, std::string name, const Eigen::MatrixXf& M);


    IGL_INLINE void mlsetmatrix(Engine** engine, std::string name, const Eigen::MatrixXi& M);


    IGL_INLINE void mlsetmatrix(Engine** mlengine, std::string name, const Eigen::Matrix<unsigned int, Eigen::Dynamic, Eigen::Dynamic >& M);


    IGL_INLINE void mlsetmatrix(Engine** mlengine, std::string name, const Eigen::SparseMatrix<double>& M);






    IGL_INLINE void mlgetmatrix(Engine** engine, std::string name, Eigen::MatrixXd& M);


    IGL_INLINE void mlgetmatrix(Engine** engine, std::string name, Eigen::MatrixXf& M);


    IGL_INLINE void mlgetmatrix(Engine** engine, std::string name, Eigen::MatrixXi& M);


    IGL_INLINE void mlgetmatrix(Engine** mlengine, std::string name, Eigen::Matrix<unsigned int, Eigen::Dynamic, Eigen::Dynamic >& M);






    IGL_INLINE void mlsetscalar(Engine** engine, std::string name, double s);


    IGL_INLINE double mlgetscalar(Engine** engine, std::string name);







    IGL_INLINE std::string mleval(Engine** engine, std::string code);
  }
}


#ifndef IGL_STATIC_LIBRARY
#  include "matlabinterface.cpp"
#endif

#endif
