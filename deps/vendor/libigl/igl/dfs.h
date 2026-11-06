#ifndef IGL_DFS_H
#define IGL_DFS_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{












  template <
    typename AType,
    typename DerivedD,
    typename DerivedP,
    typename DerivedC>
  IGL_INLINE void dfs(
    const std::vector<std::vector<AType> > & A,
    const size_t s,
    Eigen::PlainObjectBase<DerivedD> & D,
    Eigen::PlainObjectBase<DerivedP> & P,
    Eigen::PlainObjectBase<DerivedC> & C);

  template <
    typename AType,
    typename DType,
    typename PType,
    typename CType>
  IGL_INLINE void dfs(
    const std::vector<std::vector<AType> > & A,
    const size_t s,
    std::vector<DType> & D,
    std::vector<PType> & P,
    std::vector<CType> & C);

}
#ifndef IGL_STATIC_LIBRARY
#  include "dfs.cpp"
#endif
#endif
