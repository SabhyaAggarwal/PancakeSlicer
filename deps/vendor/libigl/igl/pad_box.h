#ifndef IGL_PAD_BOX_H
#define IGL_PAD_BOX_H

#include <Eigen/Geometry>
#include "igl_inline.h"

namespace igl
{




  template <typename Scalar, int DIM>
  IGL_INLINE void pad_box(
    const Scalar pad,
    Eigen::AlignedBox<Scalar,DIM> & box);
}

#ifndef IGL_STATIC_LIBRARY
#  include "pad_box.cpp"
#endif

#endif
