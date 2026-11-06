#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {












struct ImexZoneLayout
{

    int primary_head = -1;
















    std::map<int, Vec2d> head_zone_centers;


    std::optional<BoundingBoxf> primary_zone_box;





    std::vector<BoundingBoxf> copy_zones;
    std::vector<BoundingBoxf> mirror_zones;



    std::vector<BoundingBoxf3> secondary_zone_boxes;






    std::vector<BoundingBoxf3> collision_zones;



    std::vector<BoundingBoxf> margin_bands;
};

















ImexZoneLayout compute_imex_zone_layout(const DynamicPrintConfig& printer_cfg,
                                        const std::string&        plate_mode,
                                        const std::string&        process_mode,
                                        const BoundingBoxf&       bed_extents);

}
