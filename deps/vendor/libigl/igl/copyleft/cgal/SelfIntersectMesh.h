// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_CGAL_SELFINTERSECTMESH_H
#define IGL_COPYLEFT_CGAL_SELFINTERSECTMESH_H

#include "CGAL_includes.hpp"
#include "RemeshSelfIntersectionsParam.h"
#include "../../unique.h"
#include "../../default_num_threads.h"

#include <Eigen/Dense>
#include <list>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <cstdio>


#ifndef IGL_FIRST_HIT_EXCEPTION
#define IGL_FIRST_HIT_EXCEPTION 10
#endif



namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {






      template <
        typename Kernel,
        typename DerivedV,
        typename DerivedF,
        typename DerivedVV,
        typename DerivedFF,
        typename DerivedIF,
        typename DerivedJ,
        typename DerivedIM>
      class SelfIntersectMesh
      {
        typedef
          SelfIntersectMesh<
          Kernel,
          DerivedV,
          DerivedF,
          DerivedVV,
          DerivedFF,
          DerivedIF,
          DerivedJ,
          DerivedIM> Self;
        public:

          typedef CGAL::Point_3<Kernel>    Point_3;
          typedef CGAL::Segment_3<Kernel>  Segment_3;
          typedef CGAL::Triangle_3<Kernel> Triangle_3;
          typedef CGAL::Plane_3<Kernel>    Plane_3;
          typedef CGAL::Tetrahedron_3<Kernel> Tetrahedron_3;

          typedef CGAL::Point_2<Kernel>    Point_2;
          typedef CGAL::Segment_2<Kernel>  Segment_2;
          typedef CGAL::Triangle_2<Kernel> Triangle_2;

          typedef CGAL::Exact_intersections_tag Itag;

          typedef std::vector<Triangle_3> Triangles;
          typedef typename Triangles::iterator TrianglesIterator;
          typedef typename Triangles::const_iterator TrianglesConstIterator;
          typedef
            CGAL::Box_intersection_d::Box_with_handle_d<double,3,TrianglesIterator>
            Box;


          const Eigen::MatrixBase<DerivedV> & V;
          const Eigen::MatrixBase<DerivedF> & F;

          typedef typename DerivedF::Index Index;
          Index count;
          typedef std::vector<std::pair<Index, CGAL::Object>> ObjectList;

          Triangles T;
          typedef std::vector<Index> IndexList;
          IndexList lIF;


          std::map<Index,ObjectList> offending;

          typedef std::pair<Index,Index> EMK;


          typedef std::vector<Index> EMV;

          typedef std::map<EMK,EMV> EdgeMap;

          std::vector<std::pair<TrianglesIterator, TrianglesIterator> >
              candidate_triangle_pairs;

        public:
          RemeshSelfIntersectionsParam params;
        public:














          inline SelfIntersectMesh(
              const Eigen::MatrixBase<DerivedV> & V,
              const Eigen::MatrixBase<DerivedF> & F,
              const RemeshSelfIntersectionsParam & params,
              Eigen::PlainObjectBase<DerivedVV> & VV,
              Eigen::PlainObjectBase<DerivedFF> & FF,
              Eigen::PlainObjectBase<DerivedIF> & IF,
              Eigen::PlainObjectBase<DerivedJ> & J,
              Eigen::PlainObjectBase<DerivedIM> & IM);
        private:



          inline void mark_offensive(const Index f);




          inline void count_intersection( const Index fa, const Index fb);










          inline bool intersect(
              const Triangle_3 & A,
              const Triangle_3 & B,
              const Index fa,
              const Index fb);













          inline bool single_shared_vertex(
              const Triangle_3 & A,
              const Triangle_3 & B,
              const Index fa,
              const Index fb,
              const Index va,
              const Index vb);








          inline bool single_shared_vertex(
              const Triangle_3 & A,
              const Triangle_3 & B,
              const Index fa,
              const Index fb,
              const Index va);











          inline bool double_shared_vertex(
              const Triangle_3 & A,
              const Triangle_3 & B,
              const Index fa,
              const Index fb,
              const std::vector<std::pair<Index,Index> > shared);

        public:






          inline void box_intersect(const Box& a, const Box& b);

          inline void process_intersecting_boxes();
        public:







          static inline void box_intersect_static(
            SelfIntersectMesh * SIM,
            const Box &a,
            const Box &b);
        private:
          std::mutex m_offending_lock;
      };
    }
  }
}



#include "mesh_to_cgal_triangle_list.h"
#include "remesh_intersections.h"

#include "../../get_seconds.h"
#include "../../C_STR.h"


#include <functional>
#include <algorithm>
#include <exception>
#include <cassert>




















template <
  typename Kernel,
  typename DerivedV,
  typename DerivedF,
  typename DerivedVV,
  typename DerivedFF,
  typename DerivedIF,
  typename DerivedJ,
  typename DerivedIM>
inline void igl::copyleft::cgal::SelfIntersectMesh<
  Kernel,
  DerivedV,
  DerivedF,
  DerivedVV,
  DerivedFF,
  DerivedIF,
  DerivedJ,
  DerivedIM>::box_intersect_static(
  Self * SIM,
  const typename Self::Box &a,
  const typename Self::Box &b)
{
  SIM->box_intersect(a,b);
}

template <
  typename Kernel,
  typename DerivedV,
  typename DerivedF,
  typename DerivedVV,
  typename DerivedFF,
  typename DerivedIF,
  typename DerivedJ,
  typename DerivedIM>
inline igl::copyleft::cgal::SelfIntersectMesh<
  Kernel,
  DerivedV,
  DerivedF,
  DerivedVV,
  DerivedFF,
  DerivedIF,
  DerivedJ,
  DerivedIM>::SelfIntersectMesh(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedF> & F,
  const RemeshSelfIntersectionsParam & params,
  Eigen::PlainObjectBase<DerivedVV> & VV,
  Eigen::PlainObjectBase<DerivedFF> & FF,
  Eigen::PlainObjectBase<DerivedIF> & IF,
  Eigen::PlainObjectBase<DerivedJ> & J,
  Eigen::PlainObjectBase<DerivedIM> & IM):
  V(V),
  F(F),
  count(0),
  T(),
  lIF(),
  offending(),
  params(params)
{
  using namespace std;
  using namespace Eigen;

#ifdef IGL_SELFINTERSECTMESH_TIMING
  const auto & tictoc = []() -> double
  {
    static double t_start = igl::get_seconds();
    double diff = igl::get_seconds()-t_start;
    t_start += diff;
    return diff;
  };
  const auto log_time = [&](const std::string& label) -> void{
    std::printf("%50s: %0.5lf\n",
      C_STR("SelfIntersectMesh." << label),tictoc());
  };
  tictoc();
#endif


  mesh_to_cgal_triangle_list(V,F,T);
#ifdef IGL_SELFINTERSECTMESH_TIMING
  log_time("convert_to_triangle_list");
#endif


  std::vector<Box> boxes;
  boxes.reserve(T.size());
  for (
    TrianglesIterator tit = T.begin();
    tit != T.end();
    ++tit)
  {
    if (!tit->is_degenerate())
    {
      boxes.push_back(Box(tit->bbox(), tit));
    }
  }

  std::function<void(const Box &a,const Box &b)> cb =
    std::bind(&box_intersect_static, this,


      std::placeholders::_1,
      std::placeholders::_2);
#ifdef IGL_SELFINTERSECTMESH_TIMING
  log_time("box_and_bind");
#endif

  CGAL::box_self_intersection_d(boxes.begin(), boxes.end(),cb,std::ptrdiff_t(params.cutoff));
#ifdef IGL_SELFINTERSECTMESH_TIMING
  log_time("box_intersection_d");
#endif
  try{
    process_intersecting_boxes();
  }catch(int e)
  {

    if(e != IGL_FIRST_HIT_EXCEPTION)
    {
      throw e;
    }
    // Otherwise just fall through
  }
#ifdef IGL_SELFINTERSECTMESH_TIMING
  log_time("resolve_intersection");
#endif


  assert(lIF.size()%2 == 0);
  IF.resize(lIF.size()/2,2);
  {
    Index i=0;
    for(
      typename IndexList::const_iterator ifit = lIF.begin();
      ifit!=lIF.end();
      )
    {
      IF(i,0) = (*ifit);
      ifit++;
      IF(i,1) = (*ifit);
      ifit++;
      i++;
    }
  }
#ifdef IGL_SELFINTERSECTMESH_TIMING
  log_time("store_intersecting_face_pairs");
#endif

  if(params.detect_only)
  {
    return;
  }

  remesh_intersections(
    V,F,T,offending,
    params.stitch_all,params.slow_and_more_precise_rounding,VV,FF,J,IM);

#ifdef IGL_SELFINTERSECTMESH_TIMING
  log_time("remesh_intersection");
#endif
}


template <
  typename Kernel,
  typename DerivedV,
  typename DerivedF,
  typename DerivedVV,
  typename DerivedFF,
  typename DerivedIF,
  typename DerivedJ,
  typename DerivedIM>
inline void igl::copyleft::cgal::SelfIntersectMesh<
  Kernel,
  DerivedV,
  DerivedF,
  DerivedVV,
  DerivedFF,
  DerivedIF,
  DerivedJ,
  DerivedIM>::mark_offensive(const Index f)
{
  using namespace std;
  lIF.push_back(f);
  if(offending.count(f) == 0)
  {

    offending[f] = {};
  }
}

template <
  typename Kernel,
  typename DerivedV,
  typename DerivedF,
  typename DerivedVV,
  typename DerivedFF,
  typename DerivedIF,
  typename DerivedJ,
  typename DerivedIM>
inline void igl::copyleft::cgal::SelfIntersectMesh<
  Kernel,
  DerivedV,
  DerivedF,
  DerivedVV,
  DerivedFF,
  DerivedIF,
  DerivedJ,
  DerivedIM>::count_intersection(
  const Index fa,
  const Index fb)
{
  std::lock_guard<std::mutex> guard(m_offending_lock);
  mark_offensive(fa);
  mark_offensive(fb);
  this->count++;

  if(params.first_only && this->count >= 1)
  {
    throw IGL_FIRST_HIT_EXCEPTION;
  }

}

template <
  typename Kernel,
  typename DerivedV,
  typename DerivedF,
  typename DerivedVV,
  typename DerivedFF,
  typename DerivedIF,
  typename DerivedJ,
  typename DerivedIM>
inline bool igl::copyleft::cgal::SelfIntersectMesh<
  Kernel,
  DerivedV,
  DerivedF,
  DerivedVV,
  DerivedFF,
  DerivedIF,
  DerivedJ,
  DerivedIM>::intersect(
  const Triangle_3 & A,
  const Triangle_3 & B,
  const Index fa,
  const Index fb)
{

  if(!CGAL::do_intersect(A,B))
  {
    return false;
  }
  count_intersection(fa,fb);
  if(!params.detect_only)
  {

    CGAL::Object result = CGAL::intersection(A,B);


    std::lock_guard<std::mutex> guard(m_offending_lock);
    offending[fa].push_back({fb, result});
    offending[fb].push_back({fa, result});
  }
  return true;
}

template <
  typename Kernel,
  typename DerivedV,
  typename DerivedF,
  typename DerivedVV,
  typename DerivedFF,
  typename DerivedIF,
  typename DerivedJ,
  typename DerivedIM>
inline bool igl::copyleft::cgal::SelfIntersectMesh<
  Kernel,
  DerivedV,
  DerivedF,
  DerivedVV,
  DerivedFF,
  DerivedIF,
  DerivedJ,
  DerivedIM>::single_shared_vertex(
  const Triangle_3 & A,
  const Triangle_3 & B,
  const Index fa,
  const Index fb,
  const Index va,
  const Index vb)
{
  if(single_shared_vertex(A,B,fa,fb,va))
  {
    return true;
  }
  return single_shared_vertex(B,A,fb,fa,vb);
}

template <
  typename Kernel,
  typename DerivedV,
  typename DerivedF,
  typename DerivedVV,
  typename DerivedFF,
  typename DerivedIF,
  typename DerivedJ,
  typename DerivedIM>
inline bool igl::copyleft::cgal::SelfIntersectMesh<
  Kernel,
  DerivedV,
  DerivedF,
  DerivedVV,
  DerivedFF,
  DerivedIF,
  DerivedJ,
  DerivedIM>::single_shared_vertex(
  const Triangle_3 & A,
  const Triangle_3 & B,
  const Index fa,
  const Index fb,
  const Index va)
{

  using namespace std;
  Segment_3 sa(
    A.vertex((va+1)%3),
    A.vertex((va+2)%3));

  if(CGAL::do_intersect(sa,B))
  {


    if(params.detect_only)
    {
      count_intersection(fa,fb);
      return true;
    }
    CGAL::Object result = CGAL::intersection(sa,B);
    if(const Point_3 * p = CGAL::object_cast<Point_3 >(&result))
    {

      CGAL::Object seg = CGAL::make_object(Segment_3(
        A.vertex(va),
        *p));
      count_intersection(fa,fb);
      std::lock_guard<std::mutex> guard(m_offending_lock);
      offending[fa].push_back({fb, seg});
      offending[fb].push_back({fa, seg});
      return true;
    }else if(CGAL::object_cast<Segment_3 >(&result))
    {

      bool test = intersect(A,B,fa,fb);
      ((void)test);
      assert(test && "intersect should agree with do_intersect");
      return true;
    }else
    {

      assert(false && "Segment ∩ triangle neither point nor segment?");
    }
  }

  return false;
}


template <
  typename Kernel,
  typename DerivedV,
  typename DerivedF,
  typename DerivedVV,
  typename DerivedFF,
  typename DerivedIF,
  typename DerivedJ,
  typename DerivedIM>
inline bool igl::copyleft::cgal::SelfIntersectMesh<
  Kernel,
  DerivedV,
  DerivedF,
  DerivedVV,
  DerivedFF,
  DerivedIF,
  DerivedJ,
  DerivedIM>::double_shared_vertex(
  const Triangle_3 & A,
  const Triangle_3 & B,
  const Index fa,
  const Index fb,
  const std::vector<std::pair<Index,Index> > shared)
{
  using namespace std;

  auto opposite_vertex = [](const Index a0, const Index a1) {

    int a2=-1;
	for(int c=0;c<3;++c)
      if(c!=a0 && c!=a1) {
        a2 = c;
        break;
      }
      assert(a2 != -1);
      return a2;
  };


  Index a2 = opposite_vertex(shared[0].first, shared[1].first);
  if (! B.supporting_plane().has_on(A.vertex(a2)))
    return false;

  Index b2 = opposite_vertex(shared[0].second, shared[1].second);

  if (int(CGAL::coplanar_orientation(A.vertex(shared[0].first), A.vertex(shared[1].first), A.vertex(a2))) *
	  int(CGAL::coplanar_orientation(B.vertex(shared[0].second), B.vertex(shared[1].second), B.vertex(b2))) < 0)

    return false;








  const auto & opposite_point_inside = [](
    const Triangle_3 & A, const Index a2, const Triangle_3 & B)
    -> bool
  {
    return CGAL::do_intersect(A.vertex(a2),B);
  };



  const auto & opposite_edges_intersect = [](
    const Triangle_3 & A, const Index va,
    const Triangle_3 & B, const Index vb) -> bool
  {
    Segment_3 sa( A.vertex((va+1)%3), A.vertex((va+2)%3));
    Segment_3 sb( B.vertex((vb+1)%3), B.vertex((vb+2)%3));
    bool ret = CGAL::do_intersect(sa,sb);
    return ret;
  };

  if(
    !opposite_point_inside(A,a2,B) &&
    !opposite_point_inside(B,b2,A) &&
    !opposite_edges_intersect(A,shared[0].first,B,shared[1].second) &&
    !opposite_edges_intersect(A,shared[1].first,B,shared[0].second))
  {
    return false;
  }


  count_intersection(fa,fb);
  if(params.detect_only)
  {
    return true;
  }

  try
  {

    CGAL::Object result = CGAL::intersection(A,B);
    if(!result.empty())
    {
      if(CGAL::object_cast<Segment_3 >(&result))
      {

        assert(false &&
          "Co-planar non-degenerate triangles should intersect over triangle");
        return false;
      } else if(CGAL::object_cast<Point_3 >(&result))
      {

        assert(false &&
          "Co-planar non-degenerate triangles should intersect over triangle");
        return false;
      } else
      {

        std::lock_guard<std::mutex> guard(m_offending_lock);
        offending[fa].push_back({fb, result});
        offending[fb].push_back({fa, result});
        return true;
      }
    }else
    {

      assert(false && "CGAL::intersection should agree with predicate tests");
      return false;
    }
  }catch(...)
  {








    return false;
  }

  return false;
}

template <
  typename Kernel,
  typename DerivedV,
  typename DerivedF,
  typename DerivedVV,
  typename DerivedFF,
  typename DerivedIF,
  typename DerivedJ,
  typename DerivedIM>
inline void igl::copyleft::cgal::SelfIntersectMesh<
  Kernel,
  DerivedV,
  DerivedF,
  DerivedVV,
  DerivedFF,
  DerivedIF,
  DerivedJ,
  DerivedIM>::box_intersect(
  const Box& a,
  const Box& b)
{
  candidate_triangle_pairs.push_back({a.handle(), b.handle()});
}

template <
  typename Kernel,
  typename DerivedV,
  typename DerivedF,
  typename DerivedVV,
  typename DerivedFF,
  typename DerivedIF,
  typename DerivedJ,
  typename DerivedIM>
inline void igl::copyleft::cgal::SelfIntersectMesh<
  Kernel,
  DerivedV,
  DerivedF,
  DerivedVV,
  DerivedFF,
  DerivedIF,
  DerivedJ,
  DerivedIM>::process_intersecting_boxes()
{
  std::mutex exception_mutex;
  bool exception_fired = false;
  int exception = -1;





  auto process_chunk = [&]( const size_t first, const size_t last) -> void
  {
    try
    {
      assert(last >= first);

      for (size_t i=first; i<last; i++)
      {
        if(exception_fired) return;
        Index fa=T.size(), fb=T.size();
        {
          const auto& tri_pair = candidate_triangle_pairs[i];
          fa = tri_pair.first - T.begin();
          fb = tri_pair.second - T.begin();
        }
        assert(fa < T.size());
        assert(fb < T.size());

        if(exception_fired) return;

        const Triangle_3& A = T[fa];
        const Triangle_3& B = T[fb];


        Index comb_shared_vertices = 0;


        Index geo_shared_vertices = 0;

        std::vector<std::pair<Index,Index> > shared;
        Index ea,eb;
        for(ea=0;ea<3;ea++)
        {
          for(eb=0;eb<3;eb++)
          {
            if(F(fa,ea) == F(fb,eb))
            {
              comb_shared_vertices++;
              shared.emplace_back(ea,eb);
            }else if(A.vertex(ea) == B.vertex(eb))
            {
              geo_shared_vertices++;
              shared.emplace_back(ea,eb);
            }
          }
        }
        const Index total_shared_vertices =
          comb_shared_vertices + geo_shared_vertices;
        if(exception_fired) return;

        if(comb_shared_vertices== 3)
        {
          assert(shared.size() == 3);


          continue;
        }
        if(total_shared_vertices== 3)
        {
          assert(shared.size() == 3);


          continue;
        }
        if(total_shared_vertices == 2)
        {
          assert(shared.size() == 2);








          double_shared_vertex(A,B,fa,fb,shared);
          continue;
        }
        assert(total_shared_vertices<=1);
        if(total_shared_vertices==1)
        {
          single_shared_vertex(A,B,fa,fb,shared[0].first,shared[0].second);
        }else
        {
          intersect(A,B,fa,fb);
        }
      }
    }catch(int e)
    {
      std::lock_guard<std::mutex> exception_lock(exception_mutex);
      exception_fired = true;
      exception = e;
    }
  };
  const size_t num_threads = default_num_threads();
  assert(num_threads > 0);
  const size_t num_pairs = candidate_triangle_pairs.size();
  const size_t chunk_size = num_pairs / num_threads;
  std::vector<std::thread> threads;
  for (size_t i=0; i<num_threads-1; i++)
  {
    threads.emplace_back(process_chunk, i*chunk_size, (i+1)*chunk_size);
  }

  process_chunk((num_threads-1)*chunk_size, num_pairs);
  for (auto& t : threads)
  {
    if (t.joinable()) t.join();
  }
  if(exception_fired) throw exception;

}

#endif
