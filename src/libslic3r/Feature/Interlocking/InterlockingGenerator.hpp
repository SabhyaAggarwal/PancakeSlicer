// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef INTERLOCKING_GENERATOR_HPP
#define INTERLOCKING_GENERATOR_HPP

#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"
#include "VoxelUtils.hpp"
#include <functional>
#include <cstddef>
#include <utility>
#include <unordered_set>
#include <vector>
#include "libslic3r/MultiMaterialSegmentation.hpp"

namespace Slic3r { class PrintObject; }

namespace Slic3r {































class InterlockingGenerator
{
public:



    static void generate_interlocking_structure(PrintObject* print_object, const std::function<void()>& throw_on_cancel);

private:



    void generateInterlockingStructure() const;













    InterlockingGenerator(PrintObject&          print_object,
                          const size_t          region_a_index,
                          const size_t          region_b_index,
                          const coord_t         beam_width,
                          const coord_t         boundary_avoidance,
                          const float           rotation,
                          const Vec3crd&        cell_size,
                          const coord_t         beam_layer_count,
                          const DilationKernel& interface_dilation,
                          const DilationKernel& air_dilation,
                          const bool            air_filtering,
                          const std::function<void()>& throw_on_cancel)
        : print_object(print_object)
        , region_a_index(region_a_index)
        , region_b_index(region_b_index)
        , beam_width(beam_width)
        , boundary_avoidance(boundary_avoidance)
        , vu(cell_size)
        , rotation(rotation)
        , cell_size(cell_size)
        , beam_layer_count(beam_layer_count)
        , interface_dilation(interface_dilation)
        , air_dilation(air_dilation)
        , air_filtering(air_filtering)
        , throw_on_cancel(throw_on_cancel)
    {}








    std::pair<ExPolygons, ExPolygons> growBorderAreasPerpendicular(const ExPolygons& a, const ExPolygons& b, const coord_t& detect) const;






    void handleThinAreas(const std::unordered_set<GridPoint3>& has_all_meshes) const;








    std::vector<std::unordered_set<GridPoint3>> getShellVoxels(const DilationKernel& kernel) const;









    void addBoundaryCells(const std::vector<ExPolygons>& layers, const DilationKernel& kernel, std::unordered_set<GridPoint3>& cells) const;







    std::vector<ExPolygons> computeUnionedVolumeRegions() const;





    std::vector<std::vector<ExPolygons>> generateMicrostructure() const;







    void applyMicrostructureToOutlines(const std::unordered_set<GridPoint3>& cells, const std::vector<ExPolygons>& layer_regions) const;

    static const coord_t ignored_gap_ = 100u;

    PrintObject&  print_object;
    const size_t  region_a_index;
    const size_t  region_b_index;
    const coord_t beam_width;
    const coord_t boundary_avoidance;

    const VoxelUtils vu;

    const float          rotation;
    const Vec3crd        cell_size;
    const coord_t        beam_layer_count;
    const DilationKernel interface_dilation;
    const DilationKernel air_dilation;


    const bool air_filtering;

    const std::function<void()>& throw_on_cancel;
};

}

#endif
