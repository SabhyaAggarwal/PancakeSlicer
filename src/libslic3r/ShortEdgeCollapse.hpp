#ifndef SRC_LIBSLIC3R_SHORTEDGECOLLAPSE_HPP_
#define SRC_LIBSLIC3R_SHORTEDGECOLLAPSE_HPP_

#include "libslic3r/TriangleMesh.hpp"
#include <cstddef>

namespace Slic3r{




void its_short_edge_collpase(indexed_triangle_set &mesh, size_t target_triangle_count);

}


#endif
