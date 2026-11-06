#ifndef IGL_COPYLEFT_CGAL_MESH_BOOLEAN_TYPE_TO_FUNCS_H
#define IGL_COPYLEFT_CGAL_MESH_BOOLEAN_TYPE_TO_FUNCS_H

#include "../../igl_inline.h"
#include "../../MeshBooleanType.h"
#include <Eigen/Core>
#include <functional>
namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {










      IGL_INLINE void mesh_boolean_type_to_funcs(
        const MeshBooleanType & type,
        std::function<int(const Eigen::Matrix<int,1,Eigen::Dynamic>) >&
          wind_num_op,
        std::function<int(const int, const int)> & keep);
    }
  }
}
#ifndef IGL_STATIC_LIBRARY
#  include "mesh_boolean_type_to_funcs.cpp"
#endif
#endif
