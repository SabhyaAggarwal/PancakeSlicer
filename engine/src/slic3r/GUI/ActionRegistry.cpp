#include "ActionRegistry.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "NativeCommands.hpp"
#include "Notebook.hpp"
#include "OptionsGroup.hpp"
#include "Plater.hpp"
#include "SettingsIndex.hpp"
#include "Tab.hpp"
#include "slic3r/plugin/PluginManager.hpp"

#include <cassert>
#include <boost/filesystem/operations.hpp>
#include <libslic3r/AppConfig.hpp>
#include <libslic3r/Config.hpp>
#include "slic3r/plugin/PluginDescriptor.hpp"
#include <memory>
#include "libslic3r/Preset.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include <slic3r/plugin/PythonPluginInterface.hpp>

#include <vector>
#include <utility>
#include <wx/app.h>
#include <wx/thread.h>

#include <boost/filesystem.hpp>
#include <boost/nowide/convert.hpp>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <exception>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Slic3r { namespace GUI {

std::vector<std::string> cap_favourites(const std::vector<std::string>& ids, size_t limit)
{
    std::vector<std::string> out;
    out.reserve(std::min(ids.size(), limit));
    for (const auto& id : ids) {
        if (out.size() >= limit)
            break;
        if (std::find(out.begin(), out.end(), id) == out.end())
            out.push_back(id);
    }
    return out;
}

namespace {

constexpr const char* kConfigSection = "speed_dial";

nlohmann::json parse_config_json(const std::string& value, nlohmann::json fallback)
{
    nlohmann::json parsed = nlohmann::json::parse(value, nullptr, false);
    return parsed.is_discarded() ? std::move(fallback) : parsed;
}

nlohmann::json read_section(const char* key, nlohmann::json fallback)
{ return parse_config_json(wxGetApp().app_config->get(kConfigSection, key), std::move(fallback)); }

void write_section(const char* key, const nlohmann::json& j) { wxGetApp().app_config->set(kConfigSection, key, j.dump()); }

std::vector<std::string> read_string_array(const char* key)
{
    auto j = read_section(key, nlohmann::json::array());
    std::vector<std::string> v;
    for (auto& e : j)
        if (e.is_string())
            v.push_back(e.get<std::string>());
    return v;
}

double frecency_score(int count, long long last, long long now)
{
    if (count <= 0)
        return 0.0;
    constexpr double HALF_LIFE_DAYS = 30.0;
    double age                      = std::max(0.0, double(now - last) / 86400.0);
    return count * std::pow(2.0, -age / HALF_LIFE_DAYS);
}

std::string find_loaded_source_name(PluginManager& manager, const std::string& plugin_key)
{
    PluginDescriptor descriptor;
    if (manager.try_get_plugin_descriptor(plugin_key, descriptor) && !descriptor.name.empty())
        return descriptor.name;
    return plugin_key;
}

struct PluginScriptAction : AppAction
{
    static constexpr const char* kIdPrefix = "plugin_script_action";

    std::string plugin_key;
    std::string capability;

    static std::string id_for(const std::string& plugin_key, const std::string& capability)
    { return AppAction::compose_id(kIdPrefix, capability.empty() ? plugin_key : capability, plugin_key); }

    PluginScriptAction(std::string plugin_key_in, std::string capability_in, std::string source_name)
        : AppAction(kIdPrefix,
                    capability_in.empty() ? plugin_key_in : capability_in,
                    plugin_key_in,
                    std::move(source_name))
        , plugin_key(std::move(plugin_key_in))
        , capability(std::move(capability_in))
    {}

    AppActionRunResult run(const std::string& ) const override
    {
        std::string error;
        const ExecutionResult result = PluginManager::instance().run_script_capability(plugin_key, capability, error);
        if (!error.empty())
            return {AppActionRunResult::Level::Error, from_u8(error)};

        const bool skipped      = result.status == PluginResult::Skipped;
        const wxString fallback = skipped ? _L("Script plugin skipped.") : _L("Script plugin finished.");
        return {skipped ? AppActionRunResult::Level::Info : AppActionRunResult::Level::Success,
                result.message.empty() ? fallback : from_u8(result.message)};
    }
};

std::unique_ptr<AppAction> make_action(const std::string& plugin_key, const std::string& capability, const std::string& source_name)
{
    PluginManager& manager = PluginManager::instance();
    if (!manager.is_plugin_loaded(plugin_key))
        return nullptr;
    if (!manager.get_plugin_capability({PluginCapabilityType::Script, capability, plugin_key}))
        return nullptr;
    return std::make_unique<PluginScriptAction>(plugin_key, capability, source_name);
}

struct PluginPageAction : AppAction
{
    static constexpr const char* kIdPrefix = "plugin_page_action";

    std::string plugin_key;
    std::string capability;

    static std::string id_for(const std::string& plugin_key, const std::string& capability)
    { return AppAction::compose_id(kIdPrefix, capability.empty() ? plugin_key : capability, plugin_key); }

    PluginPageAction(std::string plugin_key_in, std::string capability_in, std::string source_name)
        : AppAction(kIdPrefix,
                    capability_in.empty() ? plugin_key_in : capability_in,
                    plugin_key_in,
                    std::move(source_name))
        , plugin_key(std::move(plugin_key_in))
        , capability(std::move(capability_in))
    {
        this->kind = AppActionKind::Plugin;
    }

    AppActionRunResult run(const std::string& ) const override
    {
        MainFrame* mf = wxGetApp().mainframe;
        if (mf)
            mf->plugin_pages().select_page({PluginCapabilityType::Pages, capability, plugin_key});
        return {AppActionRunResult::Level::Success};
    }
};

std::unique_ptr<AppAction> make_page_action(const std::string& plugin_key, const std::string& capability, const std::string& source_name)
{
    PluginManager& manager = PluginManager::instance();
    if (!manager.is_plugin_loaded(plugin_key))
        return nullptr;
    if (!manager.get_plugin_capability({PluginCapabilityType::Pages, capability, plugin_key}))
        return nullptr;
    return std::make_unique<PluginPageAction>(plugin_key, capability, source_name);
}

constexpr const char* kSettingPrefix       = "orca_setting";
constexpr const char* kPlateGotoPrefix     = "orca_plate_goto";
constexpr const char* kRecentProjectPrefix = "orca_recent_project";

std::string setting_type_context(Preset::Type type)
{
    switch (type) {
    case Preset::TYPE_FILAMENT:
    case Preset::TYPE_SLA_MATERIAL: return _u8L("Filament");
    case Preset::TYPE_PRINTER: return _u8L("Printer");
    case Preset::TYPE_PRINT:
    case Preset::TYPE_SLA_PRINT:
    default: return _u8L("Process");
    }
}

const char* mode_key(ConfigOptionMode mode)
{
    switch (mode) {
    case comAdvanced: return "advanced";
    case comExpert: return "expert";
    case comDevelop: return "develop";
    default: return "simple";
    }
}

struct SettingAction : AppAction
{
    std::string opt_key;
    Preset::Type type;
    std::wstring category;

    static std::string id_for(const std::string& opt_key, Preset::Type type)
    { return std::string(kSettingPrefix) + ":" + opt_key + ":" + std::to_string(int(type)); }

    SettingAction(std::string opt_key_in,
                  Preset::Type type_in,
                  std::string title,
                  std::string group,
                  std::wstring category_in,
                  std::string source_name,
                  ConfigOptionMode mode_in)
        : AppAction(AppActionId{id_for(opt_key_in, type_in)}, std::move(title), kOrcaSourceKey, std::move(source_name))
        , opt_key(std::move(opt_key_in))
        , type(type_in)
        , category(std::move(category_in))
    {
        this->kind          = AppActionKind::Command;
        this->group         = std::move(group);
        this->required_mode = mode_in;
    }

    AppActionRunResult run(const std::string& ) const override
    {
        wxGetApp().sidebar().jump_to_option(opt_key, type, category);
        return {AppActionRunResult::Level::Success};
    }
};

void seed_from(const nlohmann::json& stats, const std::vector<std::string>& favs, const std::string& id, AppAction& a)
{
    a.favourite = std::find(favs.begin(), favs.end(), id) != favs.end();
    if (auto it = stats.find(id); it != stats.end() && it->is_object()) {
        a.count = it->value("count", 0);
        a.last  = it->value("last", 0LL);
    }
}

void drop_stale(std::unordered_map<std::string, std::shared_ptr<AppAction>>& actions, const char* prefix,
                const std::unordered_set<std::string>& seen)
{
    for (auto it = actions.begin(); it != actions.end();) {
        if (it->first.rfind(prefix, 0) == 0 && !seen.count(it->first))
            it = actions.erase(it);
        else
            ++it;
    }
}

struct PlateAction : AppAction
{
    int plate_index;

    static std::string id_for(int index) { return AppAction::compose_id(kPlateGotoPrefix, std::to_string(index), kOrcaSourceKey); }

    PlateAction(int index, std::string title, std::string source_name)
        : AppAction(AppActionId{id_for(index)}, std::move(title), kOrcaSourceKey, std::move(source_name)), plate_index(index)
    {
        this->kind  = AppActionKind::Command;
        this->group = _u8L("Plate");
    }

    AppActionRunResult run(const std::string& ) const override
    { return NativeCommands::run("plate_goto", std::to_string(plate_index)); }
};

struct RecentProjectAction : AppAction
{
    std::string file_path;

    static std::string id_for(const std::string& path) { return AppAction::compose_id(kRecentProjectPrefix, path, kOrcaSourceKey); }

    RecentProjectAction(std::string path, std::string title, std::string source)
        : AppAction(AppActionId{id_for(path)}, std::move(title), kOrcaSourceKey, std::move(source)), file_path(std::move(path))
    {
        this->kind  = AppActionKind::Command;
        this->group = _u8L("Recent Projects");
    }

    AppActionRunResult run(const std::string& ) const override
    {
        if (MainFrame* mf = wxGetApp().mainframe; mf)
            mf->open_recent_project(size_t(-1), wxString::FromUTF8(file_path));
        return {AppActionRunResult::Level::Success};
    }
};

}

ActionRegistry::~ActionRegistry() = default;

void ActionRegistry::init()
{
    assert(wxThread::IsMain());
    assert(!m_started);
    m_started = true;

    PluginManager& manager = PluginManager::instance();

    auto on_source = [this](const std::string& plugin_key, ActionChange change) {
        if (!wxTheApp || wxGetApp().is_closing())
            return;
        wxGetApp().CallAfter([this, plugin_key, change] {
            if (!wxGetApp().is_closing())
                this->refresh_source(plugin_key, change);
        });
    };
    auto on_capability = [this](const PluginCapabilityId& capability, ActionChange change) {
        if (capability.type != PluginCapabilityType::Script && capability.type != PluginCapabilityType::Pages)
            return;
        if (!wxTheApp || wxGetApp().is_closing())
            return;
        const PluginCapabilityType type = capability.type;
        const std::string plugin_key    = capability.plugin_key;
        const std::string name          = capability.name;
        wxGetApp().CallAfter([this, type, plugin_key, name, change] {
            if (wxGetApp().is_closing())
                return;
            if (type == PluginCapabilityType::Script)
                this->refresh_capability(plugin_key, name, change);
            else
                this->refresh_page_capability(plugin_key, name, change);
        });
    };

    manager.subscribe_on_load_callback([on_source](const std::string& key) { on_source(key, ActionChange::Added); });
    manager.subscribe_on_unload_callback([on_source](const std::string& key) { on_source(key, ActionChange::Removed); });
    manager.subscribe_on_capability_load_callback(
        [on_capability](const PluginCapabilityId& capability) { on_capability(capability, ActionChange::Added); });
    manager.subscribe_on_capability_unload_callback(
        [on_capability](const PluginCapabilityId& capability) { on_capability(capability, ActionChange::Removed); });

    std::unordered_map<std::string, std::string> source_names;
    for (const PluginDescriptor& desc : manager.get_plugin_descriptors())
        if (!desc.name.empty())
            source_names.emplace(desc.plugin_key, desc.name);

    for (const auto& capability : manager.get_plugin_capabilities("", PluginCapabilityType::Script)) {
        if (!capability)
            continue;
        const std::string& key         = capability->audit_plugin_key();
        auto it                        = source_names.find(key);
        const std::string& source_name = it == source_names.end() ? key : it->second;
        if (auto action = make_action(key, capability->name(), source_name))
            upsert(std::move(action));
    }

    for (const auto& capability : manager.get_plugin_capabilities("", PluginCapabilityType::Pages)) {
        if (!capability)
            continue;
        const std::string& key         = capability->audit_plugin_key();
        auto it                        = source_names.find(key);
        const std::string& source_name = it == source_names.end() ? key : it->second;
        if (auto action = make_page_action(key, capability->name(), source_name))
            upsert(std::move(action));
    }

    for (const NativeCommand& c : NativeCommands::catalog())
        upsert(NativeCommands::make_action(c));
}

void ActionRegistry::relocalize_builtins()
{
    assert(wxThread::IsMain());
    if (!m_started)
        return;

    std::vector<std::string> stale;
    for (const auto& [id, action] : m_actions)
        if (action->source_key() == kOrcaSourceKey && action->kind == AppActionKind::Command)
            stale.push_back(id);
    for (const std::string& id : stale)
        remove(id);

    NativeCommands::rebuild_catalog();
    for (const NativeCommand& c : NativeCommands::catalog())
        upsert(NativeCommands::make_action(c));
}

void ActionRegistry::refresh_source(const std::string& plugin_key, ActionChange change)
{
    assert(wxThread::IsMain());

    std::vector<std::string> stale;
    for (const auto& [id, action] : m_actions)
        if (action->source_key() == plugin_key)
            stale.push_back(id);
    for (const std::string& id : stale)
        remove(id);

    if (change == ActionChange::Removed)
        return;

    PluginManager& manager        = PluginManager::instance();
    const std::string source_name = find_loaded_source_name(manager, plugin_key);
    for (const auto& capability : manager.get_plugin_capabilities(plugin_key, PluginCapabilityType::Script)) {
        if (!capability)
            continue;
        if (auto action = make_action(plugin_key, capability->name(), source_name))
            upsert(std::move(action));
    }
    for (const auto& capability : manager.get_plugin_capabilities(plugin_key, PluginCapabilityType::Pages)) {
        if (!capability)
            continue;
        if (auto action = make_page_action(plugin_key, capability->name(), source_name))
            upsert(std::move(action));
    }
}

void ActionRegistry::refresh_capability(const std::string& plugin_key, const std::string& capability, ActionChange change)
{
    assert(wxThread::IsMain());

    const std::string id = PluginScriptAction::id_for(plugin_key, capability);
    if (change == ActionChange::Removed) {
        remove(id);
        return;
    }

    PluginManager& manager = PluginManager::instance();
    if (auto action = make_action(plugin_key, capability, find_loaded_source_name(manager, plugin_key)))
        upsert(std::move(action));
    else
        remove(id);
}

void ActionRegistry::refresh_page_capability(const std::string& plugin_key, const std::string& capability, ActionChange change)
{
    assert(wxThread::IsMain());

    const std::string id = PluginPageAction::id_for(plugin_key, capability);
    if (change == ActionChange::Removed) {
        remove(id);
        return;
    }

    PluginManager& manager = PluginManager::instance();
    if (auto action = make_page_action(plugin_key, capability, find_loaded_source_name(manager, plugin_key)))
        upsert(std::move(action));
    else
        remove(id);
}

void ActionRegistry::upsert(std::unique_ptr<AppAction> action)
{
    assert(wxThread::IsMain());
    if (!action)
        return;

    seed_state(*action);
    std::string id                    = action->id();
    std::shared_ptr<AppAction> stored = std::move(action);
    m_actions.insert_or_assign(std::move(id), std::move(stored));
}

void ActionRegistry::remove(const std::string& id)
{
    assert(wxThread::IsMain());
    m_actions.erase(id);
}

void ActionRegistry::seed_state(AppAction& a) const
{
    auto favs   = favourite_ids();
    a.favourite = std::find(favs.begin(), favs.end(), a.id()) != favs.end();

    nlohmann::json stats = read_section("stats", nlohmann::json::object());
    auto it              = stats.find(a.id());
    if (it != stats.end() && it->is_object()) {
        a.count = it->value("count", 0);
        a.last  = it->value("last", 0LL);
    } else {
        a.count = 0;
        a.last  = 0;
    }
}

void ActionRegistry::load_persisted(nlohmann::json& stats, std::vector<std::string>& favs) const
{
    stats = read_section("stats", nlohmann::json::object());
    if (!stats.is_object())
        stats = nlohmann::json::object();
    favs = favourite_ids();
}

const AppAction* ActionRegistry::by_id(const std::string& id) const
{
    assert(wxThread::IsMain());
    auto it = m_actions.find(id);
    return it == m_actions.end() ? nullptr : it->second.get();
}

AppAction* ActionRegistry::find(const std::string& id) { return const_cast<AppAction*>(by_id(id)); }

AppActionRunResult ActionRegistry::run(const std::string& id, const std::string& param)
{
    assert(wxThread::IsMain());
    auto it = m_actions.find(id);
    if (it == m_actions.end())
        return {};
    std::shared_ptr<AppAction> keep = it->second;
    AppActionRunResult o            = keep->run(param);
    if (o.level == AppActionRunResult::Level::Busy)
        return o;

    nlohmann::json stats = read_section("stats", nlohmann::json::object());
    if (!stats.is_object())
        stats = nlohmann::json::object();
    nlohmann::json& e = stats[id];
    if (!e.is_object())
        e = nlohmann::json::object();
    e["count"] = e.value("count", 0) + 1;
    e["last"]  = (long long) std::time(nullptr);
    write_section("stats", stats);
    if (AppAction* live = find(id)) {
        live->count = e["count"];
        live->last  = e["last"];
    }
    return o;
}

bool ActionRegistry::set_favourite(const std::string& id, bool on)
{
    assert(wxThread::IsMain());
    auto favs = favourite_ids();
    auto it   = std::find(favs.begin(), favs.end(), id);
    if (on && it == favs.end()) {
        if (favs.size() >= kFavLimit)
            return false;
        favs.push_back(id);
    }
    if (!on && it != favs.end())
        favs.erase(it);
    write_section("favourite_actions", nlohmann::json(favs));
    if (AppAction* live = find(id))
        live->favourite = on;
    return true;
}

std::vector<std::string> ActionRegistry::favourite_ids() const
{
    assert(wxThread::IsMain());
    return cap_favourites(read_string_array("favourite_actions"), kFavLimit);
}

void ActionRegistry::reorder_favourites(const std::vector<std::string>& ids)
{
    assert(wxThread::IsMain());
    auto cur = read_string_array("favourite_actions");
    std::vector<std::string> next;
    for (const auto& id : ids)
        if (std::find(cur.begin(), cur.end(), id) != cur.end() && std::find(next.begin(), next.end(), id) == next.end())
            next.push_back(id);
    for (const auto& id : cur)
        if (std::find(next.begin(), next.end(), id) == next.end())
            next.push_back(id);
    next = cap_favourites(next, kFavLimit);
    write_section("favourite_actions", nlohmann::json(next));
}

void ActionRegistry::materialize_setting_actions()
{
    assert(wxThread::IsMain());

    const std::vector<Search::Option>& options = wxGetApp().sidebar().settings_index().all_options();

    nlohmann::json           stats;
    std::vector<std::string> favs;
    load_persisted(stats, favs);

    std::unordered_set<std::string> seen;
    for (const Search::Option& opt : options) {
        Tab* tab = wxGetApp().get_tab(opt.type);
        Tab::SettingRowState row;
        if (tab)
            row = tab->setting_row_state(opt.opt_key());
        if (!row.visible)
            continue;

        const std::string id = SettingAction::id_for(opt.opt_key(), opt.type);
        seen.insert(id);

        const std::string search_label = boost::nowide::narrow(opt.label_local.empty() ? opt.label : opt.label_local);
        std::string       title        = into_u8(Search::resolve_setting_title(from_u8(opt.display_label), row.label, row.multi));
        if (title.empty())
            title = search_label;

        std::wstring path = boost::nowide::widen(setting_type_context(opt.type));
        if (!opt.category_local.empty())
            path += L" : " + opt.category_local;
        if (!opt.group_local.empty())
            path += L" : " + opt.group_local;

        auto action = std::make_unique<SettingAction>(opt.opt_key(), opt.type, title, std::string(), opt.category,
                                                      boost::nowide::narrow(path), opt.mode);
        if (title != search_label)
            action->full_label = search_label;

        action->icon = opt.group_icon;
        if (action->icon.empty() && !opt.category.empty() && tab) {
            const auto& icons = tab->get_category_icon_map();
            auto        it    = icons.find(wxString(opt.category));
            if (it != icons.end())
                action->icon = it->second;
        }

        action->tooltip = opt.tooltip;
        if (!opt.wiki_path.empty())
            action->help_url = into_u8(OptionsGroup::get_url(opt.wiki_path));

        seed_from(stats, favs, id, *action);
        auto const action_id  = action->id();
        auto const app_action = std::shared_ptr<AppAction>(std::move(action));
        m_actions.insert_or_assign(action_id, app_action);
    }

    drop_stale(m_actions, kSettingPrefix, seen);
}

void ActionRegistry::materialize_plate_actions()
{
    assert(wxThread::IsMain());

    Plater* plater = wxTheApp ? wxGetApp().plater() : nullptr;
    if (!plater || plater->printer_technology() != ptFFF || plater->only_gcode_mode()) {
        drop_stale(m_actions, kPlateGotoPrefix, {});
        return;
    }

    nlohmann::json           stats;
    std::vector<std::string> favs;
    load_persisted(stats, favs);

    const std::vector<PartPlate*>& list = plater->get_partplate_list().get_plate_list();
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < list.size(); ++i) {
        PartPlate* plate = list[i];
        if (!plate)
            continue;
        const std::string id = PlateAction::id_for(int(i));
        seen.insert(id);

        std::string title(_u8L("Go to Plate"));
        title += " " + std::to_string(i + 1);
        const std::string name = plate->get_plate_name();
        if (!name.empty())
            title += " (" + name + ")";

        auto action = std::make_unique<PlateAction>(int(i), title, kOrcaSourceName);
        seed_from(stats, favs, id, *action);
        auto const action_id  = action->id();
        auto const app_action = std::shared_ptr<AppAction>(std::move(action));
        m_actions.insert_or_assign(action_id, app_action);
    }

    drop_stale(m_actions, kPlateGotoPrefix, seen);
}

void ActionRegistry::materialize_recent_project_actions()
{
    assert(wxThread::IsMain());

    nlohmann::json           stats;
    std::vector<std::string> favs;
    load_persisted(stats, favs);

    std::vector<std::string> recents = wxGetApp().app_config->get_recent_projects();
    std::reverse(recents.begin(), recents.end());

    std::unordered_set<std::string> seen;
    for (const std::string& path : recents) {
        boost::system::error_code ec;
        if (path.empty() || !boost::filesystem::exists(boost::filesystem::path(path), ec))
            continue;

        const std::string id = RecentProjectAction::id_for(path);
        seen.insert(id);

        boost::filesystem::path p(path);
        std::string title = p.filename().string();
        if (title.empty())
            title = path;

        auto action = std::make_unique<RecentProjectAction>(path, std::move(title), path);
        seed_from(stats, favs, id, *action);
        auto const action_id  = action->id();
        auto const app_action = std::shared_ptr<AppAction>(std::move(action));
        m_actions.insert_or_assign(action_id, app_action);
    }

    drop_stale(m_actions, kRecentProjectPrefix, seen);
}

bool ActionRegistry::should_ask(const std::string& id) const
{
    assert(wxThread::IsMain());
    auto arr = read_string_array("ask_suppressed");
    return std::find(arr.begin(), arr.end(), id) == arr.end();
}

void ActionRegistry::suppress_ask(const std::string& id)
{
    assert(wxThread::IsMain());
    auto arr = read_string_array("ask_suppressed");
    if (std::find(arr.begin(), arr.end(), id) == arr.end())
        arr.push_back(id);
    write_section("ask_suppressed", nlohmann::json(arr));
}

bool ActionRegistry::tooltip_expanded() const
{
    assert(wxThread::IsMain());
    const nlohmann::json j = read_section("tooltip_expanded", nlohmann::json(true));
    return j.is_boolean() ? j.get<bool>() : true;
}

void ActionRegistry::set_tooltip_expanded(bool expanded)
{
    assert(wxThread::IsMain());
    write_section("tooltip_expanded", nlohmann::json(expanded));
}

nlohmann::json ActionRegistry::snapshot()
{
    assert(wxThread::IsMain());
    materialize_setting_actions();
    materialize_plate_actions();
    materialize_recent_project_actions();

    std::vector<const AppAction*> sorted;
    sorted.reserve(m_actions.size());
    for (const auto& entry : m_actions)
        sorted.push_back(entry.second.get());

    const long long now = (long long) std::time(nullptr);
    std::sort(sorted.begin(), sorted.end(), [&](const AppAction* a, const AppAction* b) {
        double sa = frecency_score(a->count, a->last, now);
        double sb = frecency_score(b->count, b->last, now);
        if (sa != sb)
            return sa > sb;
        if (a->title() != b->title())
            return a->title() < b->title();
        if (a->source_name() != b->source_name())
            return a->source_name() < b->source_name();
        return a->id() < b->id();
    });

    auto action_to_json = [](const AppAction* a) {
        return nlohmann::json({{"id", a->id()},
                               {"title", a->title()},
                               {"full_label", a->full_label},
                               {"source", a->source_name()},
                               {"group", a->group},
                               {"kind", a->kind == AppActionKind::Plugin ? "plugin" : "command"},
                               {"input", a->input},
                               {"icon", a->icon},
                               {"mode", mode_key(a->required_mode)},
                               {"desc", a->tooltip},
                               {"wiki", !a->help_url.empty()}});
    };

    nlohmann::json actions = nlohmann::json::array();
    for (const AppAction* a : sorted)
        actions.push_back(action_to_json(a));

    std::vector<std::string> favs = favourite_ids();
    std::vector<std::string> live_favs;
    live_favs.reserve(favs.size());
    for (const auto& id : favs)
        if (m_actions.count(id))
            live_favs.push_back(id);
    if (live_favs.size() != favs.size())
        write_section("favourite_actions", nlohmann::json(live_favs));
    nlohmann::json favourites(live_favs);

    const size_t recent_limit = size_t(wxGetApp().app_config->get_speed_dial_recent_count());
    std::vector<const AppAction*> recent;
    for (const auto& entry : m_actions)
        if (entry.second->last > 0)
            recent.push_back(entry.second.get());
    std::sort(recent.begin(), recent.end(), [](const AppAction* a, const AppAction* b) {
        if (a->last != b->last)
            return a->last > b->last;
        return a->id() < b->id();
    });
    if (recent.size() > recent_limit)
        recent.resize(recent_limit);
    nlohmann::json recent_json = nlohmann::json::array();
    for (const AppAction* a : recent)
        recent_json.push_back(action_to_json(a));

    return {{"actions", std::move(actions)},
            {"favourites", std::move(favourites)},
            {"recent", std::move(recent_json)},
            {"user_mode", mode_key(wxGetApp().get_mode())},
            {"tooltip_expanded", tooltip_expanded()}};
}

nlohmann::json ActionRegistry::tab_options() const
{
    assert(wxThread::IsMain());
    nlohmann::json out = nlohmann::json::array();
    if (!wxTheApp || wxGetApp().is_closing())
        return out;
    MainFrame* mf = wxGetApp().mainframe;
    if (!mf || !mf->m_tabpanel)
        return out;
    Notebook* notebook = mf->m_tabpanel;
    for (size_t i = 0; i < notebook->GetPageCount(); ++i) {
        const wxString id = notebook->GetPageName(i);
        if (id.empty())
            continue;
        out.push_back({{"id", id.ToStdString()},
                       {"title", notebook->GetPageLabel(i).ToStdString()},
                       {"icon", notebook->GetPageIcon(i)}});
    }
    return out;
}

}}
