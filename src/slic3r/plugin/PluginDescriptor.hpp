#pragma once

#include "PythonPluginInterface.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {

struct CloudPluginState
{
    std::string uuid;
    bool installed        = false;
    bool update_available = false;
    bool unauthorized     = false;
    bool is_mine          = false;
    bool orphaned         = false;
};

enum class PluginUpdateStatus
{
    Normal,
    UpdateAvailable,
    Unauthorized,
};

struct PluginChangelog {
    std::string changelog_id;
    std::string plugin_uuid;
    std::string version;
    std::string changelog;
    long long created_time = 0;
};

inline void sort_plugin_changelog(std::vector<PluginChangelog>& changelog)
{
    std::sort(changelog.begin(), changelog.end(), [](const PluginChangelog& lhs, const PluginChangelog& rhs) {
        if (lhs.created_time != rhs.created_time)
            return lhs.created_time > rhs.created_time;
        return lhs.version > rhs.version;
    });
}

struct PluginDescriptor
{
    std::string plugin_key;
    std::string name;
    std::string description;
    std::string author;
    std::string version;
    std::string latest_version;
    std::string installed_version;
    std::vector<std::string> display_types;
    std::string plugin_root;
    std::string entry_path;
    std::string entry_package;
    std::vector<std::string> dependencies;
    std::vector<PluginChangelog> changelog;

    std::string error;
    std::optional<CloudPluginState> cloud;
    bool metadata_valid = false;
    bool install_state_valid = false;
    bool enabled = false;
    std::string sharing_token;
    std::string thumbnail_url;

    bool is_cloud_plugin() const { return cloud.has_value(); }
    std::string cloud_uuid() const { return cloud.has_value() ? cloud->uuid : std::string{}; }
    bool has_local_package() const { return !is_cloud_plugin() || cloud->installed || !plugin_root.empty() || !entry_path.empty(); }
    bool is_metadata_valid() const { return metadata_valid; }

    bool is_invalid_package() const { return !metadata_valid && has_local_package(); }

    std::string normalized_error() const
    {
        auto begin = std::find_if_not(error.begin(), error.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
        if (begin == error.end())
            return {};

        auto end = std::find_if_not(error.rbegin(), error.rend(), [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
        return std::string(begin, end);
    }

    bool has_error() const { return !normalized_error().empty(); }

    PluginUpdateStatus get_update_status() const
    {
        if (!cloud.has_value())
            return PluginUpdateStatus::Normal;
        if (cloud->orphaned)
            return PluginUpdateStatus::Normal;
        if (cloud->unauthorized)
            return PluginUpdateStatus::Unauthorized;
        if (cloud->update_available)
            return PluginUpdateStatus::UpdateAvailable;
        return PluginUpdateStatus::Normal;
    }

    bool has_update_available() const { return get_update_status() == PluginUpdateStatus::UpdateAvailable; }
    bool is_unauthorized() const { return get_update_status() == PluginUpdateStatus::Unauthorized; }
    std::string latest_available_version() const
    {
        if (!latest_version.empty())
            return latest_version;
        return version;
    }

    void set_metadata_valid(bool is_valid)
    {
        metadata_valid = is_valid;
        if (!is_valid && !has_error())
            error = "Plugin metadata is invalid.";
    }

    void set_unauthorized(bool unauthorized)
    {
        if (!cloud.has_value())
            return;
        cloud->unauthorized = unauthorized;
        if (unauthorized)
            cloud->update_available = false;
    }

    void clear_error() { error.clear(); }
    void set_error(std::string message) { error = std::move(message); }
};

inline void apply_plugin_metadata_fallbacks(PluginDescriptor& target, const PluginDescriptor& fallback)
{
    if (target.name.empty())
        target.name = fallback.name;
    if (target.description.empty())
        target.description = fallback.description;
    if (target.author.empty())
        target.author = fallback.author;
    if (target.version.empty())
        target.version = fallback.version;
    if (target.entry_package.empty())
        target.entry_package = fallback.entry_package;
    if (target.dependencies.empty())
        target.dependencies = fallback.dependencies;
}

inline std::string filesystem_safe_escape(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.') {
            escaped += static_cast<char>(ch);
            continue;
        }
        if (escaped.empty() || escaped.back() != '_')
            escaped += '_';
    }
    return escaped.empty() ? "path" : escaped;
}

inline std::string sanitize_plugin_name(std::string name)
{
    std::replace(name.begin(), name.end(), ';', '_');
    return name;
}

inline bool is_uuid(const std::string& s)
{
    if (s.size() != 36)
        return false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char ch = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (ch != '-')
                return false;
        } else if (std::isxdigit(static_cast<unsigned char>(ch)) == 0) {
            return false;
        }
    }
    return true;
}

inline std::string make_local_plugin_key(const std::string& stem)
{
    return filesystem_safe_escape(stem);
}

}
