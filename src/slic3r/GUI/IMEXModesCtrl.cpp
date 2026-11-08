#include "slic3r/GUI/IMEXModesCtrl.hpp"

#include <wx/anybutton.h>
#include <libslic3r/IMEXHelpers.hpp>
#include <cstddef>
#include <vector>
#include <tuple>
#include <libslic3r/Config.hpp>
#include <map>
#include <string>
#include <optional>
#include <functional>
#include <wx/app.h>
#include <wx/event.h>
#include <wx/colour.h>
#include <wx/font.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/gdicmn.h>
#include <wx/stattext.h>

#include <algorithm>
#include <boost/algorithm/string/predicate.hpp>
#include <utility>
#include <wx/string.h>

#include "slic3r/GUI/EditGCodeDialog.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/ComboBox.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

namespace Slic3r {
namespace GUI {

static constexpr int kImexIconPx = 20;

static constexpr int kNameColPx = 176;

IMEXModesCtrl::IMEXModesCtrl(wxWindow* parent, int n_cols, int n_rows, int layout)
    : wxPanel(parent, wxID_ANY), m_n_cols(std::max(1, n_cols)), m_n_rows(std::max(1, n_rows)), m_layout(layout)
{
    SetBackgroundColour(parent->GetBackgroundColour());

    m_outer = new wxBoxSizer(wxVERTICAL);
    m_rows_sizer = new wxBoxSizer(wxVERTICAL);

    m_info_panel = new wxPanel(this, wxID_ANY);
    m_info_panel->SetBackgroundColour(GetBackgroundColour());
    m_hdr_panel = new wxPanel(this, wxID_ANY);
    m_hdr_panel->SetBackgroundColour(GetBackgroundColour());
    rebuild_info_and_header();

    m_outer->Add(m_info_panel, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    m_outer->Add(m_hdr_panel,  0, wxEXPAND | wxBOTTOM, FromDIP(2));
    m_outer->Add(m_rows_sizer, 0, wxEXPAND);
    m_outer->AddSpacer(FromDIP(4));
    SetSizer(m_outer);
}

static wxPanel* framed_input(wxWindow* parent)
{
    wxColour clr(0xDB, 0xDB, 0xDB);
    if (wxGetApp().dark_mode())
        clr = StateColor::darkModeColorFor(clr);
    auto* frame = new wxPanel(parent, wxID_ANY);
    frame->SetBackgroundColour(clr);
    frame->SetSizer(new wxBoxSizer(wxVERTICAL));
    return frame;
}

static wxStaticText* page_label(wxStaticText* lbl)
{
    lbl->SetForegroundColour(wxColour("#363636"));
    wxGetApp().UpdateDarkUI(lbl);
    return lbl;
}

void IMEXModesCtrl::rebuild_info_and_header() {
    m_info_panel->DestroyChildren();
    auto* info_sizer = new wxBoxSizer(wxVERTICAL);

    const wxString instructions =
        _L("Each mode names the tool heads that take part and the role each one plays. "
           "Click a tool button to cycle its role — hover a color below for what each role does.");

    auto* leg_sizer = new wxBoxSizer(wxHORIZONTAL);
    const int swatch_side = m_info_panel->GetCharHeight();

    auto* act_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto* add_btn = new Button(m_info_panel, _L("Add Mode"));
    add_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Window);
    add_btn->SetMinSize(wxSize(FromDIP(130), add_btn->GetBestSize().GetHeight()));
    add_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { add_row(); notify(); });
    act_sizer->Add(add_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    auto* help_btn = new ScalableButton(m_info_panel, wxID_ANY, "icon_qusetion", wxEmptyString,
                                        wxDefaultSize, wxDefaultPosition,
                                        wxBU_EXACTFIT | wxNO_BORDER, true, kImexIconPx);
    help_btn->SetToolTip(instructions);
    act_sizer->Add(help_btn, 0, wxALIGN_CENTER_VERTICAL);
    info_sizer->Add(act_sizer, 0, wxBOTTOM, FromDIP(6));
    for (const ImexRoleDesc& d : kImexRoleTable) {
        if (!role_offered(d.role)) continue;
        const RoleStyle style = role_style(d.role);
        auto* swatch = new wxPanel(m_info_panel, wxID_ANY, wxDefaultPosition, wxSize(swatch_side, swatch_side));
        swatch->SetBackgroundColour(style.bg);
        auto* leg_label = page_label(new wxStaticText(m_info_panel, wxID_ANY, style.label));
        const wxString hint = role_hint(d.role);
        swatch->SetToolTip(hint);
        leg_label->SetToolTip(hint);
        leg_sizer->Add(swatch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
        leg_sizer->Add(leg_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(16));
    }
    info_sizer->Add(leg_sizer, 0, wxBOTTOM, FromDIP(6));
    m_info_panel->SetSizerAndFit(info_sizer);

    m_hdr_panel->DestroyChildren();
    auto* hdr_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto* hdr_name  = page_label(new wxStaticText(m_hdr_panel, wxID_ANY, _L("Mode")));
    auto* hdr_tools = page_label(new wxStaticText(m_hdr_panel, wxID_ANY, _L("Tools")));
    auto* hdr_gcode = page_label(new wxStaticText(m_hdr_panel, wxID_ANY, _L("G-code")));
    hdr_name->SetToolTip(_L("How the mode is labeled in the plate's IDEX/IQEX mode selector. "
                            "Required — a mode with no name cannot be selected."));
    hdr_tools->SetToolTip(_L("Which tool heads take part in the mode and what role each one plays. "
                             "Click a tile to cycle its role."));
    hdr_gcode->SetToolTip(_L("G-code run at print start to put the printer into this mode "
                             "(for example a Klipper SET_PRINT_MODE call). The slicer only emits "
                             "the Primary tool's paths; the firmware drives the others."));
    const int grid_px = m_n_cols * FromDIP(26) - FromDIP(2);
    const int name_col_px = FromDIP(kNameColPx + 6);
    hdr_sizer->Add(hdr_name,  0, wxALIGN_CENTER_VERTICAL);
    hdr_sizer->AddSpacer(std::max(0, name_col_px - hdr_name->GetBestSize().x));
    hdr_sizer->Add(hdr_tools, 0, wxALIGN_CENTER_VERTICAL);
    hdr_sizer->AddSpacer(std::max(0, grid_px + FromDIP(6) - hdr_tools->GetBestSize().x));
    hdr_sizer->Add(hdr_gcode, 1, wxALIGN_CENTER_VERTICAL);
    m_hdr_panel->SetSizerAndFit(hdr_sizer);
}

void IMEXModesCtrl::set_grid_size(int n_cols, int n_rows, int layout) {
    n_cols = std::max(1, n_cols);
    n_rows = std::max(1, n_rows);
    if (layout < 0) layout = m_layout;
    if (n_cols == m_n_cols && n_rows == m_n_rows && layout == m_layout) return;
    auto [names, tools, gcodes] = get_mode_data();
    clear_rows();
    m_n_cols = n_cols;
    m_n_rows = n_rows;
    m_layout = layout;
    rebuild_info_and_header();
    for (size_t i = 0; i < names.size(); ++i)
        add_row(names[i], tools[i], gcodes[i], (names[i] == kImexPrimaryMode));
    Layout();
}

void IMEXModesCtrl::load_from_config(const DynamicPrintConfig& cfg) {
    clear_rows();
    const std::vector<ImexMode> table = imex_mode_table(cfg);

    const ImexMode primary = find_imex_mode(cfg, kImexPrimaryMode);
    add_row(kImexPrimaryMode, primary.active_tools, primary.gcode, true);

    for (const ImexMode& m : table) {
        if (m.index == primary.index) continue;
        add_row(m.name, m.active_tools, m.gcode);
    }
    refresh_reset_buttons();
    Layout();
}

std::tuple<std::vector<std::string>, std::vector<std::string>, std::vector<std::string>>
IMEXModesCtrl::get_mode_data() const {
    std::vector<std::string> names, tools, gcodes;
    for (auto& r : m_rows) {
        std::string nm = r.is_primary ? std::string(kImexPrimaryMode) : into_u8(r.name->GetTextCtrl()->GetValue());
        if (nm.empty()) nm = unique_mode_name(names);
        names.push_back(nm);
        tools.push_back(active_tools_string(r));
        gcodes.push_back(into_u8(r.gcode->GetValue()));
    }
    return {names, tools, gcodes};
}

bool IMEXModesCtrl::matches_config(const DynamicPrintConfig& cfg) const {
    auto [names, tools, gcodes] = get_mode_data();
    auto cfg_strings = [&cfg](const char* key) {
        std::vector<std::string> v;
        if (auto* o = cfg.option<ConfigOptionStrings>(key)) v = o->values;
        return v;
    };
    return names  == cfg_strings("imex_mode_names")
        && tools  == cfg_strings("imex_mode_active_tools")
        && gcodes == cfg_strings("imex_mode_gcodes");
}

std::map<int, ImexRole> IMEXModesCtrl::roles_for_mode(const std::string& active_tools) {
    std::map<int, ImexRole> roles;
    for (const auto& [phys, role] : parse_imex_active_tools(active_tools))
        roles[phys] = role;
    const int primary = imex_primary_tool_for_mode(active_tools);
    if (primary >= 0) {
        auto it = roles.find(primary);
        if (it != roles.end())
            it->second = ImexRole::Primary;
    }
    return roles;
}

std::string IMEXModesCtrl::unique_mode_name(const std::vector<std::string>& also_taken, const std::string& base) const {
    auto is_taken = [&](const std::string& cand) {
        if (std::find(also_taken.begin(), also_taken.end(), cand) != also_taken.end())
            return true;
        for (const auto& r : m_rows)
            if (!r.is_primary && r.name && into_u8(r.name->GetTextCtrl()->GetValue()) == cand)
                return true;
        return false;
    };
    for (int n = 2; ; ++n) {
        std::string cand = base + " " + std::to_string(n);
        if (!is_taken(cand))
            return cand;
    }
}

bool IMEXModesCtrl::fix_row_name(Row& r) {
    if (r.is_primary || !r.name)
        return false;
    const std::string name = into_u8(r.name->GetTextCtrl()->GetValue());
    const bool reserved = boost::iequals(name, kImexPrimaryMode);
    bool taken = false;
    for (const Row& other : m_rows)
        if (&other != &r && !other.is_primary && other.name && into_u8(other.name->GetTextCtrl()->GetValue()) == name)
            taken = true;
    if (!name.empty() && !reserved && !taken)
        return false;
    std::string base = name;
    if (const size_t sp = base.find_last_of(' '); sp != std::string::npos && sp > 0 && sp + 1 < base.size() &&
        base.find_first_not_of("0123456789", sp + 1) == std::string::npos)
        base.erase(sp);
    r.name->GetTextCtrl()->ChangeValue(from_u8(name.empty() || reserved ? unique_mode_name({}) : unique_mode_name({}, base)));
    return true;
}

IMEXModesCtrl::RoleStyle IMEXModesCtrl::role_style(std::optional<ImexRole> role) {
    if (!role)
        return { wxColour(90, 90, 90), *wxWHITE, "Inactive" };
    switch (*role) {
    case ImexRole::Primary: return { wxColour(50,  160, 50),  *wxWHITE, "Primary" };
    case ImexRole::Copy:    return { wxColour(60,  120, 210), *wxWHITE, "Copy"    };
    case ImexRole::Mirror:  return { wxColour(210, 130, 20),  *wxWHITE, "Mirror"  };
    case ImexRole::Span:    return { wxColour(180, 180, 40),  *wxWHITE, "Span"    };
    }
    return { wxColour(90, 90, 90), *wxWHITE, "Inactive" };
}

wxString IMEXModesCtrl::role_hint(ImexRole role) {
    switch (role) {
    case ImexRole::Primary:
        return _L("Drives every sliced path. Always tool 0 — use Tool 0 Position above to choose "
                  "which corner of the bed it occupies. It cannot be moved from the grid.");
    case ImexRole::Copy:
        return _L("Follows the Primary at the firmware level, printing the same paths offset into "
                  "its own zone.");
    case ImexRole::Mirror:
        return _L("Follows the Primary at the firmware level, printing the same paths reflected "
                  "across the boundary between the two zones.");
    case ImexRole::Span:
        return _L("Marks a tool on the Primary's gantry as its multicolor partner. Required to "
                  "allow multi-color printing in paired-gantry modes.");
    }
    return wxEmptyString;
}

bool IMEXModesCtrl::role_offered(ImexRole role) const {
    if (role == ImexRole::Span)
        return m_n_rows >= 2;
    return true;
}

bool IMEXModesCtrl::role_allowed_on_tile(ImexRole role, int tool_idx,
                                         bool other_primary, int primary_gantry) const {
    if (!role_offered(role))
        return false;
    if (role == ImexRole::Primary)
        return !other_primary;
    if (role == ImexRole::Span)
        return primary_gantry >= 0 && (tool_idx / m_n_cols) == primary_gantry;
    return true;
}

std::optional<ImexRole> IMEXModesCtrl::next_tile_role(std::optional<ImexRole> current,
                                                      const std::function<bool(ImexRole)>& allowed) {
    const int n = (int)(sizeof(kImexRoleTable) / sizeof(kImexRoleTable[0]));
    int start = 0;
    if (current) {
        for (int i = 0; i < n; ++i)
            if (kImexRoleTable[i].role == *current) { start = i + 1; break; }
    }
    for (int i = start; i < n; ++i)
        if (allowed(kImexRoleTable[i].role))
            return kImexRoleTable[i].role;
    return std::nullopt;
}

std::vector<wxString> IMEXModesCtrl::suggested_mode_names(const ::ComboBox* skip) const {
    std::vector<std::string> pool = { "copy", "mirror" };
    if (m_n_cols * m_n_rows >= 4)
        for (const char* n : { "iq-copy", "iq-mirror" })
            pool.emplace_back(n);
    if (m_n_cols >= 2 && m_n_rows >= 2)
        for (const char* n : { "mc-copy", "mc-mirror" })
            pool.emplace_back(n);

    std::vector<wxString> out;
    for (const std::string& cand : pool) {
        bool taken = false;
        for (const Row& r : m_rows)
            if (!r.is_primary && r.name && r.name != skip &&
                into_u8(r.name->GetTextCtrl()->GetValue()) == cand) {
                taken = true;
                break;
            }
        if (!taken)
            out.push_back(from_u8(cand));
    }
    return out;
}

void IMEXModesCtrl::apply_btn(wxButton* btn, int tool_idx, std::optional<ImexRole> role) {
    const RoleStyle style = role_style(role);
    btn->SetLabel(wxString::Format("T%d", tool_idx));
    btn->SetBackgroundColour(style.bg);
    btn->SetForegroundColour(style.fg);
    btn->Refresh();
}

IMEXModesCtrl::RowSnapshot IMEXModesCtrl::snapshot_row(const Row& r) const {
    RowSnapshot s;
    s.name  = r.is_primary ? std::string(kImexPrimaryMode) : into_u8(r.name->GetTextCtrl()->GetValue());
    s.tools = active_tools_string(r);
    s.gcode = into_u8(r.gcode->GetValue());
    return s;
}

bool IMEXModesCtrl::row_differs_from_parent(int row_idx) const {
    if (!m_parent_lookup) return false;
    const DynamicPrintConfig* parent = m_parent_lookup();
    if (!parent) return false;
    auto* p_names  = parent->option<ConfigOptionStrings>("imex_mode_names");
    auto* p_tools  = parent->option<ConfigOptionStrings>("imex_mode_active_tools");
    auto* p_gcodes = parent->option<ConfigOptionStrings>("imex_mode_gcodes");
    if (!p_names || row_idx < 0 || row_idx >= (int)p_names->values.size()) return false;
    const Row& r = m_rows[row_idx];
    const RowSnapshot s = snapshot_row(r);
    if (!r.is_primary && s.name != p_names->values[row_idx]) return true;
    if (p_tools  && row_idx < (int)p_tools->values.size()  && s.tools  != p_tools->values[row_idx])  return true;
    if (p_gcodes && row_idx < (int)p_gcodes->values.size() && s.gcode != p_gcodes->values[row_idx]) return true;
    return false;
}

bool IMEXModesCtrl::row_has_parent_counterpart(int row_idx) const {
    if (!m_parent_lookup) return false;
    const DynamicPrintConfig* parent = m_parent_lookup();
    if (!parent) return false;
    auto* p_names = parent->option<ConfigOptionStrings>("imex_mode_names");
    return p_names && row_idx >= 0 && row_idx < (int)p_names->values.size();
}

void IMEXModesCtrl::add_row(const std::string& name,
                            const std::string& active_tools,
                            const std::string& gcode,
                            bool is_primary)
{
    Row r;
    r.is_primary = is_primary;
    r.panel = new wxPanel(this, wxID_ANY);
    r.panel->SetBackgroundColour(GetBackgroundColour());
    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    auto* name_col = new wxBoxSizer(wxVERTICAL);

    if (is_primary) {
        r.orig_name = kImexPrimaryMode;
        r.name = nullptr;
        auto* lbl = page_label(new wxStaticText(r.panel, wxID_ANY, _L("Primary"),
                                                wxDefaultPosition, FromDIP(wxSize(kNameColPx, -1))));
        wxFont f = lbl->GetFont();
        f.SetWeight(wxFONTWEIGHT_BOLD);
        lbl->SetFont(f);
        name_col->Add(lbl, 0);
    } else {
        const std::string nm = name.empty() ? unique_mode_name({}) : name;
        r.orig_name = nm;
        r.name = new ::ComboBox(r.panel, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                FromDIP(wxSize(kNameColPx, -1)), 0, nullptr, 0);
        for (const wxString& sug : suggested_mode_names())
            r.name->Append(sug);
        const auto on_open_click = [this, cb = r.name](wxMouseEvent& e) {
            if (!cb->is_drop_down()) {
                cb->Clear();
                for (const wxString& sug : suggested_mode_names(cb))
                    cb->Append(sug);
                if (cb->GetCount() == 0) {
                    cb->SetFocus();
                    return;
                }
            }
            e.Skip();
        };
        r.name->Bind(wxEVT_LEFT_DOWN, on_open_click);
        r.name->Bind(wxEVT_LEFT_DCLICK, on_open_click);
        r.name->GetTextCtrl()->ChangeValue(from_u8(nm));
        r.name->GetTextCtrl()->SetHint(_L("Mode name (required)"));
        r.name->SetToolTip(_L("Name of this parallel mode, as it appears in the plate's IDEX/IQEX mode "
                              "selector. Stored in the project by name, so renaming a mode that "
                              "plates already use makes them fall back to Primary. Cannot be empty "
                              "— a blank name is replaced with a generated one."));
        r.name->GetTextCtrl()->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { notify(); });
        r.name->Bind(wxEVT_COMBOBOX, [this, cb = r.name](wxCommandEvent& e) {
            cb->GetTextCtrl()->ChangeValue(cb->GetDropDown().GetValue());
            notify();
        });
        r.name->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [this, panel = r.panel](wxFocusEvent& e) {
            e.Skip();
            for (auto& row_ref : m_rows)
                if (row_ref.panel == panel && row_ref.name)
                    row_ref.focus_name = into_u8(row_ref.name->GetTextCtrl()->GetValue());
        });
        r.name->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this, panel = r.panel](wxFocusEvent& e) {
            e.Skip();
            if (m_clearing_rows) return;
            for (auto& row_ref : m_rows) {
                if (row_ref.panel != panel) continue;
                const wxString value = row_ref.name ? row_ref.name->GetTextCtrl()->GetValue() : wxString();
                if (!value.empty() && into_u8(value) == row_ref.focus_name)
                    return;
                if (fix_row_name(row_ref))
                    notify();
                return;
            }
        });
    }

    auto* grid_panel = new wxPanel(r.panel, wxID_ANY);
    grid_panel->SetBackgroundColour(GetBackgroundColour());
    auto* grid_sizer = new wxGridSizer(m_n_rows, m_n_cols, FromDIP(2), FromDIP(2));

    auto tool_roles = roles_for_mode(active_tools);
    if (tool_roles.empty())
        tool_roles[0] = ImexRole::Primary;
    r.all_tool_roles = tool_roles;
    wxPanel* this_panel = r.panel;

    bool flip_x = (m_layout == 1 || m_layout == 3);
    bool flip_y = (m_layout == 2 || m_layout == 3);
    for (int row = m_n_rows - 1; row >= 0; --row) {
        for (int col = 0; col < m_n_cols; ++col) {
            int raw_row = flip_y ? (m_n_rows - 1 - row) : row;
            int raw_col = flip_x ? (m_n_cols - 1 - col) : col;
            int tool_idx = raw_row * m_n_cols + raw_col;
            std::optional<ImexRole> role;
            auto it = tool_roles.find(tool_idx);
            if (it != tool_roles.end()) role = it->second;

            auto* btn = new wxButton(grid_panel, wxID_ANY, wxEmptyString,
                                     wxDefaultPosition, FromDIP(wxSize(24, 24)), wxBU_EXACTFIT | wxBU_AUTODRAW);
            apply_btn(btn, tool_idx, role);

            int btn_pos = (int)r.btns.size();
            r.btns.push_back(btn);
            r.btn_roles.push_back(role);
            r.btn_tool_idx.push_back(tool_idx);

            if (is_primary || role == ImexRole::Primary) {
                btn->Disable();
            } else {
                btn->Bind(wxEVT_BUTTON, [this, this_panel, btn_pos](wxCommandEvent&) {
                    for (auto& row_ref : m_rows) {
                        if (row_ref.panel != this_panel) continue;
                        std::optional<ImexRole>& tile = row_ref.btn_roles[btn_pos];
                        const int tidx = row_ref.btn_tool_idx[btn_pos];

                        bool other_primary = false;
                        int  primary_gantry = -1;
                        for (int j = 0; j < (int)row_ref.btn_roles.size(); ++j) {
                            if (row_ref.btn_roles[j] == ImexRole::Primary) {
                                if (j != btn_pos) other_primary = true;
                                primary_gantry = row_ref.btn_tool_idx[j] / m_n_cols;
                            }
                        }

                        tile = next_tile_role(tile, [&](ImexRole cand) {
                            return role_allowed_on_tile(cand, tidx, other_primary, primary_gantry);
                        });

                        if (tile)
                            row_ref.all_tool_roles[tidx] = *tile;
                        else
                            row_ref.all_tool_roles.erase(tidx);

                        apply_btn(row_ref.btns[btn_pos], row_ref.btn_tool_idx[btn_pos], tile);
                        notify();
                        break;
                    }
                });
            }

            grid_sizer->Add(btn, 0);
        }
    }
    grid_panel->SetSizerAndFit(grid_sizer);

    auto* gcode_frame = framed_input(r.panel);
    r.gcode = new wxTextCtrl(gcode_frame, wxID_ANY, from_u8(gcode),
                             wxDefaultPosition, FromDIP(wxSize(220, 54)),
                             wxTE_MULTILINE | wxBORDER_NONE);
    gcode_frame->GetSizer()->Add(r.gcode, 1, wxEXPAND | wxALL, 1);
    r.gcode->SetFont(wxGetApp().code_font());
    r.gcode->SetBackgroundColour(*wxWHITE);
    r.gcode->SetForegroundColour(wxColour("#262E30"));
    wxGetApp().UpdateDarkUI(r.gcode);
    r.gcode->SetToolTip(_L("G-code emitted once at the start of a print that uses this mode, before "
                           "the machine start G-code. This is where the printer is put into the "
                           "matching firmware mode — for example a Klipper SET_PRINT_MODE call or a "
                           "RepRapFirmware M567 — since the slicer only emits the Primary tool's "
                           "paths and the firmware drives the Copy / Mirror tools. Placeholders are "
                           "supported; use the edit button to browse them. Leave empty if the mode "
                           "needs no firmware setup."));
    r.gcode->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { notify(); });

    if (!is_primary)
        name_col->Add(r.name, 0);
    sizer->Add(name_col, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    sizer->Add(grid_panel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    sizer->Add(gcode_frame, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));

    auto* btn_col = new wxBoxSizer(wxVERTICAL);
    wxTextCtrl* gcode_ctrl = r.gcode;
    auto* ph_btn = new ScalableButton(r.panel, wxID_ANY, "edit", wxEmptyString,
                                      wxDefaultSize, wxDefaultPosition,
                                      wxBU_EXACTFIT | wxNO_BORDER, true, kImexIconPx);
    ph_btn->SetToolTip(_L("Edit G-code / browse placeholders"));
    ph_btn->Bind(wxEVT_BUTTON, [this, gcode_ctrl](wxCommandEvent&) {
        EditGCodeDialog dlg(this, "imex_mode_gcode", into_u8(gcode_ctrl->GetValue()));
        if (dlg.ShowModal() == wxID_OK)
            gcode_ctrl->SetValue(from_u8(dlg.get_edited_gcode()));
    });
    btn_col->Add(ph_btn, 0, wxBOTTOM, FromDIP(2));

    if (!is_primary) {
        auto* rm = new ScalableButton(r.panel, wxID_ANY, "delete", wxEmptyString,
                                      wxDefaultSize, wxDefaultPosition,
                                      wxBU_EXACTFIT | wxNO_BORDER, true, kImexIconPx);
        rm->SetToolTip(_L("Remove mode"));
        rm->Bind(wxEVT_BUTTON, [this, this_panel](wxCommandEvent&) {
            const std::vector<std::string> removed = remove_row(this_panel);
            auto removed_cb = on_mode_removed;
            notify();
            if (removed_cb && !removed.empty())
                removed_cb(removed);
        });
        name_col->Add(rm, 0, wxALIGN_RIGHT | wxTOP, FromDIP(2));
    }
    wxPanel* this_panel_for_reset = r.panel;
    if (row_has_parent_counterpart(static_cast<int>(m_rows.size()))) {
        r.reset_btn = new ScalableButton(r.panel, wxID_ANY, "dot", wxEmptyString,
                                         wxDefaultSize, wxDefaultPosition,
                                         wxBU_EXACTFIT | wxNO_BORDER, true, kImexIconPx);
        r.reset_btn->SetToolTip(_L("Discard in-session edits to this mode (snap back to saved value)"));
        r.reset_btn->Bind(wxEVT_BUTTON, [this, this_panel_for_reset](wxCommandEvent&) {
            reset_row_to_parent(this_panel_for_reset);
        });
        btn_col->Add(r.reset_btn, 0, wxTOP, FromDIP(2));
    }
    sizer->Add(btn_col, 0, wxALIGN_TOP | wxRIGHT, FromDIP(4));
    r.panel->SetSizerAndFit(sizer);

    m_rows_sizer->Add(r.panel, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    m_rows.push_back(std::move(r));
    Layout();
}

std::vector<std::string> IMEXModesCtrl::remove_row(wxPanel* panel) {
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].panel != panel) continue;
        if (m_rows[i].is_primary) return {};
        std::vector<std::string> removed_names;
        const std::string current = m_rows[i].name ? into_u8(m_rows[i].name->GetTextCtrl()->GetValue())
                                                   : std::string();
        for (const std::string& n : {m_rows[i].orig_name, current})
            if (!n.empty() && std::find(removed_names.begin(), removed_names.end(), n) == removed_names.end())
                removed_names.push_back(n);

        m_rows_sizer->Detach(panel);
        m_rows.erase(m_rows.begin() + i);

        auto still_in_use = [this](const std::string& n) {
            return std::any_of(m_rows.begin(), m_rows.end(), [&n](const Row& r) {
                return r.is_primary ? n == kImexPrimaryMode
                                    : r.name && into_u8(r.name->GetTextCtrl()->GetValue()) == n;
            });
        };
        removed_names.erase(std::remove_if(removed_names.begin(), removed_names.end(), still_in_use),
                            removed_names.end());

        Layout();
        wxTheApp->CallAfter([panel]() { panel->Destroy(); });
        return removed_names;
    }
    return {};
}

void IMEXModesCtrl::clear_rows() {
    m_clearing_rows = true;
    for (auto& r : m_rows) { m_rows_sizer->Detach(r.panel); r.panel->Destroy(); }
    m_rows.clear();
    m_clearing_rows = false;
}

std::string IMEXModesCtrl::active_tools_string(const Row& r) const {
    std::string s;
    for (const auto& [idx, role] : r.all_tool_roles) {
        if (!s.empty()) s += ',';
        s += std::to_string(idx) + ':' + imex_role_letter(role);
    }
    return s;
}

void IMEXModesCtrl::notify() {
    refresh_reset_buttons();
    if (on_change) on_change();
}

void IMEXModesCtrl::refresh_reset_buttons() {
    for (size_t i = 0; i < m_rows.size(); ++i) {
        Row& r = m_rows[i];
        if (!r.reset_btn || !r.reset_btn->IsEnabled()) continue;
        const bool dirty = row_differs_from_parent(static_cast<int>(i));
        if (dirty == r.reset_dirty_cached) continue;
        r.reset_dirty_cached = dirty;
        r.reset_btn->SetBitmap_(dirty ? "undo" : "dot");
    }
}

void IMEXModesCtrl::reset_row_to_parent(wxPanel* panel) {
    if (!m_parent_lookup) return;
    const DynamicPrintConfig* parent = m_parent_lookup();
    if (!parent) return;
    auto* p_names  = parent->option<ConfigOptionStrings>("imex_mode_names");
    auto* p_tools  = parent->option<ConfigOptionStrings>("imex_mode_active_tools");
    auto* p_gcodes = parent->option<ConfigOptionStrings>("imex_mode_gcodes");
    for (size_t i = 0; i < m_rows.size(); ++i) {
        Row& r = m_rows[i];
        if (r.panel != panel) continue;
        if (!p_names || i >= p_names->values.size()) return;
        if (!r.is_primary && r.name) {
            r.name->GetTextCtrl()->ChangeValue(from_u8(p_names->values[i]));
            fix_row_name(r);
        }
        if (p_gcodes && i < p_gcodes->values.size())
            r.gcode->ChangeValue(from_u8(p_gcodes->values[i]));
        if (p_tools && i < p_tools->values.size()) {
            r.all_tool_roles = roles_for_mode(p_tools->values[i]);
            for (size_t j = 0; j < r.btns.size(); ++j) {
                int tidx = r.btn_tool_idx[j];
                auto it = r.all_tool_roles.find(tidx);
                std::optional<ImexRole> role;
                if (it != r.all_tool_roles.end()) role = it->second;
                r.btn_roles[j] = role;
                apply_btn(r.btns[j], tidx, role);
            }
        }
        notify();
        return;
    }
}

}}
