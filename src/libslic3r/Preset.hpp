#ifndef slic3r_Preset_hpp_
#define slic3r_Preset_hpp_

#include <cstddef>
#include <cassert>
#include <cmath>
#include <algorithm>
#include <deque>
#include <map>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <mutex>
#include <optional>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/property_tree/ptree_fwd.hpp>
#include <vector>
#include <utility>

#include "Config.hpp"
#include "PrintConfig.hpp"
#include "Semver.hpp"
#include "ProjectTask.hpp"
#include "libslic3r.h"

#define PRESET_SYSTEM_DIR      "system"
#define PRESET_USER_DIR        "user"
#define PRESET_FILAMENT_NAME    "filament"
#define PRESET_PRINT_NAME     "process"
#define PRESET_PRINTER_NAME     "machine"
#define PRESET_SLA_PRINT_NAME  "sla_print"
#define PRESET_SLA_MATERIALS_NAME "sla_materials"
#define PRESET_PROFILES_DIR "profiles"
#define PRESET_PROFILES_TEMOLATE_DIR "profiles_template"
#define PRESET_TEMPLATE_DIR "Template"
#define PRESET_CUSTOM_VENDOR "Custom"

#define PRESET_LOCAL_DIR          "_local"
#define PRESET_SUBSCRIBED_DIR     "_subscribed"
#define PRESET_BUNDLE_METADATA    "bundle_metadata.json"

#define PRESET_IOT_PRINTER_TYPE     "printer"
#define PRESET_IOT_FILAMENT_TYPE    "filament"
#define PRESET_IOT_PRINT_TYPE       "print"


#define BBL_JSON_KEY_VERSION        "version"
#define BBL_JSON_KEY_IS_CUSTOM      "is_custom_defined"
#define BBL_JSON_KEY_URL            "url"
#define BBL_JSON_KEY_NAME           "name"
#define BBL_JSON_KEY_DESCRIPTION    "description"
#define BBL_JSON_KEY_FORCE_UPDATE   "force_update"
#define BBL_JSON_KEY_MACHINE_MODEL_LIST     "machine_model_list"
#define BBL_JSON_KEY_PROCESS_LIST   "process_list"
#define BBL_JSON_KEY_SUB_PATH       "sub_path"
#define BBL_JSON_KEY_FILAMENT_LIST  "filament_list"
#define BBL_JSON_KEY_MACHINE_LIST   "machine_list"
#define BBL_JSON_KEY_TYPE           "type"
#define BBL_JSON_KEY_FROM           "from"
#define BBL_JSON_KEY_SETTING_ID     "setting_id"
#define BBL_JSON_KEY_BASE_ID        "base_id"
#define BBL_JSON_KEY_USER_ID        "user_id"
#define BBL_JSON_KEY_FILAMENT_ID    "filament_id"
#define UNKNOWN_FILAMENT_ID         "__unknown__"
#define ORCA_JSON_KEY_UPDATE_TIME    "updated_time"
#define ORCA_JSON_KEY_CREATED_TIME    "created_time"
#define BBL_JSON_KEY_INHERITS       "inherits"
#define BBL_JSON_KEY_INCLUDES       "include"
#define BBL_JSON_KEY_INSTANTIATION  "instantiation"
#define BBL_JSON_KEY_NOZZLE_DIAMETER            "nozzle_diameter"
#define BBL_JSON_KEY_PRINTER_TECH                 "machine_tech"
#define BBL_JSON_KEY_FAMILY                     "family"
#define BBL_JSON_KEY_BED_MODEL                  "bed_model"
#define BBL_JSON_KEY_BED_TEXTURE                "bed_texture"
#define BBL_JSON_KEY_IMAGE_BED_TYPE             "image_bed_type"
#define BBL_JSON_KEY_BOTTOM_TEXTURE_END_NAME    "bottom_texture_end_name"
#define BBL_JSON_KEY_USE_DOUBLE_EXTRUDER_DEFAULT_TEXTURE  "use_double_extruder_default_texture"
#define BBL_JSON_KEY_BOTTOM_TEXTURE_RECT        "bottom_texture_rect"
#define BBL_JSON_KEY_BOTTOM_TEXTURE_RECT_LONGER  "bottom_texture_rect_longer"
#define BBL_JSON_KEY_MIDDLE_TEXTURE_RECT        "middle_texture_rect"

#define BBL_JSON_KEY_HOTEND_MODEL               "hotend_model"
#define BBL_JSON_KEY_DEFAULT_MATERIALS          "default_materials"
#define BBL_JSON_KEY_NOT_SUPPORT_BED_TYPE       "not_support_bed_type"
#define BBL_JSON_KEY_MODEL_ID                   "model_id"

#define ORCA_JSON_KEY_RENAMED_FROM              "renamed_from"


static constexpr const char* GENERIC_PREFIX = "Generic ";

namespace Slic3r {

class AppConfig;
class PresetBundle;

std::string generate_preset_setting_id(const std::string& vendor,
                                       const std::string& type,
                                       const std::string& name);

enum ConfigFileType
{
    CONFIG_FILE_TYPE_UNKNOWN,
    CONFIG_FILE_TYPE_APP_CONFIG,
    CONFIG_FILE_TYPE_CONFIG,
    CONFIG_FILE_TYPE_CONFIG_BUNDLE,
};

extern Semver get_version_from_json(std::string file_path);
extern int get_values_from_json(std::string file_path, std::vector<std::string>& keys, std::map<std::string, std::string>& key_values);

extern ConfigFileType guess_config_file_type(const boost::property_tree::ptree &tree);

extern void extend_default_config_length(DynamicPrintConfig& config, const bool set_nil_to_default, const DynamicPrintConfig& defaults);

class VendorProfile
{
public:
    std::string                     name;
    std::string                     id;
    Semver                          config_version;
    std::string                     config_update_url;
    std::string                     changelog_url;

    struct PrinterVariant {
        PrinterVariant() {}
        PrinterVariant(const std::string &name) : name(name) {}
        std::string                 name;

        template<class Archive>
        void serialize(Archive& ar) { ar(name); }
    };

    struct PrinterModel {
        PrinterModel() {}
        std::string                 id;
        std::string                 name;
        std::string                 model_id;
        PrinterTechnology           technology = ptFFF;
        std::string                 family;
        std::vector<PrinterVariant> variants;
        std::vector<std::string>	default_materials;
        std::vector<std::string>    not_support_bed_types;
        std::string 			 	bed_model;
        std::string 				bed_texture;
        std::string                 image_bed_type;
        std::string                 bottom_texture_end_name;
        std::string                 use_double_extruder_default_texture;
        std::string                 bottom_texture_rect;
        std::string                 bottom_texture_rect_longer;
        std::string                 middle_texture_rect;
        std::string                 hotend_model;
        PrinterVariant*       variant(const std::string &name) {
            for (auto &v : this->variants)
                if (v.name == name)
                    return &v;
            return nullptr;
        }

        const PrinterVariant* variant(const std::string &name) const { return const_cast<PrinterModel*>(this)->variant(name); }

        template<class Archive>
        void serialize(Archive& ar)
        {
            ar(id, name, model_id, technology, family, variants, default_materials,
               not_support_bed_types, bed_model, bed_texture, image_bed_type,
               bottom_texture_end_name, use_double_extruder_default_texture,
               bottom_texture_rect, bottom_texture_rect_longer, middle_texture_rect,
               hotend_model);
        }
    };
    std::vector<PrinterModel>          models;

    std::set<std::string>              default_filaments;
    std::set<std::string>              default_sla_materials;

    VendorProfile() {}
    VendorProfile(std::string id) : id(std::move(id)) {}

    bool 		valid() const { return ! name.empty() && ! id.empty() && config_version.valid(); }

    template<class Archive>
    void serialize(Archive& ar)
    {
        ar(name, id, config_version, config_update_url, changelog_url,
           models, default_filaments, default_sla_materials);
    }

    static VendorProfile from_ini(const boost::filesystem::path &path, bool load_all=true);
    static VendorProfile from_ini(const boost::property_tree::ptree &tree, const boost::filesystem::path &path, bool load_all=true);

    size_t      num_variants() const { size_t n = 0; for (auto &model : models) n += model.variants.size(); return n; }
    std::vector<std::string> families() const;

    bool        operator< (const VendorProfile &rhs) const { return this->id <  rhs.id; }
    bool        operator==(const VendorProfile &rhs) const { return this->id == rhs.id; }
};

class Preset;

struct PresetWithVendorProfile {
	PresetWithVendorProfile(const Preset &preset, const VendorProfile *vendor) : preset(preset), vendor(vendor) {}
	const Preset 		&preset;
	const VendorProfile *vendor;
};

typedef std::map<std::string, VendorProfile> VendorMap;

class Preset
{
public:
    enum Type
    {
        TYPE_INVALID,
        TYPE_PRINT,
        TYPE_SLA_PRINT,
        TYPE_FILAMENT,
        TYPE_SLA_MATERIAL,
        TYPE_PRINTER,
        TYPE_COUNT,
        TYPE_PHYSICAL_PRINTER,
        TYPE_PLATE,
        TYPE_MODEL,
    };

    Type                type        = TYPE_INVALID;

    bool                is_default = false;
    bool                is_external = false;
    bool                is_system   = false;
    bool                is_visible  = true;
    bool                is_dirty    = false;
    bool                is_compatible = true;

    bool                is_project_embedded = false;
    ConfigSubstitutions *loading_substitutions{nullptr};
    bool                is_user() const { return ! this->is_default && ! this->is_system && ! this->is_project_embedded && ! this->is_from_bundle(); }
    bool                can_overwrite() const { return ! this->is_default && ! this->is_system && ! this->is_from_bundle(); }
    std::string         name;
    std::string         file;
    const VendorProfile *vendor      = nullptr;

    bool                loaded      = false;

    DynamicPrintConfig  config;

    std::string         alias;
    std::vector<std::string> renamed_from;

    std::set<std::string> m_excluded_from;

    bool m_from_orca_filament_lib = false;

    std::string         bundle_id;
    bool                is_from_bundle() const { return ! bundle_id.empty(); }

    Semver              version;
    std::string         ini_str;
    std::string         setting_id;
    std::string         filament_id;
    std::string         user_id;
    std::string         base_id;
    std::string         sync_info;
    std::string         description;
    long long           updated_time{0};
    std::map<std::string, std::string> key_values;

    static std::string  get_type_string(Preset::Type type);
    static std::string  get_iot_type_string(Preset::Type type);
    static Preset::Type get_type_from_string(std::string type_str);
    void                load_info(const std::string& file);
    void                save_info(std::string file = "");
    void                remove_files(bool cloud_already_deleted = false);

    bool                save(DynamicPrintConfig* parent_config);
    void                reload(Preset const & parent);

    std::string         label(bool no_alias) const;

    void                set_dirty(const DynamicPrintConfig &config) { this->is_dirty = ! this->config.diff(config).empty(); }
    void                set_dirty(bool dirty = true) { this->is_dirty = dirty; }
    void                reset_dirty() { this->is_dirty = false; }

    static std::string& inherits(DynamicPrintConfig &cfg) { return cfg.option<ConfigOptionString>("inherits", true)->value; }
    std::string&        inherits() { return Preset::inherits(this->config); }
    const std::string&  inherits() const { return Preset::inherits(const_cast<Preset*>(this)->config); }

    static void normalize_inherits(DynamicPrintConfig &cfg, const Preset *resolved_parent)
    {
        if (resolved_parent == nullptr)
            return;
        std::string &inherits = Preset::inherits(cfg);
        if (inherits != resolved_parent->name)
            inherits = resolved_parent->name;
    }

    static std::string& compatible_prints_condition(DynamicPrintConfig &cfg) { return cfg.option<ConfigOptionString>("compatible_prints_condition", true)->value; }
    std::string&        compatible_prints_condition() {
		assert(this->type == TYPE_FILAMENT || this->type == TYPE_SLA_MATERIAL);
        return Preset::compatible_prints_condition(this->config);
    }
    const std::string&  compatible_prints_condition() const { return const_cast<Preset*>(this)->compatible_prints_condition(); }

    static std::string& compatible_printers_condition(DynamicPrintConfig &cfg) { return cfg.option<ConfigOptionString>("compatible_printers_condition", true)->value; }
    std::string&        compatible_printers_condition() {
		assert(this->type == TYPE_PRINT || this->type == TYPE_SLA_PRINT || this->type == TYPE_FILAMENT || this->type == TYPE_SLA_MATERIAL);
        return Preset::compatible_printers_condition(this->config);
    }
    const std::string&  compatible_printers_condition() const { return const_cast<Preset*>(this)->compatible_printers_condition(); }

    static PrinterTechnology printer_technology(const DynamicPrintConfig &cfg) {
        auto *opt = cfg.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
        return (opt == nullptr) ? ptFFF : opt->value;
    }
    PrinterTechnology   printer_technology() const { return Preset::printer_technology(this->config); }
    PrinterTechnology&  printer_technology_ref() { return this->config.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology", true)->value; }

    void                set_visible_from_appconfig(const AppConfig &app_config);

    void                set_num_extruders(unsigned int n) { this->config.set_num_extruders(n); }

    bool                operator<(const Preset &other) const { return this->name < other.name; }

    std::string get_filament_type(std::string &display_filament_type);
    std::string get_printer_type(PresetBundle *preset_bundle);
    std::string get_current_printer_type(PresetBundle *preset_bundle);

    static void get_extruder_names_and_keysets(Type type, std::string& extruder_id_name, std::string& extruder_variant_name, std::set<std::string>** p_key_set1, std::set<std::string>** p_key_set2);
    static DynamicPrintConfig load_external_config(Type type, const DynamicPrintConfig &default_config, const DynamicPrintConfig &project_config,
                                                   const std::set<std::string> &different_settings_list,
                                                   const std::function<DynamicPrintConfig *(const std::string &inherits)> &find_base,
                                                   t_config_option_keys *keys = nullptr);
    std::string get_printer_id() const { return vendor ? vendor->id : ""; }

    bool has_lidar(PresetBundle *preset_bundle);
    bool is_custom_defined();

    BedType get_default_bed_type(PresetBundle *preset_bundle);
    bool has_cali_lines(PresetBundle* preset_bundle);


    static double convert_pellet_flow_to_filament_diameter(double pellet_flow_coefficient)
    {
        return sqrt(4 / (PI * pellet_flow_coefficient)); 
    }

    static double convert_filament_diameter_to_pellet_flow(double filament_diameter)
    {
        return 4 / (pow(filament_diameter, 2) * PI); 
    }

    static const std::vector<std::string>&  print_options();
    static const std::vector<std::string>&  filament_options();
    static const std::vector<std::string>&  printer_options();
    static const std::vector<std::string>&  nozzle_options();
    static const std::vector<std::string>&  machine_limits_options();

    static const char*                      plugin_overrides_key(Type type);

    static const std::vector<std::string>&  sla_printer_options();
    static const std::vector<std::string>&  sla_material_options();
    static const std::vector<std::string>&  sla_print_options();

	static void                             update_suffix_modified(const std::string& new_suffix_modified);
    static const std::string&               suffix_modified();
    static std::string                      remove_suffix_modified(const std::string& name);
    static void                             normalize(DynamicPrintConfig &config);
    static std::string                      remove_invalid_keys(DynamicPrintConfig &config, const DynamicPrintConfig &default_config,
                                                                const DynamicPrintConfig *added = nullptr);

    Preset(Type type, const std::string &name, bool is_default = false) : type(type), is_default(is_default), name(name) {}

protected:
    friend class        PresetCollection;
    friend class        PresetBundle;

    Preset() = default;
};

bool is_compatible_with_print  (const PresetWithVendorProfile &preset, const PresetWithVendorProfile &active_print, const PresetWithVendorProfile &active_printer);
bool is_compatible_with_printer(const PresetWithVendorProfile &preset, const PresetWithVendorProfile &active_printer, const DynamicPrintConfig *extra_config);
bool is_compatible_with_printer(const PresetWithVendorProfile &preset, const PresetWithVendorProfile &active_printer);
bool is_compatible_with_printer(const DynamicPrintConfig &preset_config, Preset::Type preset_type,
                                const DynamicPrintConfig &printer_config, const std::string &printer_name);

struct PresetOrigin {
    enum class Kind { Auto, User, LocalBundle, SubscribedBundle };

    Kind        kind { Kind::Auto };
    std::string bundle_id;

    PresetOrigin() = default;
    PresetOrigin(Kind kind, std::string bundle_id = {}) : kind(kind), bundle_id(std::move(bundle_id)) {}

    bool is_bundle() const { return kind == Kind::LocalBundle || kind == Kind::SubscribedBundle; }
};

std::string get_preset_canonical_name(const std::string &preset_bare_name, const PresetOrigin &origin);

std::string get_preset_bare_name(const std::string &canonical_name);

std::string user_presets_lock_path(bool read_only = false);

PresetOrigin detect_origin_from_path(const boost::filesystem::path &path, const PresetOrigin &explicit_origin = PresetOrigin());

enum class PresetSelectCompatibleType {
	Never,
	OnlyIfWasCompatible,
	Always
};

struct PresetConfigSubstitutions {
    std::string                             preset_name;
    Preset::Type                            preset_type;
    enum class Source {
        UserFile,
        ConfigBundle,
        UserCloud,
        ProjectFile,
    };
    Source                                  preset_source;
    std::string                             preset_file;
    ConfigSubstitutions                     substitutions;
};

using PresetsConfigSubstitutions = std::vector<PresetConfigSubstitutions>;

class PresetCollection
{
public:
    PresetCollection(Preset::Type type, const std::vector<std::string> &keys, const Slic3r::StaticPrintConfig &defaults, const std::string &default_name = "Default Setting");

    typedef std::deque<Preset>::iterator Iterator;
    typedef std::deque<Preset>::const_iterator ConstIterator;
    typedef std::function<void(Preset* preset, std::string sync_info)> SyncFunc;
    Iterator        lbegin() { return m_presets.begin(); }
    bool            validate_preset(const std::string &name, std::string &inherit);

    Iterator        begin() { return m_presets.begin() + m_num_default_presets; }
    ConstIterator   begin() const { return m_presets.cbegin() + m_num_default_presets; }
    ConstIterator   cbegin() const { return m_presets.cbegin() + m_num_default_presets; }
    Iterator        end() { return m_presets.end(); }
    ConstIterator   end() const { return m_presets.cend(); }
    ConstIterator   cend() const { return m_presets.cend(); }

    Iterator        erase(Iterator it) { return m_presets.erase(it); }
    SyncFunc        sync_func{ nullptr };
    void            set_sync_func(SyncFunc func) { sync_func = func; }
    void            lock() { m_mutex.lock(); }
    void            unlock() { m_mutex.unlock(); }

    void            reset(bool delete_files);

    Preset::Type    type() const { return m_type; }
    std::string     name() const;
    std::string     section_name() const;
    const std::deque<Preset>& operator()() const { return m_presets; }

    void            add_default_preset(const std::vector<std::string> &keys, const Slic3r::StaticPrintConfig &defaults, const std::string &preset_name);

    void            load_presets(const std::string &dir_path, const std::string &subdir, PresetsConfigSubstitutions& substitutions, ForwardCompatibilitySubstitutionRule rule, std::function<void(Preset&)> preset_loaded_fn = nullptr, const PresetOrigin &load_origin = PresetOrigin(), bool read_only = false);

    void            update_user_presets_directory(const std::string& dir_path, const std::string& type);
    void            save_user_presets(const std::string& dir_path, const std::string& type, std::map<std::string, std::string>& need_to_delete_list);
    bool            load_user_preset(std::string name, std::map<std::string, std::string> preset_values, PresetsConfigSubstitutions& substitutions, ForwardCompatibilitySubstitutionRule rule, const PresetOrigin &load_origin = PresetOrigin(PresetOrigin::Kind::User));
    void            update_after_user_presets_loaded();
    int  get_user_presets(PresetBundle *preset_bundle, std::vector<Preset> &result_presets);
    void set_sync_info_and_save(std::string name, std::string setting_id, std::string syncinfo, long long update_time, const std::string& user_id);
    bool need_sync(std::string name, std::string setting_id, long long update_time);

    Preset* get_preset_differed_for_save(Preset& preset);
    int get_differed_values_to_update(Preset& preset, std::map<std::string, std::string>& key_values);

    void load_project_embedded_presets(std::vector<Preset*>& project_presets, const std::string& type, PresetsConfigSubstitutions& substitutions, ForwardCompatibilitySubstitutionRule rule);
    std::vector<Preset*> get_project_embedded_presets();
    bool reset_project_embedded_presets();

    Preset&         load_preset(const std::string &path, const std::string &name, const DynamicPrintConfig &config, bool select = true, Semver file_version = Semver());
    Preset&         load_preset(const std::string &path, const std::string &name, DynamicPrintConfig &&config, bool select = true, Semver file_version = Semver());

    bool clone_presets(std::vector<Preset const *> const &presets, std::vector<std::string> &failures, std::function<void(Preset &, Preset::Type &)> modifier, bool force_rewritten = false);
    bool clone_presets_for_printer(
        std::vector<Preset const *> const &templates, std::vector<std::string> &failures, std::string const &printer, std::function <std::string(std::string)> create_filament_id, bool force_rewritten = false);
    bool clone_presets_for_filament(Preset const *const &     preset,
                                    std::vector<std::string> &failures,
                                    std::string const &       filament_name,
                                    std::string const &       filament_id,
                                    const DynamicConfig &     dynamic_config,
                                    const std::string &       compatible_printers,
                                    bool                      force_rewritten = false);

    std::map<std::string, std::vector<Preset const *>> get_filament_presets() const;

    enum class LoadAndSelect {
        Never,
        Always,
        OnlyIfModified,
    };
    std::pair<Preset*, bool> load_external_preset(
        const std::string           &path,
        const std::string           &name,
        const std::string           &original_name,
        const DynamicPrintConfig    &config,
        const std::set<std::string> &different_settings_list,
        LoadAndSelect                select = LoadAndSelect::Always,
        const Semver                file_version = Semver(),
        const std::string           filament_id = std::string());

    void            save_current_preset(const std::string &new_name, bool detach = false, bool save_to_project = false, Preset* _curr_preset = nullptr);
    std::string     add_detached_preset(const std::string &name_base, DynamicPrintConfig config,
                                        const std::string &filament_id = std::string());

    bool            delete_current_preset();
    bool            delete_preset(const std::string& name, bool force = false);

    void check_and_fix_syncinfo(Preset& preset, const std::string& user_id);

    void            set_default_suppressed(bool default_suppressed);
    bool            is_default_suppressed() const { return m_default_suppressed; }

    Preset&         select_preset(size_t idx);
    Preset&         get_selected_preset() {
        if ((m_idx_selected < 0) || (m_idx_selected >= m_presets.size())) {
            select_preset(first_visible_idx());
        }
        return m_presets[m_idx_selected];
    }
    const Preset&   get_selected_preset() const { return m_presets[m_idx_selected]; }
    size_t          get_selected_idx()    const { return m_idx_selected; }
    std::string     get_selected_preset_name() const {
        if (m_idx_selected == size_t(-1) || m_idx_selected >= m_presets.size())
            return std::string();
        return this->get_selected_preset().name;
    }
    const Preset*   get_selected_preset_parent() const;
	const Preset*	get_preset_parent(const Preset& child) const;
	const Preset*	get_preset_base(const Preset& child) const;
    Preset&         get_edited_preset()         { return m_edited_preset; }
    const Preset&   get_edited_preset() const   { return m_edited_preset; }

    const Preset& get_selected_preset_base() const { return *get_preset_base(m_presets[m_idx_selected]); }

    PresetWithVendorProfile get_preset_with_vendor_profile(const Preset &preset) const;
    PresetWithVendorProfile get_edited_preset_with_vendor_profile() const { return this->get_preset_with_vendor_profile(this->get_edited_preset()); }

    const std::string& 		get_preset_name_by_alias(const std::string& alias) const;
	const std::string*		get_preset_name_renamed(const std::string &old_name) const;
    bool                    is_alias_exist(const std::string &alias, Preset* preset = nullptr);
    void                    set_printer_hold_alias(const std::string &alias, Preset &preset, bool remove = false);

	const std::deque<Preset>&	get_presets() const	{ return m_presets; }
    size_t                      get_idx_selected()	{ return m_idx_selected; }
	static const std::string&	get_suffix_modified();

	Preset&			default_preset(size_t idx = 0)		 { assert(idx < m_num_default_presets); return m_presets[idx]; }
	const Preset&   default_preset(size_t idx = 0) const { assert(idx < m_num_default_presets); return m_presets[idx]; }
	virtual const Preset& default_preset_for(const DynamicPrintConfig & ) const { return this->default_preset(); }
    Preset&         preset(size_t idx, bool real = false) {
        if (real) return m_presets[idx];
        return (idx == m_idx_selected) ? m_edited_preset : m_presets[idx];
    }
    const Preset&   preset(size_t idx) const    { return const_cast<PresetCollection*>(this)->preset(idx); }
    void            discard_current_changes() {
        m_presets[m_idx_selected].reset_dirty();
        m_edited_preset = m_presets[m_idx_selected];
    }

    Preset* find_preset(const std::string& name, bool first_visible_if_not_found = false, bool real = false, bool only_from_library = false);
    const Preset* find_preset(const std::string& name, bool first_visible_if_not_found = false) const
    {
        return const_cast<PresetCollection*>(this)->find_preset(name, first_visible_if_not_found);
    }
    Preset* find_preset2(const std::string& name, bool auto_match = true);
    const Preset* find_preset2(const std::string& name, bool auto_match = true) const
    {
        return const_cast<PresetCollection*>(this)->find_preset2(name, auto_match);
    }
    
    size_t first_visible_idx() const;
    size_t first_visible_idx_by_type(const std::string& filament_type) const;
    std::string filament_id_by_type(const std::string& filament_type) const;
    template<typename PreferedCondition> size_t first_compatible_idx(PreferedCondition prefered_condition) const
    {
        size_t i             = m_default_suppressed ? m_num_default_presets : 0;
        size_t n             = this->m_presets.size();
        size_t i_compatible  = n;
        int    match_quality = -1;
        for (; i < n; ++i)
            if (m_presets[i].is_compatible && m_presets[i].is_visible) {
                int this_match_quality = prefered_condition(m_presets[i]);
                if (this_match_quality > match_quality) {
                    if (match_quality == std::numeric_limits<int>::max())
                        return i;
                    i_compatible  = i;
                    match_quality = this_match_quality;
                }
            }
        return (i_compatible == n) ?
                    0 :
                    i_compatible;
}
    size_t          first_compatible_idx() const { return this->first_compatible_idx([](const Preset&) -> int { return 0; }); }

    std::vector<std::string> diameters_of_selected_printer();

    Preset&         first_visible()             { return this->preset(this->first_visible_idx()); }
    const Preset&   first_visible() const       { return this->preset(this->first_visible_idx()); }
    Preset&         first_compatible()          { return this->preset(this->first_compatible_idx()); }
    template<typename PreferedCondition>
    Preset&         first_compatible(PreferedCondition prefered_condition) { return this->preset(this->first_compatible_idx(prefered_condition)); }
    const Preset&   first_compatible() const    { return this->preset(this->first_compatible_idx()); }

    size_t          size() const                { return m_presets.size(); }
    bool            has_defaults_only() const   { return m_presets.size() <= m_num_default_presets; }
    int             error_count() const         { return m_errors; }

    template<typename PreferedCondition>
    void            update_compatible(const PresetWithVendorProfile &active_printer, const PresetWithVendorProfile *active_print, PresetSelectCompatibleType select_other_if_incompatible, PreferedCondition prefered_condition)
    {
        if (this->update_compatible_internal(active_printer, active_print, select_other_if_incompatible) == (size_t)-1) {
            size_t index = this->first_compatible_idx(prefered_condition);
            this->select_preset(index);
        }
    }
    void            update_compatible(const PresetWithVendorProfile &active_printer, const PresetWithVendorProfile *active_print, PresetSelectCompatibleType select_other_if_incompatible)
        { this->update_compatible(active_printer, active_print, select_other_if_incompatible, [](const Preset&) -> int { return 0; }); }

    size_t          num_visible() const { return std::count_if(m_presets.begin(), m_presets.end(), [](const Preset &preset){return preset.is_visible;}); }

    bool                        current_is_dirty() const
        { return is_dirty(&this->get_edited_preset(), &this->get_selected_preset()); }
    std::vector<std::string>    current_dirty_options(const bool deep_compare = false) const
        { return dirty_options(&this->get_edited_preset(), &this->get_selected_preset(), deep_compare); }
    std::vector<std::string>    current_different_from_parent_options(const bool deep_compare = false) const
        { return dirty_options(&this->get_edited_preset(), this->get_selected_preset_parent(), deep_compare); }

    bool                        saved_is_dirty() const
        { return is_dirty(&this->get_edited_preset(), &m_saved_preset); }
    void                        update_saved_preset_from_current_preset() { m_saved_preset = m_edited_preset; }

    std::vector<std::string>    system_preset_names() const;

    bool            update_dirty();

    bool            select_preset_by_name(const std::string &name, bool force);
    bool is_base_preset(const Preset &preset) const { return preset.is_system || (preset.is_user() && preset.inherits().empty()); }

    std::string     path_from_name(const std::string &new_name, bool detach = false) const;
    std::string     path_for_preset(const Preset & preset) const;

    std::string     get_preset_alias(Preset &preset, bool force = false);

    size_t num_default_presets() { return m_num_default_presets; }

protected:
    PresetCollection() = default;
    PresetCollection(const PresetCollection &other) = delete;
    PresetCollection& operator=(const PresetCollection &other);
    void            update_vendor_ptrs_after_copy(const VendorMap &vendors);

    bool            select_preset_by_name_strict(const std::string &name);

    std::vector<std::vector<std::string>> merge_presets(const std::vector<PresetCollection*> &others, const VendorMap &new_vendors);

	void 			update_map_alias_to_profile_name();

    void 			update_map_system_profile_renamed();

    void 			update_library_profile_excluded_from();


    void            set_custom_preset_alias(Preset &preset);

private:
    struct PresetFilesOnDisk
    {
        std::optional<std::string> json;
        std::optional<std::string> info;

        static PresetFilesOnDisk read(const boost::filesystem::path &file);
        bool operator==(const PresetFilesOnDisk &rhs) const { return json == rhs.json && info == rhs.info; }
    };

    struct UserPresetLoad
    {
        Preset      preset;
        bool        install { false };
        bool        complete { false };
        bool        save_compatible_printers { false };
        bool        discard_file { false };
        std::string info_file;
        PresetFilesOnDisk on_disk;
        std::vector<std::string>   errors;
        PresetsConfigSubstitutions substitutions;
    };

    UserPresetLoad  resolve_user_preset(const boost::filesystem::path &file, const std::string &canonical_name,
                                        const PresetOrigin &load_origin, ForwardCompatibilitySubstitutionRule substitution_rule,
                                        const std::string &extruder_id_name, const std::string &extruder_variant_name,
                                        std::set<std::string> *key_set1, std::set<std::string> *key_set2) const;

    void            commit_user_preset(UserPresetLoad &&loaded, std::deque<Preset> &presets_loaded,
                                       PresetsConfigSubstitutions &substitutions,
                                       const std::function<void(Preset&)> &preset_loaded_fn,
                                       bool read_only);

    std::string canonical_preset_name(const std::string &name, const PresetOrigin &load_origin = PresetOrigin()) const;

    static bool filament_preset_less(const Preset &a, const Preset &b) {
        bool a_generic = boost::starts_with(a.name, GENERIC_PREFIX);
        bool b_generic = boost::starts_with(b.name, GENERIC_PREFIX);
        if (a_generic != b_generic)
            return a_generic;
        return a.name < b.name;
    }

    Preset& append_preset(std::string &&path, const std::string &name, DynamicPrintConfig &&config);

    void sort_presets() {
        if (m_type == Preset::TYPE_FILAMENT)
            std::sort(m_presets.begin() + m_num_default_presets, m_presets.end(), filament_preset_less);
        else
            std::sort(m_presets.begin() + m_num_default_presets, m_presets.end());
    }

    std::deque<Preset>::iterator find_preset_internal(const std::string &name, bool from_orca_lib_only = false)
    {
        auto it = Slic3r::lower_bound_by_predicate(m_presets.begin() + m_num_default_presets, m_presets.end(),
            [&name, this](const auto& l) {
                if (m_type == Preset::TYPE_FILAMENT) {
                    bool l_generic = boost::starts_with(l.name, GENERIC_PREFIX);
                    bool name_generic = boost::starts_with(name, GENERIC_PREFIX);
                    if (l_generic && !name_generic) return true;
                    if (!l_generic && name_generic) return false;
                }
                return l.name < name;
            });
        if (it == m_presets.end() || it->name != name) {
            for (size_t i = 0; i < m_num_default_presets; ++ i)
                if (m_presets[i].name == name && (!from_orca_lib_only || m_presets[i].m_from_orca_filament_lib)) {
                    it = m_presets.begin() + i;
                    break;
                }
        }
        return it;
    }
    std::deque<Preset>::const_iterator find_preset_internal(const std::string &name) const
        { return const_cast<PresetCollection*>(this)->find_preset_internal(name); }
    std::deque<Preset>::iterator 	   find_preset_renamed(const std::string &name) {
    	auto it_renamed = m_map_system_profile_renamed.find(name);
    	auto it = (it_renamed == m_map_system_profile_renamed.end()) ? m_presets.end() : this->find_preset_internal(it_renamed->second);
    	assert((it_renamed == m_map_system_profile_renamed.end()) || (it != m_presets.end() && it->name == it_renamed->second));
    	return it;
    }
    std::deque<Preset>::const_iterator find_preset_renamed(const std::string &name) const
        { return const_cast<PresetCollection*>(this)->find_preset_renamed(name); }

    size_t update_compatible_internal(const PresetWithVendorProfile &active_printer, const PresetWithVendorProfile *active_print, PresetSelectCompatibleType unselect_if_incompatible);
public:
    static bool                     is_dirty(const Preset *edited, const Preset *reference);
    static std::vector<std::string> dirty_options(const Preset *edited, const Preset *reference, const bool deep_compare = false);
    static std::vector<std::string> dirty_options_without_option_list(const Preset *edited, const Preset *reference, const std::set<std::string>& option_ignore_list, const bool deep_compare = false);
private:
    Preset::Type            m_type;
    std::deque<Preset>      m_presets;
    std::map<std::string, std::vector<std::string>> m_map_alias_to_profile_name;
    std::unordered_map<std::string, std::unordered_set<std::string>> m_printer_hold_alias;
    std::map<std::string, std::string> m_map_system_profile_renamed;
    Preset                  m_edited_preset;
    Preset                  m_saved_preset;

    size_t                  m_idx_selected;
    bool                    m_default_suppressed  = true;
    size_t                  m_num_default_presets = 0;

    std::string             m_dir_path;

    friend class PresetBundle;

    std::recursive_mutex          m_mutex;

    int m_errors = 0;
};

class PrinterPresetCollection : public PresetCollection
{
public:
    PrinterPresetCollection(Preset::Type type, const std::vector<std::string> &keys, const Slic3r::StaticPrintConfig &defaults, const std::string &default_name = "Default Printer") :
		PresetCollection(type, keys, defaults, default_name) {}

    const Preset&   default_preset_for(const DynamicPrintConfig &config) const override;

    const Preset*   find_system_preset_by_model_and_variant(const std::string &model_id, const std::string &variant) const;
    const Preset*   find_custom_preset_by_model_and_variant(const std::string &model_id, const std::string &variant) const;

    bool            only_default_printers() const;
private:
    PrinterPresetCollection() = default;
    PrinterPresetCollection(const PrinterPresetCollection &other) = delete;
    PrinterPresetCollection& operator=(const PrinterPresetCollection &other) = default;

    friend class PresetBundle;
};

namespace PresetUtils {
	const VendorProfile::PrinterModel* system_printer_model(const Preset &preset);
    std::string system_printer_bed_model(const Preset& preset);
    std::string system_printer_bed_texture(const Preset& preset);
    std::string system_printer_hotend_model(const Preset& preset);
}

class PhysicalPrinter
{
public:
    PhysicalPrinter(const std::string& name, const DynamicPrintConfig &default_config);
    PhysicalPrinter(const std::string& name, const DynamicPrintConfig &default_config, const Preset& preset);
    void set_name(const std::string &name);

    std::string         name;
    std::string         file;
    DynamicPrintConfig  config;
    std::set<std::string> preset_names;

    bool                loaded = false;

    static std::string  separator();
    static const std::vector<std::string>&  printer_options();
    static const std::vector<std::string>&  print_host_options();
    static std::vector<std::string>         presets_with_print_host_information(const PrinterPresetCollection& printer_presets);
    static bool has_print_host_information(const DynamicPrintConfig& config);

    const std::set<std::string>&            get_preset_names() const;

    void                update_preset_names_in_config();

    void                save(DynamicPrintConfig* parent_config);
    void                save(const std::string& file_name_from, const std::string& file_name_to);

    void                update_from_preset(const Preset& preset);
    void                update_from_config(const DynamicPrintConfig &new_config);

    bool                add_preset(const std::string& preset_name);
    bool                delete_preset(const std::string& preset_name);
    void                reset_presets();

    static PrinterTechnology printer_technology(const DynamicPrintConfig& cfg) {
        auto* opt = cfg.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
        return (opt == nullptr) ? ptFFF : opt->value;
    }
    PrinterTechnology   printer_technology() const { return printer_technology(this->config); }

    bool                operator<(const PhysicalPrinter& other) const { return this->name < other.name; }

    std::string         get_full_name(std::string preset_name) const;

    static std::string  get_short_name(std::string full_name);

    static std::string  get_preset_name(std::string full_name);

protected:
    friend class        PhysicalPrinterCollection;
};


class PhysicalPrinterCollection
{
public:
    PhysicalPrinterCollection(const std::vector<std::string>& keys);

    typedef std::deque<PhysicalPrinter>::iterator Iterator;
    typedef std::deque<PhysicalPrinter>::const_iterator ConstIterator;
    Iterator        begin() { return m_printers.begin(); }
    ConstIterator   begin() const { return m_printers.cbegin(); }
    ConstIterator   cbegin() const { return m_printers.cbegin(); }
    Iterator        end() { return m_printers.end(); }
    ConstIterator   end() const { return m_printers.cend(); }
    ConstIterator   cend() const { return m_printers.cend(); }

    bool            empty() const {return m_printers.empty(); }

    void            reset(bool delete_files) {};

    const std::deque<PhysicalPrinter>& operator()() const { return m_printers; }

    void            load_printers(const std::string& dir_path, const std::string& subdir, PresetsConfigSubstitutions& substitutions, ForwardCompatibilitySubstitutionRule rule);
    void            load_printers_from_presets(PrinterPresetCollection &printer_presets);
    void            load_printer(const std::string& path, const std::string& name, DynamicPrintConfig&& config, bool select, bool save=false);

    void            save_printer(PhysicalPrinter& printer, const std::string& renamed_from = "");

    bool            delete_printer(const std::string& name);
    bool            delete_selected_printer();
    bool            delete_preset_from_printers(const std::string& preset_name);

    std::vector<std::string> get_printers_with_preset( const std::string &preset_name);
    std::vector<std::string> get_printers_with_only_preset( const std::string &preset_name);

    PhysicalPrinter&        get_selected_printer() { return m_printers[m_idx_selected]; }
    const PhysicalPrinter&  get_selected_printer() const { return m_printers[m_idx_selected]; }

    size_t                  get_selected_idx()    const { return m_idx_selected; }
    std::string             get_selected_printer_name() const { return (m_idx_selected == size_t(-1)) ? std::string() : this->get_selected_printer().name; }
    DynamicPrintConfig*     get_selected_printer_config() { return (m_idx_selected == size_t(-1)) ? nullptr : &(this->get_selected_printer().config); }
    PrinterTechnology       get_selected_printer_technology() { return (m_idx_selected == size_t(-1)) ? PrinterTechnology::ptAny : this->get_selected_printer().printer_technology(); }

    std::string     get_selected_full_printer_name() const;
    std::string     get_selected_printer_preset_name() const { return (m_idx_selected == size_t(-1)) ? std::string() : m_selected_preset; }

    void select_printer(const std::string& full_name);
    void select_printer(const PhysicalPrinter& printer);
    void select_printer(const std::string& printer_name, const std::string& preset_name);
    bool has_selection() const;
    void unselect_printer() ;
    bool is_selected(ConstIterator it, const std::string &preset_name) const;

    PhysicalPrinter& printer(size_t idx) { return m_printers[idx]; }
    const PhysicalPrinter& printer(size_t idx) const { return const_cast<PhysicalPrinterCollection*>(this)->printer(idx); }

    PhysicalPrinter* find_printer(const std::string& name, bool case_sensitive_search = true);
    const PhysicalPrinter* find_printer(const std::string& name, bool case_sensitive_search = true) const
    {
        return const_cast<PhysicalPrinterCollection*>(this)->find_printer(name, case_sensitive_search);
    }

    std::string     path_from_name(const std::string& new_name) const;

    const DynamicPrintConfig& default_config() const { return m_default_config; }

private:
    friend class PresetBundle;
    PhysicalPrinterCollection() = default;
    PhysicalPrinterCollection& operator=(const PhysicalPrinterCollection& other) = default;

    std::deque<PhysicalPrinter>::iterator find_printer_internal(const std::string& name, bool case_sensitive_search = true);
    std::deque<PhysicalPrinter>::const_iterator find_printer_internal(const std::string& name, bool case_sensitive_search = true) const
    {
        return const_cast<PhysicalPrinterCollection*>(this)->find_printer_internal(name);
    }

    std::deque<PhysicalPrinter> m_printers;

    DynamicPrintConfig          m_default_config;

    size_t                      m_idx_selected = size_t(-1);
    std::string                 m_selected_preset;

    std::string                 m_dir_path;
};


}

#endif
