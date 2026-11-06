// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
// High Resolution Timer.
//
// Resolution on Mac (clock tick)
// Resolution on Linux (1 us not tested)
// Resolution on Windows (clock tick not tested)

#ifndef IGL_TIMER_H
#define IGL_TIMER_H

#ifdef WIN32
#include <windows.h>
#elif __APPLE__
#include <mach/mach_time.h>
#else
#include <sys/time.h>
#endif
#include <cstddef>
#include <cstdint>

namespace igl
{

  class Timer
  {
  public:

    Timer():
      stopped(0),
#ifdef WIN32
      frequency(),
      startCount(),
      endCount()
#elif __APPLE__
      startCount(0),
      endCount(0)
#else
      startCount(),
      endCount()
#endif
    {
#ifdef WIN32
      QueryPerformanceFrequency(&frequency);
      startCount.QuadPart = 0;
      endCount.QuadPart = 0;
#elif __APPLE__
      startCount = 0;
      endCount = 0;
#else
      startCount.tv_sec = startCount.tv_usec = 0;
      endCount.tv_sec = endCount.tv_usec = 0;
#endif

      stopped = 0;
    }

    ~Timer()
    {

    }

#ifdef __APPLE__




    double subtractTimes( std::uint64_t endTime, std::uint64_t startTime )
    {
      std::uint64_t difference = endTime - startTime;
      static double conversion = 0.0;

      if( conversion == 0.0 )
      {
        mach_timebase_info_data_t info;
        kern_return_t err = mach_timebase_info( &info );


        if( err == 0  )
          conversion = 1e-9 * (double) info.numer / (double) info.denom;
      }

      return conversion * (double) difference;
    }
#endif


    void   start()
    {
      stopped = 0;
#ifdef WIN32
      QueryPerformanceCounter(&startCount);
#elif __APPLE__
      startCount = mach_absolute_time();
#else
      gettimeofday(&startCount, NULL);
#endif

    }


    void   stop()
    {
      stopped = 1;

#ifdef WIN32
      QueryPerformanceCounter(&endCount);
#elif __APPLE__
      endCount = mach_absolute_time();
#else
      gettimeofday(&endCount, NULL);
#endif

    }


    double getElapsedTime()
    {
      return this->getElapsedTimeInSec();
    }


    double getElapsedTimeInSec()
    {
      return this->getElapsedTimeInMicroSec() * 0.000001;
    }



    double getElapsedTimeInMilliSec()
    {
      return this->getElapsedTimeInMicroSec() * 0.001;
    }


    double getElapsedTimeInMicroSec()
    {
      double startTimeInMicroSec = 0;
      double endTimeInMicroSec = 0;

#ifdef WIN32
      if(!stopped)
        QueryPerformanceCounter(&endCount);

      startTimeInMicroSec =
        startCount.QuadPart * (1000000.0 / frequency.QuadPart);
      endTimeInMicroSec = endCount.QuadPart * (1000000.0 / frequency.QuadPart);
#elif __APPLE__
      if (!stopped)
        endCount = mach_absolute_time();

      return subtractTimes(endCount,startCount)/1e-6;
#else
      if(!stopped)
        gettimeofday(&endCount, NULL);

      startTimeInMicroSec =
        (startCount.tv_sec * 1000000.0) + startCount.tv_usec;
      endTimeInMicroSec = (endCount.tv_sec * 1000000.0) + endCount.tv_usec;
#endif

      return endTimeInMicroSec - startTimeInMicroSec;
    }

  private:

    int    stopped;
#ifdef WIN32

    LARGE_INTEGER frequency;
    LARGE_INTEGER startCount;
    LARGE_INTEGER endCount;
#elif __APPLE__
    std::uint64_t startCount;
    std::uint64_t endCount;
#else
    timeval startCount;
    timeval endCount;
#endif
  };
}
#endif
