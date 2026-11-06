// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_PARALLEL_FOR_H
#define IGL_PARALLEL_FOR_H
#include "igl_inline.h"
#include <functional>




namespace igl
{










































  template<typename Index, typename FunctionType >
  inline bool parallel_for(
    const Index loop_size,
    const FunctionType & func,
    const size_t min_parallel=0);

































  template<
    typename Index,
    typename PrepFunctionType,
    typename FunctionType,
    typename AccumFunctionType
    >
  inline bool parallel_for(
    const Index loop_size,
    const PrepFunctionType & prep_func,
    const FunctionType & func,
    const AccumFunctionType & accum_func,
    const size_t min_parallel=0);
}



#include "default_num_threads.h"

#include <cmath>
#include <cassert>
#include <thread>
#include <vector>
#include <algorithm>

template<typename Index, typename FunctionType >
inline bool igl::parallel_for(
  const Index loop_size,
  const FunctionType & func,
  const size_t min_parallel)
{
  using namespace std;

  const auto & no_op = [](const size_t  ){};

  const auto & wrapper = [&func](Index i,size_t  ){ func(i); };
  return parallel_for(loop_size,no_op,wrapper,no_op,min_parallel);
}

template<
  typename Index,
  typename PreFunctionType,
  typename FunctionType,
  typename AccumFunctionType>
inline bool igl::parallel_for(
  const Index loop_size,
  const PreFunctionType & prep_func,
  const FunctionType & func,
  const AccumFunctionType & accum_func,
  const size_t min_parallel)
{
  assert(loop_size>=0);
  if(loop_size==0) return false;


#ifdef IGL_PARALLEL_FOR_FORCE_SERIAL
  const size_t nthreads = 1;
#else
  const size_t nthreads = igl::default_num_threads();
#endif
  if(loop_size<min_parallel || nthreads<=1)
  {

    prep_func(1);
    for(Index i = 0;i<loop_size;i++) func(i,0);
    accum_func(0);
    return false;
  }else
  {

    Index slice =
      std::max(
        (Index)std::round((loop_size+1)/static_cast<double>(nthreads)),(Index)1);


    const auto & range = [&func](const Index k1, const Index k2, const size_t t)
    {
      for(Index k = k1; k < k2; k++) func(k,t);
    };
    prep_func(nthreads);

    std::vector<std::thread> pool;
    pool.reserve(nthreads);

    Index i1 = 0;
    Index i2 = std::min(0 + slice, loop_size);
    {
      size_t t = 0;
      for (; t+1 < nthreads && i1 < loop_size; ++t)
      {
        pool.emplace_back(range, i1, i2, t);
        i1 = i2;
        i2 = std::min(i2 + slice, loop_size);
      }
      if (i1 < loop_size)
      {
        pool.emplace_back(range, i1, loop_size, t);
      }
    }

    for (std::thread &t : pool) if (t.joinable()) t.join();

    for(size_t t = 0;t<nthreads;t++)
    {
      accum_func(t);
    }
    return true;
  }
}




#endif
