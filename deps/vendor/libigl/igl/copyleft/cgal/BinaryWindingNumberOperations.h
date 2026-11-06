// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
//
#ifndef IGL_COPYLEFT_CGAL_BINARY_WINDING_NUMBER_OPERATIONS_H
#define IGL_COPYLEFT_CGAL_BINARY_WINDING_NUMBER_OPERATIONS_H

#include <stdexcept>
#include "../../igl_inline.h"
#include "../../MeshBooleanType.h"
#include <Eigen/Core>







namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {

      template <igl::MeshBooleanType Op>
      class BinaryWindingNumberOperations {
        public:
          template<typename DerivedW>
            typename DerivedW::Scalar operator()(
                const Eigen::MatrixBase<DerivedW>&  ) const {
              throw (std::runtime_error("not implemented!"));
            }
      };


      template <>
      class BinaryWindingNumberOperations<MESH_BOOLEAN_TYPE_UNION> {
        public:
          template<typename DerivedW>
          typename DerivedW::Scalar operator()(
              const Eigen::MatrixBase<DerivedW>& win_nums) const
          {
            for(int i = 0;i<win_nums.size();i++)
            {
              if(win_nums(i) > 0) return true;
            }
            return false;
          }
      };


      template <>
      class BinaryWindingNumberOperations<MESH_BOOLEAN_TYPE_INTERSECT> {
        public:
          template<typename DerivedW>
          typename DerivedW::Scalar operator()(
              const Eigen::MatrixBase<DerivedW>& win_nums) const
          {
            for(int i = 0;i<win_nums.size();i++)
            {
              if(win_nums(i)<=0) return false;
            }
            return true;
          }
      };


      template <>
      class BinaryWindingNumberOperations<MESH_BOOLEAN_TYPE_MINUS> {
        public:
          template<typename DerivedW>
          typename DerivedW::Scalar operator()(
              const Eigen::MatrixBase<DerivedW>& win_nums) const
          {
            assert(win_nums.size()>1);

            bool union_rest = false;
            for(int i = 1;i<win_nums.size();i++)
            {
              union_rest = union_rest || win_nums(i) > 0;
              if(union_rest) break;
            }

            return win_nums(0) > 0 && !union_rest;
          }
      };


      template <>
      class BinaryWindingNumberOperations<MESH_BOOLEAN_TYPE_XOR> {
        public:
          template<typename DerivedW>
          typename DerivedW::Scalar operator()(
              const Eigen::MatrixBase<DerivedW>& win_nums) const
          {

            int count = 0;
            for(int i = 0;i<win_nums.size();i++)
            {
              if(win_nums(i) > 0) count++;
            }
            return count % 2 == 1;
          }
      };


      template <>
      class BinaryWindingNumberOperations<MESH_BOOLEAN_TYPE_RESOLVE> {
        public:
          template<typename DerivedW>
            typename DerivedW::Scalar operator()(
                const Eigen::MatrixBase<DerivedW>&  ) const {
              return true;
            }
      };

      typedef BinaryWindingNumberOperations<MESH_BOOLEAN_TYPE_UNION> BinaryUnion;
      typedef BinaryWindingNumberOperations<MESH_BOOLEAN_TYPE_INTERSECT> BinaryIntersect;
      typedef BinaryWindingNumberOperations<MESH_BOOLEAN_TYPE_MINUS> BinaryMinus;
      typedef BinaryWindingNumberOperations<MESH_BOOLEAN_TYPE_XOR> BinaryXor;
      typedef BinaryWindingNumberOperations<MESH_BOOLEAN_TYPE_RESOLVE> BinaryResolve;


      enum KeeperType {

        KEEP_INSIDE,

        KEEP_ALL
      };


      template<KeeperType T>
      class WindingNumberFilter {
        public:
          template<typename DerivedW>
            short operator()(
                const Eigen::MatrixBase<DerivedW>&  ) const {
              throw std::runtime_error("Not implemented");
            }
      };


      template<>
      class WindingNumberFilter<KEEP_INSIDE> {
        public:
          template<typename T>
          short operator()(T out_w, T in_w) const {
            if (in_w > 0 && out_w <= 0) return 1;
            else if (in_w <= 0 && out_w > 0) return -1;
            else return 0;
          }
      };


      template<>
      class WindingNumberFilter<KEEP_ALL> {
        public:
          template<typename T>
            short operator()(T  , T  ) const {
              return 1;
            }
      };

      using KeepInside = WindingNumberFilter<KEEP_INSIDE>;
      using KeepAll = WindingNumberFilter<KEEP_ALL>;
    }
  }
}

#endif
