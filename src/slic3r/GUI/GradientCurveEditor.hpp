#ifndef slic3r_GradientCurveEditor_hpp_
#define slic3r_GradientCurveEditor_hpp_

#include <vector>
#include <wx/colour.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/geometry.h>
#include <wx/panel.h>

#include "libslic3r/FilamentMixer.hpp"

class wxWindow;

namespace Slic3r {
namespace GUI {

class GradientCurveEditor : public wxPanel
{
public:
    using PointList = std::vector<GradientAnchor>;

    GradientCurveEditor(wxWindow* parent,
                        const wxColour& color_low  = wxColour(217, 217, 217),
                        const wxColour& color_high = wxColour(217, 217, 217));

    ~GradientCurveEditor() override;

    void set_points(const PointList& pts);
    const PointList& get_points() const { return m_points; }

    void set_colors(const wxColour& color_low, const wxColour& color_high);

    void set_selected_curve(int curve_idx);
    int  get_selected_curve() const { return m_selected_curve; }

    void reset_to_linear(double y0, double y1);
    void reverse();

private:
    enum class DragMode {
        None,
        Anchor,
    };

    void normalize_points();
    void emit_changed();

    void on_paint(wxPaintEvent& evt);
    void on_left_down(wxMouseEvent& evt);
    void on_left_up(wxMouseEvent& evt);
    void on_right_down(wxMouseEvent& evt);
    void on_motion(wxMouseEvent& evt);
    void on_leave(wxMouseEvent& evt);
    void on_size(wxSizeEvent& evt);

    wxRect plot_rect() const;
    wxPoint2DDouble data_to_px_f(double x, double y) const;
    wxPoint data_to_px(double x, double y) const;
    void    px_to_data(int px, int py, double& x, double& y) const;
    int     hit_test(int px, int py) const;
    int     hit_test_curve(int px, int py, int* seg_out = nullptr) const;

    double sample_curve_y(double x) const;

    static double to_stored_y(int curve_idx, double visual_y) {
        return (curve_idx == 0) ? visual_y : (1.0 - visual_y);
    }
    static double to_visual_y(int curve_idx, double stored_y) {
        return (curve_idx == 0) ? stored_y : (1.0 - stored_y);
    }

    PointList m_points;
    wxColour  m_color_low;
    wxColour  m_color_high;

    int      m_selected_curve = 0;
    DragMode m_drag_mode      = DragMode::None;
    int      m_drag_idx       = -1;
    bool     m_dragged_moved  = false;
};

wxDECLARE_EVENT(wxEVT_GRADIENT_CURVE_CHANGED, wxCommandEvent);

}
}

#endif
