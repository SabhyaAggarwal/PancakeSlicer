#ifndef IGL_SWEPT_VOLUME_H
#define IGL_SWEPT_VOLUME_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Geometry>
namespace igl
{













  IGL_INLINE void swept_volume(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi & F,
    const std::function<Eigen::Affine3d(const double t)> & transform,
    const size_t steps,
    const size_t grid_res,
    const size_t isolevel,
    Eigen::MatrixXd & SV,
    Eigen::MatrixXi & SF);

}

#ifndef IGL_STATIC_LIBRARY
#  include "swept_volume.cpp"
#endif

#endif
