// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

// # MUTUAL DEPENDENCY ISSUE FOR HEADER ONLY VERSION
// MUST INCLUDE winding_number.h first before guard:
#include "winding_number.h"

#ifndef IGL_WINDINGNUMBERAABB_H
#define IGL_WINDINGNUMBERAABB_H
#include "WindingNumberTree.h"
#include "PlainMatrix.h"

namespace igl
{


  template <
    typename Scalar,
    typename Index>
  class WindingNumberAABB : public WindingNumberTree<Scalar,Index>
  {
    protected:

      using Point = typename WindingNumberTree<Scalar,Index>::Point;
      using MatrixXF = typename WindingNumberTree<Scalar,Index>::MatrixXF;
      Point min_corner;
      Point max_corner;
      Scalar total_positive_area;
    public:
      enum SplitMethod
      {
        CENTER_ON_LONGEST_AXIS = 0,
        MEDIAN_ON_LONGEST_AXIS = 1,
        NUM_SPLIT_METHODS = 2
      } split_method;
    public:
      inline WindingNumberAABB():
        total_positive_area(std::numeric_limits<Scalar>::infinity()),
        split_method(MEDIAN_ON_LONGEST_AXIS)
      {}




      template <typename DerivedV, typename DerivedF>
      inline WindingNumberAABB(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedF> & F);
      inline WindingNumberAABB(
        const WindingNumberTree<Scalar,Index> & parent,
        const typename WindingNumberTree<Scalar,Index>::MatrixXF & F);




      template <typename DerivedV, typename DerivedF>
      inline void set_mesh(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedF> & F);
      inline void init();
      inline bool inside(const Point & p) const;
      inline virtual void grow();

      inline void compute_min_max_corners();
      inline Scalar max_abs_winding_number(const Point & p) const;
      inline Scalar max_simple_abs_winding_number(const Point & p) const;
  };
}



#include "winding_number.h"

#include "barycenter.h"
#include "median.h"
#include "doublearea.h"
#include "per_face_normals.h"

#include <limits>
#include <vector>
#include <iostream>



#ifndef WindingNumberAABB_MIN_F
#  define WindingNumberAABB_MIN_F 100
#endif

template <typename Scalar, typename Index>
  template <typename DerivedV, typename DerivedF>
inline void igl::WindingNumberAABB<Scalar,Index>::set_mesh(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F)
{

  static_assert(
    DerivedF::ColsAtCompileTime == 3 || DerivedF::ColsAtCompileTime == Eigen::Dynamic,
    "F should have 3 or Dynamic columns");
  igl::WindingNumberTree<Scalar,Index>::set_mesh(V,F);
  init();
}

template <typename Scalar, typename Index>
inline void igl::WindingNumberAABB<Scalar,Index>::init()
{
  using namespace Eigen;
  assert(max_corner.size() == 3);
  assert(min_corner.size() == 3);
  compute_min_max_corners();
  Eigen::Matrix<Scalar,Eigen::Dynamic,1> dblA;
  doublearea((*this->Vptr),(this->F),dblA);
  total_positive_area = dblA.sum()/2.0;
}

template <typename Scalar, typename Index>
template <typename DerivedV, typename DerivedF>
inline igl::WindingNumberAABB<Scalar,Index>::WindingNumberAABB(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedF> & F):
  WindingNumberTree<Scalar,Index>(V,F),
  min_corner(),
  max_corner(),
  total_positive_area(
    std::numeric_limits<Scalar>::infinity()),
  split_method(MEDIAN_ON_LONGEST_AXIS)
{
  init();
}

template <typename Scalar, typename Index>
inline igl::WindingNumberAABB<Scalar,Index>::WindingNumberAABB(
  const WindingNumberTree<Scalar,Index> & parent,
  const typename WindingNumberTree<Scalar,Index>::MatrixXF & F):
  WindingNumberTree<Scalar,Index>(parent,F),
  min_corner(),
  max_corner(),
  total_positive_area(
    std::numeric_limits<Scalar>::infinity()),
  split_method(MEDIAN_ON_LONGEST_AXIS)
{
  init();
}

template <typename Scalar, typename Index>
inline void igl::WindingNumberAABB<Scalar,Index>::grow()
{
  using namespace std;
  using namespace Eigen;

  this->delete_children();





  if(
    (this->F).rows() <= (WindingNumberAABB_MIN_F>0?WindingNumberAABB_MIN_F:0) ||
    ((this->cap).rows() - 2) >= (this->F).rows())
  {

    return;
  }


  int max_d = -1;
  Scalar max_len =
    -numeric_limits<Scalar>::infinity();
  for(int d = 0;d<min_corner.size();d++)
  {
    if( (max_corner[d] - min_corner[d]) > max_len )
    {
      max_len = (max_corner[d] - min_corner[d]);
      max_d = d;
    }
  }

  Eigen::Matrix<Scalar,Eigen::Dynamic,Eigen::Dynamic> BC;
  barycenter((*this->Vptr),(this->F),BC);




  Scalar split_value;

  switch(split_method)
  {
    case MEDIAN_ON_LONGEST_AXIS:

      median(BC.col(max_d),split_value);
      break;
    default:
      assert(false);
    case CENTER_ON_LONGEST_AXIS:
      split_value = 0.5*(max_corner[max_d] + min_corner[max_d]);
      break;
  }



  vector<int> id( (this->F).rows());
  for(int i = 0;i<(this->F).rows();i++)
  {
    if(BC(i,max_d) <= split_value)
    {
      id[i] = 0;
    }else
    {
      id[i] = 1;
    }
  }

  const int lefts = (int) count(id.begin(),id.end(),0);
  const int rights = (int) count(id.begin(),id.end(),1);
  if(lefts == 0 || rights == 0)
  {

    return;
  }
  assert(lefts+rights == (this->F).rows());
  MatrixXF leftF(lefts,  (this->F).cols());
  MatrixXF rightF(rights,(this->F).cols());
  int left_i = 0;
  int right_i = 0;
  for(int i = 0;i<(this->F).rows();i++)
  {
    if(id[i] == 0)
    {
      leftF.row(left_i++) = (this->F).row(i);
    }else if(id[i] == 1)
    {
      rightF.row(right_i++) = (this->F).row(i);
    }else
    {
      assert(false);
    }
  }
  assert(right_i == rightF.rows());
  assert(left_i == leftF.rows());

  WindingNumberAABB<Scalar,Index> * leftWindingNumberAABB =
    new WindingNumberAABB<Scalar,Index>(*this,leftF);
  leftWindingNumberAABB->grow();
  this->children.push_back(leftWindingNumberAABB);
  WindingNumberAABB<Scalar,Index> * rightWindingNumberAABB =
    new WindingNumberAABB<Scalar,Index>(*this,rightF);
  rightWindingNumberAABB->grow();
  this->children.push_back(rightWindingNumberAABB);
}

template <typename Scalar, typename Index>
inline bool igl::WindingNumberAABB<Scalar,Index>::inside(const Point & p) const
{
  assert(p.size() == max_corner.size());
  assert(p.size() == min_corner.size());
  for(int i = 0;i<p.size();i++)
  {



    if( p(i) < min_corner(i) || p(i) > max_corner(i))
    {
      return false;
    }
  }
  return true;
}

template <typename Scalar, typename Index>
inline void igl::WindingNumberAABB<Scalar,Index>::compute_min_max_corners()
{
  using namespace std;

  for(int d = 0;d<min_corner.size();d++)
  {
    min_corner[d] =  numeric_limits<typename Point::Scalar>::infinity();
    max_corner[d] = -numeric_limits<typename Point::Scalar>::infinity();
  }

  this->center = Point(0,0,0);

  for(int i = 0;i<(this->F).rows();i++)
  {
    for(int j = 0;j<(this->F).cols();j++)
    {
      for(int d = 0;d<min_corner.size();d++)
      {
        min_corner[d] =
          (*this->Vptr)((this->F)(i,j),d) < min_corner[d] ?
            (*this->Vptr)((this->F)(i,j),d) : min_corner[d];
        max_corner[d] =
          (*this->Vptr)((this->F)(i,j),d) > max_corner[d] ?
            (*this->Vptr)((this->F)(i,j),d) : max_corner[d];
      }


      this->center += (*this->Vptr).row((this->F)(i,j));
    }
  }

  this->center.array() /= (this->F).size();







  this->radius = (max_corner-min_corner).norm()/2.0;
}

template <typename Scalar, typename Index>
inline Scalar
igl::WindingNumberAABB<Scalar,Index>::max_abs_winding_number(const Point & p) const
{
  using namespace std;

  if(inside(p))
  {
    return numeric_limits<Scalar>::infinity();
  }


  return numeric_limits<Scalar>::infinity();
}

template <typename Scalar, typename Index>
inline Scalar
  igl::WindingNumberAABB<Scalar,Index>::max_simple_abs_winding_number(
  const Point & p) const
{
  using namespace std;
  using namespace Eigen;

  if(inside(p))
  {
    return numeric_limits<Scalar>::infinity();
  }





  typedef
    Eigen::Matrix<Scalar,Eigen::Dynamic,Eigen::Dynamic>
    MatrixXS;
  typedef
    Eigen::Matrix<Index,Eigen::Dynamic,Eigen::Dynamic>
    MatrixXF;
  MatrixXS BV((int)(1<<3),3);
  BV <<
    min_corner[0],min_corner[1],min_corner[2],
    min_corner[0],min_corner[1],max_corner[2],
    min_corner[0],max_corner[1],min_corner[2],
    min_corner[0],max_corner[1],max_corner[2],
    max_corner[0],min_corner[1],min_corner[2],
    max_corner[0],min_corner[1],max_corner[2],
    max_corner[0],max_corner[1],min_corner[2],
    max_corner[0],max_corner[1],max_corner[2];
  MatrixXF BF(2*2*3,3);
  BF <<
    0,6,4,
    0,2,6,
    0,3,2,
    0,1,3,
    2,7,6,
    2,3,7,
    4,6,7,
    4,7,5,
    0,4,5,
    0,5,1,
    1,5,7,
    1,7,3;
  MatrixXS BFN;
  per_face_normals(BV,BF,BFN);



  MatrixXF PBF(BF.rows(),BF.cols());
  int pbfi = 0;
  Point p2c = 0.5*(min_corner+max_corner)-p;
  for(int i = 0;i<BFN.rows();i++)
  {
    if(p2c.dot(BFN.row(i)) > 0)
    {
      PBF.row(pbfi++) = BF.row(i);
    }
  }
  PBF.conservativeResize(pbfi,PBF.cols());
  return igl::winding_number(BV,PBF,p);
}

#endif
