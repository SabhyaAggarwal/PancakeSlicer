#include "PluginResolver.hpp"

#include "PluginManager.hpp"
#include "../Utils/Http.hpp"
#include "../Utils/OrcaCloudServiceAgent.hpp"
#include "../Utils/NetworkAgent.hpp"
#include "../GUI/GUI.hpp"
#include "../GUI/GUI_App.hpp"
#include "../GUI/I18N.hpp"
#include "../GUI/Plater.hpp"
#include "../GUI/NotificationManager.hpp"

#include <boost/log/trivial.hpp>
#include <cstddef>
#include <libslic3r/Config.hpp>
#include "libslic3r/Preset.hpp"
#include <libslic3r/PresetBundle.hpp>
#include <memory>
#include <string>
#include "slic3r/plugin/PythonPluginInterface.hpp"
#include <utility>
#include <vector>
#include <wx/utils.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>
#include <unordered_map>
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/plugin/PluginDescriptor.hpp"

namespace Slic3r {

namespace {

std::string find_option_for_capability(Preset::Type type,
                                       const Preset& preset,
                                       const PluginCapabilityRef& ref,
                                       PluginCapabilityType capability_type = PluginCapabilityType::Unknown)
{
    if (type != Preset::TYPE_PRINT && type != Preset::TYPE_PRINTER && type != Preset::TYPE_FILAMENT)
        return {};

    const ConfigDef* def = preset.config.def();
    if (def == nullptr)
        return {};

    const std::string expected_type = plugin_capability_type_to_string(capability_type);
    const auto matches_ref = [&ref](const std::string& value) {
        return value == ref.capability_name;
    };

    for (const std::string& field : preset.config.keys()) {
        const ConfigOptionDef* opt_def = def->get(field);
        if (opt_def == nullptr || !opt_def->is_plugin_backed() ||
            (capability_type != PluginCapabilityType::Unknown && opt_def->plugin_type != expected_type))
            continue;

        const ConfigOption* option = preset.config.option(field);
        if (option == nullptr)
            continue;

        if (const auto* string_option = dynamic_cast<const ConfigOptionString*>(option)) {
            if (string_option->value == ref.capability_name)
                return field;
            continue;
        }

        if (const auto* vector_option = dynamic_cast<const ConfigOptionVectorBase*>(option)) {
            const std::vector<std::string> values = vector_option->vserialize();
            if (std::any_of(values.begin(), values.end(), matches_ref))
                return field;
        }
    }

    if (type == Preset::Type::TYPE_PRINTER && preset.config.has("printer_agent")) {
        const ConfigOptionDef* agent_def = def->get("printer_agent");
        if (agent_def != nullptr && agent_def->is_plugin_backed() &&
            (capability_type == PluginCapabilityType::Unknown || agent_def->plugin_type == expected_type))
        return "printer_agent";
    }

    return {};
}

}
static std::map<Preset::Type, std::unordered_map<std::string, MissingPlugin>> s_missing;
static std::mutex s_missing_mutex;
static std::map<Preset::Type, std::unordered_map<std::string, MissingPlugin>> s_inactive;
static std::map<Preset::Type, std::unordered_map<std::string, MissingPlugin>> s_broken;

static bool is_tracked_type(Preset::Type type)
{
    return type == Preset::TYPE_PRINT || type == Preset::TYPE_PRINTER || type == Preset::TYPE_FILAMENT;
}

std::vector<PluginCapabilityRef> referenced_capabilities(Preset::Type type, const Preset& preset)
{
    if (!is_tracked_type(type))
        return {};

    const auto* manifest = dynamic_cast<const ConfigOptionStrings*>(preset.config.option("plugins"));
    if (manifest == nullptr)
        return {};

    std::vector<PluginCapabilityRef> refs;
    for (const std::string& entry : manifest->values) {
        const auto ref = parse_capability_ref(entry);
        if (!ref)
            continue;
        if (find_option_for_capability(type, preset, *ref).empty())
            continue;
        refs.push_back(*ref);
    }
    return refs;
}

namespace {

void collect_capabilities_in_use(Preset::Type type, const Preset& preset, std::vector<PluginCapabilityId>& out)
{
    for (const PluginCapabilityRef& ref : referenced_capabilities(type, preset)) {
        const std::string key = ref.uuid.empty() ? ref.name : ref.uuid;
        if (key.empty())
            continue;

        PluginDescriptor descriptor;
        if (!PluginManager::instance().try_get_plugin_descriptor(key, descriptor))
            continue;

        for (const auto& capability : PluginManager::instance().get_plugin_capabilities(descriptor.plugin_key, PluginCapabilityType::Unknown, false))
            if (capability && capability->type() != PluginCapabilityType::Unknown &&
                capability->name() == ref.capability_name &&
                !find_option_for_capability(type, preset, ref, capability->type()).empty())
                out.push_back(capability->identity());
    }
}

}

std::vector<PluginCapabilityId> capabilities_in_use(const PresetBundle& preset_bundle, Preset::Type type)
{
    if (!is_tracked_type(type))
        return {};

    std::vector<PluginCapabilityId> result;
    if (type == Preset::TYPE_PRINT) {
        collect_capabilities_in_use(type, preset_bundle.prints.get_edited_preset(), result);
    } else if (type == Preset::TYPE_PRINTER) {
        collect_capabilities_in_use(type, preset_bundle.printers.get_edited_preset(), result);
    } else {
        for (const std::string& filament_name : preset_bundle.filament_presets)
            if (const Preset* filament = preset_bundle.filaments.find_preset(filament_name))
                collect_capabilities_in_use(type, *filament, result);
    }

    std::sort(result.begin(), result.end(), [](const PluginCapabilityId& a, const PluginCapabilityId& b) {
        return std::tie(a.plugin_key, a.name, a.type) < std::tie(b.plugin_key, b.name, b.type);
    });
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::vector<PluginCapabilityId> capabilities_in_use(Preset::Type type, const Preset& preset)
{
    std::vector<PluginCapabilityId> result;
    if (!is_tracked_type(type))
        return result;

    collect_capabilities_in_use(type, preset, result);
    std::sort(result.begin(), result.end(), [](const PluginCapabilityId& a, const PluginCapabilityId& b) {
        return std::tie(a.plugin_key, a.name, a.type) < std::tie(b.plugin_key, b.name, b.type);
    });
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

static std::string resolve_cloud_base_url()
{
    std::string cloud_base_url = "https://cloud.orcaslicer.com";
    if (auto agent = GUI::wxGetApp().getAgent()) {
        if (auto orca_agent = std::dynamic_pointer_cast<OrcaCloudServiceAgent>(agent->get_cloud_agent())) {
            if (!orca_agent->get_cloud_base_url().empty())
                cloud_base_url = orca_agent->get_cloud_base_url();
        }
    }
    return cloud_base_url;
}

std::string create_full_ref(const PluginCapabilityRef& ref) { return ref.name + ';' + ref.uuid + ';' + ref.capability_name; }

std::string resolve_recovery_url(const PluginCapabilityRef& ref)
{
    return resolve_cloud_base_url() + "/app/plugins/plugin-hub?search=" + Http::url_encode(ref.name);
}

static std::pair<bool, bool> loaded_capability_state(const std::string& plugin_key, const PluginCapabilityRef& ref)
{
    PluginManager& mgr = PluginManager::instance();

    if (!mgr.is_plugin_loaded(plugin_key))
        return {false, false};

    const auto capability = mgr.get_plugin_capability({PluginCapabilityType::Unknown, ref.capability_name, plugin_key},
                                                      false);
    if (!capability)
        return {false, false};

    return {true, capability->is_enabled()};
}

void refresh_missing_plugins(const PresetBundle& preset_bundle)
{
    const auto manifest_of = [](const Preset& preset) { return dynamic_cast<const ConfigOptionStrings*>(preset.config.option("plugins")); };
    const Preset& print_preset = preset_bundle.prints.get_edited_preset();
    refresh_missing_plugins(Preset::TYPE_PRINT, manifest_of(print_preset), &print_preset);
    const Preset& printer_preset = preset_bundle.printers.get_edited_preset();
    refresh_missing_plugins(Preset::TYPE_PRINTER, manifest_of(printer_preset), &printer_preset);

    ConfigOptionStrings filament_manifest;
    for (const std::string& filament_name : preset_bundle.filament_presets) {
        const Preset* filament = preset_bundle.filaments.find_preset(filament_name);
        if (!filament)
            continue;
        if (const auto* opt = dynamic_cast<const ConfigOptionStrings*>(filament->config.option("plugins")))
            filament_manifest.values.insert(filament_manifest.values.end(), opt->values.begin(), opt->values.end());
    }
    refresh_missing_plugins(Preset::TYPE_FILAMENT, &filament_manifest);
}

void refresh_missing_plugins(Preset::Type type, const ConfigOptionStrings* manifest, const Preset* preset)
{
    if (!is_tracked_type(type))
        return;

    std::lock_guard<std::mutex> lock(s_missing_mutex);
    auto& missing_set  = s_missing[type];
    auto& inactive_set = s_inactive[type];
    auto& broken_set   = s_broken[type];
    missing_set.clear();
    inactive_set.clear();
    broken_set.clear();
    if (manifest == nullptr)
        return;

    PluginManager& mgr = PluginManager::instance();
    for (const std::string& entry : manifest->values) {
        const auto ref = parse_capability_ref(entry);
        if (!ref)
            continue;

        const std::string key = ref->uuid.empty() ? ref->name : ref->uuid;
        if (key.empty())
            continue;

        PluginDescriptor descriptor;
        const bool in_catalog = mgr.try_get_plugin_descriptor(key, descriptor);
        const bool installed  = in_catalog && descriptor.has_local_package();
        const bool loaded     = installed && mgr.is_plugin_loaded(descriptor.plugin_key);
        const auto cap_state  = installed ? loaded_capability_state(descriptor.plugin_key, *ref)
                                          : std::pair<bool, bool>{false, false};
        const bool cap_present = cap_state.first;
        const bool cap_enabled = cap_state.second;
        if (cap_enabled)
            continue;

        std::string opt = preset != nullptr ? find_option_for_capability(type, *preset, *ref) : std::string();
        if (opt.empty())
            continue;

        if (!installed) {
            std::string recovery_url = ref->uuid.empty() ? resolve_recovery_url(*ref) : std::string();
            missing_set.emplace(entry, MissingPlugin{*ref, std::move(recovery_url), std::move(opt), type, PluginCapabilityType::Unknown});
        } else if (!loaded || cap_present) {
            inactive_set.emplace(entry, MissingPlugin{*ref, std::string(), std::move(opt), type, PluginCapabilityType::Unknown});
        } else {
            broken_set.emplace(entry, MissingPlugin{*ref, resolve_recovery_url(*ref), std::move(opt), type, PluginCapabilityType::Unknown});
        }
    }
}

std::vector<MissingPlugin> get_missing_cloud_plugins()
{
    std::vector<MissingPlugin> out;
    std::lock_guard<std::mutex> lock(s_missing_mutex);
    for (const auto& [type, set] : s_missing)
        for (const auto& [ref_str, missing] : set)
            if (!missing.ref.uuid.empty())
                out.push_back(missing);
    return out;
}

std::vector<MissingPlugin> get_missing_local_plugins()
{
    std::vector<MissingPlugin> out;
    std::lock_guard<std::mutex> lock(s_missing_mutex);
    for (const auto& [type, set] : s_missing)
        for (const auto& [ref_str, missing] : set)
            if (missing.ref.uuid.empty())
                out.push_back(missing);
    return out;
}

bool has_missing_plugins()
{
    std::lock_guard<std::mutex> lock(s_missing_mutex);
    for (const auto& [type, set] : s_missing)
        if (!set.empty())
            return true;
    return false;
}

std::vector<MissingPlugin> get_inactive_plugins()
{
    std::vector<MissingPlugin> out;
    std::lock_guard<std::mutex> lock(s_missing_mutex);
    for (const auto& [type, set] : s_inactive)
        for (const auto& [ref_str, plugin] : set)
            out.push_back(plugin);
    return out;
}

bool has_inactive_plugins()
{
    std::lock_guard<std::mutex> lock(s_missing_mutex);
    for (const auto& [type, set] : s_inactive)
        if (!set.empty())
            return true;
    return false;
}

std::vector<MissingPlugin> get_broken_plugins()
{
    std::vector<MissingPlugin> out;
    std::lock_guard<std::mutex> lock(s_missing_mutex);
    for (const auto& [type, set] : s_broken)
        for (const auto& [ref_str, plugin] : set)
            out.push_back(plugin);
    return out;
}

bool has_broken_plugins()
{
    std::lock_guard<std::mutex> lock(s_missing_mutex);
    for (const auto& [type, set] : s_broken)
        if (!set.empty())
            return true;
    return false;
}

static void report_install_failure(const std::string& message)
{
    GUI::wxGetApp().CallAfter([message]() {
        if (GUI::Plater* plater = GUI::wxGetApp().plater())
            plater->get_notification_manager()->push_notification(GUI::NotificationType::OrcaCloudAPIError,
                                                                  GUI::NotificationManager::NotificationLevel::ErrorNotificationLevel,
                                                                  _u8L("Plugin installation failed") + ": " + message);
    });
}

void resolve_missing_plugins(const std::vector<std::string>& refs, PluginInstallProgress progress)
{
    std::vector<std::string> uuids;
    for (const std::string& r : refs) {
        const auto ref = parse_capability_ref(r);
        if (ref && !ref->uuid.empty() && std::find(uuids.begin(), uuids.end(), ref->uuid) == uuids.end())
            uuids.push_back(ref->uuid);
    }
    if (uuids.empty()) {
        if (progress.on_finished)
            progress.on_finished();
        return;
    }

    std::thread worker([uuids, progress = std::move(progress)]() {
        PluginManager& mgr      = PluginManager::instance();
        const std::size_t total = uuids.size();
        for (std::size_t i = 0; i < total; ++i) {
            if (progress.is_cancelled && progress.is_cancelled())
                break;

            const std::string& uuid = uuids[i];

            std::string display_name = uuid;
            PluginDescriptor known;
            if (mgr.try_get_plugin_descriptor(uuid, known) && !known.name.empty())
                display_name = known.name;
            if (progress.on_plugin_begin)
                progress.on_plugin_begin(display_name, i, total);

            std::string error;
            if (!mgr.subscribe_and_install_cloud_plugin(uuid, error)) {
                report_install_failure(error.empty() ? uuid : (uuid + ": " + error));
                continue;
            }
            PluginDescriptor descriptor;
            if (!mgr.try_get_plugin_descriptor(uuid, descriptor)) {
                report_install_failure(uuid + ": installed plugin was not found in the catalog.");
                continue;
            }
            mgr.load_plugin(descriptor.plugin_key, false);
            if (!mgr.wait_for_plugin_load(descriptor.plugin_key, std::chrono::minutes(5), error) ||
                !mgr.is_plugin_loaded(descriptor.plugin_key)) {
                report_install_failure(descriptor.name + ": " + (error.empty() ? "plugin failed to load." : error));
            }
        }
        if (progress.on_finished)
            progress.on_finished();
    });
    worker.detach();
}

void resolve_inactive_plugins(const std::vector<std::string>& refs)
{
    PluginManager& mgr = PluginManager::instance();

    std::map<std::string, std::vector<std::string>> by_plugin;
    for (const std::string& r : refs) {
        const auto ref = parse_capability_ref(r);
        if (!ref)
            continue;
        const std::string key = ref->uuid.empty() ? ref->name : ref->uuid;
        PluginDescriptor descriptor;
        if (!mgr.try_get_plugin_descriptor(key, descriptor) || !descriptor.has_local_package())
            continue;
        by_plugin[descriptor.plugin_key].push_back(ref->capability_name);
    }
    if (by_plugin.empty())
        return;

    std::vector<std::pair<std::string, std::vector<std::string>>> work(by_plugin.begin(), by_plugin.end());
    std::thread([work = std::move(work)]() {
        PluginManager& mgr = PluginManager::instance();
        for (auto& [plugin_key, capabilities] : work) {
            mgr.load_plugin(plugin_key, false, capabilities);
            std::string error;
            mgr.wait_for_plugin_load(plugin_key, std::chrono::minutes(5), error);
        }
        GUI::wxGetApp().CallAfter([]() {
            if (GUI::Plater* plater = GUI::wxGetApp().plater())
                plater->revalidate_current_plate_if_plugins_missing();
        });
    }).detach();
}

void open_missing_plugins_on_cloud(const std::vector<std::string>& local_refs)
{
    if (local_refs.size() == 1) {
        if (const auto ref = parse_capability_ref(local_refs.front())) {
            wxLaunchDefaultBrowser(GUI::from_u8(resolve_recovery_url(*ref)), wxBROWSER_NEW_WINDOW);
            return;
        }
    }
    wxLaunchDefaultBrowser(GUI::from_u8(resolve_cloud_base_url() + "/app/plugins/plugin-hub"), wxBROWSER_NEW_WINDOW);
}

}
