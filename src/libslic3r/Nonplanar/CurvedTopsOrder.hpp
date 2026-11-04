// Curved tops: local access order within one fich-l conformal shell (AGPL-3.0).
#pragma once

#include "libslic3r/ExtrusionEntity.hpp"
#include "CurvedTopsSchedule.hpp"
#include <vector>

namespace Slic3r {

std::vector<ExtrusionPath> order_curved_tops_paths(const std::vector<ExtrusionPath> &paths,
                                                const NonplanarClearanceModel &head, const Point &start);

}
