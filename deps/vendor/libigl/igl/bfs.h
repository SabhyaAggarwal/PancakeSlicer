#ifndef IGL_BFS_H
#define IGL_BFS_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>
#include <Eigen/Sparse>
namespace igl
{










  template <
    typename AType,
    typename DerivedD,
    typename DerivedP>
  IGL_INLINE void bfs(
    const AType & A,
    const size_t s,
    Eigen::PlainObjectBase<DerivedD> & D,
    Eigen::PlainObjectBase<DerivedP> & P);











  template <
    typename AType,
    typename DType,
    typename PType>
  IGL_INLINE void bfs(
    const std::vector<std::vector<AType> > & A,
    const size_t s,
    std::vector<DType> & D,
    std::vector<PType> & P);

  template <
    typename AType,
    typename DType,
    typename PType>
  IGL_INLINE void bfs(
    const Eigen::SparseCompressedBase<AType> & A,
    const size_t s,
    std::vector<DType> & D,
    std::vector<PType> & P);
}
#ifndef IGL_STATIC_LIBRARY
#  include "bfs.cpp"
#endif
#endif
