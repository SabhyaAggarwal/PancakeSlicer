#include <cstdint>
#include <functional>
#include "TriangleMesh.hpp"

namespace Slic3r {

void its_quadric_edge_collapse(
    indexed_triangle_set &    its,
    uint32_t                  triangle_count  = 0,
    float *                   max_error       = nullptr,
    std::function<void(void)> throw_on_cancel = nullptr,
    std::function<void(int)>  statusfn        = nullptr);

}
