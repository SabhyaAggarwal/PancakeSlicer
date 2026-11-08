#ifndef slic3r_GUI_FilamentBitmapUtils_hpp_
#define slic3r_GUI_FilamentBitmapUtils_hpp_

#include <cstddef>
#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/dc.h>
#include <wx/gdicmn.h>
#include <wx/geometry.h>
#include <wx/graphics.h>
#include <array>
#include <vector>

namespace Slic3r { class DynamicPrintConfig; struct GradientCurve; }

namespace Slic3r { namespace GUI {

struct TriPoint { double x, y; };

double tri_signed_area2(TriPoint a, TriPoint b, TriPoint c);
bool tri_contains(TriPoint p, TriPoint v0, TriPoint v1, TriPoint v2);
void tri_barycentric(TriPoint p, TriPoint v0, TriPoint v1, TriPoint v2,
                     double& w0, double& w1, double& w2);
TriPoint tri_clamp(TriPoint p, TriPoint v0, TriPoint v1, TriPoint v2);

void fill_gradient_rect_east(wxDC& dc, const wxRect& rect, const wxColour& from, const wxColour& to);

enum class FilamentRenderMode {
    Single,
    Dual,
    Triple,
    Quadruple,
    Gradient
};

wxBitmap create_filament_bitmap(const std::vector<wxColour>& colors,
                              const wxSize& size,
                              bool force_gradient = false);

wxColour blend_n_colors(const std::vector<wxColour>& cols, const std::vector<double>& weights);

std::vector<wxColour> sample_gradient_ramp(const wxColour& first,
                                           const wxColour& second,
                                           const Slic3r::GradientCurve& curve,
                                           int steps);

std::vector<wxColour> mixed_gradient_ramp(const Slic3r::DynamicPrintConfig& cfg, size_t slot, int steps);

Slic3r::GradientCurve mixed_gradient_curve(const Slic3r::DynamicPrintConfig& cfg, size_t slot);

void fill_gradient_ramp_rect(wxDC& dc, const wxRect& rect, const std::vector<wxColour>& ramp);

wxBitmap create_gradient_ramp_bitmap(const std::vector<wxColour>& ramp, const wxSize& size);

void recompute_mixed_slot_colors(std::vector<wxColour>& colors,
                                 const Slic3r::DynamicPrintConfig& cfg);

struct MixedGradientCurve
{
    std::vector<wxPoint2DDouble> points;
    wxColour                     colour;
    int                          stroke_dip;
};

struct MixedGradientTheme
{
    wxColour background;
    wxColour grid;
    wxColour axis;
    wxColour label;
    wxColour label_strong;
    wxColour outline;
    wxColour point_fill;
};

wxRect mixed_gradient_plot_rect(const wxSize& canvas);

void draw_mixed_gradient_plot(wxDC& dc, const wxSize& canvas,
                              const std::vector<MixedGradientCurve>& curves,
                              const std::vector<wxPoint2DDouble>& anchors,
                              const MixedGradientTheme& theme);

void draw_mixed_ratio_blend_bar(wxDC& dc, const wxRect& rect, const wxColour& first,
                                const wxColour& second, double second_fraction);

void draw_mixed_ratio_segments(wxDC& dc, const wxRect& rect, const std::vector<wxColour>& colours,
                               const std::vector<double>& shares);

struct MixedTriangleTheme
{
    wxColour background;
    wxColour outline;
    wxColour ring;
    wxColour label;
};

std::array<TriPoint, 3> mixed_triangle_vertices(const wxSize& size, double margin_dip = 20.0);

void draw_mixed_triangle_picker(wxDC& dc, const wxSize& size, const std::array<wxColour, 3>& colours,
                                const std::array<double, 3>& weights, const MixedTriangleTheme& theme);

void draw_mixed_triangle_labels(wxDC& dc, const wxSize& size, const std::array<double, 3>& weights,
                                const MixedTriangleTheme& theme);

}}

#endif