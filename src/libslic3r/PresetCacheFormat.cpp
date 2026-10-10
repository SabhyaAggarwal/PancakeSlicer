#include "libslic3r/PresetCacheFormat.hpp"

#include <algorithm>
#include <cstdint>
#include <cereal/archives/binary.hpp>
#include <cstddef>
#include <cereal/details/helpers.hpp>
#include <cereal/cereal.hpp>
#include <ios>
#include <exception>
#include <boost/filesystem/operations.hpp>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <boost/crc.hpp>
#include <boost/filesystem.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/set.hpp>
#include <vector>

#include "Config.hpp"
#include "PrintConfig.hpp"
#include "Semver.hpp"
#include "Preset.hpp"
#include "libslic3r/Utils.hpp"

namespace Slic3r {

CacheDictionary::CacheDictionary()
{
    m_enum_values.emplace_back();
}

static std::vector<int> enum_ints(const ConfigOptionDef& def, const ConfigOption* opt)
{
    if (def.type == coEnum)
        return { opt->getInt() };
    return static_cast<const ConfigOptionInts*>(opt)->values;
}

static std::string enum_name_of(const ConfigOptionDef& def, int value)
{
    if (def.enum_keys_map != nullptr)
        for (const auto& kvp : *def.enum_keys_map)
            if (kvp.second == value)
                return kvp.first;
    return {};
}

void CacheDictionary::collect(const DynamicPrintConfig& config)
{
    for (auto it = config.cbegin(); it != config.cend(); ++ it) {
        const ConfigOptionDef* def = print_config_def.get(it->first);
        if (def == nullptr)
            continue;
        if (m_key_index.try_emplace(it->first, uint16_t(m_keys.size())).second) {
            m_keys.push_back(it->first);
            m_types.push_back(uint16_t(def->type));
        }
        if (def->type != coEnum && def->type != coEnums)
            continue;
        for (int value : enum_ints(*def, it->second.get())) {
            std::string name = enum_name_of(*def, value);
            if (! name.empty() && m_enum_index.try_emplace(name, uint16_t(m_enum_values.size())).second)
                m_enum_values.push_back(std::move(name));
        }
    }
}

uint16_t CacheDictionary::key_index(const t_config_option_key& key) const
{
    auto it = m_key_index.find(key);
    if (it == m_key_index.end())
        throw std::runtime_error("preset cache: option " + key + " was never collected into the dictionary");
    return it->second;
}

uint16_t CacheDictionary::enum_index(const std::string& name) const
{
    if (name.empty())
        return ENUM_UNNAMED;
    auto it = m_enum_index.find(name);
    return it == m_enum_index.end() ? ENUM_UNNAMED : it->second;
}

void CacheDictionary::save(cereal::BinaryOutputArchive& ar) const
{
    if (m_keys.size() > MAX_ENTRIES || m_enum_values.size() > MAX_ENTRIES)
        throw std::runtime_error("preset cache: the option dictionary outgrew the uint16 it is indexed with");
    ar(m_keys, m_types, m_enum_values);
}

void CacheDictionary::load(cereal::BinaryInputArchive& ar)
{
    ar(m_keys, m_types, m_enum_values);
    if (m_keys.size() != m_types.size())
        throw std::runtime_error("preset cache: dictionary key and type tables differ in length");
    if (m_keys.size() > MAX_ENTRIES || m_enum_values.size() > MAX_ENTRIES)
        throw std::runtime_error("preset cache: dictionary is larger than the uint16 it is indexed with");
    if (m_enum_values.empty() || ! m_enum_values.front().empty())
        throw std::runtime_error("preset cache: dictionary is missing its unnamed-enum slot");
    m_defs.resize(m_keys.size());
    for (size_t i = 0; i < m_keys.size(); ++ i) {
        const ConfigOptionDef* def = print_config_def.get(m_keys[i]);
        m_defs[i] = (def != nullptr && uint16_t(def->type) == m_types[i]) ? def : nullptr;
    }
}

static void save_enum_option(cereal::BinaryOutputArchive& ar, const ConfigOptionDef& def,
                             const ConfigOption* opt, const CacheDictionary& dict)
{
    const std::vector<int> values = enum_ints(def, opt);
    ar(uint32_t(values.size()));
    for (int value : values) {
        const uint16_t idx = dict.enum_index(enum_name_of(def, value));
        ar(idx);
        if (idx == CacheDictionary::ENUM_UNNAMED)
            ar(int32_t(value));
    }
}

static void load_enum_option(cereal::BinaryInputArchive& ar, ConfigOptionType type,
                             const ConfigOptionDef* def, DynamicPrintConfig* config,
                             const CacheDictionary& dict)
{
    uint32_t cnt = 0;
    ar(cnt);
    if (type == coEnum && cnt != 1)
        throw std::runtime_error("preset cache: a scalar enum carrying more than one value");
    bool usable = def != nullptr && config != nullptr;
    std::vector<int> values;
    values.reserve(cnt);
    for (uint32_t i = 0; i < cnt; ++ i) {
        uint16_t idx = 0;
        ar(idx);
        if (! dict.valid_enum_index(idx))
            throw std::runtime_error("preset cache: enum value index past the end of the dictionary");
        if (idx == CacheDictionary::ENUM_UNNAMED) {
            int32_t raw = 0;
            ar(raw);
            values.push_back(int(raw));
            continue;
        }
        if (! usable)
            continue;
        if (def->enum_keys_map == nullptr) {
            usable = false;
            continue;
        }
        const auto it = def->enum_keys_map->find(dict.enum_name_at(idx));
        if (it == def->enum_keys_map->end()) {
            usable = false;
            continue;
        }
        values.push_back(it->second);
    }
    if (! usable)
        return;
    if (type == coEnum) {
        config->set_key_value(def->opt_key, new ConfigOptionEnumGeneric(def->enum_keys_map, values.front()));
    } else {
        auto* opt = def->nullable ? static_cast<ConfigOptionInts*>(new ConfigOptionEnumsGenericNullable(def->enum_keys_map))
                                  : static_cast<ConfigOptionInts*>(new ConfigOptionEnumsGeneric(def->enum_keys_map));
        opt->values = std::move(values);
        config->set_key_value(def->opt_key, opt);
    }
}

void save_config(cereal::BinaryOutputArchive& ar, const DynamicPrintConfig& config, const CacheDictionary& dict)
{
    struct Written { uint16_t idx; const ConfigOptionDef* def; const ConfigOption* opt; };
    std::vector<Written> written;
    written.reserve(config.size());
    for (auto it = config.cbegin(); it != config.cend(); ++ it)
        if (const ConfigOptionDef* def = print_config_def.get(it->first))
            written.push_back({ dict.key_index(it->first), def, it->second.get() });

    ar(uint32_t(written.size()));
    for (const Written& w : written) {
        ar(w.idx);
        if (w.def->type == coEnum || w.def->type == coEnums)
            save_enum_option(ar, *w.def, w.opt, dict);
        else
            w.def->save_option_to_archive(ar, w.opt);
    }
}

static void read_config(cereal::BinaryInputArchive& ar, DynamicPrintConfig* config, const CacheDictionary& dict)
{
    uint32_t cnt = 0;
    ar(cnt);
    if (config != nullptr)
        config->clear();
    ConfigOptionDef scratch;
    for (uint32_t i = 0; i < cnt; ++ i) {
        uint16_t idx = 0;
        ar(idx);
        if (! dict.valid_key_index(idx))
            throw std::runtime_error("preset cache: option index past the end of the dictionary");
        const ConfigOptionType type = dict.type_at(idx);
        const ConfigOptionDef* def  = dict.def_at(idx);
        if (type == coEnum || type == coEnums) {
            load_enum_option(ar, type, def, config, dict);
        } else if (def != nullptr && config != nullptr) {
            config->set_key_value(def->opt_key, def->load_option_from_archive(ar));
        } else {
            scratch.type = type;
            std::unique_ptr<ConfigOption> discard(scratch.load_option_from_archive(ar));
        }
    }
}

void load_config(cereal::BinaryInputArchive& ar, DynamicPrintConfig& config, const CacheDictionary& dict)
{
    read_config(ar, &config, dict);
}

void skip_config(cereal::BinaryInputArchive& ar, const CacheDictionary& dict)
{
    read_config(ar, nullptr, dict);
}

namespace {

#pragma pack(push, 1)
struct CacheFileHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t data_size;
    uint32_t crc32;
};
#pragma pack(pop)
static_assert(sizeof(CacheFileHeader) == 20, "CacheFileHeader must be 20 bytes");

constexpr uint32_t CACHE_MAGIC   = 0x4F52435A;
constexpr uint32_t CACHE_VERSION = 2;

std::string read_bounded_string(cereal::BinaryInputArchive& ar)
{
    constexpr uint64_t MAX_STAMP_LEN = 1024;
    cereal::size_type len = 0;
    ar(cereal::make_size_tag(len));
    if (uint64_t(len) > MAX_STAMP_LEN)
        throw std::runtime_error("preset cache: string length out of bounds");
    std::string s(size_t(len), '\0');
    ar(cereal::binary_data(s.data(), size_t(len)));
    return s;
}

std::string read_cache_stamps(cereal::BinaryInputArchive& ar, const std::string& expected_vendor_name)
{
    uint32_t cache_version = 0;
    ar(cache_version);
    if (cache_version != CACHE_VERSION)
        return {};
    const std::string vendor_name    = read_bounded_string(ar);
    const std::string vendor_version = read_bounded_string(ar);
    if (vendor_name != expected_vendor_name)
        return {};
    return vendor_version;
}

bool cache_covers_version(const std::string& cached, const Semver& on_disk)
{
    if (on_disk == Semver::inf())
        return true;
    if (! on_disk.valid())
        return false;
    const auto cached_ver = Semver::parse(cached);
    return cached_ver && *cached_ver >= on_disk;
}

template<class Archive, class Entry, class ConfigFn>
void visit_entry(Archive& ar, Entry& e, ConfigFn&& config)
{
    ar(e.name, e.sub_path);
    config();
    ar(e.inherits, e.includes, e.description, e.instantiation, e.setting_id, e.filament_id, e.renamed_from);
}

constexpr uint32_t MAX_RESERVED_ENTRIES = 4096;

void save_entries(cereal::BinaryOutputArchive& ar,
                  const std::vector<CachedPreset>& entries,
                  const CacheDictionary& dict)
{
    ar(uint32_t(entries.size()));
    for (const CachedPreset& e : entries)
        visit_entry(ar, e, [&] { save_config(ar, e.config_src, dict); });
}

void load_entries(cereal::BinaryInputArchive& ar,
                  std::vector<CachedPreset>& entries,
                  const CacheDictionary& dict)
{
    uint32_t cnt = 0;
    ar(cnt);
    entries.clear();
    entries.reserve(std::min(cnt, MAX_RESERVED_ENTRIES));
    for (uint32_t i = 0; i < cnt; ++ i) {
        CachedPreset e;
        visit_entry(ar, e, [&] { load_config(ar, e.config_src, dict); });
        entries.push_back(std::move(e));
    }
}

bool read_cache_blob(const std::string& path, std::string& out_blob)
{
    try {
        boost::nowide::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open())
            return false;
        CacheFileHeader fhdr;
        if (!ifs.read(reinterpret_cast<char*>(&fhdr), sizeof(fhdr)))
            return false;
        if (fhdr.magic != CACHE_MAGIC)
            return false;
        ifs.seekg(0, std::ios::end);
        const std::streamoff file_size = ifs.tellg();
        if (file_size < std::streamoff(sizeof(fhdr)) ||
            fhdr.data_size == 0 ||
            fhdr.data_size != uint64_t(file_size) - sizeof(fhdr))
            return false;
        ifs.seekg(sizeof(fhdr), std::ios::beg);
        out_blob.assign(fhdr.data_size, '\0');
        if (!ifs.read(&out_blob[0], static_cast<std::streamsize>(fhdr.data_size)))
            return false;
        boost::crc_32_type crc;
        crc.process_bytes(out_blob.data(), out_blob.size());
        if (crc.checksum() != fhdr.crc32) {
            BOOST_LOG_TRIVIAL(warning) << "VendorCacheFile: CRC mismatch: " << path;
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "VendorCacheFile: read failed (" << path << "): " << e.what();
        return false;
    }
}

bool write_cache_blob(const std::string& path, const std::string& blob)
{
    boost::crc_32_type crc;
    crc.process_bytes(blob.data(), blob.size());
    try {
        boost::filesystem::create_directories(boost::filesystem::path(path).parent_path());
        CacheFileHeader fhdr;
        fhdr.magic     = CACHE_MAGIC;
        fhdr.version   = CACHE_VERSION;
        fhdr.data_size = static_cast<uint64_t>(blob.size());
        fhdr.crc32     = crc.checksum();
        const std::string_view header(reinterpret_cast<const char*>(&fhdr), sizeof(fhdr));
        if (const std::error_code ec = write_file_atomically(path, { header, std::string_view(blob) }, true)) {
            BOOST_LOG_TRIVIAL(warning) << "VendorCacheFile: write failed (" << path << "): " << ec.message();
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "VendorCacheFile: write failed (" << path << "): " << e.what();
        return false;
    }
}

}
bool VendorCacheFile::save(const std::string& path, const std::string& vendor_name,
                           const std::string& vendor_version, const VendorCacheData& data)
{
    try {
        CacheDictionary dict;
        for (const std::vector<CachedPreset>* entries : { &data.process_entries, &data.filament_entries, &data.machine_entries })
            for (const CachedPreset& e : *entries)
                dict.collect(e.config_src);

        std::ostringstream body(std::ios::binary);
        {
            cereal::BinaryOutputArchive ar(body);
            ar(CACHE_VERSION);
            ar(vendor_name, vendor_version);
            dict.save(ar);
            ar(data.vendors);
            save_entries(ar, data.process_entries, dict);
            save_entries(ar, data.filament_entries, dict);
            save_entries(ar, data.machine_entries, dict);
            ar(data.parse_errors);
        }
        return write_cache_blob(path, body.str());
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "VendorCacheFile: failed to save vendor cache " << path << ": " << e.what();
        return false;
    }
}

template<class ReadRest>
static bool read_cache_up_to_presets(const std::string& path, const std::string& expected_vendor_name,
                                     const Semver& expected_vendor_version, VendorMap& vendors, ReadRest&& read_rest)
{
    std::string blob;
    if (! read_cache_blob(path, blob))
        return false;
    try {
        boost::iostreams::stream<boost::iostreams::array_source> body(blob.data(), blob.size());
        cereal::BinaryInputArchive ar(body);
        const std::string vendor_version = read_cache_stamps(ar, expected_vendor_name);
        if (vendor_version.empty() || ! cache_covers_version(vendor_version, expected_vendor_version))
            return false;
        CacheDictionary dict;
        dict.load(ar);
        ar(vendors);
        read_rest(ar, dict);
        if (vendors.find(expected_vendor_name) == vendors.end())
            throw std::runtime_error("vendor cache does not carry its own vendor profile");
        return true;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "VendorCacheFile: rejecting vendor cache " << path << ": " << e.what();
        return false;
    }
}

bool VendorCacheFile::load(const std::string& path, const std::string& expected_vendor_name,
                           const Semver& expected_vendor_version, VendorCacheData& data)
{
    return read_cache_up_to_presets(path, expected_vendor_name, expected_vendor_version, data.vendors,
        [&data](cereal::BinaryInputArchive& ar, const CacheDictionary& dict) {
            load_entries(ar, data.process_entries, dict);
            load_entries(ar, data.filament_entries, dict);
            load_entries(ar, data.machine_entries, dict);
            ar(data.parse_errors);
        });
}

bool VendorCacheFile::load_vendor_profile(const std::string& path, const std::string& expected_vendor_name,
                                          const Semver& expected_vendor_version, VendorProfile& vendor)
{
    VendorMap vendors;
    if (! read_cache_up_to_presets(path, expected_vendor_name, expected_vendor_version, vendors,
                                   [](cereal::BinaryInputArchive&, const CacheDictionary&) {}))
        return false;
    vendor = std::move(vendors.find(expected_vendor_name)->second);
    return true;
}

std::string VendorCacheFile::peek_version(const std::string& path, const std::string& expected_vendor_name)
{
    try {
        boost::nowide::ifstream ifs(path, std::ios::binary);
        CacheFileHeader fhdr;
        if (! ifs.read(reinterpret_cast<char*>(&fhdr), sizeof(fhdr)) || fhdr.magic != CACHE_MAGIC)
            return {};
        std::string head(static_cast<size_t>(std::min<uint64_t>(fhdr.data_size, 1024)), '\0');
        if (! ifs.read(&head[0], static_cast<std::streamsize>(head.size())))
            return {};
        std::istringstream body(head, std::ios::binary);
        cereal::BinaryInputArchive ar(body);
        return read_cache_stamps(ar, expected_vendor_name);
    } catch (const std::exception&) {
        return {};
    }
}

Semver VendorCacheFile::usable_version(const std::string& path, const std::string& expected_vendor_name)
{
    std::string blob;
    if (! read_cache_blob(path, blob))
        return Semver::invalid();
    try {
        boost::iostreams::stream<boost::iostreams::array_source> body(blob.data(), blob.size());
        cereal::BinaryInputArchive ar(body);
        const auto ver = Semver::parse(read_cache_stamps(ar, expected_vendor_name));
        return ver ? *ver : Semver::invalid();
    } catch (const std::exception&) {
        return Semver::invalid();
    }
}

bool VendorCacheFile::carries_preset(const std::string& path, const std::string& vendor_name,
                                     Preset::Type type, const std::string& preset_name)
{
    std::string blob;
    if (! read_cache_blob(path, blob))
        return false;
    try {
        boost::iostreams::stream<boost::iostreams::array_source> body(blob.data(), blob.size());
        cereal::BinaryInputArchive ar(body);
        if (read_cache_stamps(ar, vendor_name).empty())
            return false;
        CacheDictionary dict;
        dict.load(ar);
        VendorMap vendors;
        ar(vendors);
        CachedPreset entry;
        for (Preset::Type kind : { Preset::TYPE_PRINT, Preset::TYPE_FILAMENT, Preset::TYPE_PRINTER }) {
            uint32_t cnt = 0;
            ar(cnt);
            for (uint32_t i = 0; i < cnt; ++ i) {
                visit_entry(ar, entry, [&] { skip_config(ar, dict); });
                if (kind == type && entry.name == preset_name)
                    return true;
            }
            if (kind == type)
                return false;
        }
        return false;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "VendorCacheFile: could not read preset names from " << path << ": " << e.what();
        return false;
    }
}

}
