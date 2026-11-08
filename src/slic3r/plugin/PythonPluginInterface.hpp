#ifndef slic3r_PythonPluginInterface_hpp_
#define slic3r_PythonPluginInterface_hpp_

#include <atomic>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>
#include <pybind11/embed.h>

#include <libslic3r/LifecycleEvents.hpp>

namespace Slic3r {

enum class PluginCapabilityType { PrinterConnection = 0, Pages, Analysis, Importer, Exporter, Visualization, Script, SlicingPipeline, Unknown };

struct PluginCapabilityId
{
    PluginCapabilityType type = PluginCapabilityType::Unknown;
    std::string          name;
    std::string          plugin_key;

    bool empty() const { return type == PluginCapabilityType::Unknown || name.empty() || plugin_key.empty(); }
    friend bool operator==(const PluginCapabilityId& lhs, const PluginCapabilityId& rhs)
    {
        return lhs.type == rhs.type && lhs.name == rhs.name && lhs.plugin_key == rhs.plugin_key;
    }
    friend bool operator<(const PluginCapabilityId& lhs, const PluginCapabilityId& rhs)
    {
        if (lhs.plugin_key != rhs.plugin_key)
            return lhs.plugin_key < rhs.plugin_key;
        if (lhs.name != rhs.name)
            return lhs.name < rhs.name;
        return lhs.type < rhs.type;
    }
};

inline std::string plugin_capability_type_to_string(PluginCapabilityType type)
{
    switch (type) {
    case PluginCapabilityType::PrinterConnection: return "printer-connection";
    case PluginCapabilityType::Pages: return "pages";
    case PluginCapabilityType::Analysis: return "analysis";
    case PluginCapabilityType::Importer: return "importer";
    case PluginCapabilityType::Exporter: return "exporter";
    case PluginCapabilityType::Visualization: return "visualization";
    case PluginCapabilityType::Script: return "script";
    case PluginCapabilityType::SlicingPipeline: return "slicing-pipeline";
    default: return "unknown";
    }
}

inline std::string plugin_capability_type_display_name(PluginCapabilityType type)
{
    switch (type) {
    case PluginCapabilityType::PrinterConnection: return "Printer connection";
    case PluginCapabilityType::Pages: return "Pages";
    case PluginCapabilityType::Analysis: return "Analysis";
    case PluginCapabilityType::Importer: return "Importer";
    case PluginCapabilityType::Exporter: return "Exporter";
    case PluginCapabilityType::Visualization: return "Visualization";
    case PluginCapabilityType::Script: return "Script";
    case PluginCapabilityType::SlicingPipeline: return "Slicing Pipeline";
    default: return "Unknown";
    }
}

inline PluginCapabilityType plugin_capability_type_from_string(std::string_view value)
{
    auto to_lower = [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); };
    std::string lowered;
    lowered.reserve(value.size());
    for (unsigned char ch : value) {
        lowered.push_back(to_lower(ch));
    }

    if (lowered == "printer-connection")
        return PluginCapabilityType::PrinterConnection;
    if (lowered == "pages")
        return PluginCapabilityType::Pages;
    if (lowered == "analysis")
        return PluginCapabilityType::Analysis;
    if (lowered == "importer")
        return PluginCapabilityType::Importer;
    if (lowered == "exporter")
        return PluginCapabilityType::Exporter;
    if (lowered == "visualization")
        return PluginCapabilityType::Visualization;
    if (lowered == "script")
        return PluginCapabilityType::Script;
    if (lowered == "slicing-pipeline")
        return PluginCapabilityType::SlicingPipeline;
    return PluginCapabilityType::Unknown;
}

struct PluginContext
{ std::string orca_version; };

enum class PluginResult { Success, Skipped, RecoverableError, FatalError };

struct ExecutionResult
{
    PluginResult status = PluginResult::Success;
    std::string message;
    std::string data;

    static ExecutionResult success(std::string message = {}, std::string data = {})
    { return {PluginResult::Success, std::move(message), std::move(data)}; }

    static ExecutionResult skipped(std::string message = {})
    {
        return {PluginResult::Skipped, std::move(message), {}};
    }

    static ExecutionResult failure(PluginResult status, std::string message, std::string data = {})
    { return {status, std::move(message), std::move(data)}; }
};

class PluginCapabilityInterface
{
public:
    class RefCounter
    {
    public:
        explicit RefCounter(const PluginCapabilityInterface& iface) : m_iface(&iface) { m_iface->increment(); }

        ~RefCounter() { m_iface->decrement(); }

        RefCounter(const RefCounter&)            = delete;
        RefCounter& operator=(const RefCounter&) = delete;

    private:
        const PluginCapabilityInterface* m_iface;
    };

    virtual ~PluginCapabilityInterface() = default;

    virtual std::string get_name() const = 0;
    virtual PluginCapabilityType get_type() const { return PluginCapabilityType::Unknown; }
    virtual bool has_config_ui() const { return false; }
    virtual std::string get_config_ui() const { return ""; }

    virtual nlohmann::json get_default_config() const { return nlohmann::json::object(); }

    virtual void on_load() {}
    virtual void on_unload() {}
    virtual void on_cancelled() {}

    virtual void on_lifecycle_event(LifecycleEvent event, const LifecycleEventContext& ctx) { (void) event; (void) ctx; }

    const std::string&   name() const { return m_name; }
    PluginCapabilityType type() const { return m_type; }
    PluginCapabilityId   identity() const { return {m_type, m_name, m_audit_plugin_key}; }
    void                 set_resolved_identity(std::string name, PluginCapabilityType type)
    {
        m_name = std::move(name);
        m_type = type;
    }

    bool is_enabled() const { return m_enabled.load(std::memory_order_acquire); }
    void set_enabled(bool enabled) { m_enabled.store(enabled, std::memory_order_release); }

    bool config_ui_available() const { return m_config_ui_available; }
    void set_config_ui_available(bool available) { m_config_ui_available = available; }

    void set_audit_plugin_key(std::string key) { m_audit_plugin_key = std::move(key); }
    const std::string& audit_plugin_key() const { return m_audit_plugin_key; }

    void increment() const { m_refs.fetch_add(1, std::memory_order_acq_rel); }
    void decrement() const { m_refs.fetch_sub(1, std::memory_order_acq_rel); }
    int ref_count() const { return m_refs.load(std::memory_order_acquire); }

private:
    std::string m_name;
    PluginCapabilityType m_type = PluginCapabilityType::Unknown;
    std::atomic<bool> m_enabled{true};
    bool m_config_ui_available = false;
    std::string m_audit_plugin_key;

    mutable std::atomic<int> m_refs{0};
};

}

#endif
