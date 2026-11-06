#ifndef slic3r_RetractWhenCrossingPerimeters_hpp_
#define slic3r_RetractWhenCrossingPerimeters_hpp_

#include <vector>

#include "../AABBTreeIndirect.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {


class ExPolygon;
class Layer;
class Polyline;

class RetractWhenCrossingPerimeters
{
public:
    bool    travel_inside_internal_regions(const Layer &layer, const Polyline &travel);

private:

    const Layer                        *m_layer;

    std::vector<const ExPolygon*>       m_internal_islands;

    using AABBTree = AABBTreeIndirect::Tree<2, coord_t>;
    AABBTree                            m_aabbtree_internal_islands;
};

}

#endif
