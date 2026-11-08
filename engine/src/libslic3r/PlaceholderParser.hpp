#ifndef slic3r_PlaceholderParser_hpp_
#define slic3r_PlaceholderParser_hpp_

#include "Config.hpp"
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r { class DynamicPrintConfig; }

namespace Slic3r {

class PlaceholderParser
{
public:
    struct ContextData {
        std::mt19937                    rng;
        std::unique_ptr<DynamicConfig>  global_config;
        std::set<std::string>           inactive_global_variables;
    };

    static inline bool check_inactive_branches = false;

    PlaceholderParser(const DynamicConfig *external_config = nullptr);
    
    void clear_config() { m_config.clear(); }
    std::vector<std::string> config_diff(const DynamicPrintConfig &rhs);
    bool apply_config(const DynamicPrintConfig &config);
    void apply_config(DynamicPrintConfig &&config);
    void apply_only(const DynamicPrintConfig &config, const std::vector<std::string> &keys);
    void apply_env_variables();

    void set(const std::string &key, const std::string &value)  { this->set(key, new ConfigOptionString(value)); }
    void set(const std::string &key, std::string_view value)    { this->set(key, new ConfigOptionString(std::string(value))); }
    void set(const std::string &key, const char *value)         { this->set(key, new ConfigOptionString(value)); }
    void set(const std::string &key, int value)                 { this->set(key, new ConfigOptionInt(value)); }
    void set(const std::string &key, unsigned int value)        { this->set(key, int(value)); }
    void set(const std::string &key, bool value)                { this->set(key, new ConfigOptionBool(value)); }
    void set(const std::string &key, double value)              { this->set(key, new ConfigOptionFloat(value)); }
    void set(const std::string &key, const std::vector<std::string> &values) { this->set(key, new ConfigOptionStrings(values)); }
    void set(const std::string &key, ConfigOption *opt)         { m_config.set_key_value(key, opt); }
	DynamicConfig&			config_writable()					{ return m_config; }
	const DynamicConfig&    config() const                      { return m_config; }
    const ConfigOption*     option(const std::string &key) const { return m_config.option(key); }
	const DynamicConfig*	external_config() const  			{ return m_external_config; }

    std::string process(const std::string &templ, unsigned int current_extruder_id, const DynamicConfig *config_override, DynamicConfig *config_outputs, ContextData *context) const;
    std::string process(const std::string &templ, unsigned int current_extruder_id = 0, const DynamicConfig *config_override = nullptr, ContextData *context = nullptr) const
        { return this->process(templ, current_extruder_id, config_override, nullptr , context); }

    static bool evaluate_boolean_expression(const std::string &templ, const DynamicConfig &config, const DynamicConfig *config_override = nullptr);

    static void update_timestamp(DynamicConfig &config);
    void update_timestamp() { update_timestamp(m_config); }

    static void update_user_name(DynamicConfig &config);
    void update_user_name() { update_user_name(m_config); }

private:
    DynamicConfig 			 m_config;
    const DynamicConfig 	*m_external_config;
};

}

#endif
