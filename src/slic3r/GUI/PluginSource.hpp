#pragma once

#include <string>

namespace Slic3r
{
    namespace GUI
    {
        enum class PluginSource
        {
            Mine,
            Subscribed,
            Orphaned,
            Local
        };

        inline std::string to_string(PluginSource source)
        {
            switch (source)
            {
            case PluginSource::Mine: return "mine";
            case PluginSource::Subscribed: return "subscribed";
            case PluginSource::Orphaned: return "orphaned";
            case PluginSource::Local: return "local";
            }

            return "local";
        }
    }
}
