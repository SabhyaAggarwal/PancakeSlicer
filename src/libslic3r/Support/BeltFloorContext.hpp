#pragma once

#include "../libslic3r.h"
#include "../Point.hpp"
#include "../Polygon.hpp"
#include "../Slicing.hpp"
#include "../PrintConfig.hpp"
#include <cstddef>
#include <functional>
#include <vector>

namespace Slic3r {

class PrintObject;










class BeltFloorContext
{
public:
    BeltFloorContext() = default;



    bool init(const SlicingParameters &sp, const PrintConfig &pcfg);



    bool init_local(const SlicingParameters &sp, const PrintConfig &pcfg,
                    double global_z_offset);

    bool is_active() const { return m_active; }



    Polygons surface_polygon(coordf_t print_z) const;



    Polygons valid_region_polygon(coordf_t print_z) const;



    double floor_print_z(const Point &pos_slicing) const;




    coordf_t cutoff_u(coordf_t print_z) const
        { return (print_z - m_z_shift - m_floor_offset) / m_shear_factor; }



    std::vector<Polygons> compute_per_layer_floors(
        size_t num_layers,
        const std::function<double(size_t)> &layer_print_z) const;


    double shear_factor()  const { return m_shear_factor; }
    int    from_axis()     const { return m_from_axis; }
    double z_shift()       const { return m_z_shift; }
    double floor_offset()  const { return m_floor_offset; }

private:
    bool   m_active       = false;
    double m_shear_factor = 0.0;
    int    m_from_axis    = 1;
    double m_z_shift      = 0.0;
    double m_floor_offset = 0.0;



    Polygons half_plane(coordf_t print_z, bool belt_surface) const;
};

}
