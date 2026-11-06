#pragma once
#include "libslic3r/ExPolygon.hpp"

namespace Slic3r {
class BoundingBox;
class ExtrusionEntityCollection;

namespace Nonplanar {


void clip_planar_remainder(ExtrusionEntityCollection &collection, const ExPolygons &area, const BoundingBox &area_bbox);
}
}
