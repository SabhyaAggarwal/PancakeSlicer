#ifndef IGL_ISOLINES_MAP_H
#define IGL_ISOLINES_MAP_H
#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{









  template <
    typename DerivedCM,
    typename Derivediso_color,
    typename DerivedICM >
  IGL_INLINE void isolines_map(
    const Eigen::MatrixBase<DerivedCM> & CM,
    const Eigen::MatrixBase<Derivediso_color> & iso_color,
    const int interval_thickness,
    const int iso_thickness,
    Eigen::PlainObjectBase<DerivedICM> & ICM);

  template <
    typename DerivedCM,
    typename DerivedICM>
  IGL_INLINE void isolines_map(
    const Eigen::MatrixBase<DerivedCM> & CM,
    Eigen::PlainObjectBase<DerivedICM> & ICM);
}

#ifndef IGL_STATIC_LIBRARY
#  include "isolines_map.cpp"
#endif

#endif
