#pragma once

#include <cassert>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Eigen/Geometry>
#include "Config.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r {

class PresetBundle;






inline constexpr const char* kImexPrimaryMode = "primary";


































struct ImexMode
{


    int         index = -1;

    std::string name;

    std::string active_tools;

    std::string gcode;


    bool        ragged = false;

    bool found() const { return this->index >= 0; }
    explicit operator bool() const { return this->found(); }
};





ImexMode find_imex_mode(const ConfigBase& cfg, const std::string& name);




std::vector<ImexMode> imex_mode_table(const ConfigBase& cfg);




std::vector<std::string> imex_plate_mode_choices(const ConfigBase& cfg);












































































ConfigOptionInts effective_physical_extruder_map(const ConfigOptionInts* explicit_pem, int nozzle_count);




ConfigOptionInts effective_physical_extruder_map(const PresetBundle& pb);







int imex_pem_tool_for(int filament_id, const std::string& parallel_mode, const ConfigOptionInts& pem);















int imex_physical_heater_for(bool is_imex, const ConfigOptionInts& pem, int logical_id);

















bool imex_suppresses_bare_toolchange(const std::string& parallel_mode, unsigned int toolchange_count);



































std::string imex_multicolor_block_reason(const std::string& parallel_mode,
                                         const std::string& active_tools_str,
                                         int tools_per_gantry,
                                         const std::vector<int>& used_filaments_0b,
                                         const ConfigOptionInts& pem);







int first_filament_for_physical_head(const ConfigOptionInts& pem, int physical);




bool has_mmu(const ConfigOptionInts& pem);






bool has_non_primary_mmu(const ConfigOptionInts& pem, int primary_physical);








int imex_primary_tool_for_mode(const std::string& active_tools_for_mode);







std::map<int,int> parse_imex_head_filament_map(const std::string& s);













int resolve_filament_for_head(const std::map<int,int>& plate_map,
                              const ConfigOptionInts&  pem,
                              int                       physical);











int imex_primary_logical_from_objects(const std::vector<int>&  used_slots_1b,
                                      const ConfigOptionInts&  pem,
                                      int                       primary_physical);













std::vector<int> imex_secondary_logical_slots(const std::vector<int>&   active_physicals,
                                              int                        primary_physical,
                                              const std::map<int,int>&  plate_head_filament_map,
                                              const ConfigOptionInts&   pem);

enum class ImexRole { Primary, Copy, Mirror, Span };














struct ImexRoleDesc {
    ImexRole role;
    char     letter;
};
inline constexpr ImexRoleDesc kImexRoleTable[] = {
    { ImexRole::Primary, 'P' },
    { ImexRole::Copy,    'C' },
    { ImexRole::Mirror,  'M' },
    { ImexRole::Span,    'S' },
};





int    imex_cfg_int(const ConfigBase& cfg, const std::string& key);
double imex_cfg_float(const ConfigBase& cfg, const std::string& key);



bool imex_cfg_bool(const ConfigBase& cfg, const std::string& key);


void imex_cfg_report_unregistered(const char* fn, const std::string& key);

template<class T>
T imex_cfg_enum(const ConfigBase& cfg, const std::string& key)
{











    const ConfigOptionDef* def = print_config_def.get(key);
    if (const ConfigOption* opt = cfg.option(key)) {
        if (const auto* e = dynamic_cast<const ConfigOptionEnum<T>*>(opt))
            return e->value;




        if (const auto* g = dynamic_cast<const ConfigOptionEnumGeneric*>(opt))
            if (g->keys_map == &ConfigOptionEnum<T>::get_enum_values())
                return static_cast<T>(g->value);
    }
    if (def)
        if (const ConfigOption* dv = def->default_value.get())
            if (const auto* e = dynamic_cast<const ConfigOptionEnum<T>*>(dv))
                return e->value;




    assert(false && "imex_cfg_enum: key not registered in print_config_def");
    imex_cfg_report_unregistered("imex_cfg_enum", key);
    return static_cast<T>(0);
}


char imex_role_letter(ImexRole role);






ImexRole imex_role_from_suffix(const std::string& suffix);

















std::vector<std::pair<int, ImexRole>> parse_imex_active_tools(const std::string& active_tools_for_mode);





enum class ImexModeKind { Primary, Copy, Mirror, Custom };
inline constexpr size_t kImexModeKindCount = 4;
static_assert(size_t(ImexModeKind::Custom) + 1 == kImexModeKindCount);
ImexModeKind imex_mode_kind(const std::string& active_tools_for_mode);

















struct ImexRouting
{


    bool                                  parallel = false;


    std::string                           active_tools;

    std::vector<std::pair<int, ImexRole>> tools;


    int                                   primary_phys    = -1;




    int                                   primary_logical = -1;



    std::vector<int>                      routed_heads;


    bool                                  primary_unrouted = false;

    bool routes_to_primary() const { return this->primary_logical >= 0; }
};






ImexRouting imex_resolve_routing(const ConfigBase&       cfg,
                                 const std::string&      parallel_mode,
                                 const std::vector<int>& used_slots_1b,
                                 const ConfigOptionInts& pem);












struct ImexGantryGroup {
    int                                     gantry_index = -1;
    int                                     representative_phys = -1;
    ImexRole                                representative_role = ImexRole::Copy;
    bool                                    aggregate = false;
    std::vector<std::pair<int, ImexRole>>   tools;
};

struct ImexGantryGrouping {
    int                              primary_phys     = -1;
    int                              primary_gantry   = -1;
    bool                             span_on_primary  = false;
    std::vector<ImexGantryGroup>     groups;
};





ImexGantryGrouping group_imex_active_tools_by_gantry(const std::string& active_tools_for_mode,
                                                     int                tools_per_gantry);









enum class ImexMirrorAxis { X, Y };








ImexMirrorAxis imex_mirror_axis_for(int primary_phys, int target_phys, int tools_per_gantry);





















Transform3d imex_head_transform(int primary, int target, ImexRole role,
                                const Vec2d& gantry_offset,
                                const Vec2d& primary_zone_center,
                                ImexMirrorAxis mirror_axis);






Vec2d compute_imex_slice_offset(bool firmware_managed,
                                const std::string& parallel_mode,
                                const std::optional<BoundingBoxf>& primary_zone_box);






bool imex_hull_violates_zones(const std::vector<BoundingBoxf3>& zones, const Polygon& hull);

}
