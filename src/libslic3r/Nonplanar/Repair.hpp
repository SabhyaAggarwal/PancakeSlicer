#pragma once
#include <limits>
#include <cstddef>
#include "HeightField.hpp"
#include "libslic3r/Point.hpp"
#include <vector>


namespace Slic3r::Nonplanar {

struct RepairHints
{
    struct SurfaceCap { Vec2d p; double radius = 0.; double z = 0.; };
    struct Disc { Vec2d p; double radius = 0.; double z = std::numeric_limits<double>::quiet_NaN(); };
    struct OrderBand { Vec2d p; double height = 0.; };

    std::vector<SurfaceCap> surface_caps;
    std::vector<Disc>       ironing_drops;
    std::vector<OrderBand>  order_bands;
    std::vector<Disc>       uniform_beads;
    std::vector<Disc>       upright;
    double                  travel_lift = 0.;
    std::vector<size_t>     travel_layers;
};

HeightField surface_cap_field(const RepairHints &hints, const HeightField &like);

}
