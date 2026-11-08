#ifndef slic3r_SettingsIndex_hpp_
#define slic3r_SettingsIndex_hpp_

#include <algorithm>
#include <cstddef>
#include "libslic3r/PrintConfig.hpp"
#include <map>
#include <string>
#include <vector>

#include <wx/string.h>

#include <libslic3r/Config.hpp>
#include <libslic3r/Preset.hpp>

namespace Slic3r {
namespace Search {

struct InputInfo
{
    DynamicPrintConfig *config{nullptr};
    Preset::Type        type{Preset::TYPE_INVALID};
    ConfigOptionMode    mode{comSimple};
};

struct GroupAndCategory
{
    wxString group;
    wxString category;
    wxString icon;
    wxString line_label;
    std::string path;
};

inline wxString compose_display_label(const wxString& line_label, const wxString& leaf_label, bool multi)
{
    if (line_label.empty())
        return leaf_label;
    if (!multi || leaf_label.empty() || leaf_label == line_label)
        return line_label;
    return line_label + L" \u2013 " + leaf_label;
}

inline wxString resolve_setting_title(const wxString& precomposed, const wxString& live_label, bool live_multi)
{
    if (!live_multi && !live_label.empty())
        return live_label;
    return precomposed;
}

struct Option
{
    bool operator<(const Option &other) const { return other.key > this->key; }

    std::wstring key;
    Preset::Type type{Preset::TYPE_INVALID};
    std::wstring label;
    std::wstring label_local;
    std::wstring group;
    std::wstring group_local;
    std::string  group_icon;
    std::wstring category;
    std::wstring category_local;
    bool multi_category { false };
    ConfigOptionMode mode{comSimple};
    std::string  tooltip;
    std::string  wiki_path;
    std::string  display_label;

    std::string opt_key() const;
};

class SettingsIndex
{
    std::map<std::string, GroupAndCategory> m_groups_and_categories;

    std::vector<Option> m_options;
    std::vector<Option> m_all_modes;

    void append_options(DynamicPrintConfig *config, Preset::Type type, ConfigOptionMode mode);
    void sort_options();

public:
    void init(std::vector<InputInfo> input_values);
    bool apply(DynamicPrintConfig *config, Preset::Type type, ConfigOptionMode mode);

    void add_key(const std::string &opt_key, Preset::Type type, const wxString &group, const wxString &category,
                 const wxString &icon = wxEmptyString);
    void set_path(const std::string &opt_key, Preset::Type type, const std::string &path);
    void set_line_label(const std::string &opt_key, Preset::Type type, const wxString &label);

    const std::vector<Option> &options() const { return m_options; }
    const std::vector<Option> &all_options() const { return m_all_modes; }
    const Option &             option_at(size_t pos) const { return m_options[pos]; }

    const Option &get_option(const std::string &opt_key, Preset::Type type, int &variant_index) const;
    Option        get_option(const std::string &opt_key, const wxString &label, Preset::Type type) const;

    const GroupAndCategory &get_group_and_category(const std::string &opt_key) { return m_groups_and_categories[opt_key]; }

    void sort_options_by_key()
    {
        std::sort(m_options.begin(), m_options.end(), [](const Option &o1, const Option &o2) { return o1.key < o2.key; });
    }
    void sort_options_by_label() { sort_options(); }
};

}
}

#endif
