#include "BeltFloorContext.hpp"
#include "../Point.hpp"
#include "../Polygon.hpp"
#include "../PrintConfig.hpp"
#include "../Slicing.hpp"
#include "../libslic3r.h"

#include <cmath>
#include <limits>
#include <cstddef>
#include <functional>
#include <vector>

namespace Slic3r {

bool BeltFloorContext::init(const SlicingParameters &sp, const PrintConfig &pcfg)
{
    m_active = false;
    m_shear_factor = sp.belt_floor_shear_factor;
    m_from_axis    = sp.belt_floor_from_axis;
    m_z_shift      = sp.belt_floor_z_shift;
    m_floor_offset = pcfg.belt_support_floor_offset.value;

    if (std::abs(m_shear_factor) < EPSILON)
        return false;

    m_active = true;
    return true;
}

bool BeltFloorContext::init_local(const SlicingParameters &sp, const PrintConfig &pcfg,
                                  double global_z_offset)
{
    if (!init(sp, pcfg))
        return false;
    m_z_shift -= global_z_offset;
    return true;
}

Polygons BeltFloorContext::surface_polygon(coordf_t print_z) const
{
    return half_plane(print_z, true);
}

Polygons BeltFloorContext::valid_region_polygon(coordf_t print_z) const
{
    return half_plane(print_z, false);
}

double BeltFloorContext::floor_print_z(const Point &pos_slicing) const
{
    if (!m_active)
        return -std::numeric_limits<double>::infinity();
    double pos = unscale<double>(m_from_axis == 0 ? pos_slicing.x() : pos_slicing.y());
    return m_shear_factor * pos + m_floor_offset + m_z_shift;
}

std::vector<Polygons> BeltFloorContext::compute_per_layer_floors(
    size_t num_layers,
    const std::function<double(size_t)> &layer_print_z) const
{
    std::vector<Polygons> result(num_layers);
    if (!m_active)
        return result;
    for (size_t i = 0; i < num_layers; ++i)
        result[i] = surface_polygon(layer_print_z(i));
    return result;
}

Polygons BeltFloorContext::half_plane(coordf_t print_z, bool belt_surface) const
{
    if (!m_active)
        return {};

    const double cutoff = this->cutoff_u(print_z);
    const coord_t cutoff_scaled = scale_(cutoff);
    const coord_t large_bound   = scale_(1e3);

    bool high_side = (m_shear_factor > 0) == belt_surface;

    Polygon poly;
    if (m_from_axis == 0) {
        if (high_side) {
            poly.points = {
                Point(cutoff_scaled, -large_bound),
                Point(large_bound,   -large_bound),
                Point(large_bound,    large_bound),
                Point(cutoff_scaled,  large_bound)
            };
        } else {
            poly.points = {
                Point(-large_bound,  -large_bound),
                Point(cutoff_scaled, -large_bound),
                Point(cutoff_scaled,  large_bound),
                Point(-large_bound,   large_bound)
            };
        }
    } else {
        if (high_side) {
            poly.points = {
                Point(-large_bound, cutoff_scaled),
                Point( large_bound, cutoff_scaled),
                Point( large_bound, large_bound),
                Point(-large_bound, large_bound)
            };
        } else {
            poly.points = {
                Point(-large_bound, -large_bound),
                Point( large_bound, -large_bound),
                Point( large_bound, cutoff_scaled),
                Point(-large_bound, cutoff_scaled)
            };
        }
    }
    return { poly };
}

}
