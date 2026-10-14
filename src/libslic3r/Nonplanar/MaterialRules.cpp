#include "MaterialRules.hpp"

#include "libslic3r/MaterialType.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

const MaterialTypeInfo *material_info(const std::string &filament_type, std::vector<std::string> &bases)
{
    std::string type = filament_type;
    std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    if (const MaterialTypeInfo *info = MaterialType::find(type))
        return info;
    std::vector<std::string> words;
    for (size_t begin = 0; begin <= type.size();) {
        const size_t end = std::min(type.find_first_of(" /+_", begin), type.size());
        if (end > begin)
            words.push_back(type.substr(begin, end - begin));
        begin = end + 1;
    }
    for (const std::string &word : words)
        bases.push_back(word.substr(0, word.find('-')));
    for (size_t i = 0; i < words.size(); ++i)
        for (const std::string &name : { words[i], bases[i] })
            if (const MaterialTypeInfo *info = MaterialType::find(name))
                return info;
    for (const std::string &base : bases) {
        if (base == "ULTRAPA")
            return MaterialType::find("PA");
        const MaterialTypeInfo *best = nullptr;
        for (const MaterialTypeInfo &info : MaterialType::all())
            if (info.name.size() >= 2 && base.compare(0, info.name.size(), info.name) == 0 &&
                (best == nullptr || info.name.size() > best->name.size()))
                best = &info;
        if (best != nullptr)
            return best;
    }
    return nullptr;
}

}

RoadMaterial road_material(const std::string &filament_type)
{
    std::vector<std::string> bases;
    const MaterialTypeInfo  *info = material_info(filament_type, bases);
    if ((info != nullptr && info->adhesion_coefficient < 1.) ||
        std::any_of(bases.begin(), bases.end(), [](const std::string &base) { return base == "TPE" || base == "PEBA"; }))
        return RoadMaterial::Flexible;
    if (info != nullptr && info->chamber_min_temp > 0)
        return RoadMaterial::Enclosure;
    return RoadMaterial::Standard;
}

}
