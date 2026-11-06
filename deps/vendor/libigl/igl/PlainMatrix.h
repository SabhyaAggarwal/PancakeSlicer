// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2024 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef IGL_PLAINMATRIX_H
#define IGL_PLAINMATRIX_H
#include <Eigen/Core>

#include <type_traits>
#include <Eigen/Dense>


#if __cplusplus < 201703L
namespace std {
  template <typename... Ts>
  using void_t = void;
}
#endif


#ifndef IGL_DEFAULT_MAJORING
#define IGL_DEFAULT_MAJORING Eigen::ColMajor
#endif

namespace igl
{
  template <typename Derived, int Rows, int Cols, int Options>
  struct PlainMatrixHelper {
    using Type = Eigen::Matrix<typename Derived::Scalar,Rows,Cols,((Rows == 1 && Cols != 1) ? Eigen::RowMajor : ((Cols == 1 && Rows != 1) ? Eigen::ColMajor : Options))>;
  };
  template <typename Derived, typename = void>
  struct get_options {
    static constexpr int value = IGL_DEFAULT_MAJORING;
  };

  template <typename Derived>
  struct get_options<Derived, std::void_t<decltype(Derived::Options)>> {
    static constexpr int value = Derived::Options;
  };











































  template <typename Derived,
            int Rows = Derived::RowsAtCompileTime,
            int Cols = Derived::ColsAtCompileTime,
            int Options = get_options<Derived>::value>
  using PlainMatrix = typename PlainMatrixHelper<Derived, Rows, Cols, Options>::Type;

}

#endif
