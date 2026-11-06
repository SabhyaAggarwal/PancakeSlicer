// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "trim_with_solid.h"
#include "assign.h"
#include "intersect_other.h"
#include "remesh_self_intersections.h"
#include "point_solid_signed_squared_distance.h"

#include "../../extract_manifold_patches.h"
#include "../../connected_components.h"
#include "../../facet_adjacency_matrix.h"
#include "../../placeholders.h"
#include "../../list_to_matrix.h"
#include "../../find.h"
#include "../../get_seconds.h"
#include "../../barycenter.h"
#include "../../remove_unreferenced.h"

#include <CGAL/Exact_predicates_exact_constructions_kernel.h>

#include <vector>

template <
  typename DerivedVA,
  typename DerivedFA,
  typename DerivedVB,
  typename DerivedFB,
  typename DerivedV,
  typename DerivedF,
  typename DerivedD,
  typename DerivedJ>
IGL_INLINE void igl::copyleft::cgal::trim_with_solid(
  const Eigen::MatrixBase<DerivedVA> & VA,
  const Eigen::MatrixBase<DerivedFA> & FA,
  const Eigen::MatrixBase<DerivedVB> & VB,
  const Eigen::MatrixBase<DerivedFB> & FB,
  Eigen::PlainObjectBase<DerivedV> & Vd,
  Eigen::PlainObjectBase<DerivedF> & F,
  Eigen::PlainObjectBase<DerivedD> & D,
  Eigen::PlainObjectBase<DerivedJ> & J)
{
  return trim_with_solid(
    VA,FA,VB,FB,TrimWithSolidMethod::CHECK_EACH_PATCH,
    Vd,F,D,J);
}
template <
  typename DerivedVA,
  typename DerivedFA,
  typename DerivedVB,
  typename DerivedFB,
  typename DerivedV,
  typename DerivedF,
  typename DerivedD,
  typename DerivedJ>
IGL_INLINE void igl::copyleft::cgal::trim_with_solid(
  const Eigen::MatrixBase<DerivedVA> & VA,
  const Eigen::MatrixBase<DerivedFA> & FA,
  const Eigen::MatrixBase<DerivedVB> & VB,
  const Eigen::MatrixBase<DerivedFB> & FB,
  const TrimWithSolidMethod method,
  Eigen::PlainObjectBase<DerivedV> & Vd,
  Eigen::PlainObjectBase<DerivedF> & F,
  Eigen::PlainObjectBase<DerivedD> & D,
  Eigen::PlainObjectBase<DerivedJ> & J)
{

  typedef Eigen::Matrix<CGAL::Epeck::FT,Eigen::Dynamic,3> MatrixX3E;
  typedef Eigen::Matrix<CGAL::Epeck::FT,Eigen::Dynamic,1> VectorXE;
















































  MatrixX3E V;
  const auto set_D_via_patches =
    [&V,&F,&D,&VB,&FB](const int num_patches, const Eigen::VectorXi & P)
  {

    std::vector<bool> flag(num_patches);
    std::vector<std::vector<CGAL::Epeck::FT> > vQ;
    Eigen::VectorXi P2Q(num_patches);
    for(int f = 0;f<P.rows();f++)
    {
      const auto p = P(f);

      if(!flag[p])
      {
        P2Q(p) = vQ.size();
        std::vector<CGAL::Epeck::FT> q = {
          (V(F(f,0),0)+ V(F(f,1),0)+ V(F(f,2),0))/3.,
          (V(F(f,0),1)+ V(F(f,1),1)+ V(F(f,2),1))/3.,
          (V(F(f,0),2)+ V(F(f,1),2)+ V(F(f,2),2))/3.};
        vQ.emplace_back(q);
        flag[p] = true;
      }
    }
    MatrixX3E Q;
    igl::list_to_matrix(vQ,Q);
    VectorXE SP;
    point_solid_signed_squared_distance(Q,VB,FB,SP);
    Eigen::Matrix<bool,Eigen::Dynamic,1> DP = SP.array()>0;

    D.resize(F.rows());
    for(int f = 0;f<F.rows();f++)
    {
      D(f) = DP(P2Q(P(f)));
    }
  };

  switch(method)
  {
    case CHECK_EACH_PATCH:
    case CHECK_EACH_FACE:
    {
      Eigen::MatrixXi _1;
      Eigen::VectorXi _2;

      igl::copyleft::cgal::intersect_other(
        VA,FA,VB,FB,{false,false,true},_1,V,F,J,_2);
      const auto keep = igl::find( (J.array()<FA.rows()).eval() );
      F = F(keep,igl::placeholders::all).eval();
      J = J(keep).eval();
      {
        Eigen::VectorXi _;
        igl::remove_unreferenced(decltype(V)(V),decltype(F)(F),V,F,_);
      }
      switch(method)
      {
        default:   break;
        case CHECK_EACH_PATCH:
        {
          Eigen::SparseMatrix<bool> A;
          igl::facet_adjacency_matrix(F,A);
          for(int i = 0; i < A.outerSize(); i++)
          {
            for(decltype(A)::InnerIterator it(A,i); it; ++it)
            {
              const int a = it.row();
              const int b = it.col();
              if(J(a) == J(b))
              {
                A.coeffRef(a,b) = false;
              }
            }
          }
          A.prune(false);
          Eigen::VectorXi P,K;
          const int num_patches = igl::connected_components(A,P,K);
          set_D_via_patches(num_patches,P);
          break;
        }
        case CHECK_EACH_FACE:
        {
          MatrixX3E Q;
          igl::barycenter(V,F,Q);
          VectorXE SP;
          point_solid_signed_squared_distance(Q,VB,FB,SP);
          D = (SP.array()>0).template cast<typename DerivedD::Scalar>();
          break;
        }
      }
      break;
    }
    case RESOLVE_BOTH_AND_RESTORE_THEN_CHECK_EACH_PATCH:
    {
      RemeshSelfIntersectionsParam params;


      params.stitch_all = false;
      {
        Eigen::MatrixXi IF;
        Eigen::Matrix<typename DerivedVA::Scalar,Eigen::Dynamic,3> VAB(VA.rows() + VB.rows(),3);
        VAB << VA,VB;
        Eigen::Matrix<typename DerivedFA::Scalar,Eigen::Dynamic,3> FAB(FA.rows() + FB.rows(),3);
        FAB << FA,FB.array()+VA.rows();


        {
          Eigen::VectorXi I;
          igl::copyleft::cgal::remesh_self_intersections(
            VAB, FAB, params, V, F, IF, J, I);

          {
            Eigen::Array<bool,Eigen::Dynamic,1> avoids_B =
              Eigen::Array<bool,Eigen::Dynamic,1>::Constant(FA.rows(),1,true);
            for(int p = 0;p<IF.rows();p++)
            {
              if(IF(p,0) >= FA.rows() || IF(p,1) >= FA.rows())
              {
                if(IF(p,0) < FA.rows()){ avoids_B[IF(p,0)] = false; }
                if(IF(p,1) < FA.rows()){ avoids_B[IF(p,1)] = false; }
              }
            }

            Eigen::VectorXi first = Eigen::VectorXi::Constant(FA.rows(),1,-1);
            for(int j = 0;j<J.rows();j++)
            {
              if(J(j) < FA.rows() && first[J(j)] == -1)
              {
                first[J(j)] = j;

                if(avoids_B[J(j)])
                {
                  F.row(j) = FA.row(J(j));
                }
              }
            }

            assert(first.minCoeff() >= 0 && "Every face should be found");
            std::vector<int> keep;
            for(int f = 0;f<F.rows();f++)
            {
              if(J(f)>=FA.rows() || !avoids_B[J(f)] || first[J(f)] == f)
              {
                keep.push_back(f);
              }
            }
            F = F(keep,igl::placeholders::all).eval();
            J = J(keep).eval();
          }


          assert(I.size() == V.rows());

          std::for_each(F.data(),F.data()+F.size(),[&I](typename DerivedF::Scalar & a){a=I[a];});
        }
      }

      Eigen::VectorXi P;
      const int num_patches = igl::extract_manifold_patches(F,P);

      Eigen::Array<bool,Eigen::Dynamic,1> A = J.array()< FA.rows();
      const auto AI = igl::find(A);
      F = F(AI,igl::placeholders::all).eval();
      J = J(AI).eval();
      P = P(AI).eval();
      set_D_via_patches(num_patches,P);
      break;
    }
  }
  {
    Eigen::VectorXi _;
    igl::remove_unreferenced(MatrixX3E(V),DerivedF(F),V,F,_);
  }
  assign(V,Vd);

}



#ifdef IGL_STATIC_LIBRARY

template void igl::copyleft::cgal::trim_with_solid<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int,  -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&,  Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1,  -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&,  Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1,  1> >&);
template void igl::copyleft::cgal::trim_with_solid<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Array<bool, -1, 1, 0, -1, 1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Array<bool, -1, 1, 0, -1, 1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&);
template void igl::copyleft::cgal::trim_with_solid<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Array<bool, -1, 1, 0, -1, 1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, igl::copyleft::cgal::TrimWithSolidMethod, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Array<bool, -1, 1, 0, -1, 1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&);
#endif
