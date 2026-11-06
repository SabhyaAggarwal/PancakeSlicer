// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_AABB_H
#define IGL_AABB_H

#include "Hit.h"
#include "igl_inline.h"
#include <cassert>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <vector>
namespace igl
{










  template <typename DerivedV, int DIM>
    class AABB
    {
public:




      typedef typename DerivedV::Scalar Scalar;

      typedef Eigen::Matrix<Scalar,1,DIM> RowVectorDIMS;

      typedef Eigen::Matrix<Scalar,DIM,1> VectorDIMS;

      typedef Eigen::Matrix<Scalar,Eigen::Dynamic,DIM> MatrixXDIMS;


      AABB * m_left;

      AABB * m_right;

      AABB * m_parent;

      Eigen::AlignedBox<Scalar,DIM> m_box;

      int m_primitive;






      AABB():
        m_left(nullptr), m_right(nullptr),m_parent(nullptr),
        m_box(), m_primitive(-1)


      {
        static_assert(DerivedV::ColsAtCompileTime == DIM || DerivedV::ColsAtCompileTime == Eigen::Dynamic,"DerivedV::ColsAtCompileTime == DIM || DerivedV::ColsAtCompileTime == Eigen::Dynamic");
      }


      AABB(const AABB& other):
        m_left (other.m_left  ? new AABB(*other.m_left)  : nullptr),
        m_right(other.m_right ? new AABB(*other.m_right) : nullptr),
        m_parent(other.m_parent),
        m_box(other.m_box),
        m_primitive(other.m_primitive)




        {
          if(m_left) { m_left->m_parent = this; }
          if(m_right) { m_right->m_parent = this; }
        }


      friend void swap(AABB& first, AABB& second)
      {

        using std::swap;
        swap(first.m_left,second.m_left);
        swap(first.m_right,second.m_right);
        swap(first.m_parent,second.m_parent);
        swap(first.m_box,second.m_box);
        swap(first.m_primitive,second.m_primitive);


      }


      AABB& operator=(AABB other)
      {
        swap(*this,other);
        return *this;
      }

      AABB(AABB&& other):

        AABB()
      {
        swap(*this,other);
      }



      IGL_INLINE void clear()
      {
        m_primitive = -1;
        m_box = Eigen::AlignedBox<Scalar,DIM>();
        delete m_left;
        m_left = nullptr;
        delete m_right;
        m_right = nullptr;

        if(m_parent)
        {
          if(m_parent->m_left == this)
          {
            m_parent->m_left = nullptr;
          }else if(m_parent->m_right == this)
          {
            m_parent->m_right = nullptr;
          }else
          {
            assert(false && "I'm not my parent's child");
          }
          auto * grandparent = m_parent->m_parent;
          if(grandparent)
          {











          }else
          {









          }
        }

        m_parent = nullptr;
      }

      ~AABB()
      {
        clear();
      }

      IGL_INLINE bool is_leaf() const;

      IGL_INLINE bool is_root() const;

      IGL_INLINE AABB<DerivedV,DIM>* root() const;
      IGL_INLINE AABB<DerivedV,DIM>* detach();
      IGL_INLINE void refit_lineage();




      IGL_INLINE std::vector<AABB<DerivedV,DIM>*> gather_leaves(const int m);

      IGL_INLINE std::vector<AABB<DerivedV,DIM>*> gather_leaves();




      IGL_INLINE AABB<DerivedV,DIM>* pad(
        const std::vector<AABB<DerivedV,DIM>*> & leaves,
        const Scalar pad,
        const int polish_rotate_passes=0);

















      IGL_INLINE AABB<DerivedV,DIM>* update(
          const Eigen::AlignedBox<Scalar,DIM> & new_box,
          const Scalar pad=0);




































      IGL_INLINE AABB<DerivedV,DIM>* insert(AABB * other);






















      IGL_INLINE AABB<DerivedV,DIM>* insert_as_sibling(AABB * other);

















      IGL_INLINE Scalar rotate(const bool dry_run = false);




























      IGL_INLINE Scalar rotate_across(const bool dry_run = false);





















      IGL_INLINE Scalar rotate_up(const bool dry_run = false);





























      IGL_INLINE Scalar rotate_down(const bool dry_run = false);























      static IGL_INLINE Scalar rotate_up(
        const bool dry_run,
        AABB<DerivedV,DIM>* reining,
        AABB<DerivedV,DIM>* grandparent,
        AABB<DerivedV,DIM>* parent,
        AABB<DerivedV,DIM>* challenger,
        AABB<DerivedV,DIM>* sibling);

      IGL_INLINE void rotate_lineage();









      IGL_INLINE int subtree_size() const;


      IGL_INLINE bool append_intersecting_leaves(
        const Eigen::AlignedBox<Scalar,DIM> & box,
        std::vector<const AABB<DerivedV,DIM>*> & leaves) const;

      IGL_INLINE typename DerivedV::Scalar internal_surface_area() const;


      IGL_INLINE void validate() const;

      IGL_INLINE void print(const int depth = 0) const;




      IGL_INLINE int size() const;

      IGL_INLINE int height() const;
private:
















      IGL_INLINE void set_min(
        const RowVectorDIMS & p,
        const Scalar sqr_d_candidate,
        const int & i_candidate,
        const RowVectorDIMS & c_candidate,
        Scalar & sqr_d,
        int & i,
        Eigen::PlainObjectBase<RowVectorDIMS> & c) const;
public:















      template <
        typename DerivedEle,
        typename Derivedbb_mins,
        typename Derivedbb_maxs,
        typename Derivedelements>
        IGL_INLINE void init(
            const Eigen::MatrixBase<DerivedV> & V,
            const Eigen::MatrixBase<DerivedEle> & Ele,
            const Eigen::MatrixBase<Derivedbb_mins> & bb_mins,
            const Eigen::MatrixBase<Derivedbb_maxs> & bb_maxs,
            const Eigen::MatrixBase<Derivedelements> & elements,
            const int i = 0);





      template <typename DerivedEle>
      IGL_INLINE void init(
          const Eigen::MatrixBase<DerivedV> & V,
          const Eigen::MatrixBase<DerivedEle> & Ele);











      template <typename DerivedEle, typename DerivedSI, typename DerivedI>
      IGL_INLINE void init(
          const Eigen::MatrixBase<DerivedV> & V,
          const Eigen::MatrixBase<DerivedEle> & Ele,
          const Eigen::MatrixBase<DerivedSI> & SI,
          const Eigen::MatrixBase<DerivedI>& I);


      template <typename DerivedEle>
      IGL_INLINE AABB<DerivedV,DIM>* update_primitive(
          const Eigen::MatrixBase<DerivedV> & V,
          const Eigen::MatrixBase<DerivedEle> & Ele,
          const Scalar pad=0);











      template <typename DerivedEle, typename Derivedq>
      IGL_INLINE std::vector<int> find(
          const Eigen::MatrixBase<DerivedV> & V,
          const Eigen::MatrixBase<DerivedEle> & Ele,
          const Eigen::MatrixBase<Derivedq> & q,
          const bool first=false) const;










      template <
        typename Derivedbb_mins,
        typename Derivedbb_maxs,
        typename Derivedelements>
        IGL_INLINE void serialize(
            Eigen::PlainObjectBase<Derivedbb_mins> & bb_mins,
            Eigen::PlainObjectBase<Derivedbb_maxs> & bb_maxs,
            Eigen::PlainObjectBase<Derivedelements> & elements,
            const int i = 0) const;











      template <typename DerivedEle>
      IGL_INLINE Scalar squared_distance(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedEle> & Ele,
        const RowVectorDIMS & p,
        int & i,
        Eigen::PlainObjectBase<RowVectorDIMS> & c) const;

















      template <typename DerivedEle>
      IGL_INLINE Scalar squared_distance(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedEle> & Ele,
        const RowVectorDIMS & p,
        const Scalar low_sqr_d,
        const Scalar up_sqr_d,
        int & i,
        Eigen::PlainObjectBase<RowVectorDIMS> & c) const;












      template <typename DerivedEle>
      IGL_INLINE Scalar squared_distance(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedEle> & Ele,
        const RowVectorDIMS & p,
        const Scalar up_sqr_d,
        int & i,
        Eigen::PlainObjectBase<RowVectorDIMS> & c) const;








      template <typename DerivedEle>
      IGL_INLINE bool intersect_ray(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedEle> & Ele,
        const RowVectorDIMS & origin,
        const RowVectorDIMS & dir,
        std::vector<igl::Hit<typename DerivedV::Scalar>> & hits) const;








      template <typename DerivedEle>
      IGL_INLINE bool intersect_ray(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedEle> & Ele,
        const RowVectorDIMS & origin,
        const RowVectorDIMS & dir,
        igl::Hit<typename DerivedV::Scalar> & hit) const;










      template <typename DerivedEle>
      IGL_INLINE bool intersect_ray(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedEle> & Ele,
        const RowVectorDIMS & origin,
        const RowVectorDIMS & dir,
        const Scalar min_t,
        igl::Hit<typename DerivedV::Scalar> & hit) const;











      template <
        typename DerivedEle,
        typename DerivedOrigin,
        typename DerivedDir,
        typename DerivedI,
        typename DerivedT,
        typename DerivedUV>
      IGL_INLINE void intersect_ray(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedEle> & Ele,
        const Eigen::MatrixBase<DerivedOrigin> & origin,
        const Eigen::MatrixBase<DerivedDir> & dir,
        const Scalar min_t,
        Eigen::PlainObjectBase<DerivedI> & I,
        Eigen::PlainObjectBase<DerivedT> & T,
        Eigen::PlainObjectBase<DerivedUV> & UV);
      template <
        typename DerivedEle,
        typename DerivedOrigin,
        typename DerivedDir>
      IGL_INLINE void intersect_ray(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedEle> & Ele,
        const Eigen::MatrixBase<DerivedOrigin> & origin,
        const Eigen::MatrixBase<DerivedDir> & dir,
        std::vector<std::vector<igl::Hit<typename DerivedV::Scalar>>> & hits);










      template <
        typename DerivedEle,
        typename DerivedP,
        typename DerivedsqrD,
        typename DerivedI,
        typename DerivedC>
      IGL_INLINE void squared_distance(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedEle> & Ele,
        const Eigen::MatrixBase<DerivedP> & P,
        Eigen::PlainObjectBase<DerivedsqrD> & sqrD,
        Eigen::PlainObjectBase<DerivedI> & I,
        Eigen::PlainObjectBase<DerivedC> & C) const;













      template <
        typename DerivedEle,
        typename Derivedother_V,
        typename Derivedother_Ele,
        typename DerivedsqrD,
        typename DerivedI,
        typename DerivedC>
      IGL_INLINE void squared_distance(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedEle> & Ele,
        const AABB<Derivedother_V,DIM> & other,
        const Eigen::MatrixBase<Derivedother_V> & other_V,
        const Eigen::MatrixBase<Derivedother_Ele> & other_Ele,
        Eigen::PlainObjectBase<DerivedsqrD> & sqrD,
        Eigen::PlainObjectBase<DerivedI> & I,
        Eigen::PlainObjectBase<DerivedC> & C) const;
private:
      template <
        typename DerivedEle,
        typename Derivedother_V,
        typename Derivedother_Ele,
        typename DerivedsqrD,
        typename DerivedI,
        typename DerivedC>
      IGL_INLINE Scalar squared_distance_helper(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedEle> & Ele,
        const AABB<Derivedother_V,DIM> * other,
        const Eigen::MatrixBase<Derivedother_V> & other_V,
        const Eigen::MatrixBase<Derivedother_Ele>& other_Ele,
        const Scalar up_sqr_d,
        Eigen::PlainObjectBase<DerivedsqrD> & sqrD,
        Eigen::PlainObjectBase<DerivedI> & I,
        Eigen::PlainObjectBase<DerivedC> & C) const;















      template <typename DerivedEle>
      IGL_INLINE void leaf_squared_distance(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedEle> & Ele,
        const RowVectorDIMS & p,
        const Scalar low_sqr_d,
        Scalar & sqr_d,
        int & i,
        Eigen::PlainObjectBase<RowVectorDIMS> & c) const;

      template <typename DerivedEle>
      IGL_INLINE void leaf_squared_distance(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedEle> & Ele,
        const RowVectorDIMS & p,
        Scalar & sqr_d,
        int & i,
        Eigen::PlainObjectBase<RowVectorDIMS> & c) const;








      template <typename DerivedEle>
      IGL_INLINE bool intersect_ray_opt(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedEle> & Ele,
        const RowVectorDIMS & origin,
        const RowVectorDIMS & dir,
        const RowVectorDIMS & inv_dir,
        const RowVectorDIMS & inv_dir_pad,
        std::vector<igl::Hit<typename DerivedV::Scalar>> & hits) const;









      template <typename DerivedEle>
      IGL_INLINE bool intersect_ray_opt(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedEle> & Ele,
        const RowVectorDIMS & origin,
        const RowVectorDIMS & dir,
        const RowVectorDIMS & inv_dir,
        const RowVectorDIMS & inv_dir_pad,
        const Scalar min_t,
        igl::Hit<typename DerivedV::Scalar> & hit) const;
public:
      EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    };
}


#ifndef IGL_STATIC_LIBRARY
#  include "AABB.cpp"
#endif

#endif
