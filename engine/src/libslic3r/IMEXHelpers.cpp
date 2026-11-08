#include "libslic3r/IMEXHelpers.hpp"

#include <Eigen/Core>
#include <algorithm>
#include <cstddef>
#include <map>
#include <numeric>
#include <cassert>
#include <cctype>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <vector>
#include <utility>

#include "Config.hpp"
#include "libslic3r.h"
#include "Point.hpp"
#include "BoundingBox.hpp"
#include "Polygon.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

namespace {
void strip_whitespace(std::string& token)
{
    token.erase(std::remove_if(token.begin(), token.end(),
                               [](unsigned char c) { return std::isspace(c) != 0; }),
                token.end());
}
}

ConfigOptionInts effective_physical_extruder_map(const ConfigOptionInts* explicit_pem, int nozzle_count)
{
    if (explicit_pem && nozzle_count > 0 && (int) explicit_pem->values.size() == nozzle_count)
        return *explicit_pem;

    ConfigOptionInts derived;
    derived.values.resize(std::max(nozzle_count, 1));
    std::iota(derived.values.begin(), derived.values.end(), 0);
    return derived;
}

ConfigOptionInts effective_physical_extruder_map(const PresetBundle& pb)
{
    const ConfigOptionInts* explicit_pem = pb.project_config.option<ConfigOptionInts>("physical_extruder_map");
    if (!explicit_pem || explicit_pem->values.size() < 2)
        explicit_pem = pb.printers.get_edited_preset().config.option<ConfigOptionInts>("physical_extruder_map");
    const auto* nozzles = pb.printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter");
    return effective_physical_extruder_map(explicit_pem, nozzles ? (int) nozzles->values.size() : 0);
}

int imex_pem_tool_for(int filament_id, const std::string& parallel_mode, const ConfigOptionInts& pem)
{
    const bool imex_parallel = !parallel_mode.empty() && parallel_mode != kImexPrimaryMode;
    if (!imex_parallel || pem.values.empty())
        return -1;
    if (filament_id < 0 || filament_id >= (int) pem.values.size())
        return -1;
    return pem.values[filament_id];
}

int imex_physical_heater_for(bool is_imex, const ConfigOptionInts& pem, int logical_id)
{
    if (!is_imex || logical_id < 0 || logical_id >= (int) pem.values.size())
        return logical_id;
    return pem.values[logical_id];
}

bool imex_suppresses_bare_toolchange(const std::string& parallel_mode, unsigned int toolchange_count)
{
    return toolchange_count <= 1
        && !parallel_mode.empty()
        && parallel_mode != kImexPrimaryMode;
}

std::string imex_multicolor_block_reason(const std::string& parallel_mode,
                                         const std::string& active_tools_str,
                                         int tools_per_gantry,
                                         const std::vector<int>& used_filaments_0b,
                                         const ConfigOptionInts& pem)
{
    if (parallel_mode.empty() || parallel_mode == kImexPrimaryMode) return {};
    if (used_filaments_0b.size() <= 1) return {};

    if (!pem.values.empty()) {
        std::unordered_set<int> seen_physicals;
        for (int filament : used_filaments_0b) {
            if (filament < 0 || filament >= (int)pem.values.size()) continue;
            const int phys = pem.values[filament];
            if (!seen_physicals.insert(phys).second) {
                return L("Multi-color prints in IDEX/IQEX parallel modes require each filament "
                         "to have its own dedicated physical extruder. Two or more of the active "
                         "filaments are routed to the same physical head via the printer's "
                         "physical extruder map (an MMU/AFC manifold), which the slaved gantry "
                         "cannot follow.");
            }
        }
    }

    const int primary_physical = imex_primary_tool_for_mode(active_tools_str);
    if (primary_physical < 0) {
        return L("The active IDEX/IQEX mode does not define a primary tool, so multi-color "
                 "printing cannot be scheduled. Open the printer settings IDEX/IQEX Modes "
                 "editor and assign a Primary role to one tool.");
    }

    const int tpg = std::max(1, tools_per_gantry);
    const int primary_gantry = primary_physical / tpg;
    bool span_on_primary_gantry = false;
    std::set<int> active_gantries;
    for (const auto& [phys, role] : parse_imex_active_tools(active_tools_str)) {
        active_gantries.insert(phys / tpg);
        if (phys / tpg == primary_gantry && role == ImexRole::Span)
            span_on_primary_gantry = true;
    }

    if (active_gantries.size() < 2) {
        return L("Multi-color in this mode isn't a parallel-print scenario — all of the "
                 "active tools sit on a single gantry, so there's no second gantry being "
                 "copied or mirrored to. Switch the plate to Primary mode for multi-color "
                 "printing on a single gantry, or define an IDEX/IQEX mode that includes "
                 "tools on a second gantry.");
    }

    if (!span_on_primary_gantry) {
        return L("Multi-color prints in IDEX/IQEX parallel modes require a Span tool on the "
                 "primary's gantry — without one, the mode doesn't declare a within-gantry "
                 "multicolor partner. Either reduce the print to a single filament, switch to "
                 "Primary mode, or open the printer settings IDEX/IQEX Modes editor and mark a "
                 "tool on the primary's gantry as Span.");
    }
    std::set<int> color_heads{ primary_physical };
    for (const auto& [phys, role] : parse_imex_active_tools(active_tools_str))
        if (phys / tpg == primary_gantry && role == ImexRole::Span)
            color_heads.insert(phys);
    for (int filament : used_filaments_0b) {
        const int phys = pem.values.empty() ? filament
                       : (filament >= 0 && filament < (int) pem.values.size()) ? pem.values[filament] : -1;
        if (color_heads.count(phys))
            continue;
        std::string tools;
        for (int head : color_heads)
            tools += (tools.empty() ? "T" : "/T") + std::to_string(head);
        if (phys < 0)
            return (boost::format(L("In this Span mode only %1% print colors, but filament %2% isn't routed to any "
                                    "tool. Use filaments routed to %1%, or switch this plate to Primary mode.")) %
                    tools % (filament + 1)).str();
        return (boost::format(L("In this Span mode only %1% print colors, but filament %2% is routed to T%3%. Use "
                                "filaments routed to %1%, or switch this plate to Primary mode.")) %
                tools % (filament + 1) % phys).str();
    }
    return {};
}

int first_filament_for_physical_head(const ConfigOptionInts& pem, int physical)
{
    const auto& v = pem.values;
    if (v.empty())
        return (physical == 0) ? 0 : -1;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == physical)
            return static_cast<int>(i);
    }
    return -1;
}

bool has_mmu(const ConfigOptionInts& pem)
{
    if (pem.values.size() < 2)
        return false;
    std::unordered_set<int> seen;
    for (int pv : pem.values) {
        if (!seen.insert(pv).second)
            return true;
    }
    return false;
}

bool has_non_primary_mmu(const ConfigOptionInts& pem, int primary_physical)
{
    if (pem.values.size() < 2)
        return false;
    std::unordered_set<int> seen;
    for (int pv : pem.values) {
        if (pv == primary_physical)
            continue;
        if (!seen.insert(pv).second)
            return true;
    }
    return false;
}

void imex_cfg_report_unregistered(const char* fn, const std::string& key)
{
    BOOST_LOG_TRIVIAL(error) << fn << ": no registered default for " << key;
}

int imex_cfg_int(const ConfigBase& cfg, const std::string& key)
{
    if (const auto* opt = cfg.option<ConfigOptionInt>(key))
        return opt->value;
    if (const ConfigOptionDef* def = print_config_def.get(key))
        if (const ConfigOption* dv = def->default_value.get())
            if (dv->type() == ConfigOptionInt::static_type())
                return static_cast<const ConfigOptionInt*>(dv)->value;
    assert(false && "imex_cfg_int: key not registered in print_config_def");
    imex_cfg_report_unregistered("imex_cfg_int", key);
    return 0;
}

bool imex_cfg_bool(const ConfigBase& cfg, const std::string& key)
{
    if (const auto* opt = cfg.option<ConfigOptionBool>(key))
        return opt->value;
    if (const ConfigOptionDef* def = print_config_def.get(key))
        if (const ConfigOption* dv = def->default_value.get())
            if (dv->type() == ConfigOptionBool::static_type())
                return static_cast<const ConfigOptionBool*>(dv)->value;
    assert(false && "imex_cfg_bool: key not registered in print_config_def");
    imex_cfg_report_unregistered("imex_cfg_bool", key);
    return false;
}

double imex_cfg_float(const ConfigBase& cfg, const std::string& key)
{
    if (const auto* opt = cfg.option<ConfigOptionFloat>(key))
        return opt->value;
    if (const ConfigOptionDef* def = print_config_def.get(key))
        if (const ConfigOption* dv = def->default_value.get())
            if (dv->type() == ConfigOptionFloat::static_type())
                return static_cast<const ConfigOptionFloat*>(dv)->value;
    assert(false && "imex_cfg_float: key not registered in print_config_def");
    imex_cfg_report_unregistered("imex_cfg_float", key);
    return 0.0;
}

char imex_role_letter(ImexRole role)
{
    for (const ImexRoleDesc& d : kImexRoleTable)
        if (d.role == role)
            return d.letter;
    return 'C';
}

ImexRole imex_role_from_suffix(const std::string& suffix)
{
    if (suffix.size() == 1)
        for (const ImexRoleDesc& d : kImexRoleTable)
            if (d.letter == suffix[0])
                return d.role;
    return ImexRole::Copy;
}

std::vector<std::pair<int, ImexRole>> parse_imex_active_tools(const std::string& active_tools_for_mode)
{
    std::vector<std::pair<int, ImexRole>> out;
    if (active_tools_for_mode.empty()) return out;
    std::istringstream ss(active_tools_for_mode);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        strip_whitespace(tok);
        if (tok.empty()) continue;
        const auto colon = tok.find(':');
        const std::string idx_str = (colon == std::string::npos) ? tok : tok.substr(0, colon);
        int phys = -1;
        try { phys = std::stoi(idx_str); } catch (...) { continue; }
        if (phys < 0 || phys >= (int)MAXIMUM_EXTRUDER_NUMBER) continue;
        const ImexRole role = (colon == std::string::npos)
                                  ? ImexRole::Copy
                                  : imex_role_from_suffix(tok.substr(colon + 1));
        out.emplace_back(phys, role);
    }
    return out;
}

ImexModeKind imex_mode_kind(const std::string& active_tools_for_mode)
{
    const int          primary = imex_primary_tool_for_mode(active_tools_for_mode);
    std::set<ImexRole> roles;
    for (const auto& [phys, role] : parse_imex_active_tools(active_tools_for_mode))
        if (phys != primary && role != ImexRole::Primary)
            roles.insert(role);
    if (roles.empty())
        return ImexModeKind::Primary;
    if (roles.count(ImexRole::Span))
        return ImexModeKind::Custom;
    return roles.count(ImexRole::Mirror) ? ImexModeKind::Mirror : ImexModeKind::Copy;
}

ImexGantryGrouping group_imex_active_tools_by_gantry(const std::string& active_tools_for_mode,
                                                     int                tools_per_gantry)
{
    ImexGantryGrouping out;
    const int tpg = std::max(1, tools_per_gantry);

    const int primary = imex_primary_tool_for_mode(active_tools_for_mode);
    if (primary < 0)
        return out;
    out.primary_phys   = primary;
    out.primary_gantry = primary / tpg;

    const int primary_col = primary % tpg;

    std::map<int, std::vector<std::pair<int, ImexRole>>> by_gantry;
    bool span_seen = false;
    for (const auto& [phys, role] : parse_imex_active_tools(active_tools_for_mode)) {
        if (phys < 0) continue;
        const int g = phys / tpg;
        by_gantry[g].emplace_back(phys, role);
        if (g == out.primary_gantry && role == ImexRole::Span)
            span_seen = true;
    }
    out.span_on_primary = span_seen;

    for (auto& [g, tools] : by_gantry) {
        ImexGantryGroup grp;
        grp.gantry_index = g;
        grp.tools        = tools;

        const int paired_phys = g * tpg + primary_col;
        auto pick = std::find_if(tools.begin(), tools.end(),
                                 [&](const auto& pr) { return pr.first == paired_phys; });
        if (pick == tools.end())
            pick = std::min_element(tools.begin(), tools.end(),
                                    [](const auto& a, const auto& b) { return a.first < b.first; });
        grp.representative_phys = pick->first;
        grp.representative_role = pick->second;

        const bool same_role = std::all_of(tools.begin(), tools.end(),
            [&](const auto& pr) { return pr.second == grp.representative_role; });
        grp.aggregate = span_seen
                        && g != out.primary_gantry
                        && tools.size() >= 2
                        && same_role;

        out.groups.push_back(std::move(grp));
    }
    return out;
}

int imex_primary_tool_for_mode(const std::string& active_tools_for_mode)
{
    if (active_tools_for_mode.empty())
        return -1;
    std::istringstream ss(active_tools_for_mode);
    std::string token;
    int first_bare = -1;
    while (std::getline(ss, token, ',')) {
        strip_whitespace(token);
        if (token.empty())
            continue;
        const auto colon = token.find(':');
        const std::string idx_str = (colon == std::string::npos) ? token : token.substr(0, colon);
        int idx = -1;
        try {
            idx = std::stoi(idx_str);
        } catch (...) {
            continue;
        }
        if (idx < 0)
            continue;
        if (colon == std::string::npos) {
            if (first_bare < 0)
                first_bare = idx;
            continue;
        }
        if (imex_role_from_suffix(token.substr(colon + 1)) == ImexRole::Primary)
            return idx;
    }
    return first_bare;
}

std::map<int,int> parse_imex_head_filament_map(const std::string& s)
{
    std::map<int,int> result;
    if (s.empty()) return result;
    std::istringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ',')) {
        strip_whitespace(token);
        if (token.empty()) continue;
        auto colon = token.find(':');
        if (colon == std::string::npos) continue;
        try {
            int phys = std::stoi(token.substr(0, colon));
            int slot = std::stoi(token.substr(colon + 1));
            if (phys >= 0 && phys < (int) MAXIMUM_EXTRUDER_NUMBER
                && slot >= 1 && slot <= (int) MAXIMUM_EXTRUDER_NUMBER)
                result[phys] = slot;
        } catch (...) {}
    }
    return result;
}

int imex_primary_logical_from_objects(const std::vector<int>&  used_slots_1b,
                                      const ConfigOptionInts&  pem,
                                      int                       primary_physical)
{
    if (pem.values.empty()) return -1;
    const int pem_size = (int)pem.values.size();
    for (int slot_1b : used_slots_1b) {
        const int slot_0b = slot_1b - 1;
        if (slot_0b >= 0 && slot_0b < pem_size && pem.values[slot_0b] == primary_physical)
            return slot_0b;
    }
    return -1;
}

ImexRouting imex_resolve_routing(const ConfigBase&       cfg,
                                 const std::string&      parallel_mode,
                                 const std::vector<int>& used_slots_1b,
                                 const ConfigOptionInts& pem)
{
    ImexRouting out;
    if (parallel_mode.empty() || parallel_mode == kImexPrimaryMode)
        return out;
    out.parallel = true;

    out.active_tools = find_imex_mode(cfg, parallel_mode).active_tools;
    out.tools        = parse_imex_active_tools(out.active_tools);
    out.primary_phys = imex_primary_tool_for_mode(out.active_tools);

    if (out.primary_phys >= 0)
        out.primary_logical = imex_primary_logical_from_objects(used_slots_1b, pem, out.primary_phys);

    out.routed_heads.reserve(used_slots_1b.size());
    for (int slot_1b : used_slots_1b) {
        const int slot_0b = slot_1b - 1;
        if (slot_0b >= 0 && slot_0b < (int) pem.values.size())
            out.routed_heads.push_back(pem.values[slot_0b]);
    }
    std::sort(out.routed_heads.begin(), out.routed_heads.end());
    out.routed_heads.erase(std::unique(out.routed_heads.begin(), out.routed_heads.end()),
                           out.routed_heads.end());

    out.primary_unrouted = out.primary_phys >= 0 && !pem.values.empty() && out.primary_logical < 0;
    return out;
}

std::vector<int> imex_secondary_logical_slots(const std::vector<int>&   active_physicals,
                                              int                        primary_physical,
                                              const std::map<int,int>&  plate_head_filament_map,
                                              const ConfigOptionInts&   pem)
{
    std::vector<int> out;
    std::unordered_set<int> seen;
    for (int phys : active_physicals) {
        if (phys == primary_physical) continue;
        const int logical = resolve_filament_for_head(plate_head_filament_map, pem, phys);
        if (logical < 0) continue;
        if (seen.insert(logical).second)
            out.push_back(logical);
    }
    return out;
}

int resolve_filament_for_head(const std::map<int,int>& plate_map,
                              const ConfigOptionInts&  pem,
                              int                       physical)
{
    auto it = plate_map.find(physical);
    if (it != plate_map.end()) {
        const int zero_based = it->second - 1;
        if (zero_based >= 0 && zero_based < (int) pem.values.size())
            return zero_based;
    }
    return first_filament_for_physical_head(pem, physical);
}

ImexMirrorAxis imex_mirror_axis_for(int primary_phys, int target_phys, int tools_per_gantry)
{
    const int tpg = std::max(1, tools_per_gantry);
    return (target_phys / tpg) == (primary_phys / tpg) ? ImexMirrorAxis::X : ImexMirrorAxis::Y;
}

Transform3d imex_head_transform(int , int , ImexRole role,
                                const Vec2d& gantry_offset,
                                const Vec2d& primary_zone_center,
                                ImexMirrorAxis mirror_axis)
{
    switch (role) {
    case ImexRole::Primary:
    case ImexRole::Span:
        return Transform3d::Identity();
    case ImexRole::Copy:
        return Eigen::Translation3d(gantry_offset.x(), gantry_offset.y(), 0.0)
               * Transform3d::Identity();
    case ImexRole::Mirror: {
        const double len2 = gantry_offset.squaredNorm();
        if (len2 < 1e-12)
            return Transform3d::Identity();
        Transform3d out = Transform3d::Identity();
        if (mirror_axis == ImexMirrorAxis::Y) {
            out.linear()      = Eigen::DiagonalMatrix<double, 3>(1.0, -1.0, 1.0);
            out.translation() = Vec3d(gantry_offset.x(),
                                      2.0 * primary_zone_center.y() + gantry_offset.y(),
                                      0.0);
        } else {
            out.linear()      = Eigen::DiagonalMatrix<double, 3>(-1.0, 1.0, 1.0);
            out.translation() = Vec3d(2.0 * primary_zone_center.x() + gantry_offset.x(),
                                      gantry_offset.y(),
                                      0.0);
        }
        return out;
    }
    }
    return Transform3d::Identity();
}

Vec2d compute_imex_slice_offset(bool firmware_managed,
                                const std::string& parallel_mode,
                                const std::optional<BoundingBoxf>& primary_zone_box)
{
    if (!firmware_managed)                    return Vec2d::Zero();
    if (parallel_mode.empty())                return Vec2d::Zero();
    if (parallel_mode == kImexPrimaryMode)    return Vec2d::Zero();
    if (!primary_zone_box.has_value())        return Vec2d::Zero();
    return primary_zone_box->center();
}

namespace {
ImexMode imex_mode_at(const ConfigOptionStrings*  names,
                      const ConfigOptionStrings*  tools,
                      const ConfigOptionStrings*  gcodes,
                      size_t                      i)
{
    ImexMode m;
    m.index = (int) i;
    m.name  = names->values[i];
    const bool has_tools = tools  != nullptr && i < tools->values.size();
    const bool has_gcode = gcodes != nullptr && i < gcodes->values.size();
    if (has_tools) m.active_tools = tools->values[i];
    if (has_gcode) m.gcode        = gcodes->values[i];
    m.ragged = !has_tools || !has_gcode;
    return m;
}
}

ImexMode find_imex_mode(const ConfigBase& cfg, const std::string& name)
{
    const auto* names = cfg.option<ConfigOptionStrings>("imex_mode_names");
    if (names == nullptr)
        return {};
    const auto* tools  = cfg.option<ConfigOptionStrings>("imex_mode_active_tools");
    const auto* gcodes = cfg.option<ConfigOptionStrings>("imex_mode_gcodes");
    for (size_t i = 0; i < names->values.size(); ++i)
        if (names->values[i] == name)
            return imex_mode_at(names, tools, gcodes, i);
    return {};
}

std::vector<ImexMode> imex_mode_table(const ConfigBase& cfg)
{
    std::vector<ImexMode> table;
    const auto* names = cfg.option<ConfigOptionStrings>("imex_mode_names");
    if (names == nullptr)
        return table;
    const auto* tools  = cfg.option<ConfigOptionStrings>("imex_mode_active_tools");
    const auto* gcodes = cfg.option<ConfigOptionStrings>("imex_mode_gcodes");
    table.reserve(names->values.size());
    for (size_t i = 0; i < names->values.size(); ++i)
        table.push_back(imex_mode_at(names, tools, gcodes, i));
    return table;
}

std::vector<std::string> imex_plate_mode_choices(const ConfigBase& cfg)
{
    std::vector<std::string> choices{ kImexPrimaryMode };
    for (const ImexMode& m : imex_mode_table(cfg))
        if (!m.name.empty() && std::find(choices.begin(), choices.end(), m.name) == choices.end())
            choices.push_back(m.name);
    return choices;
}

bool imex_hull_violates_zones(const std::vector<BoundingBoxf3>& zones, const Polygon& hull)
{
    if (hull.points.empty())
        return false;
    for (const auto& box : zones) {
        if (!intersection({box.polygon(true)}, {hull}).empty())
            return true;
    }
    return false;
}

}
