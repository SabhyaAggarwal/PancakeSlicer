// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//                    Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
//
#ifndef IGL_COPYLEFT_CGAL_MESH_BOOLEAN_H
#define IGL_COPYLEFT_CGAL_MESH_BOOLEAN_H

#include "../../igl_inline.h"
#include "../../MeshBooleanType.h"
#include <Eigen/Core>
#include <functional>
#include <vector>

namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {















      template <
        typename DerivedVA,
        typename DerivedFA,
        typename DerivedVB,
        typename DerivedFB,
        typename DerivedVC,
        typename DerivedFC,
        typename DerivedJ>
      IGL_INLINE bool mesh_boolean(
        const Eigen::MatrixBase<DerivedVA > & VA,
        const Eigen::MatrixBase<DerivedFA > & FA,
        const Eigen::MatrixBase<DerivedVB > & VB,
        const Eigen::MatrixBase<DerivedFB > & FB,
        const MeshBooleanType & type,
        Eigen::PlainObjectBase<DerivedVC > & VC,
        Eigen::PlainObjectBase<DerivedFC > & FC,
        Eigen::PlainObjectBase<DerivedJ > & J);


      template <
        typename DerivedVA,
        typename DerivedFA,
        typename DerivedVB,
        typename DerivedFB,
        typename DerivedVC,
        typename DerivedFC,
        typename DerivedJ>
      IGL_INLINE bool mesh_boolean(
        const Eigen::MatrixBase<DerivedVA > & VA,
        const Eigen::MatrixBase<DerivedFA > & FA,
        const Eigen::MatrixBase<DerivedVB > & VB,
        const Eigen::MatrixBase<DerivedFB > & FB,
        const std::string & type_str,
        Eigen::PlainObjectBase<DerivedVC > & VC,
        Eigen::PlainObjectBase<DerivedFC > & FC,
        Eigen::PlainObjectBase<DerivedJ > & J);






      template <
        typename DerivedVA,
        typename DerivedFA,
        typename DerivedVB,
        typename DerivedFB,
        typename DerivedVC,
        typename DerivedFC,
        typename DerivedJ>
      IGL_INLINE bool mesh_boolean(
          const Eigen::MatrixBase<DerivedVA> & VA,
          const Eigen::MatrixBase<DerivedFA> & FA,
          const Eigen::MatrixBase<DerivedVB> & VB,
          const Eigen::MatrixBase<DerivedFB> & FB,
          const std::function<int(const Eigen::Matrix<int,1,Eigen::Dynamic>) >& wind_num_op,
          const std::function<int(const int, const int)> & keep,
          Eigen::PlainObjectBase<DerivedVC > & VC,
          Eigen::PlainObjectBase<DerivedFC > & FC,
          Eigen::PlainObjectBase<DerivedJ > & J);















      template <
        typename DerivedV,
        typename DerivedF,
        typename DerivedVC,
        typename DerivedFC,
        typename DerivedJ>
      IGL_INLINE bool mesh_boolean(
          const std::vector<DerivedV > & Vlist,
          const std::vector<DerivedF > & Flist,
          const std::function<int(const Eigen::Matrix<int,1,Eigen::Dynamic>) >& wind_num_op,
          const std::function<int(const int, const int)> & keep,
          Eigen::PlainObjectBase<DerivedVC > & VC,
          Eigen::PlainObjectBase<DerivedFC > & FC,
          Eigen::PlainObjectBase<DerivedJ > & J);

      template <
        typename DerivedV,
        typename DerivedF,
        typename DerivedVC,
        typename DerivedFC,
        typename DerivedJ>
      IGL_INLINE bool mesh_boolean(
          const std::vector<DerivedV > & Vlist,
          const std::vector<DerivedF > & Flist,
          const MeshBooleanType & type,
          Eigen::PlainObjectBase<DerivedVC > & VC,
          Eigen::PlainObjectBase<DerivedFC > & FC,
          Eigen::PlainObjectBase<DerivedJ > & J);









      template <
        typename DerivedVV,
        typename DerivedFF,
        typename Derivedsizes,
        typename DerivedVC,
        typename DerivedFC,
        typename DerivedJ>
      IGL_INLINE bool mesh_boolean(
          const Eigen::MatrixBase<DerivedVV > & VV,
          const Eigen::MatrixBase<DerivedFF > & FF,
          const Eigen::MatrixBase<Derivedsizes> & sizes,
          const std::function<int(const Eigen::Matrix<int,1,Eigen::Dynamic>) >& wind_num_op,
          const std::function<int(const int, const int)> & keep,
          Eigen::PlainObjectBase<DerivedVC > & VC,
          Eigen::PlainObjectBase<DerivedFC > & FC,
          Eigen::PlainObjectBase<DerivedJ > & J);

      template <
        typename DerivedVA,
        typename DerivedFA,
        typename DerivedVB,
        typename DerivedFB,
        typename DerivedVC,
        typename DerivedFC>
      IGL_INLINE bool mesh_boolean(
          const Eigen::MatrixBase<DerivedVA > & VA,
          const Eigen::MatrixBase<DerivedFA > & FA,
          const Eigen::MatrixBase<DerivedVB > & VB,
          const Eigen::MatrixBase<DerivedFB > & FB,
          const MeshBooleanType & type,
          Eigen::PlainObjectBase<DerivedVC > & VC,
          Eigen::PlainObjectBase<DerivedFC > & FC);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "mesh_boolean.cpp"
#endif

#endif
