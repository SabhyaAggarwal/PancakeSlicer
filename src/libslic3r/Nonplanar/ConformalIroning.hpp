#pragma once
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"

#include <functional>

namespace Slic3r {
class PrintObject;
class PrintRegionConfig;
}

namespace Slic3r::Nonplanar {

class HeightField;
struct ConformalPatch;
struct ConformalResult;



bool conformal_ironing_enabled(const PrintRegionConfig &config);








ExtrusionEntityCollection make_conformal_ironing(const PrintObject &object, const ConformalPatch &patch, const ExPolygons &shell_area,
                                                 const Point &seam, const Polyline &seam_line, double host_z, bool banded,
                                                 const std::function<bool(const HeightField &, const Vec2d &)> &wall = {});




void build_conformal_ironing(const PrintObject &object, ConformalResult &result);

}
