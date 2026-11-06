// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "is_writable.h"

#ifdef _WIN32
#include <sys/stat.h>
#ifndef S_IWUSR
#  define S_IWUSR S_IWRITE
#endif
IGL_INLINE bool is_writable(const char* filename)
{

  struct stat status;
  if(stat(filename,&status)!=0)
  {
    return false;
  }

  return S_IWUSR & status.st_mode;
}
#else
#include <sys/stat.h>
#include <unistd.h>

IGL_INLINE bool igl::is_writable(const char* filename)
{

  struct stat status;
  if(stat(filename,&status)!=0)
  {
    return false;
  }


  uid_t this_uid = getuid();
  gid_t this_gid = getgid();


  if( this_uid == status.st_uid )
  {
    return S_IWUSR & status.st_mode;
  }


  if( this_gid == status.st_gid )
  {
    return S_IWGRP & status.st_mode;
  }


  return S_IWOTH & status.st_mode;
}
#endif
