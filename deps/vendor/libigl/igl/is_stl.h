#ifndef IGL_IS_STL_H
#define IGL_IS_STL_H
#include "igl_inline.h"
#include <cstdio>
namespace igl
{






  IGL_INLINE bool is_stl(FILE * stl_file, bool & is_ascii);
  IGL_INLINE bool is_stl(FILE * stl_file);
};
#ifndef IGL_STATIC_LIBRARY
#  include "is_stl.cpp"
#endif
#endif
