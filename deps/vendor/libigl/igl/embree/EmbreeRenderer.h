// This file is part of libigl, a simple c++ geometry processing library.
//
//
// Copyright (C) 2020 Vladimir Fonov <vladimir.fonov@gmail.com>
//               2013 Alec Jacobson <alecjacobson@gmail.com>
//               2014 Christian Schüller <schuellchr@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
//

#ifndef IGL_EMBREE_EMBREE_RENDERER_H
#define IGL_EMBREE_EMBREE_RENDERER_H

#include "../colormap.h"

#include <Eigen/Geometry>
#include <Eigen/Core>
#include <Eigen/Geometry>

#include <embree4/rtcore.h>
#include <embree4/rtcore_ray.h>
#include <iostream>
#include <vector>

#include "EmbreeDevice.h"


namespace igl
{
  namespace embree
  {


    class EmbreeRenderer
    {
    public:
      typedef Eigen::RowVector3f Vec3f;

      struct Hit
      {
        int id;
        int gid;
        float u,v;
        float t;
        Vec3f N;
      };

    public:
      typedef Eigen::Matrix<float,Eigen::Dynamic,3> PointMatrixType;
      typedef Eigen::Matrix<float,Eigen::Dynamic,3> ColorMatrixType;
      typedef Eigen::Matrix<int,  Eigen::Dynamic,3> FaceMatrixType;
      typedef Eigen::Matrix<unsigned char,Eigen::Dynamic,Eigen::Dynamic> PixelMatrixType;

    public:
      EmbreeRenderer();
    private:

      EmbreeRenderer(const EmbreeRenderer & that);
      EmbreeRenderer & operator=(const EmbreeRenderer &);
    public:
      virtual ~EmbreeRenderer();






      template <typename DerivedV, typename DerivedF>
      void set_mesh(const Eigen::MatrixBase<DerivedV> & V,
                    const Eigen::MatrixBase<DerivedF> & F,
                    bool is_static=true);






      template <typename DerivedC>
      void set_colors(const Eigen::MatrixBase<DerivedC> & C);


      template <typename DerivedD>
      void set_data(const Eigen::MatrixBase<DerivedD> & D,
                    igl::ColorMapType cmap = igl::COLOR_MAP_TYPE_VIRIDIS);









      template <typename DerivedD, typename T>
      void set_data(
        const Eigen::MatrixBase<DerivedD> & D,
        T caxis_min,
        T caxis_max,
        igl::ColorMapType cmap = igl::COLOR_MAP_TYPE_VIRIDIS);




      template <typename Derivedr>
      void set_rot(const Eigen::MatrixBase<Derivedr> &r);




      template <typename T>
      void set_zoom(T z);




      template <typename Derivedtr>
      void set_translation(const Eigen::MatrixBase<Derivedtr> &tr);




      void set_face_based(bool f);




      void set_orthographic(bool f );





      void set_double_sided(bool f);









      void render_buffer(PixelMatrixType &R,
                         PixelMatrixType &G,
                         PixelMatrixType &B,
                         PixelMatrixType &A);












      bool intersect_ray(
        const Eigen::RowVector3f& origin,
        const Eigen::RowVector3f& direction,
        Hit & hit,
        float tnear = 0,
        float tfar = std::numeric_limits<float>::infinity(),
        int mask = 0xFFFFFFFF) const;

    private:









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

      void init_view();


      PointMatrixType V;
      FaceMatrixType  F;
      ColorMatrixType C;

      Eigen::RowVector3f uC;

      bool face_based;
      bool uniform_color;
      bool double_sided ;


      float camera_base_zoom;
      float camera_zoom;

      Eigen::Vector3f camera_base_translation;
      Eigen::Vector3f camera_translation;
      Eigen::Vector3f camera_eye;
      Eigen::Vector3f camera_up;
      Eigen::Vector3f camera_center;
      float camera_view_angle;
      float camera_dnear;
      float camera_dfar;


      Eigen::Matrix4f view;
      Eigen::Matrix4f proj;
      Eigen::Matrix4f norm;

      Eigen::Matrix3f rot_matrix;

      bool orthographic;


      RTCScene scene;
      unsigned geomID;
      bool initialized;

      RTCDevice device;

      void create_ray(
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
#  include "EmbreeRenderer.cpp"
#endif
#endif
