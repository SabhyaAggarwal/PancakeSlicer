#include "PythonPluginBridge.hpp"

#include <algorithm>
#include <boost/log/trivial.hpp>
#include <exception>
#include "libslic3r/LifecycleEvents.hpp"
#include <memory>
#include <mutex>
#include <slic3r/plugin/PluginAuditManager.hpp>
#include "slic3r/plugin/PythonPluginInterface.hpp"
#include <string>
#include <unordered_map>

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <vector>
#include <utility>

#include "PythonInterpreter.hpp"
#include "PluginFsUtils.hpp"
#include "PluginConfig.hpp"
#include "host/PluginHost.hpp"
#include "PyPluginPackage.hpp"
#include "PyPluginTrampoline.hpp"
#include "pluginTypes/printerAgent/PrinterAgentPluginCapability.hpp"
#include "pluginTypes/pages/PagesPluginCapability.hpp"
#include "pluginTypes/script/ScriptPluginCapability.hpp"
#include "pluginTypes/slicingPipeline/SlicingPipelinePluginCapability.hpp"
#include <pybind11/pytypes.h>
#include <pybind11/gil.h>

namespace py = pybind11;

namespace Slic3r {
namespace {

thread_local std::string g_active_plugin_key;
std::mutex g_registry_mutex;
std::unordered_map<std::string, std::vector<py::object>> g_pending_capabilities;
std::unordered_map<std::string, py::object> g_pending_package;
std::unordered_map<std::string, std::vector<std::string>> g_pending_fs_read_permissions;
struct PluginInstanceHandle
{
    std::shared_ptr<PluginCapabilityInterface> plugin;
    py::object keep_alive;

    ~PluginInstanceHandle()
    {
        if (keep_alive) {
            PythonGILState gil;
            if (gil) {
                keep_alive = py::object();
            } else {
                (void) keep_alive.release();
            }
        }
    }
};

void discard_pending_capture_without_python(const std::string& plugin_key)
{
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    auto                        capabilities = g_pending_capabilities.find(plugin_key);
    if (capabilities != g_pending_capabilities.end()) {
        for (py::object& capability : capabilities->second)
            (void) capability.release();
        g_pending_capabilities.erase(capabilities);
    }

    auto package = g_pending_package.find(plugin_key);
    if (package != g_pending_package.end()) {
        (void) package->second.release();
        g_pending_package.erase(package);
    }

    g_pending_fs_read_permissions.erase(plugin_key);
}

}

PythonPluginBridge& PythonPluginBridge::instance()
{
    static PythonPluginBridge bridge;
    return bridge;
}

void PythonPluginBridge::begin_plugin_capture(const std::string& plugin_key)
{
    PythonGILState gil;
    if (!gil) {
        BOOST_LOG_TRIVIAL(warning) << "Cannot begin Python plugin capture while the interpreter is shutting down";
        return;
    }
    BOOST_LOG_TRIVIAL(info) << "Beginning Python plugin capture for key " << plugin_key;
    {
        std::lock_guard<std::mutex> lock(g_registry_mutex);
        g_pending_capabilities.erase(plugin_key);
        g_pending_package.erase(plugin_key);
        g_pending_fs_read_permissions.erase(plugin_key);
    }
    g_active_plugin_key = plugin_key;
}

std::vector<CapturedCapability> PythonPluginBridge::finalize_plugin_capture(const std::string& capture_key,
                                                                              const std::string& plugin_key,
                                                                              std::string&       error)
{
    PythonGILState gil;
    if (!gil) {
        error = "Python interpreter is shutting down";
        return {};
    }
    BOOST_LOG_TRIVIAL(info) << "Finalizing Python plugin capture for key " << capture_key;

    {
        auto clear_active_key = [&capture_key]() {
            if (g_active_plugin_key == capture_key)
                g_active_plugin_key.clear();
        };
        auto discard_pending_for_key = [&capture_key]() {
            std::lock_guard<std::mutex> lock(g_registry_mutex);
            g_pending_capabilities.erase(capture_key);
            g_pending_package.erase(capture_key);
            g_pending_fs_read_permissions.erase(capture_key);
        };

        try {
            py::object package_cls;
            {
                std::lock_guard<std::mutex> lock(g_registry_mutex);
                auto it = g_pending_package.find(capture_key);
                if (it != g_pending_package.end()) {
                    package_cls = it->second;
                    g_pending_package.erase(it);
                }
            }
            if (!package_cls) {
                error = "Plugin did not register a package class; decorate it with @orca.plugin";
                BOOST_LOG_TRIVIAL(error) << error << " for key " << plugin_key;
                discard_pending_for_key();
                clear_active_key();
                return {};
            }

            py::object package = package_cls();
            package.attr("register_capabilities")();

            std::vector<std::string> fs_read_permissions;
            {
                std::lock_guard<std::mutex> lock(g_registry_mutex);
                auto it = g_pending_fs_read_permissions.find(capture_key);
                if (it != g_pending_fs_read_permissions.end()) {
                    fs_read_permissions = std::move(it->second);
                    g_pending_fs_read_permissions.erase(it);
                }
            }
            if (!fs_read_permissions.empty()) {
                bool granted = false;
                {
                    py::gil_scoped_release release;
                    granted = PluginAuditManager::instance().request_filesystem_read_permissions(plugin_key, fs_read_permissions);
                }
                if (!granted) {
                    error = "Plugin filesystem read permission request denied";
                    BOOST_LOG_TRIVIAL(warning) << error << " for key " << plugin_key;
                    discard_pending_for_key();
                    clear_active_key();
                    return {};
                }
            }
        } catch (py::error_already_set& err) {
            log_python_exception_keep(err);
            error = err.what();
            BOOST_LOG_TRIVIAL(error) << "Plugin register_capabilities raised Python exception for key " << plugin_key
                                     << " error=" << error;
            discard_pending_for_key();
            clear_active_key();
            return {};
        } catch (const std::exception& ex) {
            error = ex.what();
            BOOST_LOG_TRIVIAL(error) << "Plugin register_capabilities raised exception for key " << plugin_key
                                     << " error=" << error;
            discard_pending_for_key();
            clear_active_key();
            return {};
        }
    }

    std::vector<py::object> classes;
    {
        std::lock_guard<std::mutex> lock(g_registry_mutex);
        auto it = g_pending_capabilities.find(capture_key);
        if (it != g_pending_capabilities.end()) {
            classes = std::move(it->second);
            g_pending_capabilities.erase(it);
        }
    }

    if (g_active_plugin_key == capture_key)
        g_active_plugin_key.clear();

    BOOST_LOG_TRIVIAL(info) << "Collected " << classes.size() << " registered capability class(es) for key " << capture_key;

    std::vector<CapturedCapability> capabilities;
    capabilities.reserve(classes.size());

    for (auto& cls : classes) {
        try {
            py::object instance = cls();
            if (!py::isinstance<PluginCapabilityInterface>(instance)) {
                error = "Registered capability must inherit from a PluginCapability base";
                BOOST_LOG_TRIVIAL(error) << "Python plugin capture failed type check for key " << capture_key
                                         << " error=" << error;
                return {};
            }

            auto capability_iface = instance.cast<std::shared_ptr<PluginCapabilityInterface>>();
            if (!capability_iface) {
                error = "Failed to cast Python capability to PluginCapabilityInterface";
                BOOST_LOG_TRIVIAL(error) << "Python plugin capture failed cast for key " << capture_key
                                         << " error=" << error;
                return {};
            }

            std::string name = capability_iface->get_name();

            if (name.find(';') != std::string::npos) {
                error = "Capability name must not contain ';': " + name;
                BOOST_LOG_TRIVIAL(error) << "Python plugin capture rejected capability for key " << capture_key
                                         << " error=" << error;
                return {};
            }

            auto handle        = std::make_shared<PluginInstanceHandle>();
            handle->keep_alive = instance;
            handle->plugin     = std::move(capability_iface);

            CapturedCapability captured;
            captured.instance = std::shared_ptr<PluginCapabilityInterface>(handle, handle->plugin.get());
            captured.name     = std::move(name);
            capabilities.emplace_back(std::move(captured));
        } catch (py::error_already_set& err) {
            log_python_exception_keep(err);
            error = err.what();
            BOOST_LOG_TRIVIAL(error) << "Python plugin capture raised Python exception for key " << capture_key
                                     << " error=" << error;
            return {};
        } catch (const std::exception& ex) {
            error = ex.what();
            BOOST_LOG_TRIVIAL(error) << "Python plugin capture raised exception for key " << capture_key
                                     << " error=" << error;
            return {};
        }
    }

    BOOST_LOG_TRIVIAL(info) << "Instantiated " << capabilities.size() << " Python capability instance(s) for key " << plugin_key;
    return capabilities;
}

void PythonPluginBridge::cancel_plugin_capture(const std::string& plugin_key)
{
    PythonGILState gil;
    BOOST_LOG_TRIVIAL(warning) << "Cancelling Python plugin capture for key " << plugin_key;

    if (!gil) {
        discard_pending_capture_without_python(plugin_key);
        if (g_active_plugin_key == plugin_key)
            g_active_plugin_key.clear();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_registry_mutex);
        g_pending_capabilities.erase(plugin_key);
        g_pending_package.erase(plugin_key);
        g_pending_fs_read_permissions.erase(plugin_key);
    }

    if (g_active_plugin_key == plugin_key)
        g_active_plugin_key.clear();
}

void PythonPluginBridge::clear_pending_captures()
{
    PythonGILState gil;
    if (!gil) {
        std::lock_guard<std::mutex> lock(g_registry_mutex);
        BOOST_LOG_TRIVIAL(info) << "Clearing " << g_pending_capabilities.size()
                                << " pending Python plugin capture(s) without Python interpreter";
        for (auto& [plugin_key, plugins] : g_pending_capabilities) {
            (void) plugin_key;
            for (py::object& plugin : plugins)
                (void) plugin.release();
        }
        g_pending_capabilities.clear();
        for (auto& [plugin_key, pkg] : g_pending_package) {
            (void) plugin_key;
            (void) pkg.release();
        }
        g_pending_package.clear();
        g_pending_fs_read_permissions.clear();
        g_active_plugin_key.clear();
        return;
    }

    std::lock_guard<std::mutex> lock(g_registry_mutex);
    BOOST_LOG_TRIVIAL(info) << "Clearing " << g_pending_capabilities.size() << " pending Python plugin capture(s)";
    g_pending_capabilities.clear();
    g_pending_package.clear();
    g_pending_fs_read_permissions.clear();
    g_active_plugin_key.clear();
}

void bind_python_api(pybind11::module_& m)
{
    m.doc() = "OrcaSlicer plugin API";

    py::enum_<PluginCapabilityType>(m, "PluginType", "Available plugin capability groups")
        .value("PrinterConnection", PluginCapabilityType::PrinterConnection)
        .value("Pages", PluginCapabilityType::Pages)
        .value("Analysis", PluginCapabilityType::Analysis)
        .value("Importer", PluginCapabilityType::Importer)
        .value("Exporter", PluginCapabilityType::Exporter)
        .value("Visualization", PluginCapabilityType::Visualization)
        .value("Script", PluginCapabilityType::Script)
        .value("SlicingPipeline", PluginCapabilityType::SlicingPipeline)
        .value("Unknown", PluginCapabilityType::Unknown)
        .export_values();

    py::enum_<PluginResult>(m, "PluginResult", "Execution summary code")
        .value("Success", PluginResult::Success)
        .value("Skipped", PluginResult::Skipped)
        .value("RecoverableError", PluginResult::RecoverableError)
        .value("FatalError", PluginResult::FatalError)
        .export_values();

    py::enum_<LifecycleEvent>(m, "LifecycleEvent", "Application lifecycle moment passed to on_lifecycle_event")
        .value("NewProject", LifecycleEvent::NewProject)
        .value("ProjectOpened", LifecycleEvent::ProjectOpened)
        .value("ProjectBeforeSave", LifecycleEvent::ProjectBeforeSave)
        .value("ProjectAfterSave", LifecycleEvent::ProjectAfterSave)
        .value("ProjectClosed", LifecycleEvent::ProjectClosed)
        .value("ProjectDirtyChanged", LifecycleEvent::ProjectDirtyChanged)
        .value("SliceStarted", LifecycleEvent::SliceStarted)
        .value("SliceGeometryFinished", LifecycleEvent::SliceGeometryFinished)
        .value("GCodeExportStarted", LifecycleEvent::GCodeExportStarted)
        .value("GCodeExportFinished", LifecycleEvent::GCodeExportFinished)
        .value("SlicingJobComplete", LifecycleEvent::SlicingJobComplete)
        .value("ObjectAdded", LifecycleEvent::ObjectAdded)
        .value("ObjectDeleted", LifecycleEvent::ObjectDeleted)
        .value("ObjectTransformed", LifecycleEvent::ObjectTransformed)
        .value("ObjectChanged", LifecycleEvent::ObjectChanged)
        .value("ObjectRenamed", LifecycleEvent::ObjectRenamed)
        .value("PlateCreated", LifecycleEvent::PlateCreated)
        .value("PlateDeleted", LifecycleEvent::PlateDeleted)
        .value("PlateSelected", LifecycleEvent::PlateSelected)
        .value("PlateRenamed", LifecycleEvent::PlateRenamed)
        .value("PresetSelected", LifecycleEvent::PresetSelected)
        .value("PresetSaved", LifecycleEvent::PresetSaved)
        .value("PrintStateChanged", LifecycleEvent::PrintStateChanged)
        .value("DeviceOnline", LifecycleEvent::DeviceOnline)
        .value("DeviceOffline", LifecycleEvent::DeviceOffline)
        .value("DeviceDiscovered", LifecycleEvent::DeviceDiscovered)
        .value("DeviceSelected", LifecycleEvent::DeviceSelected)
        .value("UploadStarted", LifecycleEvent::UploadStarted)
        .value("UploadFinished", LifecycleEvent::UploadFinished)
        .value("PrintJobStarted", LifecycleEvent::PrintJobStarted)
        .value("PrintJobFinished", LifecycleEvent::PrintJobFinished)
        .value("SendJobStarted", LifecycleEvent::SendJobStarted)
        .value("SendJobFinished", LifecycleEvent::SendJobFinished)
        .export_values();

    py::enum_<LifecycleEvtCode>(m, "LifecycleEvtCode", "Outcome code accompanying a LifecycleEventContext")
        .value("Ok", LifecycleEvtCode::Ok)
        .value("Error", LifecycleEvtCode::Error)
        .value("Warn", LifecycleEvtCode::Warn)
        .export_values();

    py::class_<LifecycleEventContext>(m, "LifecycleEventContext", "Payload accompanying a LifecycleEvent")
        .def(py::init<>())
        .def_readonly("name", &LifecycleEventContext::name)
        .def_readonly("code", &LifecycleEventContext::code)
        .def_readonly("msg", &LifecycleEventContext::msg)
        .def_readonly("id", &LifecycleEventContext::id)
        .def_readonly("previous_name", &LifecycleEventContext::previous_name)
        .def_readonly("device_id", &LifecycleEventContext::device_id)
        .def_readonly("job_id", &LifecycleEventContext::job_id)
        .def_readonly("source", &LifecycleEventContext::source)
        .def_readonly("index", &LifecycleEventContext::index)
        .def_readonly("dirty", &LifecycleEventContext::dirty);

    py::class_<PluginContext>(m, "PluginContext", "Context shared with plugin entry points")
        .def(py::init<>())
        .def_readwrite("orca_version", &PluginContext::orca_version);

    py::class_<ExecutionResult>(m, "ExecutionResult", "Structured execution outcome")
        .def(py::init<>())
        .def(py::init<PluginResult, std::string, std::string>())
        .def_readwrite("status", &ExecutionResult::status)
        .def_readwrite("message", &ExecutionResult::message)
        .def_readwrite("data", &ExecutionResult::data)
        .def_static("success", &ExecutionResult::success, py::arg("message") = std::string(), py::arg("data") = std::string())
        .def_static("skipped", &ExecutionResult::skipped, py::arg("message") = std::string())
        .def_static("failure", &ExecutionResult::failure, py::arg("status"), py::arg("message"), py::arg("data") = std::string());

    py::class_<PluginCapabilityInterface, PyPluginInterfaceTrampoline, std::shared_ptr<PluginCapabilityInterface>>(m, "PythonPluginBase")
        .def(py::init<>())
        .def("get_name", &PluginCapabilityInterface::get_name)
        .def("get_type", &PluginCapabilityInterface::get_type)
        .def("on_load", &PluginCapabilityInterface::on_load)
        .def("on_unload", &PluginCapabilityInterface::on_unload)
        .def("on_lifecycle_event", &PluginCapabilityInterface::on_lifecycle_event,
             "Override to react to an application lifecycle moment (LifecycleEvent) and its\n"
             "LifecycleEventContext payload. Available on every capability type.")
        .def("has_config_ui", &PluginCapabilityInterface::has_config_ui,
             "Override to return True to replace the host's default JSON editor with your own HTML\n"
             "UI, returned by get_config_ui(). Every capability is configurable and appears in the\n"
             "Plugins dialog's Config tab regardless; this only chooses how its config is edited.")
        .def("get_config_ui", &PluginCapabilityInterface::get_config_ui,
             "Override to return the custom configuration UI as an HTML string. Only called when\n"
             "has_config_ui() is True; an empty result falls back to the default JSON editor.\n"
             "Inside the page, use window.orca.getConfig()/saveConfig() to reach this same config.")
        .def(
            "get_default_config",
            [](const PluginCapabilityInterface& self) {
                nlohmann::json config = self.get_default_config();
                return config.dump();
            },
            "Override to return the config that the Config tab's \"Restore defaults\" action writes\n"
            "back, as a dict. Optional: without it the action stores an empty config, which already\n"
            "restores the defaults of a capability that keeps its stored config sparse and applies\n"
            "its own defaults on read. Override it to write an explicit starting config instead.\n"
            "Calling it returns that config as a JSON string.")
        .def(
            "get_config",
            [](const PluginCapabilityInterface& self) {
                nlohmann::json config = capability_get_config(self);
                return config.dump();
            },
            "Return this capability's stored config as a JSON string — json.loads() it to use.\n"
            "\"{}\" if it has never been saved, so the parsed result is always indexable.")
        .def(
            "get_config_version", [](const PluginCapabilityInterface& self) { return capability_get_config_version(self); },
            "Return the plugin version that last wrote this capability's config, so a newer\n"
            "release can spot a stale config and migrate it. Empty string if never saved.")
        .def(
            "save_config",
            [](const PluginCapabilityInterface& self, const std::string& config_str) {
                nlohmann::json config = nlohmann::json::parse(config_str, nullptr,  false);
                if (config.is_discarded()) {
                    BOOST_LOG_TRIVIAL(error) << "save_config: capability '" << self.get_name() << "' passed a config that is not valid JSON";
                    return false;
                }
                return capability_save_config(self, config);
            },
            py::arg("config"),
            "Persist this capability's config, given as a JSON string (e.g. json.dumps(cfg)).\n"
            "The plugin key, capability name and version are supplied by the host. Returns False if\n"
            "the string is not valid JSON, or if the config file could not be written.");

    py::class_<PyPluginPackage, PyPluginPackageTrampoline>(m, "base")
        .def(py::init<>())
        .def("register_capabilities", &PyPluginPackage::register_capabilities);

    BOOST_LOG_TRIVIAL(debug) << "Registering embedded Python plugin type bindings";

    PrinterAgentPluginCapability::RegisterBindings(m);
    PagesPluginCapability::RegisterBindings(m);
    ScriptPluginCapability::RegisterBindings(m);
    SlicingPipelinePluginCapability::RegisterBindings(m);
    PluginHost::RegisterBindings(m);
    BOOST_LOG_TRIVIAL(debug) << "Registered ScriptPluginCapability Python bindings";

    m.def(
        "register_capability",
        [](py::object plugin_cls) {
            if (g_active_plugin_key.empty()) {
                throw py::value_error("register_capability() called outside plugin discovery context");
            }

            py::handle base = py::type::of<PluginCapabilityInterface>();
            const int is_subclass = PyObject_IsSubclass(plugin_cls.ptr(), base.ptr());
            if (is_subclass != 1) {
                if (is_subclass < 0)
                    PyErr_Clear();
                throw py::value_error("Registered class must inherit from a PluginCapability base");
            }

            std::lock_guard<std::mutex> lock(g_registry_mutex);
            g_pending_capabilities[g_active_plugin_key].push_back(std::move(plugin_cls));
            BOOST_LOG_TRIVIAL(debug) << "Registered Python plugin capability class for key " << g_active_plugin_key;
        },
        R"pbdoc(Register a PluginCapability subclass while OrcaSlicer loads your module.)pbdoc");

    m.def(
        "request_permissions",
        [](const std::vector<std::string>& fs_read) {
            if (g_active_plugin_key.empty())
                throw py::value_error("request_permissions() called outside plugin discovery context");

            std::lock_guard<std::mutex> lock(g_registry_mutex);
            auto& requested = g_pending_fs_read_permissions[g_active_plugin_key];
            for (const std::string& path : fs_read) {
                if (path.empty())
                    throw py::value_error("request_permissions(fs_read=...) does not accept empty paths");
                if (std::find(requested.begin(), requested.end(), path) == requested.end())
                    requested.push_back(path);
            }
        },
        py::arg("fs_read") = std::vector<std::string>{},
        R"pbdoc(Request filesystem read permissions while OrcaSlicer loads your module.

The host presents the request to the user after register_capabilities() returns. Denying the
request aborts the plugin load.)pbdoc");

    m.def("plugin", [](py::object cls) {
        if (g_active_plugin_key.empty())
            throw py::value_error("@orca.plugin used outside plugin discovery context");
        if (!PyType_Check(cls.ptr()))
            throw py::value_error("@orca.plugin must decorate a class");
        py::handle base = py::type::of<PyPluginPackage>();
        const int is_subclass = PyObject_IsSubclass(cls.ptr(), base.ptr());
        if (is_subclass != 1) {
            if (is_subclass < 0)
                PyErr_Clear();
            throw py::value_error("@orca.plugin must decorate a subclass of orca.base");
        }
        {
            std::lock_guard<std::mutex> lock(g_registry_mutex);
            auto& slot = g_pending_package[g_active_plugin_key];
            if (slot)
                throw py::value_error("multiple @orca.plugin classes registered; exactly one is allowed per plugin");
            slot = cls;
        }
        return cls;
    }, R"pbdoc(Mark the single plugin package class (the orca.base subclass) for this file.)pbdoc");

}

}

#ifdef ORCA_PYTHON_STUBGEN_MODULE
PYBIND11_MODULE(orca, m) { Slic3r::bind_python_api(m); }
#else
PYBIND11_EMBEDDED_MODULE(orca, m) { Slic3r::bind_python_api(m); }
#endif
