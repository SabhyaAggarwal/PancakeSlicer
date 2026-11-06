// Copyright (c) 2020 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef CURAENGINE_WALLTOOLPATHS_H
#define CURAENGINE_WALLTOOLPATHS_H

#include <cstddef>
#include <boost/container_hash/hash.hpp>
#include <memory>
#include <ankerl/unordered_dense.h>
#include <vector>
#include <utility>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"
#include "utils/ExtrusionLine.hpp"
#include "../Polygon.hpp"
#include "../PrintConfig.hpp"

namespace Slic3r::Arachne
{

constexpr bool    fill_outline_gaps = true;
inline coord_t    meshfix_maximum_resolution() { return scaled<coord_t>(0.5); }
inline coord_t    meshfix_maximum_deviation() { return scaled<coord_t>(0.025); }
inline coord_t    meshfix_maximum_extrusion_area_deviation() { return scaled<coord_t>(2.); }

class WallToolPathsParams
{
public:
    float   min_bead_width                   = 0.f;
    float   min_feature_size                 = 0.f;
    float   min_length_factor                = 0.5f;
    float   wall_transition_length           = 0.f;
    float   wall_transition_angle            = 10.f;
    float   wall_transition_filter_deviation = 0.f;
    int     wall_distribution_count          = 1;
    bool    is_top_or_bottom_layer           = false;

    coord_t wall_maximum_resolution = meshfix_maximum_resolution();
    coord_t wall_maximum_deviation  = meshfix_maximum_deviation();
};

WallToolPathsParams make_paths_params(const int layer_id, const PrintObjectConfig &print_object_config, const PrintConfig &print_config);

class WallToolPaths
{
public:








    WallToolPaths(const Polygons& outline, coord_t bead_width_0, coord_t bead_width_x, size_t inset_count, coord_t wall_0_inset, coordf_t layer_height, const WallToolPathsParams &params);





    const std::vector<VariableWidthLines> &generate();





    const std::vector<VariableWidthLines> &getToolPaths();






    void separateOutInnerContour();













    const Polygons& getInnerContour();






    static bool removeEmptyToolPaths(std::vector<VariableWidthLines> &toolpaths);

    using ExtrusionLineSet = ankerl::unordered_dense::set<std::pair<const ExtrusionLine *, const ExtrusionLine *>, boost::hash<std::pair<const ExtrusionLine *, const ExtrusionLine *>>>;









    static ExtrusionLineSet getRegionOrder(const std::vector<ExtrusionLine *> &input, bool outer_to_inner);

protected:





    static void stitchToolPaths(std::vector<VariableWidthLines> &toolpaths, coord_t bead_width_x);




    void removeSmallLines(std::vector<VariableWidthLines> &toolpaths);








    static void simplifyToolPaths(std::vector<VariableWidthLines>& toolpaths, const WallToolPathsParams& params);

private:
    const Polygons& outline;
    coord_t bead_width_0;
    coord_t bead_width_x;
    size_t inset_count;
    coord_t wall_0_inset;
    coordf_t layer_height;
    bool print_thin_walls;
    coord_t min_feature_size;
    coord_t min_bead_width;
    double small_area_length;
    coord_t wall_transition_filter_deviation;
    bool toolpaths_generated;
    std::vector<VariableWidthLines> toolpaths;
    Polygons inner_contour;
    const WallToolPathsParams m_params;
};

}

#endif
