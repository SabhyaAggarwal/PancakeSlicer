// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_EMBREE_UNPROJECT_IN_MESH
#define IGL_EMBREE_UNPROJECT_IN_MESH
#include "../igl_inline.h"
#include <Eigen/Core>

#include <vector>
#include "../Hit.h"

namespace igl
{
  namespace embree
  {

    class EmbreeIntersector;

























    template < typename Derivedobj>
    IGL_INLINE int unproject_in_mesh(
      const Eigen::Vector2f& pos,
      const Eigen::Matrix4f& model,
      const Eigen::Matrix4f& proj,
      const Eigen::Vector4f& viewport,
      const EmbreeIntersector & ei,
      Eigen::PlainObjectBase<Derivedobj> & obj,
      std::vector<igl::Hit<float>  > & hits);

    template < typename Derivedobj>
    IGL_INLINE int unproject_in_mesh(
      const Eigen::Vector2f& pos,
      const Eigen::Matrix4f& model,
      const Eigen::Matrix4f& proj,
      const Eigen::Vector4f& viewport,
      const EmbreeIntersector & ei,
      Eigen::PlainObjectBase<Derivedobj> & obj);

  }
}
#ifndef IGL_STATIC_LIBRARY
#  include "unproject_in_mesh.cpp"
#endif
#endif
