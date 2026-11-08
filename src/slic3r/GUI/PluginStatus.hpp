#pragma once

#include <string>

namespace Slic3r
{
    namespace GUI
    {
        enum class PluginStatus
        {
            Activated,
            Error,
            RuntimeError,
            Inactive,
            Loading
        };

        inline std::string to_string(PluginStatus status)
        {
            switch (status)
            {
            case PluginStatus::Activated: return "Activated";
            case PluginStatus::Error: return "Error";
            case PluginStatus::RuntimeError: return "RuntimeError";
            case PluginStatus::Inactive: return "Inactive";
            case PluginStatus::Loading: return "Loading";
            }

            return "Inactive";
        }

        inline PluginStatus resolve_plugin_status(bool loading, bool has_error, bool is_loaded)
        {
            if (loading)
                return PluginStatus::Loading;
            if (has_error)
                return is_loaded ? PluginStatus::RuntimeError : PluginStatus::Error;
            if (is_loaded)
                return PluginStatus::Activated;
            return PluginStatus::Inactive;
        }
    }
}
