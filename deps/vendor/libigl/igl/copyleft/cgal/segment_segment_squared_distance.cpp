// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "segment_segment_squared_distance.h"
#include <CGAL/Vector_3.h>


template < typename Kernel>
IGL_INLINE bool igl::copyleft::cgal::segment_segment_squared_distance(
    const CGAL::Segment_3<Kernel> & S1,
    const CGAL::Segment_3<Kernel> & S2,
    CGAL::Point_3<Kernel> & P1,
    CGAL::Point_3<Kernel> & P2,
    typename Kernel::FT & dst)
{
  typedef CGAL::Vector_3<Kernel> Vector_3;
  if(S1.is_degenerate())
  {

    P1 = S1.source();
    point_segment_squared_distance(P1,S2,P2,dst);
    return true;
  }else if(S2.is_degenerate())
  {
    assert(!S1.is_degenerate());

    P2 = S2.source();
    point_segment_squared_distance(P2,S1,P1,dst);
    return true;
  }

  assert(!S1.is_degenerate());
  assert(!S2.is_degenerate());

  Vector_3 u = S1.target() - S1.source();
  Vector_3 v = S2.target() - S2.source();
  Vector_3 w = S1.source() - S2.source();

  const auto a = u.dot(u);
  const auto b = u.dot(v);
  const auto c = v.dot(v);
  const auto d = u.dot(w);
  const auto e = v.dot(w);
  const auto D = a*c - b*b;
  assert(D>=0);
  const auto sc=D, sN=D, sD = D;
  const auto tc=D, tN=D, tD = D;

  bool parallel = false;

  if (D==0)
  {

    parallel = true;
    sN = 0.0;
    sD = 1.0;
    tN = e;
    tD = c;
  } else
  {

    sN = (b*e - c*d);
    tN = (a*e - b*d);
    if (sN < 0.0)
    {

      sN = 0.0;
      tN = e;
      tD = c;
    } else if (sN > sD)
    {
      sN = sD;
      tN = e + b;
      tD = c;
    }
  }

  if (tN < 0.0)
  {

    tN = 0.0;

    if (-d < 0.0)
    {
      sN = 0.0;
    }else if (-d > a)
    {
      sN = sD;
    }else
    {
      sN = -d;
      sD = a;
    }
  }else if (tN > tD)
  {

    tN = tD;

    if ((-d + b) < 0.0)
    {
      sN = 0;
    }else if ((-d + b) > a)
    {
      sN = sD;
    }else
    {
      sN = (-d +  b);
      sD = a;
    }
  }

  sc = (abs(sN) == 0 ? 0.0 : sN / sD);
  tc = (abs(tN) == 0 ? 0.0 : tN / tD);


  P1 = S1.source() + sc*(S1.target()-S1.source());
  P2 = S2.source() + sc*(S2.target()-S2.source());
  Vector_3   dP = w + (sc * u) - (tc * v);
  assert(dP == (P1-P2));

  dst = dP.squared_length();
  return parallel;
}
