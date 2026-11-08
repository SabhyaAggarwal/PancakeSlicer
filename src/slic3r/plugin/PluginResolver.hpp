#ifndef slic3r_PluginResolver_hpp_
#define slic3r_PluginResolver_hpp_

#include <libslic3r/Config.hpp>
#include <libslic3r/Preset.hpp>
#include <libslic3r/PresetBundle.hpp>
#include <slic3r/plugin/PythonPluginInterface.hpp>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Slic3r {

struct MissingPlugin
{
    PluginCapabilityRef ref;
    std::string recovery_url;
    std::string opt;
    Preset::Type opt_type;
    PluginCapabilityType type{PluginCapabilityType::Unknown};
};

void refresh_missing_plugins(Preset::Type type, const ConfigOptionStrings* manifest, const Preset* preset = nullptr);
void refresh_missing_plugins(const PresetBundle& preset_bundle);

std::vector<MissingPlugin> get_missing_cloud_plugins();
std::vector<MissingPlugin> get_missing_local_plugins();
bool                     has_missing_plugins();

std::vector<MissingPlugin> get_inactive_plugins();
bool                     has_inactive_plugins();

std::vector<MissingPlugin> get_broken_plugins();
bool                     has_broken_plugins();

struct PluginInstallProgress
{
    std::function<void(const std::string& name, std::size_t index, std::size_t total)> on_plugin_begin;
    std::function<bool()> is_cancelled;
    std::function<void()> on_finished;
};

void resolve_missing_plugins(const std::vector<std::string>& refs,
                             PluginInstallProgress progress = {});

void resolve_inactive_plugins(const std::vector<std::string>& refs);

void open_missing_plugins_on_cloud(const std::vector<std::string>& local_refs);

std::string create_full_ref(const PluginCapabilityRef& ref);
std::string resolve_recovery_url(const PluginCapabilityRef& ref);

std::vector<PluginCapabilityRef> referenced_capabilities(Preset::Type type, const Preset& preset);
std::vector<PluginCapabilityId> capabilities_in_use(Preset::Type type, const Preset& preset);

std::vector<PluginCapabilityId> capabilities_in_use(const PresetBundle& preset_bundle, Preset::Type type);

bool check_capability_in_use(const std::string& capability_refs);

}

#endif
