// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_UNPROJECT_IN_MESH
#define IGL_UNPROJECT_IN_MESH
#include "igl_inline.h"
#include <Eigen/Core>

#include <vector>
#include "Hit.h"

namespace igl
{
















  template < typename DerivedV, typename DerivedF, typename Derivedobj>
    IGL_INLINE int unproject_in_mesh(
        const Eigen::Vector2f& pos,
        const Eigen::Matrix4f& model,
        const Eigen::Matrix4f& proj,
        const Eigen::Vector4f& viewport,
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedF> & F,
        Eigen::PlainObjectBase<Derivedobj> & obj,
        std::vector<igl::Hit<float> > & hits);

  template < typename DerivedV, typename DerivedF, typename Derivedobj>
    IGL_INLINE int unproject_in_mesh(
        const Eigen::Vector2f& pos,
        const Eigen::Matrix4f& model,
        const Eigen::Matrix4f& proj,
        const Eigen::Vector4f& viewport,
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedF> & F,
        Eigen::PlainObjectBase<Derivedobj> & obj);





  template < typename Derivedobj>
    IGL_INLINE int unproject_in_mesh(
        const Eigen::Vector2f& pos,
        const Eigen::Matrix4f& model,
        const Eigen::Matrix4f& proj,
        const Eigen::Vector4f& viewport,
        const std::function<
          void(
            const Eigen::Vector3f&,
            const Eigen::Vector3f&,
            std::vector<igl::Hit<float>> &)
            > & shoot_ray,
        Eigen::PlainObjectBase<Derivedobj> & obj,
        std::vector<igl::Hit<float> > & hits);
}
#ifndef IGL_STATIC_LIBRARY
#  include "unproject_in_mesh.cpp"
#endif
#endif
