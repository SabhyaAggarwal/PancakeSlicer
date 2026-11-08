#include "PublishSettings.hpp"

#include "Config.hpp"
#include "PresetBundle.hpp"
#include "Preset.hpp"
#include "PrintConfig.hpp"
#include "MaterialType.hpp"

#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/trim.hpp>

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Slic3r {

std::string publish_base_key(const std::string &key)
{
    const size_t pos = key.find('#');
    return pos == std::string::npos ? key : key.substr(0, pos);
}

static int publish_variant_index(const std::string &key, const std::string &base_key)
{
    if (key.size() <= base_key.size() || key.compare(0, base_key.size(), base_key) != 0 || key[base_key.size()] != '#')
        return -1;
    const std::string suffix = key.substr(base_key.size() + 1);
    if (suffix.empty())
        return -1;
    int idx = 0;
    for (const char c : suffix) {
        if (c < '0' || c > '9')
            return -1;
        idx = idx * 10 + (c - '0');
        if (idx > 1000000)
            return -1;
    }
    return idx;
}

std::string normalize_filament_type(const std::string& type)
{
    if (type.empty())
        return type;
    if (MaterialType::find(type) != nullptr)
        return type;
    const size_t sep = type.find(' ');
    if (sep != std::string::npos) {
        const std::string base = type.substr(0, sep);
        if (MaterialType::find(base) != nullptr)
            return base;
    }
    return type;
}

void make_publish_universal(DynamicPrintConfig &config)
{
    if (auto *opt = config.opt<ConfigOptionStrings>("compatible_printers", false))
        opt->values.clear();
    if (auto *opt = config.opt<ConfigOptionStrings>("compatible_prints", false))
        opt->values.clear();
    if (auto *opt = config.opt<ConfigOptionString>("compatible_printers_condition", false))
        opt->value.clear();
    if (auto *opt = config.opt<ConfigOptionString>("compatible_prints_condition", false))
        opt->value.clear();
}

std::string publish_material_base_name(const std::string &preset_name)
{
    if (preset_name.empty())
        return preset_name;
    const size_t at = preset_name.find('@');
    std::string base = (at == std::string::npos) ? preset_name : preset_name.substr(0, at);
    boost::trim_right(base);
    return base;
}

const std::set<std::string>& publish_structural_keys()
{
    static const std::set<std::string> structural_keys = {
        "printer_settings_id", "filament_settings_id", "print_settings_id",
        "sla_print_settings_id", "sla_material_settings_id",
        "compatible_printers", "compatible_prints",
        "compatible_printers_condition", "compatible_prints_condition",
        "default_filament_profile", "default_print_profile",
        "default_sla_print_profile", "default_sla_material_profile",
        "extruder_count", "bed_shape",
        "inherits", "inherits_group",
        "printer_technology", "printer_model", "printer_variant",
        "physical_printer_settings_id", "filament_ids",
        "different_settings_to_system"
    };
    return structural_keys;
}

const std::set<std::string>& publish_mixed_keys()
{
    static const std::set<std::string> mixed_keys = {
        "filament_is_mixed",
        "filament_mixed_components",
        "filament_mixed_sublayer_ratios",
        "filament_mixed_gradient",
        "filament_mixed_gradient_range",
        "filament_mixed_gradient_curve",
        "filament_mixed_gradient_per_part"
    };
    return mixed_keys;
}

const std::vector<PublishablePrinterOption>& publishable_printer_retraction_options()
{
    static const std::vector<PublishablePrinterOption> options = {
        { "retraction_length",              "printer_extruder_retraction#length" },
        { "retract_restart_extra",          "printer_extruder_retraction#extra-length-on-restart" },
        { "retraction_speed",               "printer_extruder_retraction#retraction-speed" },
        { "deretraction_speed",             "printer_extruder_retraction#deretraction-speed" },
        { "retraction_minimum_travel",      "printer_extruder_retraction#travel-distance-threshold" },
        { "retract_when_changing_layer",    "printer_extruder_retraction#retract-on-layer-change" },
        { "wipe",                           "printer_extruder_retraction#wipe-while-retracting" },
        { "wipe_distance",                  "printer_extruder_retraction#wipe-distance" },
        { "retract_before_wipe",            "printer_extruder_retraction#retract-amount-before-wipe" },
        { "retract_after_wipe",             "printer_extruder_retraction#retract-amount-after-wipe" },
    };
    return options;
}

const std::vector<PublishablePrinterOption>& publishable_printer_z_hop_options()
{
    static const std::vector<PublishablePrinterOption> options = {
        { "retract_lift_enforce", "printer_extruder_z_hop#on-surfaces" },
        { "z_hop_types",          "printer_extruder_z_hop#z-hop-type" },
        { "z_hop",                "printer_extruder_z_hop#z-hop-height" },
        { "travel_slope",         "printer_extruder_z_hop#traveling-angle" },
        { "retract_lift_above",   "printer_extruder_z_hop#only-lift-z-above" },
        { "retract_lift_below",   "printer_extruder_z_hop#only-lift-z-below" },
    };
    return options;
}

const std::set<std::string>& publishable_printer_keys()
{
    static const std::set<std::string> printer_keys = [] {
        std::set<std::string> keys;
        for (const PublishablePrinterOption &opt : publishable_printer_retraction_options())
            keys.insert(opt.key);
        for (const PublishablePrinterOption &opt : publishable_printer_z_hop_options())
            keys.insert(opt.key);
        return keys;
    }();
    return printer_keys;
}

std::vector<std::string> collect_dirty_settings_keys(const PresetBundle& bundle)
{
    std::set<std::string> keys;

    for (const std::string& key : bundle.prints.current_dirty_options(true))
        keys.insert(key);
    for (const std::string& key : bundle.printers.current_dirty_options(true))
        keys.insert(key);
    for (const std::string& key : bundle.filaments.current_dirty_options(true))
        keys.insert(key);

    return std::vector<std::string>(keys.begin(), keys.end());
}

DynamicPrintConfig filter_published_config(
    const DynamicPrintConfig &full_config,
    const std::vector<std::string> &published_keys,
    const std::vector<PublishedMaterialEntry> &material_keys)
{
    DynamicPrintConfig filtered;

    std::set<std::string> base_keys_to_include;
    std::set<std::string>         mask_exempt_keys;
    std::map<std::string, std::set<int>> slot_mask_map;

    static const std::vector<std::string> s_material_identity_keys = {
        "filament_colour",
        "filament_type",
        "filament_vendor",
        "filament_ids",
        "filament_diameter",
        "filament_self_index",
        "filament_extruder_variant"
    };
    for (const std::string &key : s_material_identity_keys) {
        base_keys_to_include.insert(key);
        mask_exempt_keys.insert(key);
    }

    static const std::vector<std::string> s_plate_geometry_keys = {
        "wipe_tower_x",
        "wipe_tower_y",
        "wipe_tower_rotation_angle"
    };
    for (const std::string &key : s_plate_geometry_keys) {
        base_keys_to_include.insert(key);
        mask_exempt_keys.insert(key);
    }

    const std::set<std::string> &printer_keys = publishable_printer_keys();
    for (const std::string &key : published_keys) {
        const std::string base_key = publish_base_key(key);
        if (base_key.empty())
            continue;
        base_keys_to_include.insert(base_key);
        if (printer_keys.count(base_key) != 0) {
            const int variant_idx = publish_variant_index(key, base_key);
            if (variant_idx >= 0)
                slot_mask_map[base_key].insert(variant_idx);
            else
                mask_exempt_keys.insert(base_key);
        } else {
            mask_exempt_keys.insert(base_key);
        }
    }

    for (const PublishedMaterialEntry &entry : material_keys) {
        for (const std::string &key : entry.keys) {
            const std::string base_key = publish_base_key(key);
            if (base_key.empty())
                continue;
            base_keys_to_include.insert(base_key);
            if (entry.slot >= 0)
                slot_mask_map[base_key].insert(entry.slot);
        }
        for (const std::string &key : entry.full_keys) {
            const std::string base_key = publish_base_key(key);
            if (base_key.empty())
                continue;
            base_keys_to_include.insert(base_key);
            if (entry.slot >= 0)
                slot_mask_map[base_key].insert(entry.slot);
        }
    }

    auto can_mask_slots = [](const ConfigOption &opt, const ConfigOptionDef *def) -> bool {
        if (def == nullptr || !def->default_value || def->default_value->type() != opt.type())
            return false;
        const auto *vec         = dynamic_cast<const ConfigOptionVectorBase *>(&opt);
        const auto *default_vec = dynamic_cast<const ConfigOptionVectorBase *>(def->default_value.get());
        return vec != nullptr && vec->size() > 0 && default_vec != nullptr && !default_vec->empty();
    };
    auto mask_slots = [](ConfigOption &opt, const ConfigOptionDef *def, const std::set<int> &keep_slots) {
        auto *vec = dynamic_cast<ConfigOptionVectorBase*>(&opt);
        for (size_t idx = 0; idx < vec->size(); ++idx)
            if (keep_slots.count(static_cast<int>(idx)) == 0)
                vec->set_at(def->default_value.get(), idx, 0);
    };

    for (const std::string &key : base_keys_to_include) {
        const ConfigOption *opt = full_config.option(key);
        if (opt == nullptr)
            continue;
        const auto  mask_it       = slot_mask_map.find(key);
        const bool  needs_masking = mask_exempt_keys.count(key) == 0 && mask_it != slot_mask_map.end() && !mask_it->second.empty();
        if (needs_masking && !can_mask_slots(*opt, print_config_def.get(key))) {
            BOOST_LOG_TRIVIAL(warning) << "publish: dropping unmaskable key \"" << key
                                       << "\" from the published payload (no usable option default)";
            continue;
        }
        ConfigOption *cloned = opt->clone();
        if (needs_masking)
            mask_slots(*cloned, print_config_def.get(key), mask_it->second);
        filtered.set_key_value(key, cloned);
    }

    return filtered;
}

}
