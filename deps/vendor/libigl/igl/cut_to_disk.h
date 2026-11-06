#ifndef IGL_CUT_TO_DISK_H
#define IGL_CUT_TO_DISK_H
#include "igl_inline.h"

#include <Eigen/Core>

#include <vector>

namespace igl
{























  template <
    typename DerivedF,
    typename Index>
  IGL_INLINE void cut_to_disk(
    const Eigen::MatrixBase<DerivedF> &F,
    std::vector<std::vector<Index> > &cuts);
};

#ifndef IGL_STATIC_LIBRARY
#include "cut_to_disk.cpp"
#endif

#endif
