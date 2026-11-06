#ifndef IGL_EDGE_CROSSINGS_H
#define IGL_EDGE_CROSSINGS_H

#include "igl_inline.h"
#include <Eigen/Core>
#include <unordered_map>

namespace igl
{










  template <
    typename DeriveduE,
    typename DerivedS,
    typename DerivedT>
  void edge_crossings(
    const Eigen::MatrixBase<DeriveduE> & uE,
    const Eigen::MatrixBase<DerivedS> & S,
    const typename DerivedS::Scalar val,
    std::unordered_map<int,int> & uE2I,
    Eigen::PlainObjectBase<DerivedT> & T);
}

#ifndef IGL_STATIC_LIBRARY
#  include "edge_crossings.cpp"
#endif

#endif
