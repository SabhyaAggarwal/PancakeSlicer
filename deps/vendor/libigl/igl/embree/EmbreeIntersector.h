// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//               2014 Christian Schüller <schuellchr@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
// igl function interface for Embree2.2
//
// Necessary changes to switch from previous Embree versions:
// * Use igl:Hit instead of embree:Hit (where id0 -> id)
// * For Embree2.2
// * Uncomment #define __USE_RAY_MASK__ in platform.h to enable masking

#ifndef IGL_EMBREE_EMBREE_INTERSECTOR_H
#define IGL_EMBREE_EMBREE_INTERSECTOR_H

#include "../Hit.h"
#include <Eigen/Geometry>
#include <Eigen/Core>

#include <embree4/rtcore.h>
#include <embree4/rtcore_ray.h>
#include <iostream>
#include <vector>

#include "EmbreeDevice.h"

namespace igl
{
  namespace embree
  {

    class EmbreeIntersector
    {
    public:
      typedef Eigen::Matrix<float,Eigen::Dynamic,3> PointMatrixType;
      typedef Eigen::Matrix<int,Eigen::Dynamic,3> FaceMatrixType;
    public:
      EmbreeIntersector();
    private:

      EmbreeIntersector(const EmbreeIntersector & that);
      EmbreeIntersector & operator=(const EmbreeIntersector &);
    public:
      virtual ~EmbreeIntersector();








      void init(
        const PointMatrixType& V,
        const FaceMatrixType& F,
        bool isStatic = false);










      void init(
        const std::vector<const PointMatrixType*>& V,
        const std::vector<const FaceMatrixType*>& F,
        const std::vector<int>& masks,
        bool isStatic = false);




      void deinit();










      bool intersectRay(
        const Eigen::RowVector3f& origin,
        const Eigen::RowVector3f& direction,
        Hit<float>& hit,
        float tnear = 0,
        float tfar = std::numeric_limits<float>::infinity(),
        int mask = 0xFFFFFFFF) const;














      bool intersectBeam(
        const Eigen::RowVector3f& origin,
        const Eigen::RowVector3f& direction,
        Hit<float>& hit,
        float tnear = 0,
        float tfar = std::numeric_limits<float>::infinity(),
        int mask = 0xFFFFFFFF,
        int geoId = -1,
        bool closestHit = true,
        unsigned int samples = 4) const;











      bool intersectRay(
        const Eigen::RowVector3f& origin,
        const Eigen::RowVector3f& direction,
        std::vector<Hit<float>> &hits,
        int& num_rays,
        float tnear = 0,
        float tfar = std::numeric_limits<float>::infinity(),
        int mask = 0xFFFFFFFF) const;







      bool intersectSegment(
        const Eigen::RowVector3f& a,
        const Eigen::RowVector3f& ab,
        Hit<float> &hit,
        int mask = 0xFFFFFFFF) const;

    private:

      struct Vertex   {float x,y,z,a;};
      struct Triangle {int v0, v1, v2;};

      RTCScene scene;
      unsigned geomID;
      Vertex* vertices;
      Triangle* triangles;
      bool initialized;

      RTCDevice device;

      void createRay(
        RTCRayHit& ray,
        const Eigen::RowVector3f& origin,
        const Eigen::RowVector3f& direction,
        float tnear,
        float tfar,
        int mask) const;
    };
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "EmbreeIntersector.cpp"
#endif

#endif
