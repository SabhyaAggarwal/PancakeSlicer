#pragma once

#include <vector>

#include "../Polygon.hpp"

namespace Slic3r {

class ConfigBase;
enum class WipeTowerType;




struct WipeTowerFootprint
{
    double width      = 0.;
    double depth      = 0.;
    double height     = 0.;
    double brim_width = 0.;
};




WipeTowerType resolve_wipe_tower_type(const ConfigBase &config);





Polygon estimate_wipe_tower_first_layer_outline(const ConfigBase &config, WipeTowerType tower_type, double width, double depth, double height);








WipeTowerFootprint estimate_wipe_tower_footprint(const ConfigBase                &config,
                                                 WipeTowerType                    tower_type,
                                                 const std::vector<unsigned int> &filament_ids,
                                                 double                           layer_height,
                                                 double                           max_object_height);












bool prime_tower_is_printed(const ConfigBase &config, int used_filaments, int num_objects, bool has_mixed_filament);

}
