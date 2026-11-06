#pragma once
#include "libslic3r/ExPolygon.hpp"
#include <vector>

namespace Slic3r {
class Layer;
class PrintObject;
namespace Nonplanar {
ExPolygons smooth_curved_top_footprint(const ExPolygons &area, double max_inset_mm);

void finish_curved_top_boundaries(const PrintObject &object, const std::vector<Layer *> &shells);
}
}
