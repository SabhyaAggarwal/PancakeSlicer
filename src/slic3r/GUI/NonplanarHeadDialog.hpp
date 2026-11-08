#pragma once

#include "GUI_Utils.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include <string>
#include <vector>
#include <wx/gdicmn.h>

class wxGrid;
class wxPanel;
class wxNotebook;
class TextInput;
class Button;

namespace Slic3r::GUI {

class NonplanarHeadDialog : public DPIDialog
{
public:
    NonplanarHeadDialog(wxWindow *parent, const Nonplanar::HeadEnvelope &head, double layer_height);
    const std::string &saved_head_id() const { return m_head.id; }

private:
    Nonplanar::HeadEnvelope m_head;
    double m_layer_height;
    wxGrid *m_boxes = nullptr;
    wxGrid *m_angles = nullptr;
    wxPanel *m_plot = nullptr;
    wxNotebook *m_pages = nullptr;
    ::TextInput *m_id = nullptr;
    ::TextInput *m_name = nullptr;
    ::TextInput *m_margin = nullptr;
    ::TextInput *m_lateral_margin = nullptr;
    ::TextInput *m_gap = nullptr;
    std::vector<::Button *> m_actions;

    void fill_boxes();
    Nonplanar::HeadEnvelope read_head() const;
    double read_layer_height() const;
    void refresh_plot();
    void calibrate();
    void save();
    void on_dpi_changed(const wxRect &) override;
    void on_sys_color_changed() override;
};

}
