#pragma once
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"

#include <cstddef>

namespace Slic3r { class ExtrusionPathConformal; class PrintObject; }

namespace Slic3r::Nonplanar {

class HeightField;
struct ConformalResult;

struct HelixRamp
{
    double begin = 0.;
    double end   = 0.;
};

Polyline fill_helix(const ExPolygons &area, double spacing, const Point &start, bool clockwise = false, HelixRamp *ramp = nullptr,
                    size_t min_rings = 1);

constexpr size_t TopHelixMinRings = 8;

bool helix_on_surface(const HeightField &surface, const ExPolygons &area);

void ramp_helix_flow(ExtrusionPathConformal &path, const HelixRamp &ramp, bool reversed);

void build_vase_lid(PrintObject &object, ConformalResult &result);

bool lid_start(const PrintObject &object, Point &start);

}
