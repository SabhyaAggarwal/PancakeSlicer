#ifndef IGL_INTERSECTION_BLOCKING_COLLAPSE_EDGE_CALLBACKS_H
#define IGL_INTERSECTION_BLOCKING_COLLAPSE_EDGE_CALLBACKS_H
#include "igl_inline.h"
#include "decimate_callback_types.h"
#include "placeholders.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{

  template <typename DerivedV, int DIM> class AABB;



























































  IGL_INLINE void intersection_blocking_collapse_edge_callbacks(
    const decimate_pre_collapse_callback  & orig_pre_collapse,
    const decimate_post_collapse_callback & orig_post_collapse,
    const std::vector<AABB<Eigen::MatrixXd,3> *> & leaves,
          AABB<Eigen::MatrixXd,3> * & tree,
          decimate_pre_collapse_callback  & pre_collapse,
          decimate_post_collapse_callback & post_collapse);

  IGL_INLINE void intersection_blocking_collapse_edge_callbacks(
    const decimate_pre_collapse_callback  & orig_pre_collapse,
    const decimate_post_collapse_callback & orig_post_collapse,
          AABB<Eigen::MatrixXd,3> * & tree,
          decimate_pre_collapse_callback  & pre_collapse,
          decimate_post_collapse_callback & post_collapse);

  IGL_INLINE void intersection_blocking_collapse_edge_callbacks(
    AABB<Eigen::MatrixXd,3> * & tree,
    decimate_pre_collapse_callback  & pre_collapse,
    decimate_post_collapse_callback & post_collapse);
}

#ifndef IGL_STATIC_LIBRARY
#  include "intersection_blocking_collapse_edge_callbacks.cpp"
#endif
#endif
