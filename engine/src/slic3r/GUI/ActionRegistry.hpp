#pragma once

#include <nlohmann/json.hpp>

#include <libslic3r/Config.hpp>

#include <wx/string.h>
#include <wx/thread.h>

#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r { namespace GUI {

enum class ActionChange { Added, Removed };

enum class AppActionKind { Plugin, Command };

struct AppActionRunResult
{
    enum class Level { Success, Info, Error, Busy };

    Level level = Level::Info;
    wxString message;
};

struct AppActionId
{
    std::string id;
};

struct AppAction
{
    const std::string& id() const { return m_id; }
    const std::string& title() const { return m_title; }
    const std::string& source_key() const { return m_source_key; }
    const std::string& source_name() const { return m_source_name; }
    static std::string compose_id(std::string_view prefix, std::string_view title, std::string_view source_key)
    {
        std::string out;
        out.reserve(prefix.size() + title.size() + source_key.size() + 2);
        out.append(prefix).append(1, ':').append(title).append(1, ':').append(source_key);
        return out;
    }

    bool favourite = false;
    int count      = 0;
    long long last = 0;
    AppActionKind kind = AppActionKind::Plugin;
    std::string group;
    std::string input;
    std::string icon;
    ConfigOptionMode required_mode = comSimple;
    std::string tooltip;
    std::string full_label;
    std::string help_url;

    virtual ~AppAction() = default;
    virtual AppActionRunResult run(const std::string& param = {}) const = 0;

protected:
    AppAction(std::string_view prefix, std::string title, std::string source_key, std::string source_name)
        : m_id(compose_id(prefix, title, source_key))
        , m_title(std::move(title))
        , m_source_key(std::move(source_key))
        , m_source_name(std::move(source_name))
    {}

    AppAction(AppActionId id, std::string title, std::string source_key, std::string source_name)
        : m_id(std::move(id.id)), m_title(std::move(title)), m_source_key(std::move(source_key)), m_source_name(std::move(source_name))
    {}

private:
    std::string m_id;
    std::string m_title;
    std::string m_source_key;
    std::string m_source_name;
};

inline constexpr const char* kOrcaSourceKey  = "orca";
inline constexpr const char* kOrcaSourceName = "OrcaSlicer";

inline bool requires_mode_switch(ConfigOptionMode setting_mode, ConfigOptionMode current_mode)
{
    return setting_mode > current_mode;
}

std::vector<std::string> cap_favourites(const std::vector<std::string>& ids, size_t limit);

class ActionRegistry
{
public:
    ~ActionRegistry();

    void init();

    void relocalize_builtins();

    void upsert(std::unique_ptr<AppAction> action);

    void remove(const std::string& id);

    const AppAction* by_id(const std::string& id) const;

    static constexpr size_t kFavLimit = 10;

    AppActionRunResult run(const std::string& id, const std::string& param = {});
    bool set_favourite(const std::string& id, bool on);
    void reorder_favourites(const std::vector<std::string>& ids);
    std::vector<std::string> favourite_ids() const;

    bool should_ask(const std::string& id) const;
    void suppress_ask(const std::string& id);

    bool tooltip_expanded() const;
    void set_tooltip_expanded(bool expanded);

    nlohmann::json snapshot();

    nlohmann::json tab_options() const;

private:
    void seed_state(AppAction& a) const;
    AppAction* find(const std::string& id);

    void load_persisted(nlohmann::json& stats, std::vector<std::string>& favs) const;

    void materialize_setting_actions();

    void materialize_plate_actions();

    void materialize_recent_project_actions();

    void refresh_source(const std::string& plugin_key, ActionChange change);
    void refresh_capability(const std::string& plugin_key, const std::string& capability, ActionChange change);
    void refresh_page_capability(const std::string& plugin_key, const std::string& capability, ActionChange change);

    bool m_started = false;
    std::unordered_map<std::string, std::shared_ptr<AppAction>> m_actions;
};

}}
