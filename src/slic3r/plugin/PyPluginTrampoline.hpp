#ifndef slic3r_PyPluginTrampoline_hpp_
#define slic3r_PyPluginTrampoline_hpp_

#include "libslic3r/LifecycleEvents.hpp"
#include <pybind11/embed.h>

#include <boost/log/trivial.hpp>

#include <optional>
#include <stdexcept>
#include <string>

#include "PythonPluginInterface.hpp"
#include "PythonInterpreter.hpp"
#include "PluginFsUtils.hpp"
#include "PluginAuditManager.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/gil.h>
#include <pybind11/pytypes.h>

#define ORCA_PY_LOGGED_OVERRIDE_BODY(override_call) \
    try { \
        override_call; \
    } catch (pybind11::error_already_set & err) { \
        const bool _orca_audit_denial = ::Slic3r::PluginAuditManager::instance().audit_denial_pending(); \
        if (_orca_audit_denial) \
            ::Slic3r::PluginAuditManager::instance().clear_current_plugin(); \
        ::Slic3r::log_python_exception_keep(err); \
        if (_orca_audit_denial) \
            ::Slic3r::PluginAuditManager::instance().clear_audit_denial(); \
        throw; \
    }

#define ORCA_PY_AUDIT_SCOPE() \
    std::optional<::Slic3r::ScopedPluginAuditContext> _orca_audit_scope; \
    if (const std::string& _orca_audit_key = this->audit_plugin_key(); !_orca_audit_key.empty()) \
    _orca_audit_scope.emplace(_orca_audit_key, this->name())

#define ORCA_PY_OVERRIDE_AUDITED(audit_setup, override_macro, ret, base, name, ...) \
    do { \
        ::Slic3r::PluginCapabilityInterface::RefCounter _orca_ref_counter(*this); \
        ::Slic3r::PythonGILState _orca_python_gil; \
        if (!_orca_python_gil) \
            throw std::runtime_error("Python interpreter is shutting down"); \
        ORCA_PY_AUDIT_SCOPE(); \
        if (_orca_audit_scope) \
            audit_setup(); \
        ORCA_PY_LOGGED_OVERRIDE_BODY(override_macro(ret, base, name, ##__VA_ARGS__)); \
    } while (0)

namespace Slic3r {
template<class Base> class PyPluginCommonTrampoline : public Base
{
public:
    using Base::Base;

    std::string get_name() const override
    {
        ORCA_PY_OVERRIDE_AUDITED([] {}, PYBIND11_OVERRIDE_PURE, std::string, Base, get_name);
    }

    bool has_config_ui() const override
    {
        ORCA_PY_OVERRIDE_AUDITED(
            [] {},
            PYBIND11_OVERRIDE,
            bool,
            Base,
            has_config_ui);
    }

    std::string get_config_ui() const override
    {
        ORCA_PY_OVERRIDE_AUDITED(
            [] {},
            PYBIND11_OVERRIDE,
            std::string,
            Base,
            get_config_ui);
    }

    nlohmann::json get_default_config() const override
    {
        ORCA_PY_AUDIT_SCOPE();
        try {
            pybind11::gil_scoped_acquire gil;
            pybind11::function override = pybind11::get_override(static_cast<const Base*>(this), "get_default_config");
            if (!override)
                return Base::get_default_config();

            nlohmann::json config = ::Slic3r::py_to_json(override());
            if (!config.is_object()) {
                BOOST_LOG_TRIVIAL(warning)
                    << "Plugin capability '" << this->name() << "' of plugin '" << this->audit_plugin_key()
                    << "': get_default_config() returned " << config.type_name() << ", not an object; restoring an empty config";
                return Base::get_default_config();
            }
            return config;
        } catch (pybind11::error_already_set& err) {
            ::Slic3r::log_python_exception_keep(err);
            throw;
        }
    }

    void on_load() override
    {
        ORCA_PY_OVERRIDE_AUDITED([] {}, PYBIND11_OVERRIDE, void, Base, on_load);
    }

    void on_unload() override
    {
        ORCA_PY_OVERRIDE_AUDITED([] {}, PYBIND11_OVERRIDE, void, Base, on_unload);
    }

    void on_cancelled() override
    {
        ORCA_PY_OVERRIDE_AUDITED([] {}, PYBIND11_OVERRIDE, void, Base, on_cancelled);
    }

    void on_lifecycle_event(LifecycleEvent event, const LifecycleEventContext& ctx) override
    {
        ORCA_PY_OVERRIDE_AUDITED([] {}, PYBIND11_OVERRIDE, void, Base, on_lifecycle_event, event, ctx);
    }
};

class PyPluginInterfaceTrampoline : public PyPluginCommonTrampoline<PluginCapabilityInterface>
{
public:
    using PyPluginCommonTrampoline<PluginCapabilityInterface>::PyPluginCommonTrampoline;

    PluginCapabilityType get_type() const override
    {
        ORCA_PY_OVERRIDE_AUDITED(
            [] {}, PYBIND11_OVERRIDE, PluginCapabilityType, PluginCapabilityInterface,
            get_type);
    }
};
}

#endif
