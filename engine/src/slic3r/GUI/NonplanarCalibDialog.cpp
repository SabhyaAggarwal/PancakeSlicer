#include "NonplanarCalibDialog.hpp"
#include <cstddef>
#include "libslic3r/Config.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Nonplanar/Calibration.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include <string>
#include <utility>
#include <vector>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/toplevel.h>

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "PartPlate.hpp"
#include "format.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/DialogButtons.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"
#include "Widgets/TextInput.hpp"
#include "libslic3r/GCode/ClearanceVerifier.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <wx/colour.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/string.h>

#include <algorithm>
#include <cmath>
#include <exception>

namespace Slic3r::GUI {

using namespace Nonplanar::Calibration;

namespace {

constexpr int WrapWidth  = 620;
constexpr int LabelWidth = 170;
constexpr int FieldWidth = 300;

wxString step_text(int k)
{
    if (k == 0)
        return _L("None (cone 1 already touched)");
    const ClearanceStep &step = clearance_steps()[size_t(k - 1)];
    return format_wxstr(_L("Cone %1%: %2%°, %3% mm"), k, step.slope_deg, step.relief);
}

wxString first_touch_text(int k)
{
    return k > int(clearance_steps().size()) ? _L("no cone") : format_wxstr(_L("cone %1%"), k);
}

PresetBundle &bundle() { return *wxGetApp().preset_bundle; }

wxString percent(double value) { return wxString::Format("%g%%", value); }

}

NonplanarCalibDialog::NonplanarCalibDialog(wxWindow *parent, Plater *plater)
    : DPIDialog(parent, wxID_ANY, _L("Non-planar calibration"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
    , m_plater(plater)
{
    SetBackgroundColour(*wxWHITE);
    SetFont(Label::Body_14);
    auto *root = new wxBoxSizer(wxVERTICAL);
    m_pages    = new wxNotebook(this, wxID_ANY);

    wxSizer *sizer = nullptr;
    wxPanel *page  = add_page(_L("Head clearance"),
        _L("Prints eight cones, each steeper and taller than the one before. The slicer prints every cone curved, "
           "whatever the printer's head, so where the nozzle, sock, fan duct or gantry passes over earlier material it "
           "scuffs that cone. Pick the last cone whose flank and top are clean. Write head saves a calibrated copy of "
           "the printer's head that clears exactly those cones and selects it for the printer."),
        sizer);
    add_button(page, sizer, _L("Generate clearance plate"))->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { generate(Test::Clearance); });
    m_steps = add_label(page, sizer, {});
    std::vector<wxString> steps;
    for (int k = 0; k <= int(clearance_steps().size()); ++k)
        steps.push_back(step_text(k));
    m_last_clean = add_choice(page, sizer, _L("Last clean cone"), steps, int(clearance_steps().size()) / 2);
    auto *id_row = new wxBoxSizer(wxHORIZONTAL);
    auto *id_label = new wxStaticText(page, wxID_ANY, _L("New head ID"));
    id_label->SetMinSize(wxSize(FromDIP(LabelWidth), -1));
    id_row->Add(id_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
    m_head_id = new ::TextInput(page, wxEmptyString);
    m_head_id->SetMinSize(FromDIP(wxSize(FieldWidth, 30)));
    m_inputs.push_back(m_head_id);
    id_row->Add(m_head_id, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(id_row, 0, wxALL, FromDIP(6));
    add_button(page, sizer, _L("Write head"))->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { write_head(); });
    m_fit = add_label(page, sizer, {});

    page = add_page(_L("Tilted-bead flow"),
        _L("Prints five rows of curved patches at 0, 10, 20, 30 and 40° slope; along each row the flow steps from 0.90 "
           "to 1.10 of the current value. Each object's name gives its slope and flow. For each slope pick the patch "
           "whose top is closed without ridges, as in the flow rate calibration. Write tilt flow sets Curved top flow "
           "by slope in the process preset."),
        sizer);
    add_button(page, sizer, _L("Generate tilted-bead flow plate"))->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { generate(Test::TiltFlow); });
    std::vector<wxString> multipliers;
    for (size_t i = 0; i < tilt_multipliers().size(); ++i)
        multipliers.push_back(format_wxstr(_L("Patch %1% (×%2$.2f)"), i + 1, tilt_multipliers()[i]));
    for (double slope : tilt_slopes())
        m_tilt_picks.push_back(add_choice(page, sizer, format_wxstr(_L("Best patch at %1%°"), slope), multipliers,
                                          int(tilt_multipliers().size()) / 2));
    add_button(page, sizer, _L("Write tilt flow"))->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { write_tilt_flow(); });
    m_tilt_result = add_label(page, sizer, {});

    page = add_page(_L("Curved-top quality"),
        // xgettext:no-c-format, no-boost-format
        _L("Five dome and saddle pairs in a cross. The front row prints the curved top at 60, 80 and 100% of the "
           "top surface speed (left to right); the right column at 0, 50 and 100% extra fan toward the overhang fan "
           "speed (front to back), at full speed. Pick the best speed in the front row and the best fan in the right "
           "column. Both settings are written to the process preset."),
        sizer);
    add_button(page, sizer, _L("Generate curved-top plate"))->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { generate(Test::CurvedTop); });
    std::vector<wxString> speeds, fans;
    for (double s : top_speed_factors())
        speeds.push_back(format_wxstr(_L("%1% of top surface speed"), percent(s)));
    for (double f : top_fan_boosts())
        fans.push_back(format_wxstr(_L("%1% fan boost"), percent(f)));
    m_speed_pick = add_choice(page, sizer, _L("Best speed (front row)"), speeds, int(speeds.size()) - 1);
    m_fan_pick   = add_choice(page, sizer, _L("Best fan (right column)"), fans, 0);
    add_button(page, sizer, _L("Write curved-top settings"))->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { write_curved_top(); });
    m_top_result = add_label(page, sizer, {});

    page = add_page(_L("Check head model"),
        _L("Runs the clearance check with the selected head over the sliced clearance plate. The first cone it finds "
           "the head touching should be the one after the last clean cone on your print. Generate the plate (or keep "
           "the clearance plate), slice it, then check."),
        sizer);
    add_button(page, sizer, _L("Generate clearance plate"))->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { generate(Test::Verifier); });
    m_verify_head = add_choice(page, sizer, _L("Head"), {}, 0);
    add_button(page, sizer, _L("Check sliced plate"))->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { check_verifier(); });
    m_verify_result = add_label(page, sizer, {});

    root->Add(m_pages, 1, wxEXPAND | wxALL, FromDIP(10));
    auto *buttons = new DialogButtons(this, { "OK" });
    buttons->GetOK()->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_OK); });
    root->Add(buttons, 0, wxEXPAND);

    m_base = base_head();
    m_head_id->GetTextCtrl()->SetValue(from_u8(m_base.id + "-calibrated"));
    refresh_heads();
    refresh_steps();
    SetSizerAndFit(root);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

int NonplanarCalibDialog::ShowModal()
{
    if (!has_clearance_plate()) {
        m_clearance_plate.reset();
        m_clearance_objects.clear();
    }
    const auto head = base_head();
    const bool changed_head = head.id != m_base.id;
    if (changed_head)
        m_head_id->GetTextCtrl()->ChangeValue(from_u8(head.id + "-calibrated"));
    m_base = head;
    refresh_heads();
    if (changed_head)
        m_verify_head->SetStringSelection(from_u8(head.id));
    refresh_steps();
    GetSizer()->SetSizeHints(this);
    return DPIDialog::ShowModal();
}

void NonplanarCalibDialog::refresh_heads()
{
    const wxString selected = m_verify_head->GetValue();
    m_verify_head->Clear();
    for (const std::string &id : Nonplanar::HeadLibrary::instance().ids())
        if (id != ProbeHeadId)
            m_verify_head->Append(from_u8(id));
    int selection = m_verify_head->FindString(selected);
    if (selection == wxNOT_FOUND)
        selection = m_verify_head->FindString(from_u8(m_base.id));
    if (selection == wxNOT_FOUND && m_verify_head->GetCount() > 0)
        selection = 0;
    m_verify_head->SetSelection(selection);
}

wxPanel *NonplanarCalibDialog::add_page(const wxString &title, const wxString &text, wxSizer *&sizer)
{
    auto *page = new wxPanel(m_pages);
    page->SetBackgroundColour(*wxWHITE);
    sizer = new wxBoxSizer(wxVERTICAL);
    auto *intro = new wxStaticText(page, wxID_ANY, text);
    intro->Wrap(FromDIP(WrapWidth));
    sizer->Add(intro, 0, wxEXPAND | wxALL, FromDIP(10));
    page->SetSizer(sizer);
    m_pages->AddPage(page, title);
    return page;
}

::Button *NonplanarCalibDialog::add_button(wxWindow *parent, wxSizer *sizer, const wxString &label)
{
    auto *button = new ::Button(parent, label);
    button->SetStyle(ButtonStyle::Regular, ButtonType::Choice);
    sizer->Add(button, 0, wxALL, FromDIP(6));
    m_buttons.push_back(button);
    return button;
}

::ComboBox *NonplanarCalibDialog::add_choice(wxWindow *parent, wxSizer *sizer, const wxString &label, const std::vector<wxString> &items, int selection)
{
    auto *row = new wxBoxSizer(wxHORIZONTAL);
    auto *text = new wxStaticText(parent, wxID_ANY, label);
    text->SetMinSize(wxSize(FromDIP(LabelWidth), -1));
    row->Add(text, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
    auto *combo = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(FieldWidth, -1)), 0, nullptr, wxCB_READONLY);
    for (const wxString &item : items)
        combo->Append(item);
    if (!items.empty())
        combo->SetSelection(std::clamp(selection, 0, int(items.size()) - 1));
    row->Add(combo, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(row, 0, wxALL, FromDIP(6));
    m_combos.push_back(combo);
    return combo;
}

Label *NonplanarCalibDialog::add_label(wxWindow *parent, wxSizer *sizer, const wxString &text)
{
    auto *label = new Label(parent, text, LB_AUTO_WRAP);
    label->SetMinSize(wxSize(FromDIP(WrapWidth), -1));
    sizer->Add(label, 0, wxEXPAND | wxALL, FromDIP(6));
    return label;
}

Nonplanar::HeadEnvelope NonplanarCalibDialog::base_head() const
{
    return calibrated_head(bundle().full_config(), bundle().printers.get_selected_preset().config);
}

double NonplanarCalibDialog::layer_height() const { return bundle().prints.get_edited_preset().config.opt_float("layer_height"); }

void NonplanarCalibDialog::refresh_steps()
{
    const Nonplanar::HeadEnvelope &envelope = m_base;
    wxString text = format_wxstr(_L("Head %1%, layer height %2% mm:"), from_u8(envelope.id), layer_height());
    for (int k = 1; k <= int(clearance_steps().size()); ++k)
        text += "\n" + step_text(k) + L" \u2014 " +
                (step_clearance(envelope, clearance_steps()[size_t(k - 1)], layer_height()) >= 0. ? _L("head model: clean") : _L("head model: touches"));
    m_steps->SetLabel(text);
}

void NonplanarCalibDialog::generate(Test test)
{
    if (test == Test::Clearance || test == Test::Verifier) {
        m_base = base_head();
        refresh_steps();
    }
    const std::vector<double> tilt_curve = bundle().prints.get_edited_preset().config.option<ConfigOptionFloats>("nonplanar_tilt_flow")->values;
    Plate plate;
    if (!m_plater->calib_nonplanar(test, plate))
        return;
    if (test == Test::TiltFlow)
        m_tilt_plate_curve = tilt_curve;
    m_clearance_plate.reset();
    m_clearance_objects.clear();
    if (test == Test::Clearance || test == Test::Verifier) {
        m_clearance_plate = std::move(plate);
        for (const ModelObject *object : m_plater->model().objects)
            m_clearance_objects.push_back(object->id().id);
    }
    EndModal(wxID_OK);
}

void NonplanarCalibDialog::write_head()
{
    try {
        if (m_base.id == ProbeHeadId)
            throw RuntimeError(into_u8(_L("The printer's head is the calibration probe. Select the printer's own head first.")));
        const std::string id  = into_u8(m_head_id->GetTextCtrl()->GetValue());
        const ClearanceFit fit = fit_clearance(m_base, m_last_clean->GetSelection(), layer_height(), id, m_base.name + " (calibrated)");
        Nonplanar::HeadLibrary::instance().save_user_head(fit.head);
        refresh_heads();
        m_verify_head->SetStringSelection(from_u8(id));
        if (auto *tab = dynamic_cast<TabPrinter *>(wxGetApp().get_tab(Preset::TYPE_PRINTER)))
            tab->select_nonplanar_head(id);
        m_fit->SetLabel(format_wxstr(_L("Saved %1% and selected it for the printer. Body moved %2$.2f mm; steepest plane %3$.1f°, "
                                        "relief at %4%°: %5$.2f mm, gantry floor %6$.1f mm. Save the printer preset to keep it."),
                                     from_u8(id), fit.shift, fit.slope_cap_deg, clearance_steps().back().slope_deg, fit.band_height,
                                     fit.gantry_floor));
        GetSizer()->SetSizeHints(this);
    } catch (const std::exception &error) {
        show_error(this, _L("Could not write the calibrated head.") + "\n" + from_u8(error.what()));
    }
}

void NonplanarCalibDialog::write_tilt_flow()
{
    try {
        std::vector<int> picks;
        for (const ::ComboBox *combo : m_tilt_picks)
            picks.push_back(combo->GetSelection());
        if (m_tilt_plate_curve.empty()) {
            m_tilt_plate_curve = bundle().prints.get_edited_preset().config.option<ConfigOptionFloats>("nonplanar_tilt_flow")->values;
            if (m_tilt_plate_curve.size() != tilt_slopes().size())
                m_tilt_plate_curve.assign(tilt_slopes().size(), 1.);
        }
        const std::vector<double> curve = tilt_flow_result(m_tilt_plate_curve, picks);
        DynamicPrintConfig changed;
        changed.set_key_value("nonplanar_tilt_flow", new ConfigOptionFloats(curve));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(changed);
        m_tilt_result->SetLabel(format_wxstr(_L("Tilt flow %1% written to the process preset. Save it to keep it."),
                                             from_u8(changed.opt_serialize("nonplanar_tilt_flow"))));
        GetSizer()->SetSizeHints(this);
    } catch (const std::exception &error) {
        show_error(this, from_u8(error.what()));
    }
}

void NonplanarCalibDialog::write_curved_top()
{
    try {
        const DynamicPrintConfig changed = curved_top_result(m_speed_pick->GetSelection(), m_fan_pick->GetSelection());
        wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(changed);
        m_top_result->SetLabel(format_wxstr(_L("Curved top at %1% speed with %2% fan boost written to the process preset. Save it to keep it."),
                                            percent(changed.option<ConfigOptionPercent>("nonplanar_top_speed_factor")->value),
                                            percent(changed.option<ConfigOptionPercent>("nonplanar_top_fan_boost")->value)));
        GetSizer()->SetSizeHints(this);
    } catch (const std::exception &error) {
        show_error(this, from_u8(error.what()));
    }
}

bool NonplanarCalibDialog::has_clearance_plate() const
{
    if (!m_clearance_plate || m_clearance_objects.size() != m_plater->model().objects.size())
        return false;
    for (size_t i = 0; i < m_clearance_objects.size(); ++i)
        if (m_clearance_objects[i] != m_plater->model().objects[i]->id().id)
            return false;
    return true;
}

void NonplanarCalibDialog::check_verifier()
{
    try {
        const GCodeProcessorResult *result = m_plater->get_partplate_list().get_current_slice_result();
        const PartPlate *plate = m_plater->get_partplate_list().get_curr_plate();
        std::vector<size_t> sliced_objects;
        if (result != nullptr)
            for (const auto &object : result->nonplanar_report.objects)
                sliced_objects.push_back(object.model_object_id);
        auto expected = m_clearance_objects;
        std::sort(expected.begin(), expected.end());
        std::sort(sliced_objects.begin(), sliced_objects.end());
        if (!has_clearance_plate() || plate == nullptr || !plate->is_slice_result_valid() ||
            result == nullptr || result->moves.empty() || sliced_objects != expected)
            throw RuntimeError(into_u8(_L("Generate the clearance plate and slice it first.")));
        if (m_verify_head->GetSelection() == wxNOT_FOUND)
            throw RuntimeError(into_u8(_L("Select a head.")));
        const auto *head = Nonplanar::HeadLibrary::instance().find(into_u8(m_verify_head->GetString(unsigned(m_verify_head->GetSelection()))));
        if (head == nullptr)
            throw RuntimeError(into_u8(_L("Select a head.")));
        PrintConfig printer;
        printer.apply(bundle().full_config(), true);
        ClearanceReport report;
        const int verified = verifier_first_touch(*m_clearance_plate, *result, *head, *make_tool_kinematics(printer), &report);
        const int model    = predicted_first_touch(*head, layer_height());
        const int measured = m_last_clean->GetSelection() + 1;
        m_verify_result->SetLabel(format_wxstr(_L("First touch: clearance check %1%, head model %2%, your print %3%."),
                                               first_touch_text(verified), first_touch_text(model), first_touch_text(measured)) +
                                  "\n" +
                                  (verified == measured ? _L("The clearance check agrees with your print.") :
                                                          _L("The clearance check disagrees with your print: calibrate the head on the Head clearance page.")));
        GetSizer()->SetSizeHints(this);
    } catch (const std::exception &error) {
        show_error(this, from_u8(error.what()));
    }
}

void NonplanarCalibDialog::on_dpi_changed(const wxRect &)
{
    for (auto *button : m_buttons)
        button->Rescale();
    for (auto *combo : m_combos)
        combo->Rescale();
    for (auto *input : m_inputs)
        input->Rescale();
    GetSizer()->SetSizeHints(this);
    Refresh();
}

void NonplanarCalibDialog::on_sys_color_changed() { wxGetApp().UpdateDlgDarkUI(this); }

}
