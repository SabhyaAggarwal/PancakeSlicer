#ifndef slic3r_PluginManager_hpp_
#define slic3r_PluginManager_hpp_

#include <atomic>
#include <boost/filesystem/path.hpp>

#include <algorithm>
#include <boost/log/trivial.hpp>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <libslic3r/Config.hpp>
#include <libslic3r/LifecycleEvents.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include "slic3r/plugin/PluginFsUtils.hpp"
#include <slic3r/plugin/PythonPluginInterface.hpp>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <pybind11/embed.h>

#include "CloudPluginService.hpp"
#include "PluginDescriptor.hpp"
#include "PluginLoader.hpp"
#include "PluginConfig.hpp"

namespace Slic3r {

class OrcaCloudServiceAgent;

struct Plugin
{
    PluginDescriptor descriptor;
    PyObject*        module = nullptr;
    std::string               module_name;
    std::vector<std::string>  plugin_sys_paths;
    std::vector<std::string>  plugin_modules;
    std::vector<std::shared_ptr<PluginCapabilityInterface>> capabilities;

    Plugin()                         = default;
    Plugin(const Plugin&)            = delete;
    Plugin& operator=(const Plugin&) = delete;

    Plugin(Plugin&& other) noexcept
        : descriptor(std::move(other.descriptor)),
          module(other.module),
          module_name(std::move(other.module_name)),
          plugin_sys_paths(std::move(other.plugin_sys_paths)),
          plugin_modules(std::move(other.plugin_modules)),
          capabilities(std::move(other.capabilities))
    {
        other.module = nullptr;
    }

    Plugin& operator=(Plugin&& other) noexcept
    {
        if (this != &other) {
            release_module();
            descriptor        = std::move(other.descriptor);
            module            = other.module;
            module_name       = std::move(other.module_name);
            plugin_sys_paths  = std::move(other.plugin_sys_paths);
            plugin_modules    = std::move(other.plugin_modules);
            capabilities      = std::move(other.capabilities);
            other.module      = nullptr;
        }
        return *this;
    }

    ~Plugin() { release_module(); }

    bool is_loaded() const { return module != nullptr; }

    void release_module();
};

class PluginManager
{
public:
    using PluginLifecycleCompleteFn = std::function<void(const std::string& )>;
    using CapabilityLifecycleFn     = std::function<void(const PluginCapabilityId&)>;

    static PluginManager& instance();

    ~PluginManager();

    bool initialize();

    void shutdown();

    void set_shutting_down();

    void discover_plugins(bool async = false, bool clear = false);
    void rescan_plugins();

    PluginConfig& get_config() { return m_config; }
    const PluginConfig& get_config() const { return m_config; }

    bool is_discovery_complete() const;
    bool is_discovery_in_progress() const;
    std::string get_discovery_error() const;
    bool wait_for_discovery(std::chrono::milliseconds timeout, std::string& error) const;


    std::vector<PluginDescriptor> get_plugin_descriptors(bool include_invalid = false) const;
    bool try_get_plugin_descriptor(const std::string& plugin_key, PluginDescriptor& out) const;
    bool try_get_valid_plugin_descriptor(const std::string& plugin_key, PluginDescriptor& out) const;
    std::vector<PluginDescriptor> get_missing_plugin_descriptors() const;
    void remove_missing_plugins(const std::vector<std::string>& plugin_keys);
    std::vector<std::string> get_enabled_plugin_keys() const;
    bool try_get_plugin_descriptor_for_capability(const std::string& capability_name,
                                                  PluginCapabilityType type,
                                                  PluginDescriptor& out) const;
    std::string get_storage_dir(const std::string& plugin_key) const;

    std::vector<std::shared_ptr<PluginCapabilityInterface>> get_plugin_capabilities(
        const std::string& plugin_key = "",
        PluginCapabilityType type     = PluginCapabilityType::Unknown,
        bool only_enabled             = true) const;
    std::shared_ptr<PluginCapabilityInterface> get_plugin_capability(const PluginCapabilityId& id, bool only_enabled = true) const;

    std::shared_ptr<PluginCapabilityInterface> get_plugin_capability(const std::string& capability_name,
                                                                     PluginCapabilityType type = PluginCapabilityType::Unknown,
                                                                     bool only_enabled         = true) const;

    bool get_install_state(const std::string& plugin_key, PluginInstallState& install_state);

    void load_plugin(const std::string& plugin_key, bool skip_deps = false, std::vector<std::string> capabilities_to_enable = {});
    bool unload_plugin(const std::string& plugin_key);
    void unload_all_plugins();
    void unload_cloud_plugins();
    bool is_plugin_loaded(const std::string& plugin_key) const;
    bool is_plugin_load_in_progress(const std::string& plugin_key) const;
    void wait_for_all_plugin_loads() const;
    bool wait_for_all_plugin_loads(std::chrono::milliseconds timeout) const;
    bool wait_for_plugin_load(const std::string& plugin_key, std::chrono::milliseconds timeout, std::string& error) const;
    bool cancel_plugin_load(const std::string& plugin_key);
    std::string get_plugin_load_error(const std::string& plugin_key) const;

    void set_capability_enabled(const PluginCapabilityId& id, bool enabled);

    void set_cloud_user(const std::string& user_id);

    void subscribe_on_load_callback(PluginLifecycleCompleteFn fn);
    void subscribe_on_unload_callback(PluginLifecycleCompleteFn fn);
    void subscribe_on_capability_load_callback(CapabilityLifecycleFn fn);
    void subscribe_on_capability_unload_callback(CapabilityLifecycleFn fn);

    void set_cloud_agent(std::shared_ptr<OrcaCloudServiceAgent> agent) { m_cloud_service.set_cloud_agent(std::move(agent)); }

    bool install_plugin(const boost::filesystem::path& filepath, std::string& error);
    bool install_plugin(const boost::filesystem::path& filepath, PluginDescriptor& plugin_descriptor, std::string& error);
    bool inspect_local_plugin_package(const boost::filesystem::path& filepath,
                                      PluginDescriptor& plugin_descriptor,
                                      bool& existing_installation,
                                      std::string& error) const;

    bool set_plugin_error(const std::string& plugin_key, std::string error);
    bool clear_plugin_error(const std::string& plugin_key);

    void fetch_plugins_from_cloud(std::vector<std::string>* out_not_found = nullptr, std::vector<std::string>* out_unauthorized = nullptr);
    void update_cloud_metadata(const std::vector<PluginDescriptor>& cloud_list);
    void clear_cloud_plugin_metadata();

    bool download_and_install_cloud_plugin(const std::string& plugin_key, const std::string& version, std::string& error);
    bool subscribe_and_install_cloud_plugin(const std::string& plugin_key, std::string& error);
    bool update_cloud_plugin(const std::string& plugin_key, std::string& error, std::string version = "");

    bool delete_plugin(const std::string& plugin_key, std::string& error);
    bool unsubscribe_cloud_plugin(const std::string& plugin_key, std::string& error);
    bool delete_and_unsubscribe_cloud_plugin(const std::string& plugin_key, std::string& error);

    ExecutionResult run_script_capability(const std::string& plugin_key, const std::string& capability_name, std::string& error);

    void dispatch_lifecycle_event(LifecycleEvent evt, const LifecycleEventContext& ctx);

private:
    PluginManager()                                = default;
    PluginManager(const PluginManager&)            = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    enum class CallbackType { Load, Unload };

    Plugin* find_plugin_locked(const std::string& plugin_key);
    const Plugin* find_plugin_locked(const std::string& plugin_key) const;

    void load_plugin_impl(const std::string& plugin_key, bool skip_deps, const std::vector<std::string>& capabilities_to_enable);
    std::string check_registry_locked(const std::string& plugin_key, const Plugin& candidate) const;

    void run_discovery(bool async, bool clear);
    void run_discovery_task(bool clear);
    void merge_discovered_plugins(std::vector<PluginDescriptor> discovered, bool clear);

    void unload_and_erase_if(const std::function<bool(const Plugin&)>& should_remove,
                              const std::function<void()>& after_erase_locked = {});

    bool cancel_plugin_load_locked(const std::string& plugin_key);
    bool is_plugin_load_cancelled_locked(const std::string& plugin_key) const;
    void notify_plugin_load_state_changed(bool changed);
    void release_load_slot(const std::string& plugin_key);
    void cancel_and_wait_for_capabilities(
        const std::vector<std::shared_ptr<PluginCapabilityInterface>>& capabilities);

    std::vector<PluginLifecycleCompleteFn> copy_callbacks(CallbackType type) const;
    std::vector<CapabilityLifecycleFn> copy_capability_callbacks(CallbackType type) const;
    void run_on_load_callbacks(const std::string& plugin_key);
    void run_on_unload_callbacks(const std::string& plugin_key);
    void run_on_capability_load_callbacks(const PluginCapabilityId& id);
    void run_on_capability_unload_callbacks(const PluginCapabilityId& id);
    void clear_callbacks();

    void write_loaded_plugin_install_state(const std::string& plugin_key);
    void mark_plugin_install_state_disabled(const std::string& plugin_key);
    void revoke_plugin_permissions(const std::string& plugin_key);

    bool finalize_cloud_plugin_removal(const PluginDescriptor& plugin, bool keep_local, std::string& error);
    bool delete_installed_plugin_package(const PluginDescriptor& plugin, std::string& error);
    bool keep_installed_plugin_as_local(const PluginDescriptor& plugin_descriptor, std::string& error);

    bool m_initialized = false;
    CloudPluginService m_cloud_service;
    PluginConfig m_config;

    mutable std::mutex m_mutex;

    std::vector<Plugin> m_plugins;
    std::unordered_set<std::string> m_missing_plugin_keys;

    std::unordered_set<std::string> m_load_in_progress;
    std::unordered_set<std::string> m_cancelled;
    std::map<std::string, std::string> m_load_errors;
    mutable std::condition_variable m_load_cv;

    mutable std::mutex m_install_state_mutex;

    std::map<CallbackType, std::vector<PluginLifecycleCompleteFn>> m_callbacks;
    std::map<CallbackType, std::vector<CapabilityLifecycleFn>> m_capability_callbacks;

    bool m_discovery_complete    = false;
    bool m_discovery_in_progress = false;
    std::string m_discovery_error;
    mutable std::condition_variable m_discovery_cv;

    std::string m_cloud_user_id;
    std::atomic<bool> m_shutting_down{false};
};

template<typename T>
void execute_capabilities_from_refs(const ConfigOptionStrings& capabilities,
                                    const ConfigOptionStrings* plugins,
                                    PluginCapabilityType type,
                                    std::function<void(std::shared_ptr<T>, const PluginCapabilityRef&)> execute)
{
    PluginManager& plugin_mgr = PluginManager::instance();

    const std::string tag = plugin_capability_type_display_name(type);

    const bool has_any = std::any_of(capabilities.values.begin(), capabilities.values.end(),
                                     [](const std::string& s) { return !s.empty(); });
    if (has_any && !plugin_mgr.wait_for_all_plugin_loads(std::chrono::seconds(10))) {
        BOOST_LOG_TRIVIAL(warning) << tag << ": timed out waiting for plugin loads; unresolved capabilities will be skipped";
    }

    for (const std::string& capability : capabilities.values) {
        if (capability.empty())
            continue;

        std::optional<PluginCapabilityRef> ref;
        if (plugins != nullptr) {
            for (const std::string& plugin_ref : plugins->values) {
                auto parsed = Slic3r::parse_capability_ref(plugin_ref);
                if (parsed && parsed->capability_name == capability) {
                    ref = std::move(parsed);
                    break;
                }
            }
        }

        if (!ref) {
            BOOST_LOG_TRIVIAL(warning) << tag << ": no plugin reference found for capability '" << capability << "'; skipping";
            continue;
        }

        const std::string cap_name   = ref->capability_name;
        const std::string plugin_key = ref->uuid.empty() ? ref->name : ref->uuid;

        auto cap = plugin_mgr.get_plugin_capability({type, cap_name, plugin_key}, false);
        if (!cap) {
            BOOST_LOG_TRIVIAL(warning) << tag << ": no loaded capability '" << cap_name << "' for plugin '" << plugin_key << "'; skipping";
            continue;
        }

        if (!cap->is_enabled()) {
            BOOST_LOG_TRIVIAL(warning) << tag << ": capability '" << cap_name << "' for plugin '" << plugin_key
                                       << "' is disabled; skipping";
            continue;
        }

        auto plugin_capability = std::dynamic_pointer_cast<T>(cap);
        if (!plugin_capability) {
            BOOST_LOG_TRIVIAL(warning) << tag << ": capability '" << cap_name << "' (plugin_key=" << cap->audit_plugin_key()
                                       << ") is not a " << plugin_capability_type_to_string(type) << "; skipping";
            continue;
        }

        execute(plugin_capability, ref.value());
    }
}

}

#endif
