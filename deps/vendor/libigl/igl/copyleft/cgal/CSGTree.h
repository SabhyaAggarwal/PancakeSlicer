// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_CGAL_CSG_TREE_H
#define IGL_COPYLEFT_CGAL_CSG_TREE_H

#include "../../MeshBooleanType.h"
#include "string_to_mesh_boolean_type.h"
#include "mesh_boolean.h"
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/number_utils.h>

namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {



      class CSGTree
      {
        public:
          typedef CGAL::Epeck::FT ExactScalar;

          typedef Eigen::MatrixXi POBF;
          typedef Eigen::Matrix<ExactScalar,Eigen::Dynamic,3> MatrixX3E;
          typedef Eigen::VectorXi VectorJ;
        private:

          MatrixX3E m_V;

          POBF m_F;

          VectorJ m_J;


          size_t m_number_of_birth_faces;
        public:
          CSGTree()
          {
          }



          CSGTree(const CSGTree & other)
            :

            m_V(other.m_V),


            m_F(other.m_F),
            m_J(other.m_J),
            m_number_of_birth_faces(other.m_number_of_birth_faces)
          {
          }

          friend void swap(CSGTree& first, CSGTree& second)
          {
            using std::swap;

            swap(first.m_V,second.m_V);


            swap(first.m_F,second.m_F);
            swap(first.m_J,second.m_J);
            swap(first.m_number_of_birth_faces,second.m_number_of_birth_faces);
          }

          CSGTree& operator=(CSGTree other)
          {
            swap(*this,other);
            return *this;
          }
          CSGTree(CSGTree&& other):

            CSGTree()
          {
            swap(*this,other);
          }





          CSGTree(
            const CSGTree & A,
            const CSGTree & B,
            const MeshBooleanType & type)
          {

            mesh_boolean(A.V(),A.F(),B.V(),B.F(),type,m_V,m_F,m_J);

            std::for_each(m_J.data(),m_J.data()+m_J.size(),
              [&](typename VectorJ::Scalar & j) -> void
              {
                if(j < A.F().rows())
                {
                  j = A.J()(j);
                }else
                {
                  assert(j<(A.F().rows()+B.F().rows()));
                  j = A.number_of_birth_faces()+(B.J()(j-A.F().rows()));
                }
              });
            m_number_of_birth_faces =
              A.number_of_birth_faces() + B.number_of_birth_faces();
          }

          CSGTree(
            const CSGTree & A,
            const CSGTree & B,
            const std::string & s):
            CSGTree(A,B,string_to_mesh_boolean_type(s))
          {

          }





          template <typename DerivedV>
          CSGTree(const Eigen::MatrixBase<DerivedV> & V, const POBF & F)



          {
            m_V = V.template cast<ExactScalar>();
            m_F = F;

            m_number_of_birth_faces = m_F.rows();

            m_J = VectorJ::LinSpaced(
              m_number_of_birth_faces,0,m_number_of_birth_faces-1);
          }


          const MatrixX3E & V() const
          {
            return m_V;
          }


          template <typename DerivedV>
          DerivedV cast_V() const
          {
            DerivedV dV;
            dV.resize(m_V.rows(),m_V.cols());
            for(int i = 0;i<m_V.rows();i++)
            {
              for(int j = 0;j<m_V.cols();j++)
              {
                dV(i,j) = CGAL::to_double(m_V(i,j));
              }
            }
            return dV;
          }

          const POBF & F() const
          {
            return m_F;
          }



          const VectorJ & J() const
          {
            return m_J;
          }

          const size_t & number_of_birth_faces() const
          {
            return m_number_of_birth_faces;
          }
      };
    }
  }
}


#endif
