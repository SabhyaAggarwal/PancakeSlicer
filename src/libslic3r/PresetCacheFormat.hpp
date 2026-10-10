#ifndef slic3r_PresetCacheFormat_hpp_
#define slic3r_PresetCacheFormat_hpp_

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Semver.hpp"

namespace cereal { class BinaryInputArchive; }
namespace cereal { class BinaryOutputArchive; }

namespace Slic3r {

class CacheDictionary
{
public:
    CacheDictionary();

    static constexpr uint16_t ENUM_UNNAMED = 0;

    void collect(const DynamicPrintConfig& config);

    uint16_t key_index(const t_config_option_key& key) const;
    uint16_t enum_index(const std::string& name) const;

    const ConfigOptionDef* def_at(uint16_t idx) const { return m_defs[idx]; }
    ConfigOptionType       type_at(uint16_t idx) const { return ConfigOptionType(m_types[idx]); }
    const std::string&     enum_name_at(uint16_t idx) const { return m_enum_values[idx]; }
    bool valid_key_index(uint16_t idx) const { return size_t(idx) < m_defs.size(); }
    bool valid_enum_index(uint16_t idx) const { return size_t(idx) < m_enum_values.size(); }

    void save(cereal::BinaryOutputArchive& ar) const;
    void load(cereal::BinaryInputArchive& ar);

private:
    static constexpr size_t MAX_ENTRIES = 0xFFFF;

    std::vector<std::string> m_keys;
    std::vector<uint16_t>    m_types;
    std::vector<std::string> m_enum_values;
    std::unordered_map<std::string, uint16_t> m_key_index;
    std::unordered_map<std::string, uint16_t> m_enum_index;
    std::vector<const ConfigOptionDef*> m_defs;
};

void save_config(cereal::BinaryOutputArchive& ar, const DynamicPrintConfig& config, const CacheDictionary& dict);
void load_config(cereal::BinaryInputArchive& ar, DynamicPrintConfig& config, const CacheDictionary& dict);
void skip_config(cereal::BinaryInputArchive& ar, const CacheDictionary& dict);

struct CachedPreset
{
    std::string              name;
    std::string              sub_path;
    DynamicPrintConfig       config_src;
    std::string              inherits;
    std::vector<std::string> includes;
    std::string              description;
    std::string              instantiation;
    std::string              setting_id;
    std::string              filament_id;
    std::vector<std::string> renamed_from;
};

struct VendorCacheData
{
    VendorMap                 vendors;
    std::vector<CachedPreset> process_entries;
    std::vector<CachedPreset> filament_entries;
    std::vector<CachedPreset> machine_entries;
    uint64_t                  parse_errors = 0;
};

class VendorCacheFile
{
public:
    static bool save(const std::string& path, const std::string& vendor_name,
                     const std::string& vendor_version, const VendorCacheData& data);

    static bool load(const std::string& path, const std::string& expected_vendor_name,
                     const Semver& expected_vendor_version, VendorCacheData& data);

    static bool load_vendor_profile(const std::string& path, const std::string& expected_vendor_name,
                                    const Semver& expected_vendor_version, VendorProfile& vendor);

    static std::string peek_version(const std::string& path, const std::string& expected_vendor_name);

    static Semver usable_version(const std::string& path, const std::string& expected_vendor_name);

    static bool carries_preset(const std::string& path, const std::string& vendor_name,
                               Preset::Type type, const std::string& preset_name);
};

}

#endif
