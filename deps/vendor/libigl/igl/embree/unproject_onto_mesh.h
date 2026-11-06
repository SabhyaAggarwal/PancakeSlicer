// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Daniele Panozzo <daniele.panozzo@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_EMBREE_UNPROJECT_ONTO_MESH_H
#define IGL_EMBREE_UNPROJECT_ONTO_MESH_H
#include "../igl_inline.h"
#include <Eigen/Core>

#include <vector>

namespace igl
{
  namespace embree
  {

    class EmbreeIntersector;











    IGL_INLINE bool unproject_onto_mesh(
      const Eigen::Vector2f& pos,
      const Eigen::Matrix4f& model,
      const Eigen::Matrix4f& proj,
      const Eigen::Vector4f& viewport,
      const EmbreeIntersector & ei,
      int& fid,
      Eigen::Vector3f& bc);


    IGL_INLINE bool unproject_onto_mesh(
      const Eigen::Vector2f& pos,
      const Eigen::MatrixXi& F,
      const Eigen::Matrix4f& model,
      const Eigen::Matrix4f& proj,
      const Eigen::Vector4f& viewport,
      const EmbreeIntersector & ei,
      int& fid,
      int& vid);
  }
}
#ifndef IGL_STATIC_LIBRARY
#  include "unproject_onto_mesh.cpp"
#endif
#endif
