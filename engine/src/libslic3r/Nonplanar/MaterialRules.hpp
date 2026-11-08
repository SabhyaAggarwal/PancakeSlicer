#pragma once
#include <string>

namespace Slic3r::Nonplanar {

enum class RoadMaterial {
    Standard,
    Enclosure,
    Flexible,
};

RoadMaterial road_material(const std::string &filament_type);

}
