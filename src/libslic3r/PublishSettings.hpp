#pragma once
#include <set>
#include <string>
#include <vector>

namespace Slic3r {
class PresetBundle;


std::string publish_base_key(const std::string &key);





const std::set<std::string>& publish_structural_keys();



const std::set<std::string>& publish_mixed_keys();


struct PublishablePrinterOption {
    const char *key;
    const char *icon;
};


const std::vector<PublishablePrinterOption>& publishable_printer_retraction_options();
const std::vector<PublishablePrinterOption>& publishable_printer_z_hop_options();



const std::set<std::string>& publishable_printer_keys();



std::vector<std::string> collect_dirty_settings_keys(const PresetBundle& bundle);





struct PublishedMaterialEntry {
    std::string filament_type;
    std::string filament_vendor;
    std::string filament_id;



    std::string setting_id;



    std::string preset_name;

    int         slot{-1};
    std::vector<std::string> keys;




    bool full{false};


    std::vector<std::string> full_keys;



    bool publish_type{false};
    std::string publish_type_value;

    bool publish_color{false};
    std::string color;



    bool mixed_placeholder{false};
};


std::string normalize_filament_type(const std::string& type);

class DynamicPrintConfig;


void make_publish_universal(DynamicPrintConfig &config);



std::string publish_material_base_name(const std::string &preset_name);



DynamicPrintConfig filter_published_config(
    const DynamicPrintConfig &full_config,
    const std::vector<std::string> &published_keys,
    const std::vector<PublishedMaterialEntry> &material_keys);
}
