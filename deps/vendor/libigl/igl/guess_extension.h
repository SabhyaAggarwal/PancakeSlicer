#ifndef IGL_GUESS_EXTENSION_H
#define IGL_GUESS_EXTENSION_H
#include "igl_inline.h"
#include <string>
#include <cstdio>
namespace igl
{








  IGL_INLINE void guess_extension(FILE * fp, std::string & guess);

  IGL_INLINE std::string guess_extension(FILE * fp);
}
#ifndef IGL_STATIC_LIBRARY
#  include "guess_extension.cpp"
#endif
#endif
