
#include <cassert>
#ifndef IGL_ASSERT
#ifdef NDEBUG
#define IGL_ASSERT(x) do { (void)sizeof(x);} while (0)
#else
#define IGL_ASSERT(x) assert(x)
#endif
#endif
