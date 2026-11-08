#include "NonplanarHeadDialog.hpp"
#include <cstddef>
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include <wx/dialog.h>
#include <wx/toplevel.h>

#include "GUI_App.hpp"
#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/DialogButtons.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"
#include "Widgets/TextInput.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/libslic3r.h"

#include <wx/brush.h>
#include <wx/colour.h>
#include <wx/event.h>
#include <wx/font.h>
#include <wx/gdicmn.h>
#include <wx/string.h>
#include <wx/window.h>
#include <wx/datetime.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/grid.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/pen.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <vector>
#include <utility>

namespace Slic3r::GUI {
namespace {

double number(const wxString &text)
{
    double value;
    if (!text.ToDouble(&value) || !std::isfinite(value))
        throw RuntimeError(into_u8(_L("Expected a finite number.")));
    return value;
}

wxString decimal(double value)
{
    const double rounded = std::round(value * 1e6) / 1e6;
    return wxString::Format("%.15g", rounded == 0. ? 0. : rounded);
}

void fit_columns(wxGrid *grid, int first_width, int other_width)
{
    wxClientDC dc(grid);
    dc.SetFont(grid->GetLabelFont());
    int width = grid->GetRowLabelSize() + wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, grid) + grid->FromDIP(4);
    for (int col = 0; col < grid->GetNumberCols(); ++col) {
        const int size = std::max(grid->FromDIP(col == 0 ? first_width : other_width),
                                  dc.GetTextExtent(grid->GetColLabelValue(col)).x + grid->FromDIP(16));
        grid->SetColSize(col, size);
        width += size;
    }
    grid->SetMinSize(wxSize(width, grid->FromDIP(210)));
}

Button *action(wxWindow *parent, const wxString &label)
{
    auto *button = new Button(parent, label);
    button->SetStyle(ButtonStyle::Regular, ButtonType::Choice);
    return button;
}

}

NonplanarHeadDialog::NonplanarHeadDialog(wxWindow *parent, const Nonplanar::HeadEnvelope &head, double layer_height)
    : DPIDialog(parent, wxID_ANY, _L("Non-planar print head"), wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), m_head(head), m_layer_height(layer_height)
{
    SetBackgroundColour(*wxWHITE);
    SetFont(Label::Body_14);
    auto *root = new wxBoxSizer(wxVERTICAL);
    auto *identity = new wxFlexGridSizer(2, FromDIP(6), FromDIP(10));
    identity->AddGrowableCol(1);
    auto input = [&](wxWindow *owner, wxSizer *sizer, const wxString &label, const wxString &value) {
        sizer->Add(new wxStaticText(owner, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        auto *field = new ::TextInput(owner, value);
        field->SetMinSize(FromDIP(wxSize(200, 30)));
        sizer->Add(field, 1, wxEXPAND);
        return field;
    };
    m_id = input(this, identity, _L("New head ID"), from_u8(head.id + "-user"));
    m_name = input(this, identity, _L("Name"), from_u8(head.name));
    root->Add(identity, 0, wxEXPAND | wxALL, FromDIP(10));

    m_pages = new wxNotebook(this, wxID_ANY);
    auto *profile = new wxPanel(m_pages);
    profile->SetBackgroundColour(*wxWHITE);
    auto *profile_sizer = new wxBoxSizer(wxVERTICAL);
    m_boxes = new wxGrid(profile, wxID_ANY);
    m_boxes->CreateGrid(int(head.boxes.size()), 10);
    m_boxes->SetDefaultCellFont(GetFont());
    m_boxes->SetLabelFont(GetFont());
    const wxString columns[] = {_L("Component"), _L("Z min (mm)"), _L("Z max (mm)"),
        _L("Center X (mm)"), _L("Center Y (mm)"), _L("Half X (mm)"), _L("Half Y (mm)"),
        _L("Infinite X"), _L("Infinite Y"), _L("Nozzle")};
    for (int col = 0; col < 10; ++col) {
        m_boxes->SetColLabelValue(col, columns[col]);
        auto *attr = new wxGridCellAttr;
        if (col >= 7) {
            attr->SetEditor(new wxGridCellBoolEditor);
            attr->SetRenderer(new wxGridCellBoolRenderer);
        } else if (col > 0) {
            attr->SetEditor(new wxGridCellFloatEditor);
        }
        m_boxes->SetColAttr(col, attr);
    }
    fit_columns(m_boxes, 145, 70);
    fill_boxes();
    profile_sizer->Add(m_boxes, 1, wxEXPAND | wxALL, FromDIP(6));
    auto *row_actions = new wxBoxSizer(wxHORIZONTAL);
    auto *add = action(profile, _L("Add component"));
    auto *remove = action(profile, _L("Remove selected component"));
    m_actions = {add, remove};
    row_actions->Add(add, 0, wxRIGHT, FromDIP(6));
    row_actions->Add(remove);
    profile_sizer->Add(row_actions, 0, wxALL, FromDIP(6));
    add->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        m_boxes->AppendRows();
        const int row = m_boxes->GetNumberRows() - 1;
        m_boxes->SetCellValue(row, 0, _L("New component"));
        for (int col = 1; col < 7; ++col)
            m_boxes->SetCellValue(row, col, col == 2 ? "10" : "0");
        m_head.calibrated = false;
        refresh_plot();
    });
    remove->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        m_boxes->DisableCellEditControl();
        const int row = m_boxes->GetGridCursorRow();
        if (row >= 0 && row < m_boxes->GetNumberRows()) {
            m_boxes->DeleteRows(row);
            m_head.calibrated = false;
            refresh_plot();
        }
    });
    auto *bottom = new wxBoxSizer(wxHORIZONTAL);
    auto *limits = new wxFlexGridSizer(2, FromDIP(6), FromDIP(10));
    m_margin = input(profile, limits, _L("Vertical margin (mm)"), decimal(head.margin));
    m_lateral_margin = input(profile, limits, _L("Lateral margin (mm)"), decimal(head.lateral_margin));
    m_gap = input(profile, limits, _L("Layer height (mm)"), decimal(layer_height));
    bottom->Add(limits, 0, wxALL, FromDIP(6));
    m_plot = new wxPanel(profile, wxID_ANY);
    m_plot->SetBackgroundColour(*wxWHITE);
    m_plot->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_plot->SetMinSize(FromDIP(wxSize(300, 220)));
    bottom->Add(m_plot, 1, wxEXPAND | wxALL, FromDIP(6));
    profile_sizer->Add(bottom, 1, wxEXPAND);
    m_plot->Bind(wxEVT_SIZE, [this](wxSizeEvent &event) { m_plot->Refresh(); event.Skip(); });
    m_plot->Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
        wxAutoBufferedPaintDC dc(m_plot);
        dc.SetBackground(wxBrush(StateColor::darkModeColorFor(*wxWHITE)));
        dc.Clear();
        dc.SetFont(GetFont());
        dc.SetTextForeground(StateColor::darkModeColorFor(*wxBLACK));
        const wxSize size = m_plot->GetClientSize();
        const wxPoint center(size.x / 2, size.y / 2);
        const int radius = std::max(1, std::min(size.x, size.y) / 2 - FromDIP(30));
        dc.SetPen(wxPen(StateColor::darkModeColorFor(wxColour(180, 180, 180))));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        for (int angle : {30, 60, 90}) {
            dc.DrawCircle(center, radius * angle / 90);
            dc.DrawText(wxString::Format(L"%d\u00B0", angle), center.x + radius * angle / 90 + FromDIP(2), center.y);
        }
        dc.DrawLine(center.x - radius, center.y, center.x + radius, center.y);
        dc.DrawLine(center.x, center.y - radius, center.x, center.y + radius);
        const wxSize x_label = dc.GetTextExtent(_L("+X")), y_label = dc.GetTextExtent(_L("+Y"));
        dc.DrawText(_L("+X"), center.x + radius + FromDIP(4), center.y - x_label.y - FromDIP(2));
        dc.DrawText(_L("+Y"), center.x - y_label.x / 2, center.y - radius - y_label.y - FromDIP(2));
        try {
            const auto slopes = read_head().slope_map(72, read_layer_height());
            std::vector<wxPoint> points;
            for (size_t i = 0; i <= slopes.size(); ++i) {
                const double phi = 2. * PI * (i % slopes.size()) / slopes.size();
                const double r = radius * slopes[i % slopes.size()] / (0.5 * PI);
                points.emplace_back(center.x + int(r * std::cos(phi)), center.y - int(r * std::sin(phi)));
            }
            dc.SetPen(wxPen(StateColor::darkModeColorFor(wxColour(0, 150, 136)), FromDIP(2)));
            dc.DrawLines(int(points.size()), points.data());
        } catch (const std::exception &) {
            dc.DrawText(_L("Enter a valid profile and layer height to show slope limits."), FromDIP(6), FromDIP(6));
        }
    });
    profile->SetSizer(profile_sizer);
    m_pages->AddPage(profile, _L("Head profile"));

    auto *calibration = new wxPanel(m_pages);
    calibration->SetBackgroundColour(*wxWHITE);
    auto *calibration_sizer = new wxBoxSizer(wxVERTICAL);
    auto *instructions = new wxStaticText(calibration, wxID_ANY,
        _L("For a head that reaches further on one side. Most printers only need Calibration > Non-planar > Head "
           "clearance.\n"
           "1. Check the head profile and margins on the Head profile tab.\n"
           "2. Print resources/calib/nonplanar/slope_star.stl at the layer height shown there. Each of its eight wedges "
           "climbs in steps of rising slope toward one direction; thin pillars at the head's reach show scuffing.\n"
           "3. For each direction, counterclockwise from +X, enter the last angle that printed clean, or 0° if none did.\n"
           "Apply fits the head to these angles; a component shared by several directions can lower the limit in the "
           "others. Save it under a new head ID to keep it."));
    instructions->Wrap(FromDIP(650));
    calibration_sizer->Add(instructions, 0, wxEXPAND | wxALL, FromDIP(10));
    m_angles = new wxGrid(calibration, wxID_ANY);
    m_angles->CreateGrid(8, 2);
    m_angles->SetColLabelValue(0, _L("Azimuth (degrees)"));
    m_angles->SetColLabelValue(1, _L("Last clean angle (degrees)"));
    for (int row = 0; row < 8; ++row) {
        m_angles->SetCellValue(row, 0, wxString::Format("%d", row * 45));
        m_angles->SetReadOnly(row, 0);
    }
    auto *angle_attr = new wxGridCellAttr;
    angle_attr->SetEditor(new wxGridCellFloatEditor);
    m_angles->SetColAttr(1, angle_attr);
    m_angles->SetColSize(0, FromDIP(160));
    m_angles->SetColSize(1, FromDIP(240));
    calibration_sizer->Add(m_angles, 1, wxEXPAND | wxALL, FromDIP(10));
    auto *apply = action(calibration, _L("Apply"));
    m_actions.push_back(apply);
    apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { calibrate(); });
    calibration_sizer->Add(apply, 0, wxALL, FromDIP(10));
    calibration->SetSizer(calibration_sizer);
    m_pages->AddPage(calibration, _L("Per-direction fit"));
    root->Add(m_pages, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));
    auto *buttons = new DialogButtons(this, {L("Save as user head"), "Cancel"}, _L("Save as user head"));
    buttons->GetFIRST()->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { save(); });
    buttons->GetCANCEL()->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CANCEL); });
    root->Add(buttons, 0, wxEXPAND | wxTOP, FromDIP(10));
    m_boxes->Bind(wxEVT_GRID_CELL_CHANGED, [this](wxGridEvent &event) {
        m_head.calibrated = false;
        refresh_plot();
        event.Skip();
    });
    for (auto *field : {m_margin, m_lateral_margin, m_gap})
        field->GetTextCtrl()->Bind(wxEVT_TEXT, [this, field](wxCommandEvent &event) {
            if (field != m_gap) m_head.calibrated = false;
            refresh_plot();
            event.Skip();
        });
    SetSizerAndFit(root);
    CenterOnParent();
    on_sys_color_changed();
}

void NonplanarHeadDialog::fill_boxes()
{
    if (m_boxes->GetNumberRows() > 0)
        m_boxes->DeleteRows(0, m_boxes->GetNumberRows());
    m_boxes->AppendRows(int(m_head.boxes.size()));
    for (size_t row = 0; row < m_head.boxes.size(); ++row) {
        const auto &box = m_head.boxes[row];
        m_boxes->SetCellValue(int(row), 0, from_u8(box.name));
        const double values[] = {box.min.z(), box.max.z(), (box.min.x() + box.max.x()) / 2.,
            (box.min.y() + box.max.y()) / 2., (box.max.x() - box.min.x()) / 2., (box.max.y() - box.min.y()) / 2.};
        for (int col = 1; col < 7; ++col)
            m_boxes->SetCellValue(int(row), col, decimal(values[col - 1]));
        m_boxes->SetCellValue(int(row), 7, box.infinite_x ? "1" : "");
        m_boxes->SetCellValue(int(row), 8, box.infinite_y ? "1" : "");
        m_boxes->SetCellValue(int(row), 9, box.nozzle ? "1" : "");
    }
}

Nonplanar::HeadEnvelope NonplanarHeadDialog::read_head() const
{
    auto out = m_head;
    out.id = into_u8(m_id->GetTextCtrl()->GetValue());
    out.name = into_u8(m_name->GetTextCtrl()->GetValue());
    out.margin = number(m_margin->GetTextCtrl()->GetValue());
    out.lateral_margin = number(m_lateral_margin->GetTextCtrl()->GetValue());
    out.boxes.clear();
    for (int row = 0; row < m_boxes->GetNumberRows(); ++row) {
        Nonplanar::HeadBox box;
        box.name = into_u8(m_boxes->GetCellValue(row, 0));
        double values[6];
        for (int col = 1; col < 7; ++col)
            values[col - 1] = number(m_boxes->GetCellValue(row, col));
        if (values[4] < 0. || values[5] < 0. || values[1] < values[0])
            throw RuntimeError(into_u8(_L("Half extents must be non-negative and Z max must be at least Z min.")));
        box.min = Vec3d(values[2] - values[4], values[3] - values[5], values[0]);
        box.max = Vec3d(values[2] + values[4], values[3] + values[5], values[1]);
        box.infinite_x = m_boxes->GetCellValue(row, 7) == "1";
        box.infinite_y = m_boxes->GetCellValue(row, 8) == "1";
        box.nozzle = m_boxes->GetCellValue(row, 9) == "1";
        out.boxes.emplace_back(std::move(box));
    }
    return Nonplanar::load_head_envelope(Nonplanar::save_head_envelope(out));
}

double NonplanarHeadDialog::read_layer_height() const
{
    const double gap = number(m_gap->GetTextCtrl()->GetValue());
    if (gap <= 0.) throw RuntimeError(into_u8(_L("Layer height must be positive.")));
    return gap;
}

void NonplanarHeadDialog::refresh_plot() { m_plot->Refresh(); }

void NonplanarHeadDialog::calibrate()
{
    m_boxes->DisableCellEditControl();
    m_angles->DisableCellEditControl();
    try {
        Nonplanar::SlopeStarReading reading;
        reading.date = into_u8(wxDateTime::Today().FormatISODate());
        for (int row = 0; row < 8; ++row)
            reading.samples.push_back({row * PI / 4., number(m_angles->GetCellValue(row, 1)) * PI / 180.});
        m_layer_height = read_layer_height();
        auto base = read_head();
        base.id = m_head.id;
        m_head = Nonplanar::HeadEnvelope::from_slope_star(base, reading, float(m_layer_height));
        fill_boxes();
        m_pages->SetSelection(0);
        refresh_plot();
    } catch (const std::exception &error) {
        show_error(this, _L("Could not calibrate the head. Check all eight clean angles and the head profile.") + "\n" + from_u8(error.what()));
    }
}

void NonplanarHeadDialog::save()
{
    m_boxes->DisableCellEditControl();
    try {
        auto head = read_head();
        Nonplanar::HeadLibrary::instance().save_user_head(head);
        m_head = std::move(head);
        EndModal(wxID_OK);
    } catch (const std::exception &error) {
        show_error(this, _L("Could not save the head. Check the profile and choose a new ID of letters, digits, hyphens or underscores.") + "\n" + from_u8(error.what()));
    }
}

void NonplanarHeadDialog::on_dpi_changed(const wxRect &)
{
    for (auto *field : {m_id, m_name, m_margin, m_lateral_margin, m_gap})
        field->Rescale();
    fit_columns(m_boxes, 145, 70);
    m_plot->SetMinSize(FromDIP(wxSize(300, 220)));
    m_angles->SetColSize(0, FromDIP(160));
    m_angles->SetColSize(1, FromDIP(240));
    for (auto *button : m_actions)
        button->Rescale();
    GetSizer()->SetSizeHints(this);
    Refresh();
}

void NonplanarHeadDialog::on_sys_color_changed()
{
    wxGetApp().UpdateDlgDarkUI(this);
    for (auto *grid : {m_boxes, m_angles}) {
        if (!grid) continue;
        grid->SetDefaultCellBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
        grid->SetDefaultCellTextColour(StateColor::darkModeColorFor(*wxBLACK));
        grid->SetLabelBackgroundColour(StateColor::darkModeColorFor(wxColour(240, 240, 240)));
        grid->SetLabelTextColour(StateColor::darkModeColorFor(*wxBLACK));
        grid->SetGridLineColour(StateColor::darkModeColorFor(wxColour(200, 200, 200)));
        grid->Refresh();
    }
    if (m_plot) m_plot->Refresh();
}

}
