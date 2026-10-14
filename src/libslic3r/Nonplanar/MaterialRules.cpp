#include "MaterialRules.hpp"

#include "libslic3r/MaterialType.hpp"

namespace Slic3r::Nonplanar {

RoadMaterial road_material(const std::string &filament_type)
{
    const MaterialTypeInfo *info = MaterialType::find(filament_type);
    const std::string       base = filament_type.substr(0, filament_type.find('-'));
    if (info == nullptr)
        info = MaterialType::find(base);
    if ((info != nullptr && info->adhesion_coefficient < 1.) || base == "TPE" || base == "PEBA")
        return RoadMaterial::Flexible;
    if (info != nullptr && info->chamber_min_temp > 0)
        return RoadMaterial::Enclosure;
    return RoadMaterial::Standard;
}

}
