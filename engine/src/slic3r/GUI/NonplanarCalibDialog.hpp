#pragma once

#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "GUI_Utils.hpp"
#include "libslic3r/Nonplanar/Calibration.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <wx/gdicmn.h>

class Button;
class ComboBox;
class Label;
class TextInput;
class wxNotebook;
class wxPanel;
class wxSizer;
class wxString;
class wxWindow;

namespace Slic3r::GUI {

class Plater;

class NonplanarCalibDialog : public DPIDialog
{
public:
    NonplanarCalibDialog(wxWindow *parent, Plater *plater);
    int ShowModal() override;

private:
    Plater                    *m_plater;
    wxNotebook                *m_pages = nullptr;
    std::vector<::Button *>    m_buttons;
    std::vector<::ComboBox *>  m_combos;
    std::vector<::TextInput *> m_inputs;
    ::ComboBox  *m_last_clean = nullptr;
    ::TextInput *m_head_id    = nullptr;
    Label       *m_steps      = nullptr;
    Label       *m_fit        = nullptr;
    Nonplanar::HeadEnvelope m_base;
    std::vector<::ComboBox *> m_tilt_picks;
    Label                    *m_tilt_result = nullptr;
    std::vector<double>       m_tilt_plate_curve;
    ::ComboBox *m_speed_pick = nullptr;
    ::ComboBox *m_fan_pick   = nullptr;
    Label      *m_top_result = nullptr;
    ::ComboBox *m_verify_head   = nullptr;
    Label      *m_verify_result = nullptr;
    std::optional<Nonplanar::Calibration::Plate> m_clearance_plate;
    std::vector<size_t> m_clearance_objects;
    bool has_clearance_plate() const;

    wxPanel *add_page(const wxString &title, const wxString &text, wxSizer *&sizer);
    ::Button *add_button(wxWindow *parent, wxSizer *sizer, const wxString &label);
    ::ComboBox *add_choice(wxWindow *parent, wxSizer *sizer, const wxString &label, const std::vector<wxString> &items, int selection);
    Label *add_label(wxWindow *parent, wxSizer *sizer, const wxString &text);
    void generate(Nonplanar::Calibration::Test test);
    Nonplanar::HeadEnvelope base_head() const;
    double layer_height() const;
    void refresh_steps();
    void refresh_heads();
    void write_head();
    void write_tilt_flow();
    void write_curved_top();
    void check_verifier();
    void on_dpi_changed(const wxRect &) override;
    void on_sys_color_changed() override;
};

}
