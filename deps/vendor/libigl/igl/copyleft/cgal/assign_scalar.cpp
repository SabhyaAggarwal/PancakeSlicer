// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "assign_scalar.h"

#include <cmath>

template <>
IGL_INLINE void igl::copyleft::cgal::assign_scalar(
  const CGAL::Epeck::FT & rhs,
  const bool & slow_and_more_precise,
  double & lhs)
{
  if(slow_and_more_precise)
  {
    return assign_scalar(rhs,lhs);
  }else
  {
















    lhs = CGAL::to_double(rhs.exact());
  }
}

template <>
IGL_INLINE void igl::copyleft::cgal::assign_scalar(
  const CGAL::Epeck::FT & rhs,
  const bool & slow_and_more_precise,
  float & lhs)
{
  double d;
  igl::copyleft::cgal::assign_scalar(rhs,slow_and_more_precise,d);
  lhs = float(d);
}


template <typename RHS, typename LHS>
IGL_INLINE void igl::copyleft::cgal::assign_scalar(
  const RHS & rhs,
  const bool &  ,
  LHS & lhs)
{
  return assign_scalar(rhs,lhs);
}

IGL_INLINE void igl::copyleft::cgal::assign_scalar(
  const CGAL::Epeck::FT & cgal,
  CGAL::Epeck::FT & d)
{
  d = cgal;
}

IGL_INLINE void igl::copyleft::cgal::assign_scalar(
  const CGAL::Epeck::FT & _cgal,
  double & d)
{

  const CGAL::Epeck::FT cgal = _cgal.exact();
  const auto interval = CGAL::to_interval(cgal);
  d = interval.first;
  do {
      const double next = std::nextafter(d, interval.second);
      if (CGAL::abs(cgal-d) < CGAL::abs(cgal-next)) break;
      d = next;
  } while (d < interval.second);
}

IGL_INLINE void igl::copyleft::cgal::assign_scalar(
  const CGAL::Epeck::FT & _cgal,
  float& d)
{

  const CGAL::Epeck::FT cgal = _cgal.exact();
  const auto interval = CGAL::to_interval(cgal);
  d = interval.first;
  do {
      const float next = std::nextafter(d, float(interval.second));
      if (CGAL::abs(cgal-d) < CGAL::abs(cgal-next)) break;
      d = next;
  } while (d < float(interval.second));
}

IGL_INLINE void igl::copyleft::cgal::assign_scalar(
  const double & c,
  double & d)
{
  d = c;
}

IGL_INLINE void igl::copyleft::cgal::assign_scalar(
  const float& c,
  float& d)
{
  d = c;
}

IGL_INLINE void igl::copyleft::cgal::assign_scalar(
  const float& c,
  double& d)
{
  d = c;
}

IGL_INLINE void igl::copyleft::cgal::assign_scalar(
  const CGAL::Exact_predicates_exact_constructions_kernel_with_sqrt::FT & cgal,
  CGAL::Exact_predicates_exact_constructions_kernel_with_sqrt::FT & d)
{
  d = cgal;
}

IGL_INLINE void igl::copyleft::cgal::assign_scalar(
  const CGAL::Exact_predicates_exact_constructions_kernel_with_sqrt::FT & cgal,
  double & d)
{
  const auto interval = CGAL::to_interval(cgal);
  d = interval.first;
  do {
      const double next = std::nextafter(d, interval.second);
      if (CGAL::abs(cgal-d) < CGAL::abs(cgal-next)) break;
      d = next;
  } while (d < interval.second);
}

IGL_INLINE void igl::copyleft::cgal::assign_scalar(
  const CGAL::Exact_predicates_exact_constructions_kernel_with_sqrt::FT & cgal,
  float& d)
{
  const auto interval = CGAL::to_interval(cgal);
  d = interval.first;
  do {
      const float next = std::nextafter(d, float(interval.second));
      if (CGAL::abs(cgal-d) < CGAL::abs(cgal-next)) break;
      d = next;
  } while (d < float(interval.second));
}

#ifdef IGL_STATIC_LIBRARY


template void igl::copyleft::cgal::assign_scalar<float, double>(float const&, bool const&, double&);

template void igl::copyleft::cgal::assign_scalar<float, CGAL::Epeck::FT >(float const&, bool const&, CGAL::Epeck::FT&);
template void igl::copyleft::cgal::assign_scalar<double, double>(double const&, bool const&, double&);
template void igl::copyleft::cgal::assign_scalar<double, CGAL::Epeck::FT >(double const&, bool const&, CGAL::Epeck::FT&);
template void igl::copyleft::cgal::assign_scalar<CGAL::Epeck::FT, CGAL::Epeck::FT >(CGAL::Epeck::FT const&, bool const&, CGAL::Epeck::FT&);
#endif
