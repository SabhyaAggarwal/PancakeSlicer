// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_WINDINGNUMBERTREE_H
#define IGL_WINDINGNUMBERTREE_H
#include <list>
#include <map>
#include <Eigen/Dense>
#include "WindingNumberMethod.h"
#include <memory>

namespace igl
{

  template <
    typename Scalar,
    typename Index>
  class WindingNumberTree
  {
    public:
      using Point = Eigen::Matrix<Scalar,1,3>;


      static std::map<
        std::pair<const WindingNumberTree*,const WindingNumberTree*>,
        Scalar>
          cached;
    protected:
      WindingNumberMethod method;
      const WindingNumberTree * parent;
      std::list<WindingNumberTree * > children;
      typedef
        Eigen::Matrix<Scalar,Eigen::Dynamic,Eigen::Dynamic>
        MatrixXS;
      typedef
        Eigen::Matrix<Index,Eigen::Dynamic,Eigen::Dynamic>
        MatrixXF;


      MatrixXS SV;

      std::shared_ptr<MatrixXS> Vptr;

      MatrixXF F;

      MatrixXF cap;

      Scalar radius;

      Point center;
    public:
      inline WindingNumberTree();

      template <typename DerivedV, typename DerivedF>
      inline WindingNumberTree(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedF> & F);

      inline WindingNumberTree(
        const WindingNumberTree<Scalar,Index> & parent,
        const typename igl::WindingNumberTree<Scalar,Index>::MatrixXF & F);
      inline virtual ~WindingNumberTree();
      inline void delete_children();
      template <typename DerivedV, typename DerivedF>
      inline void set_mesh(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedF> & F);

      inline void set_method( const WindingNumberMethod & m);
    public:

      inline virtual void grow();





      inline virtual bool inside(const Point & p) const;






      inline Scalar winding_number(const Point & p) const;


      inline Scalar winding_number_all(const Point & p) const;

      inline Scalar winding_number_boundary(const Point & p) const;
















      inline void print(const char * tab="");





      inline virtual Scalar max_abs_winding_number(const Point & p) const;


      inline virtual Scalar max_simple_abs_winding_number(const Point & p) const;







      inline virtual Scalar cached_winding_number(const WindingNumberTree & that, const Point & p) const;
  };
}



#include "WindingNumberTree.h"
#include "winding_number.h"
#include "triangle_fan.h"
#include "exterior_edges.h"

#include "PI.h"
#include "remove_duplicate_vertices.h"

#include <iostream>
#include <limits>





template <typename Scalar, typename Index>
std::map< std::pair<const igl::WindingNumberTree<Scalar,Index>*,const igl::WindingNumberTree<Scalar,Index>*>, Scalar>
  igl::WindingNumberTree<Scalar,Index>::cached;

template <typename Scalar, typename Index>
inline igl::WindingNumberTree<Scalar,Index>::WindingNumberTree():
  method(EXACT_WINDING_NUMBER_METHOD),
  parent(NULL),
  SV(),
  F(),
  cap(),
  radius(std::numeric_limits<Scalar>::infinity()),
  center(0,0,0)
{
}

template <typename Scalar, typename Index>
template <typename DerivedV, typename DerivedF>
inline igl::WindingNumberTree<Scalar,Index>::WindingNumberTree(
  const Eigen::MatrixBase<DerivedV> & _V,
  const Eigen::MatrixBase<DerivedF> & _F):
  method(EXACT_WINDING_NUMBER_METHOD),
  parent(NULL),
  SV(),
  F(),
  cap(),
  radius(std::numeric_limits<Scalar>::infinity()),
  center(0,0,0)
{
  set_mesh(_V,_F);
}

template <typename Scalar, typename Index>
template <typename DerivedV, typename DerivedF>
inline void igl::WindingNumberTree<Scalar,Index>::set_mesh(
    const Eigen::MatrixBase<DerivedV> & _V,
    const Eigen::MatrixBase<DerivedF> & _F)
{
  using namespace std;



  Eigen::Matrix<typename MatrixXF::Scalar,Eigen::Dynamic,1> SVI,SVJ;
  igl::remove_duplicate_vertices(_V,_F,0.0,SV,SVI,SVJ,F);
  {
    Eigen::Matrix<typename MatrixXF::Scalar,Eigen::Dynamic,2> EE;
    igl::exterior_edges(F,EE);
    triangle_fan(EE,cap);
  }

  Vptr = std::make_shared<MatrixXS>(SV);
}

template <typename Scalar, typename Index>
inline igl::WindingNumberTree<Scalar,Index>::WindingNumberTree(
  const igl::WindingNumberTree<Scalar,Index> & parent,
  const typename igl::WindingNumberTree<Scalar,Index>::MatrixXF & _F):
  method(parent.method),
  parent(&parent),
  Vptr(parent.Vptr),
  SV(),
  F(_F),
  cap()
{
  Eigen::Matrix<typename MatrixXF::Scalar,Eigen::Dynamic,2> EE;
  igl::exterior_edges(F,EE);
  triangle_fan(EE,cap);
}

template <typename Scalar, typename Index>
inline igl::WindingNumberTree<Scalar,Index>::~WindingNumberTree()
{
  delete_children();
}

template <typename Scalar, typename Index>
inline void igl::WindingNumberTree<Scalar,Index>::delete_children()
{
  using namespace std;

  typename list<WindingNumberTree<Scalar,Index>* >::iterator cit = children.begin();
  while(cit != children.end())
  {

    delete (* cit);

    cit = children.erase(cit);
  }
}

template <typename Scalar, typename Index>
inline void igl::WindingNumberTree<Scalar,Index>::set_method(const WindingNumberMethod & m)
{
  this->method = m;
  for(auto child : children)
  {
    child->set_method(m);
  }
}

template <typename Scalar, typename Index>
inline void igl::WindingNumberTree<Scalar,Index>::grow()
{

  return;
}

template <typename Scalar, typename Index>
inline bool igl::WindingNumberTree<Scalar,Index>::inside(const Point &  ) const
{
  return true;
}

template <typename Scalar, typename Index>
inline Scalar
igl::WindingNumberTree<Scalar,Index>::winding_number(const Point & p) const
{
  using namespace std;


  if(inside(p))
  {

    if(children.size()>0)
    {

      Scalar sum = 0;
      for(
        typename list<WindingNumberTree<Scalar,Index>* >::const_iterator cit = children.begin();
        cit != children.end();
        cit++)
      {
        switch(method)
        {
          case EXACT_WINDING_NUMBER_METHOD:
            sum += (*cit)->winding_number(p);
            break;
          case APPROX_SIMPLE_WINDING_NUMBER_METHOD:
          case APPROX_CACHE_WINDING_NUMBER_METHOD:


              sum += (*cit)->winding_number(p);

            break;
          default:
            assert(false);
            break;
        }
      }
      return sum;
    }else
    {
      return winding_number_all(p);
    }
  }else{



    if((cap.rows() - 2) < F.rows())
    {
      switch(method)
      {
        case EXACT_WINDING_NUMBER_METHOD:
          return winding_number_boundary(p);
        case APPROX_SIMPLE_WINDING_NUMBER_METHOD:
        {
          Scalar dist = (p-center).norm();

          if(dist>1.0*radius)
          {
            return 0;
          }else
          {
            return winding_number_boundary(p);
          }
        }
        case APPROX_CACHE_WINDING_NUMBER_METHOD:
        {
          return parent->cached_winding_number(*this,p);
        }
        default: assert(false);break;
      }
    }else
    {

      return winding_number_all(p);
    }
  }
  return 0;
}

template <typename Scalar, typename Index>
inline Scalar
  igl::WindingNumberTree<Scalar,Index>::winding_number_all(const Point & p) const
{
  return igl::winding_number(*Vptr,F,p);
}

template <typename Scalar, typename Index>
inline Scalar
igl::WindingNumberTree<Scalar,Index>::winding_number_boundary(const Point & p) const
{
  return igl::winding_number(*Vptr,cap,p);
}

















template <typename Scalar, typename Index>
inline void igl::WindingNumberTree<Scalar,Index>::print(const char * tab)
{
  using namespace std;

  cout<<tab<<"["<<endl<<F<<endl<<"]";

  for(
      typename list<WindingNumberTree<Scalar,Index>* >::iterator cit = children.begin();
      cit != children.end();
      cit++)
  {
    cout<<","<<endl;
    (*cit)->print((string(tab)+"").c_str());
  }
}

template <typename Scalar, typename Index>
inline Scalar
igl::WindingNumberTree<Scalar,Index>::max_abs_winding_number(const Point &  ) const
{
  return std::numeric_limits<Scalar>::infinity();
}

template <typename Scalar, typename Index>
inline Scalar
igl::WindingNumberTree<Scalar,Index>::max_simple_abs_winding_number(
  const Point &  ) const
{
  using namespace std;
  return numeric_limits<Scalar>::infinity();
}

template <typename Scalar, typename Index>
inline Scalar
igl::WindingNumberTree<Scalar,Index>::cached_winding_number(
  const igl::WindingNumberTree<Scalar,Index> & that,
  const Point & p) const
{
  using namespace std;

















  bool is_far = this->radius<that.radius;
  if(is_far)
  {
    Scalar a = atan2(
      that.radius - this->radius,
      (that.center - this->center).norm());
    assert(a>0);
    is_far = (a<PI/8.0);
  }

  if(is_far)
  {

    pair<const WindingNumberTree*,const WindingNumberTree*> this_that(this,&that);

    if(cached.count(this_that)==0)
    {
      cached[this_that] =
        that.winding_number_boundary(this->center);
    }
    return cached[this_that];
  }else if(children.size() == 0)
  {

    return that.winding_number_boundary(p);
  }else
  {
    for(
      typename list<WindingNumberTree<Scalar,Index>* >::const_iterator cit = children.begin();
      cit != children.end();
      cit++)
    {
      if((*cit)->inside(p))
      {
        return (*cit)->cached_winding_number(that,p);
      }
    }




    assert(false);
  }
  return 0;
}

#endif
