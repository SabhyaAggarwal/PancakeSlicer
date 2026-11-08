#ifndef slic3r_IMEXModesCtrl_hpp_
#define slic3r_IMEXModesCtrl_hpp_

#include <libslic3r/PrintConfig.hpp>
#include <utility>
#include <wx/colour.h>
#include <wx/panel.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/textctrl.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "libslic3r/IMEXHelpers.hpp"

class ScalableButton;

class ComboBox;

namespace Slic3r {
namespace GUI {

class IMEXModesCtrl : public wxPanel {
public:
    std::function<void()> on_change;

    std::function<void(const std::vector<std::string>&)> on_mode_removed;

    using ParentConfigLookup = std::function<const DynamicPrintConfig*()>;
    void set_parent_config_lookup(ParentConfigLookup f) { m_parent_lookup = std::move(f); }

    static int parse_layout(ImexToolLayout layout) { return static_cast<int>(layout); }

    IMEXModesCtrl(wxWindow* parent, int n_cols, int n_rows, int layout = 0);

    void set_grid_size(int n_cols, int n_rows, int layout = -1);

    void load_from_config(const DynamicPrintConfig& cfg);

    std::tuple<std::vector<std::string>, std::vector<std::string>, std::vector<std::string>>
    get_mode_data() const;

    bool matches_config(const DynamicPrintConfig& cfg) const;

    void set_applicable(bool applicable)
    {
        m_applicable = applicable;
        if (!applicable)
            wxPanel::Show(false);
    }
    bool Show(bool show = true) override { return wxPanel::Show(show && m_applicable); }

private:
    bool m_applicable = true;

    static std::map<int, ImexRole> roles_for_mode(const std::string& active_tools);

    std::string unique_mode_name(const std::vector<std::string>& also_taken, const std::string& base = "Mode") const;

    struct RoleStyle { wxColour bg; wxColour fg; const char* label; };
    static RoleStyle role_style(std::optional<ImexRole> role);

    bool role_offered(ImexRole role) const;

    bool role_allowed_on_tile(ImexRole role, int tool_idx,
                              bool other_primary, int primary_gantry) const;

    static std::optional<ImexRole> next_tile_role(std::optional<ImexRole> current,
                                                  const std::function<bool(ImexRole)>& allowed);

    std::vector<wxString> suggested_mode_names(const ::ComboBox* skip = nullptr) const;

    void apply_btn(wxButton* btn, int tool_idx, std::optional<ImexRole> role);

    struct Row {
        wxPanel*             panel;
        ::ComboBox*          name;
        wxTextCtrl*          gcode;
        std::vector<wxButton*> btns;
        std::vector<std::optional<ImexRole>> btn_roles;
        std::vector<int>     btn_tool_idx;
        std::map<int, ImexRole> all_tool_roles;
        std::string          orig_name;
        std::string          focus_name;
        bool                 is_primary {false};
        ScalableButton*      reset_btn {nullptr};
        bool                 reset_dirty_cached {false};
    };

    struct RowSnapshot { std::string name, tools, gcode; };
    RowSnapshot snapshot_row(const Row& r) const;

    bool row_differs_from_parent(int row_idx) const;

    bool row_has_parent_counterpart(int row_idx) const;

    void add_row(const std::string& name = "",
                 const std::string& active_tools = "",
                 const std::string& gcode = "",
                 bool is_primary = false);

    std::vector<std::string> remove_row(wxPanel* panel);

    void clear_rows();

    std::string active_tools_string(const Row& r) const;

    bool fix_row_name(Row& r);

    void notify();

    void refresh_reset_buttons();

    void reset_row_to_parent(wxPanel* panel);

    void rebuild_info_and_header();

    static wxString role_hint(ImexRole role);

    wxBoxSizer*         m_outer;
    wxBoxSizer*         m_rows_sizer;
    wxPanel*            m_info_panel = nullptr;
    wxPanel*            m_hdr_panel  = nullptr;
    std::vector<Row>    m_rows;
    int                 m_n_cols, m_n_rows;
    int                 m_layout {0};
    bool                m_clearing_rows {false};
    ParentConfigLookup  m_parent_lookup;
};

}}

#endif
