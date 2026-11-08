#include "PublishSettingsDialog.hpp"

#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include "Tab.hpp"
#include "ConfigValueFormatter.hpp"
#include "FilamentBitmapUtils.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/DialogButtons.hpp"
#include "Widgets/StaticLine.hpp"
#include "Widgets/StateColor.hpp"
#include "Widgets/SwitchButton.hpp"

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PublishSettings.hpp"
#include "libslic3r/FilamentMixer.hpp"
#include "libslic3r/Model.hpp"

#include <boost/algorithm/string/trim.hpp>
#include <wx/colour.h>
#include <string>
#include "libslic3r/Config.hpp"
#include "slic3r/GUI/GUI.hpp"
#include <vector>
#include "slic3r/GUI/wxExtensions.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/Widgets/TabCtrl.hpp"
#include <wx/display.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/tglbtn.h>
#include <wx/treebase.h>
#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/toplevel.h>
#include <wx/panel.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/geometry.h>
#include <wx/utils.h>
#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/dcgraph.h>
#include <wx/image.h>
#include <wx/wrapsizer.h>
#include <set>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <numeric>
#include <tuple>
#include <utility>

namespace Slic3r { namespace GUI {
namespace {

wxStaticText* add_checkbox_label(wxWindow* parent,
                                 wxBoxSizer* sizer,
                                 ::CheckBox* check,
                                 const wxString& label,
                                 const wxString& tooltip,
                                 int label_width = 0)
{
    check->SetToolTip(tooltip);
    sizer->Add(check, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, parent->FromDIP(2));

    auto* text = new wxStaticText(parent, wxID_ANY, label);
    text->SetFont(Label::Body_14);
    text->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#363636")));
    if (label_width > 0) {
        text->SetMinSize(wxSize(label_width, -1));
        text->SetMaxSize(wxSize(label_width, -1));
        text->Wrap(label_width);
    }
    text->SetToolTip(tooltip);
    text->SetCursor(wxCURSOR_HAND);
    const auto toggle = [check]() {
        if (!check->IsEnabled())
            return;
        check->SetValue(!check->GetValue());
        wxCommandEvent event(wxEVT_TOGGLEBUTTON, check->GetId());
        event.SetEventObject(check);
        check->GetEventHandler()->ProcessEvent(event);
    };
    text->Bind(wxEVT_LEFT_DOWN, [toggle](wxMouseEvent& event) {
        if (!event.LeftDClick())
            toggle();
    });
    text->Bind(wxEVT_LEFT_DCLICK, [toggle](wxMouseEvent&) {
        toggle();
    });
    sizer->Add(text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, parent->FromDIP(5));
    return text;
}

enum {
    kPublishSelectAll = wxID_HIGHEST + 1,
    kPublishDeselectAll,
    kPublishSelectVisible,
    kPublishDeselectVisible,
    kPublishFilterSelected,
    kPublishFilterNonSelected
};

constexpr long s_tab_style = wxTR_NO_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_NO_LINES | wxBORDER_NONE | wxWANTS_CHARS |
                             wxTR_FULL_ROW_HIGHLIGHT;

std::string filament_color_hex(const DynamicPrintConfig& full, size_t slot)
{
    if (const auto* colours = full.opt<ConfigOptionStrings>("filament_colour"))
        if (slot < colours->size())
            return colours->get_at(slot);
    return std::string();
}

PublishMaterialIdentity material_identity(size_t slot, const DynamicPrintConfig& full)
{
    PublishMaterialIdentity identity;
    if (const auto* types = full.opt<ConfigOptionStrings>("filament_type"))
        if (slot < types->size())
            identity.type = types->get_at(slot);
    if (const auto* vendors = full.opt<ConfigOptionStrings>("filament_vendor"))
        if (slot < vendors->size())
            identity.vendor = vendors->get_at(slot);
    if (const auto* ids = full.opt<ConfigOptionStrings>("filament_ids"))
        if (slot < ids->size())
            identity.id = ids->get_at(slot);
    return identity;
}

std::string material_display_name(const std::string& preset_name)
{
    const size_t at = preset_name.find_first_of('@');
    if (at == std::string::npos)
        return preset_name;
    std::string bare = preset_name.substr(0, at);
    boost::trim_right(bare);
    return bare.empty() ? preset_name : bare;
}

wxString material_title(size_t slot, const PresetBundle* bundle, const DynamicPrintConfig& full)
{
    if (slot < bundle->filament_presets.size()) {
        const Preset* preset = bundle->filaments.find_preset(bundle->filament_presets[slot]);
        if (preset != nullptr && !preset->name.empty()) {
            const std::string& display = preset->alias.empty() ? get_preset_bare_name(preset->name) : preset->alias;
            return from_u8(material_display_name(display));
        }
    }
    const PublishMaterialIdentity identity = material_identity(slot, full);
    if (!identity.type.empty())
        return from_u8(identity.type);
    return _L("Material");
}

std::vector<unsigned int> mixed_slot_components(const DynamicPrintConfig& full, size_t slot)
{
    const auto* comp_opt = full.opt<ConfigOptionStrings>("filament_mixed_components");
    if (comp_opt == nullptr || slot >= comp_opt->size())
        return {};
    return parse_mixed_components(comp_opt->values[slot]);
}

wxString mixed_filament_label(const DynamicPrintConfig& full, size_t slot)
{
    const std::vector<unsigned int> comps = mixed_slot_components(full, slot);
    if (comps.empty())
        return _L("Mixed filament");
    const auto* grad_opt   = full.opt<ConfigOptionBools>("filament_mixed_gradient");
    const bool is_gradient = grad_opt != nullptr && slot < grad_opt->size() && grad_opt->values[slot];
    const auto* ratios_opt = full.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios");
    wxString label;
    for (size_t i = 0; i < comps.size(); ++i) {
        if (i > 0)
            label += is_gradient ? wxString::FromUTF8(" \u2192 ") : wxString::FromUTF8(" + ");
        label += wxString::Format("%u", comps[i]);
        if (!is_gradient) {
            double ratio = 100.0 / comps.size();
            if (ratios_opt != nullptr && slot < ratios_opt->size()) {
                const std::vector<double> rs = parse_mixed_ratios(ratios_opt->values[slot], comps.size());
                if (i < rs.size())
                    ratio = rs[i] * 100.0;
            }
            label += wxString::Format(" (%d%%)", int(ratio + 0.5));
        }
    }
    return label;
}

wxColour mixed_filament_blend_color(const DynamicPrintConfig& full, size_t slot)
{
    std::vector<wxColour> colors;
    if (const auto* colours = full.opt<ConfigOptionStrings>("filament_colour")) {
        colors.reserve(colours->values.size());
        for (const std::string& hex : colours->values) {
            const wxColour c(hex);
            colors.push_back(c.IsOk() ? c : wxColour(0, 0, 0));
        }
    }
    while (colors.size() <= slot)
        colors.push_back(wxColour("#D9D9D9"));
    recompute_mixed_slot_colors(colors, full);
    return slot < colors.size() ? colors[slot] : wxColour("#D9D9D9");
}

wxBitmap mixed_filament_chip_bitmap(const DynamicPrintConfig& full, size_t slot, int swatch_sz)
{
    const std::string label = std::to_string(slot + 1);
    const auto* grad_opt    = full.opt<ConfigOptionBools>("filament_mixed_gradient");
    const bool is_gradient  = grad_opt != nullptr && slot < grad_opt->size() && grad_opt->values[slot];
    wxBitmap* icon          = nullptr;
    if (is_gradient) {
        const std::vector<wxColour> ramp = mixed_gradient_ramp(full, slot, swatch_sz);
        if (!ramp.empty()) {
            icon = get_extruder_color_icon(std::vector<std::string>(), true, label, swatch_sz, swatch_sz, &ramp);
        } else {
            std::vector<std::string> hexes;
            for (const unsigned int comp : mixed_slot_components(full, slot)) {
                std::string hex = filament_color_hex(full, size_t(comp) - 1);
                if (hex.empty())
                    hex = "#D9D9D9";
                hexes.push_back(std::move(hex));
            }
            if (hexes.size() >= 2)
                icon = get_extruder_color_icon(std::move(hexes), true, label, swatch_sz, swatch_sz);
        }
    }
    if (icon == nullptr) {
        const wxColour blend = mixed_filament_blend_color(full, slot);
        const std::string blend_hex = blend.IsOk() ?
                                          std::string(wxString::Format("#%02X%02X%02X", blend.Red(), blend.Green(), blend.Blue()).ToUTF8()) :
                                          std::string("#808080");
        icon = get_extruder_color_icon(blend_hex, label, swatch_sz, swatch_sz);
    }
    return icon != nullptr ? *icon : wxNullBitmap;
}

wxBitmap mixed_filament_tab_bitmap(const DynamicPrintConfig& full, size_t slot, int swatch_sz, const wxColour& tab_bg)
{
    const std::vector<unsigned int> comps = mixed_slot_components(full, slot);
    if (comps.empty())
        return wxNullBitmap;
    const auto* grad_opt   = full.opt<ConfigOptionBools>("filament_mixed_gradient");
    const bool is_gradient = grad_opt != nullptr && slot < grad_opt->size() && grad_opt->values[slot];
    const auto* ratios_opt = full.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios");
    std::vector<double> ratio_pct(comps.size(), 100.0 / comps.size());
    if (!is_gradient && ratios_opt != nullptr && slot < ratios_opt->size()) {
        const std::vector<double> rs = parse_mixed_ratios(ratios_opt->values[slot], comps.size());
        for (size_t i = 0; i < rs.size() && i < comps.size(); ++i)
            ratio_pct[i] = rs[i] * 100.0;
    }

    const auto* colours     = full.opt<ConfigOptionStrings>("filament_colour");
    const wxString lead_sep = wxString::FromUTF8(":");
    const wxString comp_sep = is_gradient ? wxString::FromUTF8("\u2192") : wxString::FromUTF8("+");

    struct Piece
    {
        enum Kind { Swatch, Text } kind{Text};
        wxBitmap bmp;
        wxString text;
    };
    std::vector<Piece> pieces;
    bool has_lead = false;
    wxBitmap dummy(1, 1);
    wxMemoryDC measure_dc;
    measure_dc.SelectObject(dummy);
    measure_dc.SetFont(::Label::Body_12);
    const int gap = wxWindow::FromDIP(4, nullptr);

    auto push_swatch = [&](const std::string& hex, const std::string& label) {
        wxBitmap* icon = get_extruder_color_icon(hex, label, swatch_sz, swatch_sz);
        if (icon == nullptr)
            return;
        pieces.push_back({Piece::Swatch, *icon, wxString()});
    };
    auto push_text = [&](const wxString& text) { pieces.push_back({Piece::Text, wxNullBitmap, text}); };

    {
        const wxBitmap chip = mixed_filament_chip_bitmap(full, slot, swatch_sz);
        has_lead            = chip.IsOk();
        if (has_lead)
            pieces.push_back({Piece::Swatch, chip, wxString()});
    }
    for (size_t ci = 0; ci < comps.size(); ++ci) {
        if (pieces.empty())
            break;
        push_text(has_lead && pieces.size() == 1 ? lead_sep : comp_sep);
        std::string hex = "#D9D9D9";
        if (colours != nullptr && comps[ci] >= 1 && comps[ci] <= colours->size())
            hex = colours->values[comps[ci] - 1];
        const size_t before = pieces.size();
        push_swatch(hex, std::to_string(comps[ci]));
        if (pieces.size() == before)
            break;
        if (!is_gradient) {
            push_text(wxString::Format("%d%%", int(ratio_pct[ci] + 0.5)));
        }
    }
    if (pieces.empty())
        return wxNullBitmap;

    int width = 0;
    for (const Piece& p : pieces)
        width += (p.kind == Piece::Swatch ? swatch_sz : measure_dc.GetTextExtent(p.text).x + gap);

    wxBitmap composite(width, swatch_sz);
    wxMemoryDC memdc;
    memdc.SelectObject(composite);
    memdc.SetBackground(wxBrush(tab_bg));
    memdc.Clear();
    memdc.SetBackgroundMode(wxTRANSPARENT);
    memdc.SetFont(::Label::Body_12);
    memdc.SetTextForeground(StateColor::darkModeColorFor(wxColour("#262E30")));
    int x = 0;
    for (const Piece& p : pieces) {
        if (p.kind == Piece::Swatch) {
            memdc.DrawBitmap(p.bmp, x, 0, true);
            x += swatch_sz;
        } else {
            const wxSize tsz = measure_dc.GetTextExtent(p.text);
            memdc.DrawText(p.text, x + gap / 2, (swatch_sz - tsz.y) / 2);
            x += tsz.x + gap;
        }
    }
    memdc.SelectObject(wxNullBitmap);
    return composite;
}

std::set<size_t> project_used_filament_slots(const PresetBundle& bundle, const DynamicPrintConfig& full, const Model& model)
{
    std::set<size_t> used;
    if (model.objects.empty())
        return used;

    auto add_1based = [&used](int id) {
        if (id > 0)
            used.insert(size_t(id - 1));
    };

    const DynamicPrintConfig& print_cfg = bundle.prints.get_edited_preset().config;
    const int glb_support_intf   = print_cfg.opt_int("support_interface_filament");
    const int glb_support        = print_cfg.opt_int("support_filament");
    const int glb_outer_wall     = print_cfg.opt_int("outer_wall_filament_id");
    const int glb_inner_wall     = print_cfg.opt_int("inner_wall_filament_id");
    const int glb_sparse_infill  = print_cfg.opt_int("sparse_infill_filament_id");
    const int glb_internal_solid = print_cfg.opt_int("internal_solid_filament_id");
    const int glb_top_surface    = print_cfg.opt_int("top_surface_filament_id");
    const int glb_bottom_surface = print_cfg.opt_int("bottom_surface_filament_id");
    const bool glb_support_on    = print_cfg.opt_bool("enable_support") || print_cfg.opt_int("raft_layers") > 0;

    for (ModelObject* mo : model.objects) {
        for (ModelVolume* mv : mo->volumes)
            for (int e : mv->get_extruders())
                add_1based(e);

        for (const auto& range_entry : mo->layer_config_ranges)
            if (const ConfigOption* ext_opt = range_entry.second.option("extruder"))
                add_1based(ext_opt->getInt());

        bool obj_support = glb_support_on;
        if (const ConfigOption* s_opt = mo->config.option("enable_support"); s_opt != nullptr)
            obj_support = s_opt->getBool();
        else if (const ConfigOption* r_opt = mo->config.option("raft_layers"); r_opt != nullptr)
            obj_support = r_opt->getInt() > 0;

        if (obj_support) {
            if (const ConfigOption* opt = mo->config.option("support_interface_filament"); opt != nullptr && opt->getInt() != 0)
                add_1based(opt->getInt());
            else
                add_1based(glb_support_intf);

            if (const ConfigOption* opt = mo->config.option("support_filament"); opt != nullptr && opt->getInt() != 0)
                add_1based(opt->getInt());
            else
                add_1based(glb_support);
        }

        auto obj_id = [&mo](const char* key) {
            const ConfigOption* opt = mo->config.option(key);
            return (opt != nullptr) ? opt->getInt() : 0;
        };

        int obj_outer_wall = obj_id("outer_wall_filament_id");
        if (obj_outer_wall == 0)
            obj_outer_wall = obj_id("inner_wall_filament_id");
        add_1based(obj_outer_wall != 0 ? obj_outer_wall : glb_outer_wall);

        int obj_inner_wall = obj_id("inner_wall_filament_id");
        if (obj_inner_wall == 0)
            obj_inner_wall = obj_id("outer_wall_filament_id");
        add_1based(obj_inner_wall != 0 ? obj_inner_wall : glb_inner_wall);

        const int obj_sparse = obj_id("sparse_infill_filament_id");
        add_1based(obj_sparse != 0 ? obj_sparse : glb_sparse_infill);

        const int obj_internal_solid = obj_id("internal_solid_filament_id");
        add_1based(obj_internal_solid != 0 ? obj_internal_solid : glb_internal_solid);

        int obj_top = obj_id("top_surface_filament_id");
        if (obj_top == 0)
            obj_top = obj_internal_solid;
        add_1based(obj_top != 0 ? obj_top : glb_top_surface);

        int obj_bottom = obj_id("bottom_surface_filament_id");
        if (obj_bottom == 0)
            obj_bottom = obj_internal_solid;
        add_1based(obj_bottom != 0 ? obj_bottom : glb_bottom_surface);
    }

    for (const auto& plate_entry : model.plates_custom_gcodes)
        for (const CustomGCode::Item& item : plate_entry.second.gcodes)
            if (item.type == CustomGCode::Type::ToolChange)
                add_1based(item.extruder);

    const auto* is_mixed_opt = full.opt<ConfigOptionBools>("filament_is_mixed");
    const auto* comp_opt     = full.opt<ConfigOptionStrings>("filament_mixed_components");
    if (is_mixed_opt != nullptr && comp_opt != nullptr && has_any_mixed_filament(is_mixed_opt->values)) {
        const std::vector<unsigned int> raw(used.begin(), used.end());
        const std::vector<unsigned int> expanded = expand_mixed_filaments(raw, is_mixed_opt->values, comp_opt->values);
        std::set<size_t> result(expanded.begin(), expanded.end());
        for (unsigned int s : raw)
            if (s < is_mixed_opt->values.size() && is_mixed_opt->values[s])
                result.insert(s);
        return result;
    }

    return used;
}

void add_sentence_with_chips(wxWindow* parent, wxSizer* row, const wxString& sentence, const std::vector<wxWindow*>& chips)
{
    std::vector<bool> placed(chips.size(), false);
    auto add_text = [&](wxString text) {
        text.Replace("%%", "%");
        if (text.IsEmpty())
            return;
        auto* label = new wxStaticText(parent, wxID_ANY, text);
        label->SetFont(Label::Body_12);
        label->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#6B6B6B")));
        row->Add(label, 0, wxALIGN_CENTER_VERTICAL);
    };
    auto add_chip = [&](size_t i) {
        if (i < chips.size() && chips[i] != nullptr && !placed[i]) {
            placed[i] = true;
            row->Add(chips[i], 0, wxALIGN_CENTER_VERTICAL);
        }
    };

    size_t literal = 0, pos = 0;
    while ((pos = sentence.find('%', pos)) != wxString::npos) {
        size_t end = pos + 1;
        while (end < sentence.length() && sentence[end] >= '0' && sentence[end] <= '9')
            ++end;
        if (end == pos + 1 || end >= sentence.length() || sentence[end] != '%') {
            ++pos;
            continue;
        }
        long index = 0;
        sentence.Mid(pos + 1, end - pos - 1).ToLong(&index);
        add_text(sentence.Mid(literal, pos - literal));
        add_chip(size_t(index - 1));
        literal = pos = end + 1;
    }
    add_text(sentence.Mid(literal));
    for (size_t i = 0; i < chips.size(); ++i)
        add_chip(i);
}

}
class MixedFilamentWarningDialog : public MsgDialog
{
public:
    MixedFilamentWarningDialog(wxWindow* parent, const DynamicPrintConfig& full, const std::vector<MixedDependencyIssue>& issues)
        : MsgDialog(parent, _L("Warning"), wxEmptyString, wxOK | wxCANCEL | wxICON_WARNING)
    {
        auto* content = new wxBoxSizer(wxVERTICAL);

        auto* intro = new wxStaticText(this, wxID_ANY, _L("Some mixed filaments rely on filaments that will not be published:"));
        intro->SetFont(Label::Body_13);
        intro->Wrap(FromDIP(400));
        content->Add(intro, 0, wxEXPAND);
        content->AddSpacer(FromDIP(10));

        const int swatch = FromDIP(20);
        auto make_chip = [&](const wxBitmap& bmp, const wxString& name) -> wxWindow* {
            if (bmp.IsOk()) {
                auto* chip = new wxStaticBitmap(this, wxID_ANY, bmp);
                chip->SetToolTip(name);
                return chip;
            }
            auto* label = new wxStaticText(this, wxID_ANY, name);
            label->SetFont(Label::Body_12);
            return label;
        };

        for (const MixedDependencyIssue& issue : issues) {
            std::string hex = filament_color_hex(full, issue.component_slot);
            if (hex.empty())
                hex = "#D9D9D9";
            const wxBitmap* comp_bmp = get_extruder_color_icon(hex, std::to_string(issue.component_slot + 1), swatch, swatch);

            wxWindow* mix_chip  = make_chip(mixed_filament_chip_bitmap(full, issue.mixed_slot, swatch),
                                            wxString::Format(_L("Filament %d (mixed)"), int(issue.mixed_slot) + 1));
            wxWindow* comp_chip = make_chip(comp_bmp != nullptr ? *comp_bmp : wxNullBitmap,
                                            wxString::Format(_L("Filament %d"), int(issue.component_slot) + 1));

            auto* row = new wxWrapSizer(wxHORIZONTAL);
            add_sentence_with_chips(this, row,
                                    issue.reason == MixedDependencyIssue::Reason::Disabled ?
                                        _L("%1% needs %2%, which is not enabled.") :
                                        _L("%1% needs %2%, whose material will not be published."),
                                    {mix_chip, comp_chip});
            content->Add(row, 0, wxEXPAND | wxLEFT, FromDIP(10));
            content->AddSpacer(FromDIP(6));
        }

        auto* hint = new wxStaticText(
            this, wxID_ANY,
            _L("To publish a mixed filament, enable every filament it uses and choose Full Publish or check its Type requirement."));
        hint->SetFont(Label::Body_12);
        hint->Wrap(FromDIP(380));
        content->Add(hint, 0, wxEXPAND | wxTOP, FromDIP(4));

        content_sizer->Add(content, 0, wxEXPAND);

        SetButtonLabel(wxID_OK, _L("Publish anyway"));
        SetButtonLabel(wxID_CANCEL, _L("Cancel"), true);

        finalize();
    }
};

PublishSettingsDialog::MixedVisualSpec PublishSettingsDialog::make_mixed_visual_spec(const DynamicPrintConfig& full, size_t slot)
{
    MixedVisualSpec spec;
    const std::vector<unsigned int> comps = mixed_slot_components(full, slot);
    if (comps.empty())
        return spec;

    const auto* grad_opt = full.opt<ConfigOptionBools>("filament_mixed_gradient");
    spec.is_gradient     = grad_opt != nullptr && slot < grad_opt->size() && grad_opt->values[slot];

    const auto* colours = full.opt<ConfigOptionStrings>("filament_colour");
    for (unsigned int cid : comps) {
        std::string hex = "#D9D9D9";
        if (colours != nullptr && cid >= 1 && cid <= colours->size())
            hex = colours->values[cid - 1];
        const wxColour c(hex);
        spec.component_colours.push_back(c.IsOk() ? c : wxColour("#D9D9D9"));
    }

    if (!spec.is_gradient) {
        spec.ratios.assign(comps.size(), 1.0 / comps.size());
        if (const auto* ratios_opt = full.opt<ConfigOptionStrings>("filament_mixed_sublayer_ratios"))
            if (slot < ratios_opt->size()) {
                const std::vector<double> rs = parse_mixed_ratios(ratios_opt->values[slot], comps.size());
                if (rs.size() == comps.size())
                    spec.ratios = rs;
            }
        if (comps.size() == 3)
            spec.tri_weights = spec.ratios;
    } else {
        const Slic3r::GradientCurve curve = mixed_gradient_curve(full, slot);
        constexpr int kSamples            = 256;
        for (int i = 0; i <= kSamples; ++i) {
            const double t = double(i) / kSamples;
            spec.gradient_samples.emplace_back(t, sample_gradient_curve(curve, t));
        }
        for (const Slic3r::GradientAnchor& anchor : curve.points)
            spec.gradient_anchors.emplace_back(anchor.x, anchor.y);
    }

    spec.valid = true;
    return spec;
}

PublishSettingsDialog::PublishSettingsDialog(wxWindow* parent,
                                             const std::vector<std::string>* published_keys,
                                             const std::vector<Slic3r::PublishedMaterialEntry>* material_keys)
    : DPIDialog(parent ? parent : static_cast<wxWindow*>(wxGetApp().mainframe),
                wxID_ANY,
                _L("Publish 3MF..."),
                wxDefaultPosition,
                wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER | wxFULL_REPAINT_ON_RESIZE)
    , m_search(this, "search", 16)
    , m_menu(this, "filter", 16)
{
    SetBackgroundColour(*wxWHITE);

    wxPanel* f_bar = new wxPanel(this, wxID_ANY);
    f_bar->SetBackgroundColour(GetBackgroundColour());
    wxBoxSizer* f_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_filter_box = new TextInput(f_bar, "", "", "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_filter_box->SetIcon(m_search.bmp());
    m_filter_box->SetMinSize(FromDIP(wxSize(200, 24)));
    m_filter_box->SetSize(FromDIP(wxSize(-1, 24)));
    m_filter_box->SetFocus();
    m_filter_ctrl = m_filter_box->GetTextCtrl();
    m_filter_ctrl->SetFont(Label::Body_13);
    m_filter_ctrl->SetSize(wxSize(-1, FromDIP(16)));
    m_filter_ctrl->SetHint(_L("Type to filter..."));
    m_filter_ctrl->Bind(wxEVT_TEXT, [this](auto&) { apply_filter(m_filter_ctrl->GetValue()); });
    m_filter_ctrl->Bind(wxEVT_TEXT_ENTER, [this](auto&) { apply_filter(m_filter_ctrl->GetValue()); });
    m_filter_ctrl->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        if (m_filter_mode != FilterMode::Text)
            apply_filter(m_filter_ctrl->GetValue());
        e.Skip();
    });
    f_sizer->Add(m_filter_box, 1, wxEXPAND);

    m_fb_sizer      = new wxBoxSizer(wxHORIZONTAL);
    auto create_btn = [this, f_bar](wxString title, bool select) {
        auto btn = new wxStaticText(f_bar, wxID_ANY, title);
        btn->SetForegroundColour("#009687");
        btn->SetCursor(wxCURSOR_HAND);
        btn->SetFont(Label::Body_13);
        btn->Bind(wxEVT_LEFT_DOWN, [this, select](wxMouseEvent&) { select_all(select); });
        m_fb_sizer->Add(btn, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(10));
    };
    f_sizer->Add(m_fb_sizer, 0, wxALIGN_CENTER_VERTICAL);
    create_btn(_L("All"), true);
    create_btn(_L("None"), false);

    m_pseudo_chip = new wxStaticText(f_bar, wxID_ANY, "");
    m_pseudo_chip->SetForegroundColour("#009687");
    m_pseudo_chip->SetCursor(wxCURSOR_HAND);
    m_pseudo_chip->SetFont(Label::Body_13);
    m_pseudo_chip->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { apply_filter(m_filter_ctrl->GetValue()); });
    m_pseudo_chip->Hide();
    f_sizer->Add(m_pseudo_chip, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    m_menu_button = new wxStaticBitmap(f_bar, wxID_ANY, m_menu.bmp());
    m_menu_button->SetCursor(wxCURSOR_HAND);
    m_menu_button->Bind(wxEVT_LEFT_DOWN, &PublishSettingsDialog::show_menu, this);
    f_sizer->Add(m_menu_button, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    f_bar->SetSizerAndFit(f_sizer);

    m_outer_tabs = new TabCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, s_tab_style);
    m_outer_tabs->SetFont(Label::Body_14);
    m_outer_tabs->SetBackgroundColour(GetBackgroundColour());

    m_outer_host = new wxPanel(this, wxID_ANY);
#ifdef __WINDOWS__
    m_outer_host->SetDoubleBuffered(true);
#endif
    m_outer_host->SetBackgroundColour(GetBackgroundColour());
    m_outer_host_sizer = new wxBoxSizer(wxVERTICAL);
    m_outer_host->SetSizer(m_outer_host_sizer);

    wxBoxSizer* w_sizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* msg = new wxStaticText(this, wxID_ANY, _L("Select which settings to be published in the 3MF file"));
    msg->SetFont(Label::Body_13);
    msg->Wrap(-1);
    w_sizer->Add(msg, 0, wxRIGHT | wxLEFT | wxTOP, FromDIP(10));

    w_sizer->Add(f_bar, 0, wxRIGHT | wxLEFT | wxTOP | wxEXPAND, FromDIP(10));
    w_sizer->Add(m_outer_tabs, 0, wxRIGHT | wxLEFT | wxTOP | wxEXPAND, FromDIP(10));
    w_sizer->Add(m_outer_host, 1, wxRIGHT | wxLEFT | wxTOP | wxEXPAND, FromDIP(10));

    build_option_model();

    if (published_keys != nullptr || material_keys != nullptr)
        apply_selection(published_keys != nullptr ? *published_keys : std::vector<std::string>(),
                        material_keys != nullptr ? *material_keys : std::vector<Slic3r::PublishedMaterialEntry>());

    auto dlg_btns = new DialogButtons(this, {"OK", "Cancel"});

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (const std::vector<MixedDependencyIssue> issues = unpublished_mixed_components(); !issues.empty()) {
            const DynamicPrintConfig full = wxGetApp().preset_bundle->full_config();
            MixedFilamentWarningDialog warn(this, full, issues);
            if (warn.ShowModal() != wxID_OK)
                return;
        }
        EndModal(wxID_OK);
    });
    dlg_btns->GetCANCEL()->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });

    wxBoxSizer* links_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto wiki_btn = new Button(this, "", "toolbar_wiki", 0, 15);
    auto wiki_url = "https://www.orcaslicer.com/wiki/publishing_3mf/publish_3mf";
    wiki_btn->SetToolTip(_L("Wiki Guide") + "\n" + wiki_url);
    wiki_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Circle);
    wiki_btn->SetCanFocus(false);
    wiki_btn->Bind(wxEVT_LEFT_DOWN, ([wiki_url](auto& e) {wxLaunchDefaultBrowser(wiki_url);}));

    auto video_btn  = new Button(this, "", "toolbar_video_guide", 0, 15);
    auto video_url = "https://www.youtube.com/watch?v=-xt1N29UIOg";
    video_btn->SetToolTip(_L("Video Guide") + "\n" + video_url);
    video_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Circle);
    video_btn->SetCanFocus(false);
    video_btn->Bind(wxEVT_LEFT_DOWN, ([video_url](auto& e) {wxLaunchDefaultBrowser(video_url);}));

    links_sizer->Add(wiki_btn , 0, wxLEFT, FromDIP(10));
    links_sizer->Add(video_btn, 0, wxLEFT, FromDIP(10));

    wxBoxSizer* footer = new wxBoxSizer(wxHORIZONTAL);
    footer->Add(links_sizer, 0, wxALIGN_CENTER_VERTICAL);
    footer->AddStretchSpacer();
    footer->Add(dlg_btns, 0, wxALIGN_CENTER_VERTICAL);
    auto* footer_line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    footer_line->SetBackgroundColour(wxColour("#CECECE"));
    footer_line->SetMinSize(wxSize(-1, 1));
    footer_line->SetMaxSize(wxSize(-1, 1));
    w_sizer->Add(footer_line, 0, wxRIGHT | wxLEFT | wxTOP | wxEXPAND, FromDIP(10));
    w_sizer->Add(footer, 0, wxEXPAND, FromDIP(10));

    SetSizerAndFit(w_sizer);
    fit_to_content();
    wxGetApp().UpdateDlgDarkUI(this);
    refresh_mixed_tab_bitmaps();
}

void PublishSettingsDialog::fit_to_content()
{
    static const wxSize BASE{530, 530};
    static const wxSize CAP{1300, 850};

    int strip = m_outer_tabs->GetFullSize();
    for (const SectionGroup& section : m_sections) {
        strip = std::max(strip, section.tabs->GetFullSize());
        if (section.mixed_tabs != nullptr)
            strip = std::max(strip, section.mixed_tabs->GetFullSize());
    }

    const int min_w = std::max(strip + 2 * FromDIP(10), FromDIP(BASE.x));

    const int w    = std::clamp(min_w, FromDIP(BASE.x), FromDIP(CAP.x));
    const double f = double(w) / FromDIP(BASE.x);
    const int h    = std::clamp(int(FromDIP(BASE.y) * f), FromDIP(BASE.y), FromDIP(CAP.y));

    const wxRect area = wxDisplay(this).GetClientArea();
    const int max_w   = area.width * 9 / 10;
    const int max_h   = area.height * 9 / 10;

    SetMinSize(wxSize(std::min(min_w, max_w), std::min(FromDIP(BASE.y), max_h)));
    SetSize(std::min(w, max_w), std::min(h, max_h));
}

PublishSettingsDialog::~PublishSettingsDialog() {}

void PublishSettingsDialog::build_option_model()
{
    const std::set<std::string>& denylist = publish_structural_keys();
    std::set<std::string> added;

    PresetBundle* bundle    = wxGetApp().preset_bundle;
    DynamicPrintConfig full = bundle->full_config();

    m_info_nonsel = _L("No selected items...");
    m_info_allsel = _L("All items selected...");
    m_info_empty  = _L("No matching items...");
    m_info_mix    = _L("Mixed filament - published as a whole when \"Enable\" above is selected");

    m_sections.reserve(3);
    const Section tab_order[] = {Section::Printer, Section::Material, Section::Print};
    for (Section kind : tab_order)
        section_group_for(kind);
    bind_tab_events();

    auto option_text = [&denylist, &full](const std::string& opt_id, const std::string& pure_key, wxString& label, wxString& value,
                                          wxString& unit) -> bool {
        if (denylist.count(pure_key) > 0)
            return false;
        const ConfigOptionDef* def = print_config_def.get(pure_key);
        if (def == nullptr)
            return false;
        label = _(def->full_label.empty() ? def->label : def->full_label);
        if (label.IsEmpty())
            return false;
        value = get_string_value(opt_id, full);
        unit  = _(def->sidetext);
        if (unit == "%" && value.EndsWith("%"))
            unit.clear();
        return true;
    };

    {
        size_t g = section_group_for(Section::Printer);
        std::set<std::string> printer_added;
        for (Tab* tab : wxGetApp().tabs_list) {
            auto*       printer_tab = dynamic_cast<TabPrinter*>(tab);
            const Page* page        = printer_tab ? printer_tab->extruder_page() : nullptr;
            if (page == nullptr)
                continue;
            const size_t extruders_count = printer_tab->m_extruders_count;
            for (size_t extruder_idx = 0; extruder_idx < extruders_count; ++extruder_idx) {
                const wxString page_title = Tab::translate_category(extruders_count > 1 ? wxString::Format("Extruder %d", int(extruder_idx + 1)) : wxString("Extruder"), tab->m_type);
                const int variant_index = printer_tab->extruder_variant_index(int(extruder_idx));
                for (const ConfigOptionsGroupShp& optgroup : page->m_optgroups) {
                    if (optgroup->title != "Retraction" && optgroup->title != "Z-Hop")
                        continue;
                    const wxString subcategory = _(optgroup->title);
                    for (const auto& opt : optgroup->opt_map()) {
                        const std::string& pure_key = opt.second.first;
                        const std::string opt_id = pure_key + "#" + std::to_string(variant_index);
                        if (!printer_added.insert(opt_id).second)
                            continue;
                        wxString label, value, unit;
                        if (!option_text(opt_id, pure_key, label, value, unit))
                            continue;
                        size_t cat_index = category_index_for(page_title, Section::Printer, g, extruder_idx);
                        size_t sub_index = subcategory_index_for(cat_index, subcategory, optgroup->icon);
                        add_row_ui(opt_id, label, value, unit, cat_index, sub_index);
                    }
                }
            }
        }
    }

    {
        size_t g          = section_group_for(Section::Material);
        Tab* filament_tab = nullptr;
        for (Tab* tab : wxGetApp().tabs_list)
            if (tab->m_type == Preset::TYPE_FILAMENT) {
                filament_tab = tab;
                break;
            }
        if (filament_tab != nullptr) {
            const Page* overrides_page = nullptr;
            for (const PageShp& page : filament_tab->m_pages)
                if (page->title() == "Setting Overrides") {
                    overrides_page = page.get();
                    break;
                }

            if (overrides_page != nullptr) {
                const ConfigOptionBools* is_mixed_opt = full.opt<ConfigOptionBools>("filament_is_mixed");
                const std::set<size_t> used_slots = project_used_filament_slots(*bundle, full, wxGetApp().plater()->model());
                for (size_t slot = 0; slot < bundle->filament_presets.size(); ++slot) {
                    if (used_slots.find(slot) == used_slots.end())
                        continue;
                    const bool is_mixed = is_mixed_opt != nullptr && slot < is_mixed_opt->size() && is_mixed_opt->values[slot];
                    const PublishMaterialIdentity identity = material_identity(slot, full);
                    const wxString title        = is_mixed ? mixed_filament_label(full, slot) : material_title(slot, bundle, full);
                    const size_t category_index = category_index_for(title, Section::Material, g, slot, identity, is_mixed);

                    if (is_mixed) {
                        add_mixed_visual(category_index, make_mixed_visual_spec(full, slot));
                        continue;
                    }

                    {
                        const size_t req_sub = subcategory_index_for(category_index, _L("Material"), "custom-gcode_filament");
                        add_row_ui("filament_colour", _L("Color"), from_u8(filament_color_hex(full, slot)), wxString(), category_index,
                                   req_sub, RowKind::Color);
                        std::string type;
                        if (const auto* types = full.opt<ConfigOptionStrings>("filament_type"))
                            if (slot < types->size())
                                type = types->get_at(slot);
                        add_row_ui("filament_type", _L("Type"), from_u8(normalize_filament_type(type)), wxString(), category_index, req_sub,
                                   RowKind::Type);
                    }

                    std::set<std::string> material_added;

                    for (const ConfigOptionsGroupShp& optgroup : overrides_page->m_optgroups) {
                        if (optgroup->title != "Retraction" && optgroup->title != "Retraction when switching material")
                            continue;
                        for (const auto& opt : optgroup->opt_map()) {
                            const std::string& opt_id = opt.first;
                            std::string base          = publish_base_key(opt_id);
                            if (!material_added.insert(base).second)
                                continue;
                            std::string value_opt_id = base + "#" + std::to_string(slot);
                            if (const ConfigOption* opt_cfg = full.option(base))
                                if (const auto* vec = dynamic_cast<const ConfigOptionVectorBase*>(opt_cfg))
                                    if (vec->size() > 0 && slot >= vec->size())
                                        value_opt_id = base + "#0";
                            wxString label, value, unit;
                            if (!option_text(value_opt_id, base, label, value, unit))
                                continue;
                            size_t sub_index = subcategory_index_for(category_index, _(optgroup->title), optgroup->icon);
                            add_row_ui(base, label, value, unit, category_index, sub_index);
                        }
                    }
                }
            }
        }
    }

    {
        size_t g = section_group_for(Section::Print);
        for (Tab* tab : wxGetApp().tabs_list) {
            if (tab->m_type != Preset::TYPE_PRINT)
                continue;
            size_t page_index = 0;
            for (const PageShp& page : tab->m_pages) {
                wxString category           = Tab::translate_category(page->title(), tab->m_type);
                const size_t category_index = category_index_for(category, Section::Print, g, page_index);

                for (const ConfigOptionsGroupShp& optgroup : page->m_optgroups) {
                    for (const auto& opt : optgroup->opt_map()) {
                        const std::string& opt_id   = opt.first;
                        const std::string& pure_key = opt.second.first;
                        if (!added.insert(pure_key).second)
                            continue;
                        wxString label, value, unit;
                        if (!option_text(opt_id, pure_key, label, value, unit))
                            continue;
                        size_t sub_index = subcategory_index_for(category_index, _(optgroup->title), optgroup->icon);
                        add_row_ui(opt_id, label, value, unit, category_index, sub_index);
                    }
                }
                ++page_index;
            }
        }
    }

    std::set<std::string> dirty_keys;
    for (const std::string& key : collect_dirty_settings_keys(*wxGetApp().preset_bundle))
        dirty_keys.insert(key);
    for (Row& row : m_rows) {
        if (row.kind != RowKind::Setting)
            continue;
        bool dirty = dirty_keys.count(row.key) != 0;
        if (!dirty && row.section == Section::Material)
            dirty = dirty_keys.count(publish_base_key(row.key) + "#" + std::to_string(m_categories[row.inner_index].filament_slot)) != 0;
        row.dirty = dirty;
        if (row.dirty) {
            row.check->SetValue(true);
            set_row_bold(row, true);
        }
    }

    for (size_t c = 0; c < m_categories.size(); ++c)
        if (m_categories[c].enable_check != nullptr)
            m_categories[c].enable_check->Bind(wxEVT_TOGGLEBUTTON, [this, c](wxCommandEvent& event) {
                on_enable_toggle(c);
                event.Skip();
            });

    for (size_t c = 0; c < m_categories.size(); ++c)
        if (m_categories[c].full_check != nullptr)
            m_categories[c].full_check->Bind(wxEVT_TOGGLEBUTTON, [this, c](wxCommandEvent& event) {
                on_full_toggle(c);
                event.Skip();
            });

    for (Row& row : m_rows)
        row.matches_filter = true;
    apply_visibility();

    for (Category& category : m_categories) {
        category.scroll->FitInside();
        category.list_sizer->Layout();
    }
    for (SectionGroup& section : m_sections)
        if (!section.categories.empty())
            section.tabs->SelectItem(0);
    for (size_t s = 0; s < m_sections.size(); ++s)
        if (m_sections[s].kind == Section::Printer && m_sections[s].categories.size() > 1)
            setup_variant_switch(s);
    if (!m_sections.empty()) {
        m_outer_tabs->SelectItem(0);
        show_outer_page(0);
    }
    refresh_tab_indicators();
}

size_t PublishSettingsDialog::section_group_for(Section kind)
{
    for (size_t i = 0; i < m_sections.size(); ++i)
        if (m_sections[i].kind == kind)
            return i;

    SectionGroup section;
    section.kind           = kind;
    const size_t new_index = m_sections.size();
    switch (kind) {
    case Section::Printer:
        section.title     = _L("Printer");
        section.icon_name = "printer";
        break;
    case Section::Material:
        section.title     = _L("Filament");
        section.icon_name = "filament";
        break;
    case Section::Print:
        section.title     = _L("Process");
        section.icon_name = "process";
        break;
    }
    section.icon_bmp = ScalableBitmap(this, section.icon_name, 16);

    section.page = new wxPanel(m_outer_host, wxID_ANY);
    section.page->SetBackgroundColour(GetBackgroundColour());
    auto* page_sizer = new wxBoxSizer(wxVERTICAL);
    section.tabs     = new TabCtrl(section.page, wxID_ANY, wxDefaultPosition, wxDefaultSize, s_tab_style);
    section.tabs->SetFont(Label::Body_14);
    section.tabs->SetBackgroundColour(GetBackgroundColour());
    page_sizer->Add(section.tabs, 0, wxEXPAND);
    if (kind == Section::Material) {
        section.mixed_tabs = new TabCtrl(section.page, wxID_ANY, wxDefaultPosition, wxDefaultSize, s_tab_style);
        section.mixed_tabs->SetFont(Label::Body_14);
        section.mixed_tabs->SetBackgroundColour(GetBackgroundColour());
        page_sizer->Add(section.mixed_tabs, 0, wxEXPAND | wxTOP, FromDIP(2));
        section.mixed_tabs->Hide();
    }
    section.page_host = new wxPanel(section.page, wxID_ANY);
#ifdef __WINDOWS__
    section.page_host->SetDoubleBuffered(true);
#endif
    section.page_host->SetBackgroundColour(GetBackgroundColour());
    section.page_host_sizer = new wxBoxSizer(wxVERTICAL);
    section.page_host->SetSizer(section.page_host_sizer);
    page_sizer->Add(section.page_host, 1, wxEXPAND | wxTOP, FromDIP(4));
    section.page->SetSizer(page_sizer);

    if (section.icon_bmp.bmp().IsOk())
        m_outer_tabs->AppendItem(section.title, section.icon_bmp.bmp());
    else
        m_outer_tabs->AppendItem(section.title);
    m_outer_host_sizer->Add(section.page, 1, wxEXPAND);
    section.page->Hide();
    m_sections.push_back(std::move(section));
    return new_index;
}

size_t PublishSettingsDialog::category_index_for(
    const wxString& title, Section section, size_t group, size_t source_index, const PublishMaterialIdentity& identity, bool is_mixed)
{
    auto match = [&](const Category& existing) {
        return existing.title == title && existing.section == section && existing.source_index == source_index &&
               existing.filament_id == identity.id && existing.filament_type == identity.type &&
               existing.filament_vendor == identity.vendor;
    };
    for (size_t i : m_sections[group].categories)
        if (match(m_categories[i]))
            return i;
    for (size_t i : m_sections[group].mixed_categories)
        if (match(m_categories[i]))
            return i;

    Category category;
    category.title           = title;
    category.section         = section;
    category.group           = group;
    category.source_index    = source_index;
    category.filament_type   = identity.type;
    category.filament_vendor = identity.vendor;
    category.filament_id     = identity.id;
    category.filament_slot   = source_index;
    category.is_mixed        = is_mixed;
    category.page            = new wxPanel(m_sections[group].page_host, wxID_ANY);
    category.page->SetBackgroundColour(GetBackgroundColour());
    auto* page_sizer = new wxBoxSizer(wxVERTICAL);

    std::string hex;
    std::string chip_label;
    if (section == Section::Material && !is_mixed) {
        const DynamicPrintConfig full = wxGetApp().preset_bundle->full_config();
        hex                           = filament_color_hex(full, source_index);
        chip_label                    = std::to_string(source_index + 1);
    }

    if (section == Section::Material) {
        if (is_mixed) {
            auto* enable_sizer    = new wxBoxSizer(wxHORIZONTAL);
            category.enable_check = new ::CheckBox(category.page, wxID_ANY);
            category.enable_label = add_checkbox_label(category.page, enable_sizer, category.enable_check, _L("Enable"),
                                                       _L("Publish this mixed filament and enable + Full Publish its component filaments"));
            page_sizer->Add(enable_sizer, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, FromDIP(6));
        } else {
            auto* header_sizer = new wxBoxSizer(wxHORIZONTAL);
            if (wxBitmap* chip = get_extruder_color_icon(hex, chip_label, FromDIP(20), FromDIP(20))) {
                category.filament_color_chip = new wxStaticBitmap(category.page, wxID_ANY, *chip);
                header_sizer->Add(category.filament_color_chip, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
            }
            category.title_label = new wxStaticText(category.page, wxID_ANY, title);
            category.title_label->SetFont(Label::Head_14);
            header_sizer->Add(category.title_label, 0, wxALIGN_CENTER_VERTICAL);
            auto* enable_sizer    = new wxBoxSizer(wxHORIZONTAL);
            category.enable_check = new ::CheckBox(category.page, wxID_ANY);
            category.enable_label = add_checkbox_label(category.page, enable_sizer, category.enable_check, _L("Enable"),
                                                       _L("Publish this filament slot in the 3MF file"));
            header_sizer->Add(enable_sizer, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
            page_sizer->Add(header_sizer, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, FromDIP(6));

            auto* full_sizer    = new wxBoxSizer(wxHORIZONTAL);
            category.full_check = new ::CheckBox(category.page, wxID_ANY);
            category.full_label = add_checkbox_label(category.page, full_sizer, category.full_check, _L("Full Publish"),
                                                     _L("Embed the entire filament of this slot in the 3MF file"));
            category.full_line_item = page_sizer->Add(full_sizer, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, FromDIP(6));
        }
    }

    category.scroll = new wxScrolledWindow(category.page, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    category.scroll->SetScrollRate(0, FromDIP(20));
    category.scroll->SetBackgroundColour(GetBackgroundColour());
    category.list_sizer = new wxBoxSizer(wxVERTICAL);
    category.scroll->SetSizer(category.list_sizer);
    category.scroll->DisableFocusFromKeyboard();
    category.scroll->Bind(wxEVT_RIGHT_DOWN, &PublishSettingsDialog::show_menu, this);
    category.info = new wxStaticText(category.scroll, wxID_ANY, is_mixed ? m_info_mix : m_info_empty);
    category.info->SetFont(Label::Body_13);
    category.list_sizer->Add(category.info, 1, wxALIGN_CENTER_HORIZONTAL | wxALL, FromDIP(10));
    category.info->Hide();
    page_sizer->Add(category.scroll, 1, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(4));
    if (section == Section::Material)
        category.scroll->Hide();
    category.page->SetSizer(page_sizer);
    category.page->Hide();

    const size_t category_index = m_categories.size();
    m_categories.push_back(std::move(category));
    if (is_mixed)
        m_sections[group].mixed_categories.push_back(category_index);
    else
        m_sections[group].categories.push_back(category_index);
    if (section == Section::Material) {
        TabCtrl* target = is_mixed ? m_sections[group].mixed_tabs : m_sections[group].tabs;
        if (is_mixed) {
            const DynamicPrintConfig full_cfg = wxGetApp().preset_bundle->full_config();
            const wxBitmap tab_bmp = mixed_filament_tab_bitmap(full_cfg, source_index, FromDIP(20), target->GetBackgroundColour());
            if (tab_bmp.IsOk())
                target->AppendItem(wxString(), tab_bmp);
            else
                target->AppendItem(title);
        } else if (wxBitmap* chip = get_extruder_color_icon(hex, chip_label, FromDIP(20), FromDIP(20))) {
            target->AppendItem(title, *chip);
        } else {
            target->AppendItem(title);
        }
    } else {
        m_sections[group].tabs->AppendItem(title);
    }
    m_sections[group].page_host_sizer->Add(m_categories[category_index].page, 1, wxEXPAND);
    if (is_mixed) {
        if (m_sections[group].selected_mixed < 0)
            m_sections[group].selected_mixed = 0;
        m_sections[group].mixed_tabs->Show();
    } else if (m_sections[group].selected_inner < 0) {
        m_sections[group].selected_inner = 0;
    }
    return category_index;
}

size_t PublishSettingsDialog::subcategory_index_for(size_t category_index, const wxString& title, const wxString& icon)
{
    Category& category = m_categories[category_index];
    for (size_t i = 0; i < category.subs.size(); ++i)
        if (category.subs[i].title == title)
            return i;

    Subcategory sub;
    sub.title = title;
    if (!title.IsEmpty()) {
        sub.header = new ::StaticLine(category.scroll, false, title, icon);
        sub.header->SetFont(Label::Head_14);
        sub.header->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#363636")));
        auto* wrap = new wxBoxSizer(wxVERTICAL);
        wrap->Add(sub.header, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(6));
        sub.item = category.list_sizer->Add(wrap, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(5));
    }
    category.subs.push_back(std::move(sub));
    return category.subs.size() - 1;
}

void PublishSettingsDialog::add_row_ui(const std::string& key,
                                       const wxString& label,
                                       const wxString& value,
                                       const wxString& unit,
                                       size_t category_index,
                                       size_t subcategory_index,
                                       RowKind kind)
{
    Category& category = m_categories[category_index];
    Row row;
    row.key                = key;
    row.label              = label;
    row.value              = value;
    row.unit               = unit;
    row.kind               = kind;
    row.category           = category.title;
    row.subcategory        = category.subs[subcategory_index].title;
    row.section            = category.section;
    row.section_title      = m_sections[category.group].title;
    row.outer_index        = category.group;
    row.inner_index        = category_index;
    const size_t row_index = m_rows.size();
    m_rows.push_back(std::move(row));
    Row& current  = m_rows[row_index];
    current.check = new ::CheckBox(category.scroll, wxID_ANY);
    current.check->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& event) {
        refresh_tab_indicators();
        event.Skip();
    });
    auto* row_sizer = new wxBoxSizer(wxHORIZONTAL);
    current.check_label = add_checkbox_label(category.scroll, row_sizer, current.check, label + ":", wxEmptyString,
                                             24 * wxGetApp().em_unit());
    current.value_label = new wxStaticText(category.scroll, wxID_ANY, value, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    current.value_label->SetFont(Label::Body_14);
    current.value_label->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#262E30")));
    current.value_label->SetToolTip(unit.IsEmpty() ? value : value + " " + unit);
    if (kind == RowKind::Color && !value.IsEmpty()) {
        const std::string chip_label = std::to_string(category.filament_slot + 1);
        if (wxBitmap* chip = get_extruder_color_icon(value.ToStdString(), chip_label, FromDIP(20), FromDIP(20))) {
            current.color_chip = new wxStaticBitmap(category.scroll, wxID_ANY, *chip);
            row_sizer->Add(current.color_chip, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
        }
    }
    row_sizer->Add(current.value_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    if (!unit.IsEmpty()) {
        current.unit_label = new wxStaticText(category.scroll, wxID_ANY, unit);
        current.unit_label->SetFont(Label::Body_14);
        current.unit_label->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#6B6B6B")));
        row_sizer->Add(current.unit_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));
    }
    current.item = category.list_sizer->Add(row_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(5));
    category.rows.push_back(row_index);
    category.subs[subcategory_index].rows.push_back(row_index);
}

void PublishSettingsDialog::on_full_toggle(size_t category_index)
{
    Category& cat   = m_categories[category_index];
    const bool full = cat.full_check->GetValue();
    for (size_t r : cat.rows) {
        m_rows[r].check->Enable(!full);
        m_rows[r].check_label->Enable(!full);
    }
    refresh_tab_indicators();
}

void PublishSettingsDialog::on_enable_toggle(size_t category_index)
{
    Category& cat      = m_categories[category_index];
    const bool enabled = cat.enable_check->GetValue();

    if (cat.is_mixed && enabled) {
        const DynamicPrintConfig full              = wxGetApp().preset_bundle->full_config();
        const std::vector<unsigned int> components = mixed_slot_components(full, cat.filament_slot);
        for (const unsigned int component : components) {
            const size_t component_slot = size_t(component) - 1;
            for (size_t c = 0; c < m_categories.size(); ++c) {
                Category& comp_cat = m_categories[c];
                if (comp_cat.section != Section::Material || comp_cat.is_mixed || comp_cat.filament_slot != component_slot)
                    continue;
                if (comp_cat.enable_check != nullptr)
                    comp_cat.enable_check->SetValue(true);
                if (comp_cat.full_check != nullptr)
                    comp_cat.full_check->SetValue(true);
                on_full_toggle(c);
            }
        }
    }

    apply_visibility();
    if (cat.page != nullptr)
        cat.page->GetSizer()->Layout();
    refresh_tab_indicators();
}

void PublishSettingsDialog::add_mixed_visual(size_t category_index, const MixedVisualSpec& spec)
{
    if (category_index >= m_categories.size() || !spec.valid)
        return;
    Category& category = m_categories[category_index];
    if (category.page == nullptr || category.page->GetSizer() == nullptr || category.scroll == nullptr || spec.component_colours.empty())
        return;

    auto* viz = new wxPanel(category.page, wxID_ANY);
    viz->SetBackgroundStyle(wxBG_STYLE_PAINT);
    viz->Bind(wxEVT_PAINT, [this, panel = viz, spec](wxPaintEvent&) {
        const wxColour bg = StateColor::darkModeColorFor(*wxWHITE);
        wxBufferedPaintDC pdc(panel);
        pdc.SetBackground(wxBrush(bg));
        pdc.Clear();
        const wxRect rc = panel->GetClientRect();
        if (rc.width <= 0 || rc.height <= 0)
            return;

        const size_t n = spec.component_colours.size();

        if (spec.tri_weights.size() == 3 && n == 3 && !spec.is_gradient) {
            const MixedTriangleTheme tri_theme{StateColor::darkModeColorFor(*wxWHITE), StateColor::darkModeColorFor(wxColour("#CECECE")),
                                               StateColor::darkModeColorFor(wxColour("#262E30")),
                                               StateColor::darkModeColorFor(wxColour(107, 107, 107))};
            draw_mixed_triangle_picker(pdc, rc.GetSize(), {spec.component_colours[0], spec.component_colours[1], spec.component_colours[2]},
                                       {spec.tri_weights[0], spec.tri_weights[1], spec.tri_weights[2]}, tri_theme);
            draw_mixed_triangle_labels(pdc, rc.GetSize(), {spec.tri_weights[0], spec.tri_weights[1], spec.tri_weights[2]}, tri_theme);
        } else if (!spec.is_gradient) {
            if (n == 2) {
                const int bar_h = FromDIP(27);
                draw_mixed_ratio_blend_bar(pdc, wxRect(rc.x, rc.y, rc.width, bar_h), spec.component_colours[0], spec.component_colours[1],
                                           spec.ratios[1]);
                pdc.SetFont(::Label::Body_12);
                pdc.SetTextForeground(StateColor::darkModeColorFor(wxColour(107, 107, 107)));
                const wxString la = wxString::Format("%d%%", int(std::lround(spec.ratios[0] * 100.0)));
                const wxString lb = wxString::Format("%d%%", int(std::lround(spec.ratios[1] * 100.0)));
                const int lab_y   = rc.y + bar_h + FromDIP(2);
                pdc.DrawText(la, rc.x, lab_y);
                pdc.DrawText(lb, rc.x + rc.width - pdc.GetTextExtent(lb).GetWidth(), lab_y);
            } else {
                draw_mixed_ratio_segments(pdc, rc, spec.component_colours, spec.ratios);
                std::vector<double> shares = spec.ratios;
                double total               = 0.0;
                for (double r : shares)
                    total += r;
                if (total <= 0.0) {
                    shares.assign(n, 1.0 / n);
                    total = 1.0;
                }
                pdc.SetFont(::Label::Body_12);
                int x0 = rc.x;
                for (size_t i = 0; i < n; ++i) {
                    const int x1 = (i + 1 < n) ? rc.x + int(std::lround(std::accumulate(shares.begin(), shares.begin() + i + 1, 0.0) /
                                                                        total * double(rc.width))) :
                                                 rc.x + rc.width;
                    const int w  = std::max(1, x1 - x0);
                    const wxString text = wxString::Format("%d%%", int(std::lround(shares[i] / total * 100.0)));
                    const wxSize tsz    = pdc.GetTextExtent(text);
                    if (tsz.GetWidth() + FromDIP(4) <= w) {
                        const wxColour& c = spec.component_colours[i];
                        const double lum  = 0.299 * c.Red() + 0.587 * c.Green() + 0.114 * c.Blue();
                        pdc.SetTextForeground(lum > 140 ? wxColour("#262E30") : *wxWHITE);
                        pdc.DrawText(text, x0 + (w - tsz.GetWidth()) / 2, rc.y + (rc.height - tsz.GetHeight()) / 2);
                    }
                    x0 = x1;
                }
            }
        } else {
            const MixedGradientTheme grad_theme{StateColor::darkModeColorFor(*wxWHITE),
                                                StateColor::darkModeColorFor(wxColour(238, 238, 238)),
                                                StateColor::darkModeColorFor(wxColour(107, 107, 107)),
                                                StateColor::darkModeColorFor(wxColour(107, 107, 107)),
                                                StateColor::darkModeColorFor(wxColour(38, 46, 48)),
                                                StateColor::darkModeColorFor(wxColour(172, 172, 172)),
                                                StateColor::darkModeColorFor(*wxWHITE)};
            std::vector<MixedGradientCurve> curves;
            std::vector<wxPoint2DDouble> anchors;
            if (spec.gradient_samples.size() >= 2 && n >= 2) {
                const wxRect plot = mixed_gradient_plot_rect(rc.GetSize());
                auto lift_alpha   = [](wxColour c) {
                    if (c.Alpha() == 0)
                        c.Set(c.Red(), c.Green(), c.Blue(), 150);
                    return c;
                };
                const wxColour col_a = lift_alpha(spec.component_colours[0]);
                const wxColour col_b = lift_alpha(spec.component_colours[1]);
                std::vector<wxPoint2DDouble> pts_a, pts_b;
                pts_a.reserve(spec.gradient_samples.size());
                pts_b.reserve(spec.gradient_samples.size());
                for (const auto& [t, r] : spec.gradient_samples) {
                    pts_a.push_back({plot.x + t * plot.width, plot.y + (1.0 - r) * plot.height});
                    pts_b.push_back({plot.x + t * plot.width, plot.y + r * plot.height});
                }
                anchors.reserve(spec.gradient_anchors.size());
                for (const auto& [t, r] : spec.gradient_anchors)
                    anchors.push_back({plot.x + t * plot.width, plot.y + (1.0 - r) * plot.height});
                curves.push_back({std::move(pts_a), col_a, 4});
                curves.push_back({std::move(pts_b), col_b, 2});
            }
            draw_mixed_gradient_plot(pdc, rc.GetSize(), curves, anchors, grad_theme);
        }
    });

    int viz_h;
    int viz_w;
    if (spec.is_gradient) {
        viz_w = 260;
        viz_h = 200;
    } else if (spec.tri_weights.size() == 3 && spec.component_colours.size() == 3) {
        viz_w = 160;
        viz_h = 160;
    } else {
        viz_w = 240;
        viz_h = spec.component_colours.size() == 2 ? 50 : 30;
    }
    const wxSize viz_sz(FromDIP(viz_w), FromDIP(viz_h));
    viz->SetMinSize(viz_sz);
    viz->SetMaxSize(viz_sz);
    wxSizer* page_sizer = category.page->GetSizer();
    int scroll_idx      = -1;
    for (size_t i = 0; i < page_sizer->GetChildren().size(); ++i)
        if (page_sizer->GetChildren()[i]->GetWindow() == category.scroll) {
            scroll_idx = int(i);
            break;
        }
    if (scroll_idx < 0)
        page_sizer->Add(viz, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
    else
        page_sizer->Insert(scroll_idx, viz, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
}

void PublishSettingsDialog::set_row_bold(Row& row, bool bold)
{
    row.check_label->SetFont(bold ? Label::Body_13.Bold() : Label::Body_13);
}

void PublishSettingsDialog::save_scroll_position(Category& category)
{
    if (category.scroll != nullptr)
        category.scroll->GetViewStart(&category.scroll_pos.x, &category.scroll_pos.y);
}

void PublishSettingsDialog::show_outer_page(size_t section_index)
{
    if (section_index >= m_sections.size())
        return;
    if (m_selected_outer >= 0 && m_selected_outer < static_cast<int>(m_sections.size())) {
        SectionGroup& old_section = m_sections[m_selected_outer];
        if (old_section.selected_inner >= 0 && old_section.selected_inner < static_cast<int>(old_section.categories.size()))
            save_scroll_position(m_categories[old_section.categories[old_section.selected_inner]]);
        if (old_section.selected_mixed >= 0 && old_section.selected_mixed < static_cast<int>(old_section.mixed_categories.size()))
            save_scroll_position(m_categories[old_section.mixed_categories[old_section.selected_mixed]]);
        m_sections[m_selected_outer].page->Hide();
    }
    m_selected_outer      = static_cast<int>(section_index);
    SectionGroup& section = m_sections[section_index];
    section.page->Show();
    if (section.selected_inner >= 0)
        show_inner_page(section_index, section.selected_inner);
    else if (section.selected_mixed >= 0)
        show_mixed_page(section_index, section.selected_mixed);
    m_outer_host_sizer->Layout();
}

void PublishSettingsDialog::show_inner_page(size_t section_index, int inner_index)
{
    if (section_index >= m_sections.size())
        return;
    SectionGroup& section = m_sections[section_index];
    if (inner_index < 0 || inner_index >= static_cast<int>(section.categories.size()))
        return;
    if (section.selected_inner >= 0 && section.selected_inner < static_cast<int>(section.categories.size())) {
        save_scroll_position(m_categories[section.categories[section.selected_inner]]);
        m_categories[section.categories[section.selected_inner]].page->Hide();
    }
    if (section.selected_mixed >= 0 && section.selected_mixed < static_cast<int>(section.mixed_categories.size())) {
        save_scroll_position(m_categories[section.mixed_categories[section.selected_mixed]]);
        m_categories[section.mixed_categories[section.selected_mixed]].page->Hide();
        section.selected_mixed = -1;
    }
    section.selected_inner = inner_index;
    if (section.variant_switch != nullptr)
        section.variant_switch->SetSelection(inner_index);
    Category& category     = m_categories[section.categories[inner_index]];
    category.page->Show();
    category.scroll->FitInside();
    category.scroll->Scroll(category.scroll_pos.x, category.scroll_pos.y);
    if (section.mixed_tabs != nullptr)
        section.mixed_tabs->Unselect();
    section.page_host_sizer->Layout();
}

void PublishSettingsDialog::setup_variant_switch(size_t section_index)
{
    SectionGroup& section = m_sections[section_index];
    std::vector<wxString> titles;
    for (size_t category : section.categories)
        titles.push_back(m_categories[category].title);

    section.variant_switch = new MultiSwitchButton(section.page);
    section.variant_switch->SetFitToOptions();
    section.variant_switch->SetOptions(titles);
    section.variant_switch->SetSelection(section.selected_inner);
    section.variant_switch->Bind(wxCUSTOMEVT_MULTISWITCH_SELECTION, [this, section_index](wxCommandEvent& evt) {
        evt.Skip();
        SectionGroup& sec = m_sections[section_index];
        if (evt.GetInt() != sec.selected_inner)
            sec.tabs->SelectItem(evt.GetInt());
    });

    wxSizer* page_sizer = section.page->GetSizer();
    page_sizer->Insert(1, section.variant_switch, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(4));
    section.tabs->Hide();
    section.page->Layout();
}

void PublishSettingsDialog::show_mixed_page(size_t section_index, int mixed_index)
{
    if (section_index >= m_sections.size())
        return;
    SectionGroup& section = m_sections[section_index];
    if (mixed_index < 0 || mixed_index >= static_cast<int>(section.mixed_categories.size()))
        return;
    if (section.selected_mixed >= 0 && section.selected_mixed < static_cast<int>(section.mixed_categories.size())) {
        save_scroll_position(m_categories[section.mixed_categories[section.selected_mixed]]);
        m_categories[section.mixed_categories[section.selected_mixed]].page->Hide();
    }
    if (section.selected_inner >= 0 && section.selected_inner < static_cast<int>(section.categories.size())) {
        save_scroll_position(m_categories[section.categories[section.selected_inner]]);
        m_categories[section.categories[section.selected_inner]].page->Hide();
        section.selected_inner = -1;
    }
    section.selected_mixed = mixed_index;
    Category& category     = m_categories[section.mixed_categories[mixed_index]];
    category.page->Show();
    category.scroll->FitInside();
    category.scroll->Scroll(category.scroll_pos.x, category.scroll_pos.y);
    section.tabs->Unselect();
    section.page_host_sizer->Layout();
}

void PublishSettingsDialog::on_outer_tab_changed(wxCommandEvent& event)
{
    const int selection = event.GetInt();
    if (selection >= 0 && selection < static_cast<int>(m_sections.size()))
        show_outer_page(static_cast<size_t>(selection));
}

void PublishSettingsDialog::on_inner_tab_changed(size_t section_index, wxCommandEvent& event)
{
    const int selection = event.GetInt();
    if (section_index < m_sections.size() && selection >= 0 && selection < static_cast<int>(m_sections[section_index].categories.size()))
        show_inner_page(section_index, selection);
}

void PublishSettingsDialog::on_mixed_tab_changed(size_t section_index, wxCommandEvent& event)
{
    const int selection = event.GetInt();
    if (section_index < m_sections.size() && selection >= 0 &&
        selection < static_cast<int>(m_sections[section_index].mixed_categories.size()))
        show_mixed_page(section_index, selection);
}

void PublishSettingsDialog::bind_tab_events()
{
    m_outer_tabs->Bind(wxEVT_TAB_SEL_CHANGED, &PublishSettingsDialog::on_outer_tab_changed, this);
    for (size_t section_index = 0; section_index < m_sections.size(); ++section_index) {
        m_sections[section_index].tabs->Bind(wxEVT_TAB_SEL_CHANGED,
                                             [this, section_index](wxCommandEvent& event) { on_inner_tab_changed(section_index, event); });
        if (m_sections[section_index].mixed_tabs != nullptr)
            m_sections[section_index].mixed_tabs->Bind(wxEVT_TAB_SEL_CHANGED, [this, section_index](wxCommandEvent& event) {
                on_mixed_tab_changed(section_index, event);
            });
    }
}

void PublishSettingsDialog::apply_filter(const wxString& filter_text)
{
    m_filter_mode = FilterMode::Text;
    refresh_filter(filter_text.Lower());
}

void PublishSettingsDialog::apply_pseudo_filter(bool selected_only)
{
    m_filter_mode = selected_only ? FilterMode::SelectedOnly : FilterMode::UnselectedOnly;
    refresh_filter(wxString());
}

void PublishSettingsDialog::refresh_filter(const wxString& filter)
{
    Freeze();
    const bool pseudo       = m_filter_mode != FilterMode::Text;
    const bool want_checked = m_filter_mode == FilterMode::SelectedOnly;
    m_fb_sizer->Show(!pseudo);
    if (pseudo) {
        m_pseudo_chip->SetLabel((want_checked ? _L("Filter selected") : _L("Filter non-selected")) + " " + wxString::FromUTF8("\u00d7"));
    }
    m_pseudo_chip->Show(pseudo);

    if (pseudo) {
        for (Row& row : m_rows)
            row.matches_filter = row.check->IsEnabled() && row.check->GetValue() == want_checked;
    } else {
        const bool clear = filter.IsEmpty();
        for (Row& row : m_rows) {
            row.matches_filter = clear || (row.section_title + " " + row.category + " " + row.subcategory + " " + row.label + " " +
                                           row.value + " " + row.unit)
                                              .Lower()
                                              .Contains(filter);
        }
    }

    size_t first_outer    = 0;
    int first_inner       = -1;
    bool active_has_match = false;
    for (size_t s = 0; s < m_sections.size(); ++s) {
        for (size_t inner = 0; inner < m_sections[s].categories.size(); ++inner) {
            Category& category = m_categories[m_sections[s].categories[inner]];
            bool has_match     = false;
            for (size_t r : category.rows)
                has_match = has_match || m_rows[r].matches_filter;
            category.info->Show(!has_match);
            if (!has_match)
                category.info->SetLabel(category.is_mixed ? m_info_mix :
                                                            (pseudo ? (want_checked ? m_info_nonsel : m_info_allsel) : m_info_empty));
            if (has_match && first_inner < 0) {
                first_outer = s;
                first_inner = static_cast<int>(inner);
            }
            if (static_cast<int>(s) == m_selected_outer && static_cast<int>(inner) == m_sections[s].selected_inner)
                active_has_match = has_match;
        }
        for (size_t mixed : m_sections[s].mixed_categories) {
            Category& category = m_categories[mixed];
            category.info->Show(true);
            category.info->SetLabel(m_info_mix);
        }
    }
    if (!active_has_match && first_inner >= 0 &&
        (m_selected_outer != static_cast<int>(first_outer) || m_sections[first_outer].selected_inner != first_inner)) {
        if (m_selected_outer != static_cast<int>(first_outer)) {
            m_outer_tabs->SelectItem(static_cast<int>(first_outer));
            show_outer_page(first_outer);
        }
        m_sections[first_outer].tabs->SelectItem(first_inner);
        show_inner_page(first_outer, first_inner);
    }

    apply_visibility();
    for (Category& category : m_categories) {
        save_scroll_position(category);
        category.scroll->FitInside();
        category.list_sizer->Layout();
        category.scroll->Scroll(category.scroll_pos.x, category.scroll_pos.y);
    }
    Layout();
    Thaw();
}

void PublishSettingsDialog::apply_visibility()
{
    Freeze();
    for (Category& category : m_categories) {
        const bool enabled = category.section != Section::Material ||
                             (category.enable_check != nullptr && category.enable_check->GetValue());
        if (category.scroll != nullptr)
            category.scroll->Show(enabled);
        if (category.full_line_item != nullptr)
            category.full_line_item->Show(enabled);
        bool category_any = false;
        for (size_t r : category.rows)
            category_any = category_any || m_rows[r].matches_filter;
        category.info->Show(enabled && !category_any);
        for (Subcategory& sub : category.subs) {
            bool sub_any = false;
            for (size_t r : sub.rows)
                sub_any = sub_any || m_rows[r].matches_filter;
            if (sub.header != nullptr)
                sub.item->Show(enabled && sub_any);
            for (size_t r : sub.rows)
                m_rows[r].item->Show(enabled && m_rows[r].matches_filter);
        }
        category.list_sizer->Layout();
        category.scroll->FitInside();
    }
    Thaw();
}

void PublishSettingsDialog::select_all(bool value)
{
    for (Row& row : m_rows)
        if (row.check->IsEnabled())
            row.check->SetValue(value);
    for (Category& cat : m_categories)
        if (cat.section == Section::Material && cat.enable_check != nullptr)
            cat.enable_check->SetValue(value);
    for (size_t c = 0; c < m_categories.size(); ++c)
        if (m_categories[c].section == Section::Material)
            on_enable_toggle(c);
    apply_visibility();
    refresh_tab_indicators();
}

bool PublishSettingsDialog::row_is_visible(const Row& row) const
{
    if (m_selected_outer < 0 || m_selected_outer >= static_cast<int>(m_sections.size()) ||
        row.outer_index != static_cast<size_t>(m_selected_outer) || m_sections[m_selected_outer].selected_inner < 0 ||
        m_sections[m_selected_outer].selected_inner >= static_cast<int>(m_sections[m_selected_outer].categories.size()) ||
        row.inner_index != m_sections[m_selected_outer].categories[m_sections[m_selected_outer].selected_inner] || !row.matches_filter ||
        !row.check->IsEnabled())
        return false;
    return row.item->IsShown();
}

void PublishSettingsDialog::select_visible(bool value)
{
    const bool clear_pseudo = (m_filter_mode == FilterMode::UnselectedOnly && !value) ||
                              (m_filter_mode == FilterMode::SelectedOnly && value);

    for (Row& row : m_rows)
        if (row_is_visible(row))
            row.check->SetValue(value);

    if (clear_pseudo) {
        m_filter_ctrl->ChangeValue("");
        apply_filter("");
    }
    refresh_tab_indicators();
}

void PublishSettingsDialog::show_menu(wxMouseEvent& evt)
{
    bool filtering  = !m_filter_ctrl->GetValue().IsEmpty() || m_filter_mode != FilterMode::Text;
    bool list_empty = true;
    if (m_selected_outer >= 0) {
        for (const Row& row : m_rows)
            if (row_is_visible(row)) {
                list_empty = false;
                break;
            }
    }

    wxMenu m;
    m.Append(kPublishSelectAll, _L("Select All"))->Enable(!filtering);
    m.Append(kPublishDeselectAll, _L("Deselect All"))->Enable(!filtering);
    m.AppendSeparator();
    m.Append(kPublishSelectVisible, _L("Select visible"))->Enable(!list_empty && filtering);
    m.Append(kPublishDeselectVisible, _L("Deselect visible"))->Enable(!list_empty && filtering);
    m.AppendSeparator();
    m.AppendCheckItem(kPublishFilterSelected, _L("Filter selected"));
    m.AppendCheckItem(kPublishFilterNonSelected, _L("Filter non-selected"));
    m.Check(kPublishFilterSelected, m_filter_mode == FilterMode::SelectedOnly);
    m.Check(kPublishFilterNonSelected, m_filter_mode == FilterMode::UnselectedOnly);

    m.Bind(
        wxEVT_MENU,
        [this](wxCommandEvent& e) {
            switch (e.GetId()) {
            case kPublishSelectAll: select_all(true); break;
            case kPublishDeselectAll: select_all(false); break;
            case kPublishSelectVisible: select_visible(true); break;
            case kPublishDeselectVisible: select_visible(false); break;
            case kPublishFilterSelected:
                if (m_filter_mode == FilterMode::SelectedOnly)
                    apply_filter(m_filter_ctrl->GetValue());
                else
                    apply_pseudo_filter(true);
                break;
            case kPublishFilterNonSelected:
                if (m_filter_mode == FilterMode::UnselectedOnly)
                    apply_filter(m_filter_ctrl->GetValue());
                else
                    apply_pseudo_filter(false);
                break;
            default: break;
            }
        },
        kPublishSelectAll, kPublishFilterNonSelected);

    wxWindow* src = dynamic_cast<wxWindow*>(evt.GetEventObject());
    if (!src)
        return;
    wxPoint screen_pos = src->ClientToScreen(evt.GetPosition());
    wxPoint local_pos  = ScreenToClient(screen_pos);
    PopupMenu(&m, local_pos);
}

void PublishSettingsDialog::apply_selection(const std::vector<std::string>& published_keys,
                                            const std::vector<Slic3r::PublishedMaterialEntry>& material_keys)
{
    for (Row& row : m_rows)
        if (row.check != nullptr)
            row.check->SetValue(false);
    for (Category& cat : m_categories) {
        if (cat.section != Section::Material)
            continue;
        if (cat.enable_check != nullptr)
            cat.enable_check->SetValue(false);
        if (cat.full_check != nullptr)
            cat.full_check->SetValue(false);
    }

    for (const std::string& key : published_keys)
        for (Row& row : m_rows)
            if ((row.section == Section::Print || row.section == Section::Printer) && row.check != nullptr && row.key == key) {
                row.check->SetValue(true);
                break;
            }

    for (const Slic3r::PublishedMaterialEntry& entry : material_keys) {
        if (entry.slot < 0)
            continue;
        bool entry_mixed = false;
        for (const std::string& k : entry.keys)
            if (publish_mixed_keys().count(publish_base_key(k)) != 0) {
                entry_mixed = true;
                break;
            }
        size_t cat_idx = size_t(-1);
        for (size_t c = 0; c < m_categories.size(); ++c) {
            const Category& cat = m_categories[c];
            if (cat.section != Section::Material || cat.is_mixed != entry_mixed || cat.filament_slot != size_t(entry.slot))
                continue;
            cat_idx = c;
            break;
        }
        if (cat_idx == size_t(-1))
            continue;
        Category& cat = m_categories[cat_idx];
        if (cat.enable_check != nullptr) {
            cat.enable_check->SetValue(true);
            on_enable_toggle(cat_idx);
        }
        if (entry.full && cat.full_check != nullptr) {
            cat.full_check->SetValue(true);
            on_full_toggle(cat_idx);
        } else {
            for (const std::string& key : entry.keys) {
                const std::string base = publish_base_key(key);
                for (const size_t r : cat.rows) {
                    Row& row = m_rows[r];
                    if (row.kind == RowKind::Setting && row.key == base) {
                        row.check->SetValue(true);
                        break;
                    }
                }
            }
            if (entry.publish_type && !entry.publish_type_value.empty())
                for (const size_t r : cat.rows)
                    if (m_rows[r].kind == RowKind::Type && m_rows[r].check != nullptr) {
                        m_rows[r].check->SetValue(true);
                        break;
                    }
            if (entry.publish_color && !entry.color.empty())
                for (const size_t r : cat.rows)
                    if (m_rows[r].kind == RowKind::Color && m_rows[r].check != nullptr) {
                        m_rows[r].check->SetValue(true);
                        break;
                    }
        }
    }

    apply_visibility();
    refresh_tab_indicators();
}

std::vector<std::string> PublishSettingsDialog::GetPublishedKeys() const
{
    std::vector<std::string> out;
    for (const Row& row : m_rows) {
        if ((row.section != Section::Print && row.section != Section::Printer) || !row.check->GetValue())
            continue;
        out.push_back(row.key);
    }
    return out;
}

std::vector<MixedDependencyIssue> PublishSettingsDialog::unpublished_mixed_components() const
{
    std::vector<MixedDependencyIssue> out;
    for (const Category& cat : m_categories) {
        if (!cat.is_mixed || cat.section != Section::Material)
            continue;
        if (cat.enable_check == nullptr || !cat.enable_check->GetValue())
            continue;
        const DynamicPrintConfig full = wxGetApp().preset_bundle->full_config();
        for (const unsigned int component : mixed_slot_components(full, cat.filament_slot)) {
            const size_t component_slot = size_t(component) - 1;
            const Category* comp_cat = nullptr;
            for (const Category& other : m_categories) {
                if (other.section == Section::Material && !other.is_mixed && other.filament_slot == component_slot) {
                    comp_cat = &other;
                    break;
                }
            }
            if (comp_cat == nullptr || comp_cat->enable_check == nullptr)
                continue;
            if (!comp_cat->enable_check->GetValue()) {
                out.push_back({cat.filament_slot, component_slot, MixedDependencyIssue::Reason::Disabled});
                continue;
            }
            if (comp_cat->full_check != nullptr && comp_cat->full_check->GetValue())
                continue;
            bool type_checked = false;
            for (const size_t r : comp_cat->rows) {
                const Row& row = m_rows[r];
                if (row.kind == RowKind::Type && row.check->GetValue()) {
                    type_checked = true;
                    break;
                }
            }
            if (!type_checked)
                out.push_back({cat.filament_slot, component_slot, MixedDependencyIssue::Reason::MaterialNotPublished});
        }
    }
    std::sort(out.begin(), out.end(), [](const MixedDependencyIssue& a, const MixedDependencyIssue& b) {
        return std::tie(a.mixed_slot, a.component_slot) < std::tie(b.mixed_slot, b.component_slot);
    });
    return out;
}

std::vector<Slic3r::PublishedMaterialEntry> PublishSettingsDialog::GetPublishedMaterialKeys() const
{
    std::vector<Slic3r::PublishedMaterialEntry> out;
    for (const Category& cat : m_categories) {
        if (cat.section != Section::Material)
            continue;
        if (cat.enable_check != nullptr && !cat.enable_check->GetValue())
            continue;
        Slic3r::PublishedMaterialEntry entry;
        entry.filament_type   = cat.filament_type;
        entry.filament_vendor = cat.filament_vendor;
        entry.filament_id     = cat.filament_id;
        entry.slot            = static_cast<int>(cat.filament_slot);
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle != nullptr && cat.filament_slot < bundle->filament_presets.size()) {
            if (const Preset* preset = bundle->filaments.find_preset(bundle->filament_presets[cat.filament_slot], false, true)) {
                entry.setting_id  = preset->setting_id;
                entry.preset_name = preset->name;
            }
        }
        if (cat.is_mixed) {
            Slic3r::PublishedMaterialEntry mixed_entry;
            mixed_entry.filament_type   = cat.filament_type;
            mixed_entry.filament_vendor = cat.filament_vendor;
            mixed_entry.filament_id     = cat.filament_id;
            mixed_entry.slot            = static_cast<int>(cat.filament_slot);
            const DynamicPrintConfig full_cfg = wxGetApp().preset_bundle->full_config();
            const std::string mix_color       = filament_color_hex(full_cfg, cat.filament_slot);
            if (!mix_color.empty()) {
                mixed_entry.publish_color = true;
                mixed_entry.color         = mix_color;
            }
            for (const std::string& key : publish_mixed_keys())
                mixed_entry.keys.emplace_back(key);
            out.push_back(std::move(mixed_entry));
            continue;
        }
        if (cat.full_check != nullptr && cat.full_check->GetValue()) {
            entry.full               = true;
            entry.full_keys          = full_keys_for_slot();
            entry.publish_type       = true;
            entry.publish_type_value = normalize_filament_type(cat.filament_type);
            for (size_t r : cat.rows) {
                const Row& row = m_rows[r];
                if (row.kind == RowKind::Color && !row.value.IsEmpty()) {
                    entry.publish_color = true;
                    entry.color         = row.value.ToStdString();
                }
            }
            out.push_back(std::move(entry));
            continue;
        }
        for (size_t r : cat.rows) {
            const Row& row = m_rows[r];
            if (!row.check->GetValue())
                continue;
            if (row.kind == RowKind::Color) {
                entry.publish_color = true;
                entry.color         = row.value.ToStdString();
            } else if (row.kind == RowKind::Type) {
                entry.publish_type       = true;
                entry.publish_type_value = row.value.ToStdString();
            } else {
                entry.keys.push_back(row.key);
            }
        }
        if (!entry.keys.empty() || entry.publish_type || entry.publish_color)
            out.push_back(std::move(entry));
    }
    return out;
}

bool PublishSettingsDialog::category_has_selection(const Category& cat) const
{
    if (cat.section == Section::Material)
        return cat.enable_check != nullptr && cat.enable_check->GetValue();
    for (const size_t r : cat.rows)
        if (m_rows[r].check->GetValue())
            return true;
    return false;
}

void PublishSettingsDialog::refresh_tab_indicators()
{
    for (size_t s = 0; s < m_sections.size(); ++s) {
        SectionGroup& section = m_sections[s];
        bool any              = false;
        for (size_t i = 0; i < section.categories.size(); ++i) {
            const bool on = category_has_selection(m_categories[section.categories[i]]);
            section.tabs->SetItemIndicator(static_cast<unsigned int>(i), on);
            if (section.variant_switch != nullptr) {
                const wxString& title = m_categories[section.categories[i]].title;
                const wxString  text  = on ? title + wxString(" ") + wxString(wxUniChar(0x2022)) : title;
                if (section.variant_switch->GetOptionText(static_cast<unsigned int>(i)) != text)
                    section.variant_switch->SetOptionText(static_cast<unsigned int>(i), text);
            }
            any = any || on;
        }
        if (section.mixed_tabs != nullptr)
            for (size_t i = 0; i < section.mixed_categories.size(); ++i) {
                const bool on = category_has_selection(m_categories[section.mixed_categories[i]]);
                section.mixed_tabs->SetItemIndicator(static_cast<unsigned int>(i), on);
                any = any || on;
            }
        m_outer_tabs->SetItemIndicator(static_cast<unsigned int>(s), any);
    }
}

std::vector<std::string> PublishSettingsDialog::full_keys_for_slot() const
{
    const std::set<std::string>& denylist = publish_structural_keys();
    std::vector<std::string> keys;
    for (const std::string& key : Preset::filament_options())
        if (denylist.count(key) == 0)
            keys.emplace_back(key);
    keys.emplace_back("filament_colour");
    return keys;
}

void PublishSettingsDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    m_search.msw_rescale();
    m_menu.msw_rescale();
    m_filter_box->SetIcon(m_search.bmp());
    m_menu_button->SetBitmap(m_menu.bmp());
    m_outer_tabs->Rescale();

    const DynamicPrintConfig full = wxGetApp().preset_bundle->full_config();

    for (Category& cat : m_categories) {
        if (cat.full_check != nullptr)
            cat.full_check->Refresh();
        if (cat.enable_check != nullptr)
            cat.enable_check->Refresh();
        if (cat.title_label != nullptr)
            cat.title_label->Refresh();
        if (cat.filament_color_chip != nullptr) {
            if (wxBitmap* chip = get_extruder_color_icon(filament_color_hex(full, cat.filament_slot), std::to_string(cat.filament_slot + 1),
                                                         FromDIP(20), FromDIP(20))) {
                cat.filament_color_chip->SetBitmap(*chip);
            }
        }
        cat.scroll->FitInside();
        cat.list_sizer->Layout();
    }

    for (size_t s = 0; s < m_sections.size(); ++s) {
        SectionGroup& section = m_sections[s];
        section.icon_bmp.msw_rescale();
        if (section.icon_bmp.bmp().IsOk())
            m_outer_tabs->SetItemBitmap(s, section.icon_bmp.bmp());
        section.tabs->Rescale();
        if (section.mixed_tabs != nullptr)
            section.mixed_tabs->Rescale();
        if (section.variant_switch != nullptr)
            section.variant_switch->Rescale();
    }

    for (Row& row : m_rows) {
        if (row.color_chip != nullptr && !row.value.IsEmpty()) {
            const std::string chip_label = (row.inner_index < m_categories.size()) ?
                                               std::to_string(m_categories[row.inner_index].filament_slot + 1) :
                                               "";
            if (wxBitmap* chip = get_extruder_color_icon(row.value.ToStdString(), chip_label, FromDIP(20), FromDIP(20)))
                row.color_chip->SetBitmap(*chip);
        }
    }

    for (size_t category_index = 0; category_index < m_categories.size(); ++category_index) {
        const Category& category = m_categories[category_index];
        if (category.section != Section::Material || category.is_mixed)
            continue;
        if (wxBitmap* chip = get_extruder_color_icon(filament_color_hex(full, category.filament_slot),
                                                     std::to_string(category.filament_slot + 1), FromDIP(20), FromDIP(20))) {
            const SectionGroup& section = m_sections[category.group];
            const auto iter             = std::find(section.categories.begin(), section.categories.end(), category_index);
            if (iter != section.categories.end())
                m_sections[category.group].tabs->SetItemBitmap(static_cast<unsigned int>(iter - section.categories.begin()), *chip);
        }
    }
    refresh_mixed_tab_bitmaps();

    fit_to_content();
    Refresh();
}

void PublishSettingsDialog::refresh_mixed_tab_bitmaps()
{
    const DynamicPrintConfig full = wxGetApp().preset_bundle->full_config();
    for (SectionGroup& section : m_sections) {
        if (section.mixed_tabs == nullptr)
            continue;
        const wxColour tab_bg = section.mixed_tabs->GetBackgroundColour();
        for (size_t i = 0; i < section.mixed_categories.size(); ++i) {
            const Category& category = m_categories[section.mixed_categories[i]];
            const wxBitmap tab_bmp   = mixed_filament_tab_bitmap(full, category.filament_slot, FromDIP(20), tab_bg);
            if (tab_bmp.IsOk())
                section.mixed_tabs->SetItemBitmap(static_cast<unsigned int>(i), tab_bmp);
        }
    }
}

void PublishSettingsDialog::on_sys_color_changed()
{
    refresh_mixed_tab_bitmaps();
    Refresh();
}

}}
