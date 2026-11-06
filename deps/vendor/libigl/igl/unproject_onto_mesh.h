// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_UNPROJECT_ONTO_MESH
#define IGL_UNPROJECT_ONTO_MESH
#include "igl_inline.h"
#include "Hit.h"
#include <Eigen/Core>
#include <functional>

namespace igl
{























  template < typename DerivedV, typename DerivedF, typename Derivedbc>
  IGL_INLINE bool unproject_onto_mesh(
    const Eigen::Vector2f& pos,
    const Eigen::Matrix4f& model,
    const Eigen::Matrix4f& proj,
    const Eigen::Vector4f& viewport,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    int & fid,
    Eigen::PlainObjectBase<Derivedbc> & bc);



  template <typename Derivedbc>
  IGL_INLINE bool unproject_onto_mesh(
    const Eigen::Vector2f& pos,
    const Eigen::Matrix4f& model,
    const Eigen::Matrix4f& proj,
    const Eigen::Vector4f& viewport,
    const std::function<
      bool(
        const Eigen::Vector3f&,
        const Eigen::Vector3f&,
        igl::Hit<float>  &)
        > & shoot_ray,
    int & fid,
    Eigen::PlainObjectBase<Derivedbc> & bc);
}
#ifndef IGL_STATIC_LIBRARY
#  include "unproject_onto_mesh.cpp"
#endif
#endif
