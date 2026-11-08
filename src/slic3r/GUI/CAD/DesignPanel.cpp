#include "slic3r/GUI/CAD/DesignPanel.hpp"
#include "libslic3r_version.h"
#include "slic3r/GUI/CAD/DesignCanvas.hpp"
#include "slic3r/GUI/CAD/DesignSketchTool.hpp"
#include "slic3r/GUI/CAD/DesignTextDialog.hpp"
#include "slic3r/GUI/CAD/DesignRowList.hpp"
#include "slic3r/GUI/CAD/DesignOffer.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "libslic3r/CAD/GeometryEngine.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Format/OBJ.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"

#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <Eigen/Geometry>
#include <TopAbs_Orientation.hxx>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <wx/aui/framemanager.h>
#include <wx/longlong.h>
#include <wx/stopwatch.h>

#include <cassert>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include "libslic3r/CAD/SketchEngine.hpp"
#include "libslic3r/CAD/CadDocument.hpp"
#include "libslic3r/Point.hpp"
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/CAD/SketchSolver.hpp"
#include <map>
#include <math.h>
#include <set>
#include <wx/accel.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/chartype.h>
#include <vector>
#include <wx/scrolwin.h>
#include <wx/anybutton.h>
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include <utility>
#include <wx/listbase.h>
#include <wx/busycursor.h>
#include <wx/colourdata.h>
#include "slic3r/GUI/CAD/DesignInteraction.hpp"
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/spinbutt.h>
#include <wx/stattext.h>
#include <wx/checkbox.h>
#include <wx/checklst.h>
#include <wx/spinctrl.h>
#include <wx/listctrl.h>
#include <wx/string.h>
#include <wx/tglbtn.h>
#include <wx/textctrl.h>
#include <wx/time.h>
#include <wx/toplevel.h>
#include <wx/translation.h>
#include <wx/statline.h>
#include <wx/statbmp.h>
#include <wx/image.h>
#include <wx/font.h>
#include <wx/textdlg.h>
#include <wx/filedlg.h>
#include <wx/dialog.h>
#include <wx/colordlg.h>
#include <wx/menu.h>
#include <wx/progdlg.h>
#include <wx/unichar.h>
#include <wx/utils.h>

#include <string>
#include <memory>
#include <functional>
#include <cmath>
#include <algorithm>
#include <thread>
#include <atomic>
#include <wx/window.h>

#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/DropDown.hpp"
#include "slic3r/GUI/Widgets/CheckBox.hpp"
#include "slic3r/GUI/Widgets/ComboBox.hpp"
#include "slic3r/GUI/Widgets/TextInput.hpp"
#include "slic3r/GUI/Widgets/DialogButtons.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Widgets/StaticBox.hpp"
#include "libslic3r/CAD/SketchImport.hpp"
#include "libslic3r/CAD/ThreadStandards.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Shortcuts.hpp"
#include "slic3r/GUI/I18N.hpp"

namespace Slic3r { namespace GUI {

static constexpr double MESH_IMPORT_TOLERANCE        = 0.01;
static constexpr double MESH_IMPORT_MERGE_ANGLE_DEG  = 5.0;
static constexpr size_t MESH_IMPORT_TRIANGLE_WARN    = 50000;

static wxFlexGridSizer* two_col_form()
{
    auto* f = new wxFlexGridSizer(2, 6, 8);
    f->AddGrowableCol(1, 1);
    f->SetFlexibleDirection(wxHORIZONTAL);
    return f;
}


static ComboBox* make_combo(wxWindow* parent)
{
    auto* c = new ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                           wxSize(parent->FromDIP(90), parent->FromDIP(24)), 0, nullptr, wxCB_READONLY);
    c->SetMinSize(wxSize(parent->FromDIP(90), parent->FromDIP(24)));
    return c;
}

static int combo_append_index(ComboBox* c, const wxString& label, int index)
{
    const int row = c->Append(label, wxNullBitmap);
    c->SetClientData(unsigned(row), reinterpret_cast<void*>(intptr_t(index)));
    return row;
}

static wxString en_format(double v, int digits = 2)
{
    char fmt[16];
    std::snprintf(fmt, sizeof(fmt), "%%.%df", digits);
    char buf[64];
    std::snprintf(buf, sizeof(buf), fmt, v);
    for (char* c = buf; *c; ++c) if (*c == ',') *c = '.';
    return wxString::FromUTF8(buf);
}
static bool en_parse(const wxString& text, double& out)
{
    wxString t(text);
    t.Replace(wxT(","), wxT("."));
    return t.ToCDouble(&out);
}

struct DpToken { wxColour light, dark; };
enum DpTok { TokRibbonBg, TokRibbonHover, TokPanelBg, TokSecText, TokCtlText, TokItemText, TokItemDim,
             TokBorder, TokCount };
static const DpToken kDpTokens[TokCount] = {
    { wxColour(0xEC,0xEC,0xEE), wxColour(0x36,0x36,0x3C) },
    { wxColour(0xD7,0xD7,0xDB), wxColour(0x4D,0x4D,0x54) },
    { wxColour(0xFB,0xFB,0xFD), wxColour(0x2D,0x2D,0x30) },
    { wxColour(0x66,0x66,0x68), wxColour(0x81,0x81,0x83) },
    { wxColour(0x35,0x35,0x37), wxColour(0xC8,0xC8,0xC8) },
    { wxColour(0x2C,0x2C,0x2E), wxColour(0xE0,0xE0,0xE0) },
    { wxColour(0xA0,0xA0,0xA2), wxColour(0x80,0x80,0x80) },
    { wxColour(0xDB,0xDB,0xDB), wxColour(0x4A,0x4A,0x51) },
};
static bool     dp_dark()         { return wxGetApp().dark_mode(); }
static wxColour dp_tok(DpTok t)   { return dp_dark() ? kDpTokens[t].dark : kDpTokens[t].light; }
static wxColour dp_ribbon_bg()    { return dp_tok(TokRibbonBg); }
static wxColour dp_ribbon_hover() { return dp_tok(TokRibbonHover); }
static wxColour dp_panel_bg()     { return dp_tok(TokPanelBg); }
static wxColour dp_sec_text()     { return dp_tok(TokSecText); }
static wxColour dp_ctl_text()     { return dp_tok(TokCtlText); }
static wxColour dp_item_text()    { return dp_tok(TokItemText); }
static wxColour dp_item_dim()     { return dp_tok(TokItemDim); }

static wxColour dp_retheme(const wxColour& c, bool to_dark)
{
    for (const DpToken& t : kDpTokens)
        if (c == (to_dark ? t.light : t.dark))
            return to_dark ? t.dark : t.light;
    return c;
}

static StaticBox* make_card(wxWindow* parent)
{
    auto* c = new StaticBox(parent);
    c->SetCornerRadius(8);
    c->SetBorderColor(StateColor(kDpTokens[TokBorder].light));
    return c;
}

static void style_sidebar_icon_btn(::Button* b)
{
    b->SetPaddingSize(b->FromDIP(wxSize(2, 2)));
    b->SetMinSize(b->FromDIP(wxSize(24, 24)));
    b->SetCornerRadius(b->FromDIP(4));
    b->SetBackgroundColor(StateColor(
        std::pair<wxColour, int>(wxColour(DesignRowList::hover_chip), StateColor::Hovered),
        std::pair<wxColour, int>(dp_panel_bg(),                       StateColor::Normal)));
}

static ::Button* sidebar_icon_btn(wxWindow* parent, const char* icon, const wxString& tip, int px = 20)
{
    auto* b = new ::Button(parent, "", icon, wxBORDER_NONE, px);
    b->SetName("design_icon_btn");
    b->SetCanFocus(false);
    b->SetIconSpacing(0);
    b->SetBorderColor(StateColor());
    b->SetToolTip(tip);
    style_sidebar_icon_btn(b);
    return b;
}

static std::string commit_icon(bool bodies)
{
    const std::string name = bodies ? "toolbar_add_plate_bodies" : "toolbar_add_plate";
    return dp_dark() ? name + "_dark" : name;
}

enum RowAction { RowEdit, RowMove, RowVisibility, RowDelete, RowZoom };
static constexpr int kOriginRow = 0, kBedRow = 1;

static DesignRowList::Action eye_action(bool shown)
{
    return { RowVisibility, shown ? "design_eye" : "design_eye_off", shown ? _L("Hide") : _L("Show") };
}

static wxString origin_toggle_text(bool shown)
{
    return shown ? _L("Hide reference planes and axes") : _L("Show reference planes and axes");
}

static wxString bed_toggle_text(bool shown)
{
    return shown ? _L("Hide the printer bed and its plate grid")
                 : _L("Show the printer bed and its plate grid");
}

static wxString sketch_plane_prompt()
{
    return _L("Click a reference plane or a flat face to sketch on it — Esc cancels");
}

static wxSpinCtrlDouble* make_spin(wxWindow* parent, double val,
                                   double mn = 0.1, double mx = 1000.0)
{
    auto* box = new StaticBox(parent);
    box->SetCornerRadius(4);
    box->SetBorderColor(StateColor(kDpTokens[TokBorder].light));
    auto* s = new wxSpinCtrlDouble(box, wxID_ANY, "", wxDefaultPosition, parent->FromDIP(wxSize(90, -1)),
                                   wxSP_ARROW_KEYS | wxBORDER_NONE);
    s->SetRange(mn, mx);
    s->SetDigits(2);
    s->SetValue(val);
    s->Bind(wxEVT_MOUSEWHEEL, [s](wxMouseEvent& e) {
        if (wxWindow::FindFocus() == s) e.Skip();
        else if (wxWindow* p = s->GetParent())
            wxPostEvent(p, e);
    });
    auto* sz = new wxBoxSizer(wxHORIZONTAL);
    sz->Add(s, 1, wxEXPAND | wxALL, parent->FromDIP(2));
    box->SetSizer(sz);
    return s;
}

static wxWindow* spin_frame(wxWindow* spin) { return spin->GetParent(); }

static SketchPlane plane_from_index(int i)
{
    switch (i) {
        case 1:  return SketchPlane::XZ();
        case 2:  return SketchPlane::YZ();
        default: return SketchPlane::XY();
    }
}

static int index_from_plane(const SketchPlane& p)
{
    if (std::abs(p.normal.y()) > 0.5) return 1;
    if (std::abs(p.normal.x()) > 0.5) return 2;
    return 0;
}

static bool is_base_plane(const SketchPlane& p, const Vec3d& modeling_origin)
{
    SketchPlane  b = plane_from_index(index_from_plane(p));
    b.origin += modeling_origin;
    const double e = 1e-6;
    return (p.origin - b.origin).norm() < e && (p.normal - b.normal).norm() < e
        && (p.x_axis - b.x_axis).norm() < e && (p.y_axis - b.y_axis).norm() < e;
}

static SketchPlane face_plane_inward(const TopoDS_Face& face)
{
    SketchPlane p;
    p.origin = GeometryEngine::face_centroid_world(face);
    p.normal = (-GeometryEngine::face_normal_world(face)).normalized();
    Vec3d x(0, 0, 0); double best = 0;
    for (const TopoDS_Edge& e : GeometryEngine::edges_of_face(face)) {
        const std::vector<Vec3d> pts = GeometryEngine::sample_edge_world(e);
        if (pts.size() < 2) continue;
        Vec3d d = pts.back() - pts.front();
        d = d - p.normal * d.dot(p.normal);
        const double len = d.norm();
        if (len > best) { best = len; x = d / len; }
    }
    if (best < 1e-9) {
        const Vec3d ref = std::abs(p.normal.z()) < 0.9 ? Vec3d(0, 0, 1) : Vec3d(1, 0, 0);
        x = ref.cross(p.normal).normalized();
    }
    p.x_axis = x.normalized();
    p.y_axis = p.normal.cross(p.x_axis).normalized();
    return p;
}

DesignPanel::DesignPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    wxStopWatch build_clock;
    long        build_mark = 0;
    auto        build_phase = [&build_clock, &build_mark](const char* phase) {
        const long now = build_clock.Time();
        BOOST_LOG_TRIVIAL(info) << "Design tab build: " << phase << " " << now - build_mark << " ms";
        build_mark = now;
    };
    auto* body_panel = new wxPanel(this, wxID_ANY);
    m_form = new wxScrolledWindow(body_panel, wxID_ANY);
    SetBackgroundColour(dp_panel_bg());
    body_panel->SetBackgroundColour(dp_panel_bg());
    m_form->SetBackgroundColour(dp_panel_bg());

    auto* root = new wxBoxSizer(wxVERTICAL);
    {
        auto* hdr = new wxStaticText(m_form, wxID_ANY, _L("Design"));
        hdr->SetFont(Label::Head_16);
        root->Add(hdr, 0, wxLEFT | wxRIGHT | wxTOP, 12);
        root->AddSpacer(2);
    }

    m_toolbar = new wxScrolledWindow(this, wxID_ANY);
    m_toolbar->SetScrollRate(15, 0);
    m_toolbar->ShowScrollbars(wxSHOW_SB_DEFAULT, wxSHOW_SB_NEVER);
    m_toolbar->SetBackgroundColour(dp_ribbon_bg());

    auto icon_btn = [this](const char* icon, const wxString& tip) {
        auto* b = new ScalableButton(m_toolbar, wxID_ANY, icon, "", FromDIP(wxSize(40, 40)),
                                     wxDefaultPosition, wxBU_EXACTFIT | wxBORDER_NONE, false, 34);
        b->SetToolTip(tip);
        b->SetBackgroundColour(dp_ribbon_bg());
        m_tool_btns.push_back(b);
        b->Bind(wxEVT_ENTER_WINDOW, [this, b](wxMouseEvent& e) {
            b->SetBackgroundColour(b == m_active_tool_btn ? wxColour(0x52, 0xC7, 0xB8) : dp_ribbon_hover());
            b->Refresh(); e.Skip(); });
        b->Bind(wxEVT_LEAVE_WINDOW, [this, b](wxMouseEvent& e) {
            b->SetBackgroundColour(b == m_active_tool_btn ? wxColour(0x00, 0x96, 0x88) : dp_ribbon_bg());
            b->Refresh(); e.Skip(); });
        b->Bind(wxEVT_LEFT_DOWN, [this, b](wxMouseEvent& e) {
            set_active_tool_btn(b); e.Skip(); });
        return b;
    };
    auto caption = [this](const wxString& t) {
        auto* s = new wxStaticText(m_toolbar, wxID_ANY, t);
        wxFont f = Label::Body_12; f.SetWeight(wxFONTWEIGHT_BOLD);
        s->SetFont(f);
        s->SetForegroundColour(dp_sec_text());
        return s;
    };
    auto add_sep = [this](wxSizer* row) {
        row->AddSpacer(5);
        row->Add(new wxStaticLine(m_toolbar, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(1, 22)), wxLI_VERTICAL),
                 0, wxALIGN_CENTER_VERTICAL);
        row->AddSpacer(5);
    };

    m_construction = new wxCheckBox(m_toolbar, wxID_ANY, _L("Construction"));
    m_construction->SetForegroundColour(dp_ctl_text());
    auto select_tool = [this](DesignSketchTool::Mode mode, const wxString& hint) {
        if (!m_viewport) return;
        if (!m_viewport->is_sketching())
            start_sketch_on_target(false);
        m_viewport->set_sketch_tool(mode);
        m_viewport->set_sketch_construction(m_construction->GetValue());
        set_status(StatusKind::Info, m_sketch_on.IsEmpty() ? hint
                           : wxString::Format(_L("%s  ·  plane: %s"), hint, m_sketch_on));
    };

    auto sk_key = [this, select_tool](int ch, DesignSketchTool::Mode m, const wxString& h) {
        m_keys_sketch[ch] = [select_tool, m, h] { select_tool(m, h); };
    };
    sk_key('L', DesignSketchTool::Mode::Line,         _L("Line — click start, then end"));
    sk_key('R', DesignSketchTool::Mode::CornerRect,   _L("Rectangle — click two opposite corners"));
    sk_key('C', DesignSketchTool::Mode::CenterCircle, _L("Circle — click center, then radius"));
    sk_key('A', DesignSketchTool::Mode::ThreePointArc,_L("Arc — click start, end, then a point"));
    sk_key('S', DesignSketchTool::Mode::Slot,         _L("Slot — two centerline ends, then the width"));
    sk_key('E', DesignSketchTool::Mode::Ellipse,      _L("Ellipse — center, major end, minor point"));
    sk_key('B', DesignSketchTool::Mode::BSpline,      _L("Spline — click control points"));
    sk_key('P', DesignSketchTool::Mode::Point,        _L("Point — click to place"));
    sk_key('D', DesignSketchTool::Mode::Dimension,    _L("Dimension — click 2 points or an entity"));
    sk_key('T', DesignSketchTool::Mode::Trim,         _L("Trim — click a segment to trim it"));
    sk_key('X', DesignSketchTool::Mode::Extend,       _L("Extend — click a line/arc to extend it"));
    sk_key('O', DesignSketchTool::Mode::Offset,       _L("Offset — pick an entity, drag the distance"));
    sk_key('M', DesignSketchTool::Mode::Mirror,       _L("Mirror — pick axis, then entities"));
    sk_key('F', DesignSketchTool::Mode::Fillet,       _L("Fillet — pick two lines, set the radius"));
    sk_key('H', DesignSketchTool::Mode::Chamfer,      _L("Chamfer — pick two lines, set the distance"));
    m_keys_sketch['G'] = [this, select_tool] {
        if (m_viewport) {
            push_polygon_params();
        }
        select_tool(DesignSketchTool::Mode::Polygon, _L("Polygon — click center, then a vertex"));
    };
    m_keys_sketch['V'] = [this] {
        if (m_viewport && m_viewport->is_sketching() && !m_viewport->edit_sketch_selection_value())
            set_status(_L("Nothing here has a value to type — pick a line, an arc, a circle, or two entities"));
    };
    m_keys_sketch['K'] = [this] { enter_constrain_inline(); };
    m_keys_sketch['N'] = [this] {
        if (m_viewport && m_viewport->view_normal_to_sketch()) {
            set_status(StatusKind::Info, _L("Normal to the sketch plane"));
        }
    };
    m_keys_sketch['Q'] = [this] {
        if (m_viewport && m_viewport->is_sketching() &&
            m_viewport->toggle_sketch_construction_selection() > 0) {
            set_status(_L("Converted the selection between construction and real geometry"));
            return;
        }
        if (m_construction) {
            m_construction->SetValue(!m_construction->GetValue());
            if (m_viewport && m_viewport->is_sketching())
                m_viewport->set_sketch_construction(m_construction->GetValue());
        }
    };

    auto SHIFT = [](int ch) { return ch | SC_SHIFT; };

    auto status_flag = [this](const wxString& on_msg, const wxString& off_msg, bool on) {
        set_status(StatusKind::Info, on ? on_msg : off_msg);
    };
    m_keys_feature['P'] = [this, status_flag] {
        if (m_viewport) status_flag(_L("Origin planes shown"), _L("Origin planes hidden"),
                                    m_viewport->toggle_planes());
    };
    m_keys_feature['A'] = [this, status_flag] {
        if (m_viewport) status_flag(_L("World axes shown"), _L("World axes hidden"),
                                    m_viewport->toggle_axes());
    };
    m_keys_feature['X'] = [this] { toggle_section_view(); };
    m_keys_feature['F'] = [this] { place_on_face(); };

    m_keys_feature[WXK_HOME] = [this] {
        if (!m_viewport) return;
        m_viewport->set_view("iso");
        set_status(_L("Axonometric view, fitted"));
    };

    m_keys_feature['P' | SC_SHIFT | SC_CTRL] = [this] { on_commit(); };
    m_keys_feature['B' | SC_SHIFT | SC_CTRL] = [this] {
        toggle_bed();
        set_status(m_show_bed ? _L("Bed shown") : _L("Bed hidden"));
    };
    m_keys_feature['O' | SC_SHIFT | SC_CTRL] = [this] {
        toggle_origin();
        set_status(m_show_origin ? _L("Reference planes shown") : _L("Reference planes hidden"));
    };

    m_tb_feature = new wxBoxSizer(wxHORIZONTAL);
    std::map<std::string, std::vector<wxWindow*>> tb_slot;
    static const std::set<std::string> kBarKeep = { "place", "section", "flip" };
    auto fadd = [&tb_slot](const char* id, wxWindow* w) {
        if (kBarKeep.count(id) == 0) { w->Hide(); return; }
        tb_slot[id].push_back(w);
    };
    {
        struct FeatVar { const char* icon; wxString tip; wxString hint; std::function<void()> action; int key = 0; };
        struct FeatFlyout {
            std::vector<DropDown::Item> items;
            std::vector<std::function<void()>> actions;
            std::vector<std::string> icon_names;
            ScalableButton* btn = nullptr;
            DropDown drop;
            FeatFlyout() : drop(items) {}
        };
        auto feat_dropdown = [&](const char* id, const char* def_icon, const wxString& grp, std::vector<FeatVar> vars) {
            for (size_t i = 0; i < vars.size(); ++i) {
                if (vars[i].key) m_keys_feature[vars[i].key] = vars[i].action;
                m_verb_actions["fly:" + std::string(id) + "#" + std::to_string(i)] = vars[i].action;
            }
            if (kBarKeep.count(id) == 0)
                return;
            auto* b = icon_btn(def_icon, grp);
            b->SetFont(Label::Body_14);
            auto fo = std::make_shared<FeatFlyout>();
            for (auto& v : vars) {
                DropDown::Item it;
                it.text = v.tip;
                it.tip  = v.hint;
                it.icon = create_scaled_bitmap(v.icon, m_form, 18);
                fo->items.push_back(it);
                fo->actions.push_back(std::move(v.action));
                fo->icon_names.emplace_back(v.icon);
            }
            fo->btn = b;
            fo->drop.Create(b);
            m_icon_refresh.push_back([this, fp = fo.get()] {
                for (size_t i = 0; i < fp->items.size(); ++i)
                    fp->items[i].icon = create_scaled_bitmap(fp->icon_names[i], m_form, 18);
                fp->drop.Invalidate(true);
            });
            fo->drop.SetUseContentWidth(true, false);
            fo->drop.Invalidate(true);
            FeatFlyout* fp = fo.get();
            fo->drop.Bind(wxEVT_COMBOBOX, [this, fp](wxCommandEvent& e) {
                int i = e.GetInt();
                if (i >= 0 && i < (int) fp->actions.size()) {
                    fp->btn->SetBitmap_(fp->icon_names[i]);
                    fp->actions[i]();
                    set_active_tool_btn(fp->btn);
                }
            });
            b->Bind(wxEVT_BUTTON, [b, fp](wxCommandEvent&) {
                fp->drop.Invalidate(true);
                fp->drop.SetUseContentWidth(false, false);
                fp->drop.SetUseContentWidth(true, false);
                wxPoint pos = b->ClientToScreen(wxPoint(0, -6));
                fp->drop.Position(pos, wxSize(0, b->GetSize().y + b->FromDIP(12)));
                fp->drop.Popup();
            });
            m_flyout_keepalive.push_back(fo);
            fadd(id, b);
            auto* chev = new wxStaticText(m_toolbar, wxID_ANY, wxString::FromUTF8("\xE2\x96\xBE"));
            chev->SetForegroundColour(dp_sec_text());
            chev->SetFont(Label::Body_9);
            fadd(id, chev);
        };

        m_keys_feature[SHIFT('S')] = [this] { start_sketch(); };
        feat_dropdown("material", "design_extrude", _L("Add material (extrude / revolve / sweep / loft / thicken / rib)"), {
            {"design_extrude", _L_CONTEXT("Extrude", "Design"), _L("Extrude a sketch profile, or push/pull a picked face"),
             [this] {
                if (m_sel_solid_face >= 0 && !m_doc.body.IsNull()
                    && (m_viewport == nullptr || m_viewport->loop_pick_region() < 0)) {
                    m_extrude_face_src   = m_sel_solid_face;
                    m_extrude_sketch_ref = -1;
                    open_tool(Tool::Extrude);
                    return;
                }
                m_extrude_face_src   = -1;
                m_extrude_sketch_ref = resolve_extrude_sketch();
                if (m_extrude_sketch_ref < 0) {
                    set_status(StatusKind::Error, _L("Create a sketch, or pick a solid face, first"));
                    return;
                }
                open_tool(Tool::Extrude);
             }, SHIFT('E')},
            {"design_revolve", _L("Revolve"), _L("Revolve a profile about an axis"),
             [this] {
                m_revolve_sketch_ref = resolve_extrude_sketch();
                if (m_revolve_sketch_ref < 0) {
                    set_status(StatusKind::Error, _L("Create a sketch profile to revolve first"));
                    return;
                }
                fill_revolve_axes(m_revolve_axis, m_revolve_axis_ents, m_revolve_sketch_ref, 0, -2);
                open_tool(Tool::Revolve);
             }, SHIFT('R')},
            {"design_sweep", _L("Sweep"), _L("Sweep a profile along a path"),
             [this] {
                m_sweep_profile_ref = resolve_extrude_sketch();
                m_sweep_path_ref    = -1;
                if (m_sweep_profile_ref < 0) {
                    set_status(StatusKind::Error, _L("Create a profile sketch to sweep first"));
                    return;
                }
                open_tool(Tool::Sweep);
             }, SHIFT('W')},
            {"design_loft", _L("Loft"), _L("Loft (skin) between two or more profiles"),
             [this] {
                int n = 0;
                for (const auto& f : m_doc.features)
                    if (f.type == CadFeatureType::Sketch) ++n;
                if (n < 2) {
                    set_status(StatusKind::Error, _L("Create at least two profile sketches to loft"));
                    return;
                }
                m_loft_refs.clear();
                open_tool(Tool::Loft);
             }, SHIFT('L')},
            {"design_thicken", _L("Thicken"), _L("Offset a solid face into a thin plate (new body)"),
             [this] {
                if (m_doc.bodies.empty()) {
                    set_status(StatusKind::Error, _L("Thicken needs a solid body — add or import one first"));
                    return;
                }
                {
                    m_thicken_body->Clear();
                    for (size_t i = 0; i < m_doc.bodies.size(); ++i) {
                        const std::string& n = m_doc.bodies[i].name;
                        m_thicken_body->Append(n.empty() ? wxString::Format(_L("Body %zu"), i + 1) : wxString::FromUTF8(n));
                    }
                    if (m_thicken_body->GetCount() > 0)
                        m_thicken_body->SetSelection(std::min(selected_body_default(),
                                                       int(m_thicken_body->GetCount()) - 1));
                }
                if (m_thicken_body->GetSelection() != m_sel_solid_body)
                    m_sel_solid_face = -1;
                open_tool(Tool::Thicken);
             }, 0},
            {"design_rib", _L_CONTEXT("Rib", "Design"), _L("Grow a thin wall from an open sketch line, fused to a body"),
             [this] {
                if (m_doc.bodies.empty()) {
                    set_status(StatusKind::Error, _L("Rib needs a solid body — add or import one first"));
                    return;
                }
                {
                    m_rib_body->Clear();
                    for (size_t i = 0; i < m_doc.bodies.size(); ++i) {
                        const std::string& n = m_doc.bodies[i].name;
                        m_rib_body->Append(n.empty() ? wxString::Format(_L("Body %zu"), i + 1) : wxString::FromUTF8(n));
                    }
                    if (m_rib_body->GetCount() > 0)
                        m_rib_body->SetSelection(std::min(selected_body_default(),
                                                       int(m_rib_body->GetCount()) - 1));
                }
                {
                    m_rib_sketch->Clear();
                    for (int i = 0; i < int(m_doc.features.size()); ++i) {
                        if (m_doc.features[i].type == CadFeatureType::Sketch ||
                            m_doc.features[i].type == CadFeatureType::Project)
                            combo_append_index(m_rib_sketch, wxString::FromUTF8(m_doc.features[i].name), i);
                    }
                    if (m_rib_sketch->GetCount() > 0) m_rib_sketch->SetSelection(0);
                    else {
                        set_status(StatusKind::Error, _L("Create a sketch with an open line first"));
                        return;
                    }
                }
                open_tool(Tool::Rib);
             }, 'R'},
        });

        auto* b_pattern = icon_btn("design_pattern", _L_CONTEXT("Pattern", "Design"));
        std::function<void()> act_pattern = [this] {
            if (m_doc.bodies.empty()) {
                set_status(StatusKind::Error, _L("Create a solid body to pattern first"));
                return;
            }
            open_tool(Tool::Pattern);
        };
        b_pattern->Bind(wxEVT_BUTTON, [act_pattern](wxCommandEvent&) { act_pattern(); });
        m_keys_feature[SHIFT('N')] = act_pattern;
        fadd("pattern", b_pattern);
        m_body_gates.push_back({b_pattern, 1, b_pattern->GetToolTipText(),
                                _L("Pattern — needs a solid body to replicate")});

        feat_dropdown("surface", "design_surface", _L("Surface (extrude / revolve / loft / fill / offset / thicken)"), {
            {"design_extrude", _L("Surface Extrude"), _L("Extrude a sketch into a sheet body (no end caps)"),
             [this] {
                m_surf_extrude_sketch_ref = resolve_extrude_sketch();
                if (m_surf_extrude_sketch_ref < 0) {
                    set_status(StatusKind::Error, _L("Create a sketch first"));
                    return;
                }
                open_tool(Tool::SurfaceExtrude);
             }, SHIFT('G')},
            {"design_revolve", _L("Surface Revolve"), _L("Revolve a sketch profile into a sheet body"),
             [this] {
                m_surf_revolve_sketch_ref = resolve_extrude_sketch();
                if (m_surf_revolve_sketch_ref < 0) {
                    set_status(StatusKind::Error, _L("Create a sketch profile to revolve first"));
                    return;
                }
                fill_revolve_axes(m_surf_revolve_axis, m_surf_revolve_axis_ents, m_surf_revolve_sketch_ref, 0, -2);
                open_tool(Tool::SurfaceRevolve);
             }, SHIFT('J')},
            {"design_loft", _L("Surface Loft"), _L("Loft (skin) between 2+ profiles, open (no end caps)"),
             [this] {
                int n = 0;
                for (const auto& f : m_doc.features)
                    if (f.type == CadFeatureType::Sketch) ++n;
                if (n < 2) {
                    set_status(StatusKind::Error, _L("Create at least two profile sketches to loft"));
                    return;
                }
                m_surf_loft_refs.clear();
                open_tool(Tool::SurfaceLoft);
             }, SHIFT('O')},
            {"design_surface", _L("Surface Fill"), _L("Fill a sketch boundary with a smooth face"),
             [this] {
                m_surf_fill_sketch_ref = resolve_extrude_sketch();
                if (m_surf_fill_sketch_ref < 0) {
                    set_status(StatusKind::Error, _L("Create a sketch profile to fill first"));
                    return;
                }
                open_tool(Tool::SurfaceFill);
             }, SHIFT('Q')},
            {"design_offset", _L("Surface Offset"), _L("Offset a sheet body's shell by a signed distance"),
             [this] {
                populate_sheet_body_choices(m_surf_offset_body);
                if (m_surf_offset_body->GetCount() == 0) {
                    set_status(StatusKind::Error, _L("No sheet body to offset — create a surface feature first"));
                    return;
                }
                open_tool(Tool::SurfaceOffset);
             }, SHIFT('U')},
            {"design_thicken", _L("Thicken Surface"), _L("Thicken a sheet body into a solid"),
             [this] {
                populate_sheet_body_choices(m_surf_thicken_body);
                if (m_surf_thicken_body->GetCount() == 0) {
                    set_status(StatusKind::Error, _L("No sheet body to thicken — create a surface feature first"));
                    return;
                }
                open_tool(Tool::ThickenSurface);
             }, SHIFT('V')},
        });

        feat_dropdown("plane", "design_plane", _L("Datum / Curve (plane / axis / coord sys / helix / project)"), {
            {"design_plane", _L("Plane"), _L("Reference plane (offset / tilt / midplane / tangent / two edges / coincident)"),
             [this] {
                 populate_plane_choices(m_plane_base);
                 reset_plane_refs();
                 open_tool(Tool::Plane);
             }, SHIFT('P')},
            {"design_line", _L("Axis"), _L("Datum axis (two points, face normal, cylinder centerline, two planes, along edge)"),
             [this] {
                 populate_plane_choices(m_axis_plane_a);
                 populate_plane_choices(m_axis_plane_b);
                 reset_axis_refs();
                 open_tool(Tool::Axis);
             }, SHIFT('A')},
            {"design_point", _L("Coordinate system"), _L("Datum coordinate system (world point, or face + direction edge)"),
             [this] {
                 reset_coordsys_refs();
                 open_tool(Tool::CoordSys);
             }, SHIFT('C')},
            {"design_thread", _L("Helix"), _L("Helical curve (spring path) — use as a sweep path for coils / springs / augers"),
             [this] {
                 populate_plane_choices(m_helix_plane);
                 open_tool(Tool::Helix);
             }, 0},
            {"design_sketch", _L_CONTEXT("Project", "Design"), _L("Project body edges onto a plane as sketch entities"),
             [this] {
                if (m_doc.bodies.empty()) {
                    set_status(StatusKind::Error, _L("Project needs a body — add or import one first"));
                    return;
                }
                {
                    m_proj_source_body->Clear();
                    for (size_t i = 0; i < m_doc.bodies.size(); ++i) {
                        const std::string& n = m_doc.bodies[i].name;
                        m_proj_source_body->Append(n.empty() ? wxString::Format(_L("Body %zu"), i + 1) : wxString::FromUTF8(n));
                    }
                    if (m_proj_source_body->GetCount() > 0)
                        m_proj_source_body->SetSelection(std::min(selected_body_default(),
                                                       int(m_proj_source_body->GetCount()) - 1));
                }
                populate_plane_choices(m_proj_plane);
                const int src = m_proj_source_body->GetSelection();
                if (m_sel_solid_face >= 0 && m_sel_solid_body != src) m_sel_solid_face = -1;
                m_proj_face_label->SetLabel(m_sel_solid_face >= 0
                    ? wxString::Format(_L("Face %d"), m_sel_solid_face)
                    : _L("(all edges)"));
                open_tool(Tool::Project);
             }, 0},
        });

        feat_dropdown("placement", "design_move", _L("Placement (transform / mirror / mate)"), {
            {"design_move", _L("Transform"), _L("Move and/or rotate an existing body"),
             [this] {
                if (m_doc.bodies.empty()) {
                    set_status(StatusKind::Error, _L("Transform needs a body — add or import one first"));
                    return;
                }
                {
                    m_xf_body->Clear();
                    for (size_t i = 0; i < m_doc.bodies.size(); ++i) {
                        const std::string& n = m_doc.bodies[i].name;
                        m_xf_body->Append(n.empty() ? wxString::Format(_L("Body %zu"), i + 1) : wxString::FromUTF8(n));
                    }
                    if (m_xf_body->GetCount() > 0)
                        m_xf_body->SetSelection(std::min(selected_body_default(),
                                                       int(m_xf_body->GetCount()) - 1));
                }
                open_tool(Tool::Transform);
             }, SHIFT('Y')},
            {"design_mirror", _L("Mirror"), _L("Reflect a body about a plane"),
             [this] {
                if (m_doc.bodies.empty()) {
                    set_status(StatusKind::Error, _L("Mirror needs a body — add or import one first"));
                    return;
                }
                {
                    m_mirror_body->Clear();
                    for (size_t i = 0; i < m_doc.bodies.size(); ++i) {
                        const std::string& n = m_doc.bodies[i].name;
                        m_mirror_body->Append(n.empty() ? wxString::Format(_L("Body %zu"), i + 1) : wxString::FromUTF8(n));
                    }
                    if (m_mirror_body->GetCount() > 0)
                        m_mirror_body->SetSelection(std::min(selected_body_default(),
                                                       int(m_mirror_body->GetCount()) - 1));
                }
                populate_plane_choices(m_mirror_plane);
                open_tool(Tool::Mirror);
             }, SHIFT('Z')},
            {"design_c_coincident", _L("Mate"), _L("Assembly: align two coordinate systems (fastened, planar, revolute, slider, cylindrical)"),
             [this] {
                 open_tool(Tool::Mate);
             }, 0},
        });

        auto* b_boolean = icon_btn("design_boolean", _L("Boolean (combine bodies)"));
        std::function<void()> act_boolean = [this] { on_boolean_tool(); };
        b_boolean->Bind(wxEVT_BUTTON, [act_boolean](wxCommandEvent&) { act_boolean(); });
        m_keys_feature[SHIFT('B')] = act_boolean;
        fadd("boolean", b_boolean);
        m_body_gates.push_back({b_boolean, 2, b_boolean->GetToolTipText(),
                                _L("Boolean — needs two solid bodies to combine")});

        auto* b_cut = icon_btn("design_cut", _L("Cut (split a body with a plane)"));
        std::function<void()> act_cut = [this] {
            if (m_doc.bodies.empty()) {
                set_status(StatusKind::Error, _L("Create a solid body to cut first"));
                return;
            }
            populate_plane_choices(m_cut_plane);
            populate_body_choices();
            open_tool(Tool::Cut);
        };
        b_cut->Bind(wxEVT_BUTTON, [act_cut](wxCommandEvent&) { act_cut(); });
        m_keys_feature[SHIFT('X')] = act_cut;
        fadd("cut", b_cut);
        m_body_gates.push_back({b_cut, 1, b_cut->GetToolTipText(),
                                _L("Cut — needs a solid body to slice")});

        auto* b_color = icon_btn("color_palette", _L("Color — set the selected body's display color"));
        b_color->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_set_body_color(); });
        fadd("color", b_color);
        m_verb_actions["btn:colour"] = [this] { on_set_body_color(); };
        m_verb_actions["btn:zoom_to"] = [this] { zoom_to_selection(); };
        m_verb_actions["btn:delete"] = [this] { on_delete_feature(); };
        auto rename_feature = [this] {
            if (const int b = tree_body_selection(); b >= 0)
                m_parts->begin_rename(b);
            else if (tree_selection() != wxNOT_FOUND)
                m_tree->begin_rename(m_tree->selection());
            else
                set_status(_L("Select a feature, or a body, first — then rename it"));
        };
        m_verb_actions["btn:rename"] = rename_feature;
        m_keys_feature[WXK_F2]        = rename_feature;
        m_verb_actions["btn:sk_delete"] = [this] {
            if (m_viewport && m_viewport->is_sketching())
                m_viewport->delete_selected_sketch_entities();
            else
                on_delete_feature();
        };

        m_verb_actions["btn:delete_body"] = [this] { on_delete_body(); };
        m_verb_actions["btn:edit"]   = [this] { on_edit_feature(); };
        m_verb_actions["btn:mass"]   = [this] { on_mass_properties(); };
        m_verb_actions["btn:interference"] = [this] { on_check_interference(); };
        m_verb_actions["btn:constrain"] = [this] {
            on_begin_constrain();
            if (m_viewport && (m_viewport->is_constraining() || m_viewport->is_constraining_entities()))
                set_ui_mode(UiMode::Constrain);
        };

        feat_dropdown("dressup", "design_dressup", _L("Fillet / chamfer / draft / shell / delete face"), {
            {"design_dressup", _L("Fillet / Chamfer"), _L("Round or bevel a picked edge"),
             [this] { open_tool(Tool::Dressup); }, SHIFT('F')},
            {"design_draft", _L("Draft (taper a face)"), _L("Tilt a picked face by a draft angle"),
             [this] { open_tool(Tool::Draft); }, SHIFT('D')},
            {"design_shell", _L_CONTEXT("Shell", "Design"), _L("Hollow the body to a wall thickness, opening a picked face"),
             [this] { open_tool(Tool::Shell); }, SHIFT('K')},
            {"design_delete", _L("Delete Face"), _L("Remove faces from a body and heal the solid"),
             [this] {
                if (m_doc.bodies.empty()) {
                    set_status(StatusKind::Error, _L("Delete Face needs a body — add or import one first"));
                    return;
                }
                {
                    m_del_face_body->Clear();
                    for (size_t i = 0; i < m_doc.bodies.size(); ++i) {
                        const std::string& n = m_doc.bodies[i].name;
                        m_del_face_body->Append(n.empty() ? wxString::Format(_L("Body %zu"), i + 1) : wxString::FromUTF8(n));
                    }
                    if (m_del_face_body->GetCount() > 0)
                        m_del_face_body->SetSelection(std::min(selected_body_default(),
                                                       int(m_del_face_body->GetCount()) - 1));
                }
                m_del_faces.clear();
                m_del_face_list->SetLabel(_L("(none)"));
                open_tool(Tool::DeleteFace);
             }, 0},
        });

        feat_dropdown("hole", "design_hole", _L("Hole / thread"), {
            {"design_hole", _L("Hole"), _L("Drill a hole, centered on a picked face or placed on a plane"),
             [this] {
                m_hole_on_face   = false;
                m_hole_face_body = -1;
                m_hole_has_bounds = false;
                set_hole_target_label(-1);
                const int hb = m_sel_solid_body, hf = m_sel_solid_face;
                if (hf < 0 || hb < 0 || hb >= int(m_doc.bodies.size())) {
                    set_status(StatusKind::Error, _L("Pick a face or a plane to drill into"));
                    return;
                }
                if (hf >= 0 && hb >= 0 && hb < int(m_doc.bodies.size())) {
                    const TopoDS_Face face = GeometryEngine::face_by_index(
                        m_doc.bodies[hb].shape, hf);
                    if (!face.IsNull()) {
                        m_hole_face_plane = face_plane_inward(face);
                        m_hole_on_face    = true;
                        m_hole_face_body  = hb;
                        set_hole_target_label(hf);
                        m_hole_has_bounds = GeometryEngine::face_plane_bounds(
                            face, m_hole_face_plane.origin, m_hole_face_plane.x_axis,
                            m_hole_face_plane.y_axis, m_hole_umin, m_hole_umax, m_hole_vmin, m_hole_vmax);
                        if (m_hole_x) m_hole_x->SetValue(0.0);
                        if (m_hole_y) m_hole_y->SetValue(0.0);
                        if (m_hole_plane)
                            m_hole_plane->SetSelection(index_from_plane(m_hole_face_plane));
                    }
                }
                open_tool(Tool::Hole);
             }, SHIFT('H')},
            {"design_thread", _L("Thread"), _L("Thread a cylindrical surface (inner bore / outer) or a circular edge"),
             [this] {
                m_thread_on_face   = false;
                m_thread_face_body = -1;
                set_thread_target_label(-1, -1);
                GeometryEngine::CylinderFace cf;
                bool from_face = false;
                if (m_sel_solid_body >= 0 && m_sel_solid_body < int(m_doc.bodies.size())) {
                    const TopoDS_Shape& shape = m_doc.bodies[m_sel_solid_body].shape;
                    if (m_sel_solid_face >= 0)
                        cf = GeometryEngine::cylinder_of_face(GeometryEngine::face_by_index(shape, m_sel_solid_face));
                    from_face = cf.ok;
                    if (!cf.ok && m_sel_solid_edge >= 0)
                        cf = GeometryEngine::circle_of_edge(GeometryEngine::edge_by_index(shape, m_sel_solid_edge));
                }
                if (cf.ok) {
                    SketchPlane p;
                    p.origin = cf.base;
                    p.normal = cf.axis;
                    const Vec3d ref = std::abs(cf.axis.z()) < 0.9 ? Vec3d(0, 0, 1) : Vec3d(1, 0, 0);
                    p.x_axis = ref.cross(cf.axis).normalized();
                    p.y_axis = cf.axis.cross(p.x_axis).normalized();
                    m_thread_face_plane = p;
                    m_thread_on_face    = true;
                    m_thread_face_body  = m_sel_solid_body;
                    set_thread_target_label(from_face ? m_sel_solid_face : -1,
                                            from_face ? -1 : m_sel_solid_edge);
                    if (m_thread_internal) m_thread_internal->SetValue(cf.internal);
                    infer_thread_spec(2.0 * cf.radius, cf.internal);
                    if (m_thread_height && cf.height > 1e-6) m_thread_height->SetValue(cf.height);
                    if (m_thread_x) m_thread_x->SetValue(0.0);
                    if (m_thread_y) m_thread_y->SetValue(0.0);
                } else if (m_sel_solid_face >= 0 || m_sel_solid_edge >= 0) {
                    set_status(StatusKind::Error, _L("Pick a cylindrical surface (bore / outer) or a circular edge for a thread"));
                    return;
                }
                open_tool(Tool::Thread);
             }, SHIFT('T')},
        });
        auto* b_constrain = icon_btn("design_constrain", _L("Constrain selected sketch"));
        b_constrain->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            on_begin_constrain();
            if (m_viewport && (m_viewport->is_constraining() || m_viewport->is_constraining_entities()))
                set_ui_mode(UiMode::Constrain);
        });
        fadd("constrain", b_constrain);
    }

    m_tb_sketch = new wxBoxSizer(wxHORIZONTAL);
    auto sadd     = [](wxWindow* w) { w->Hide(); };
    auto sadd_bar = [this](wxWindow* w) { m_tb_sketch->Add(w, 0, wxALIGN_CENTER_VERTICAL); };
    m_tb_sketch->Add(caption(_L("SKETCH")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    {
        auto skbtn = [&](const char* icon, DesignSketchTool::Mode mode,
                         const wxString& tip, const wxString& hint) {
            auto* b = icon_btn(icon, tip);
            b->Bind(wxEVT_BUTTON, [select_tool, mode, hint](wxCommandEvent&) { select_tool(mode, hint); });
            sadd(b);
        };
        struct SkVar { const char* icon; DesignSketchTool::Mode mode; wxString tip; wxString hint; };
        struct ToolFlyout {
            std::vector<DropDown::Item> items;
            std::vector<DesignSketchTool::Mode> modes;
            std::vector<wxString> hints;
            std::vector<std::string> icon_names;
            ScalableButton* btn = nullptr;
            DropDown drop;
            ToolFlyout() : drop(items) {}
        };
        auto dropdown = [&](const char* def_icon, const wxString& grp, std::vector<SkVar> vars) {
            for (size_t i = 0; i < vars.size(); ++i) {
                const DesignSketchTool::Mode mode = vars[i].mode;
                const wxString               hint = vars[i].hint;
                m_verb_actions["fly:" + std::string(def_icon) + "#" + std::to_string(i)] =
                    [mode, hint, select_tool] { select_tool(mode, hint); };
            }
            if (kBarKeep.count(def_icon) == 0)
                return;
            auto* b = icon_btn(def_icon, grp);
            b->SetFont(Label::Body_14);
            auto fo = std::make_shared<ToolFlyout>();
            for (auto& v : vars) {
                DropDown::Item it;
                it.text = v.tip;
                it.tip  = v.hint;
                it.icon = create_scaled_bitmap(v.icon, m_form, 18);
                fo->items.push_back(it);
                fo->modes.push_back(v.mode);
                fo->hints.push_back(v.hint);
                fo->icon_names.emplace_back(v.icon);
            }
            fo->btn = b;
            fo->drop.Create(b);
            m_icon_refresh.push_back([this, fp = fo.get()] {
                for (size_t i = 0; i < fp->items.size(); ++i)
                    fp->items[i].icon = create_scaled_bitmap(fp->icon_names[i], m_form, 18);
                fp->drop.Invalidate(true);
            });
            fo->drop.SetUseContentWidth(true, false);
            fo->drop.Invalidate(true);
            ToolFlyout* fp = fo.get();
            fo->drop.Bind(wxEVT_COMBOBOX, [this, fp, select_tool](wxCommandEvent& e) {
                int i = e.GetInt();
                if (i >= 0 && i < (int) fp->modes.size()) {
                    fp->btn->SetBitmap_(fp->icon_names[i]);
                    select_tool(fp->modes[i], fp->hints[i]);
                    set_active_tool_btn(fp->btn);
                }
            });
            b->Bind(wxEVT_BUTTON, [b, fp](wxCommandEvent&) {
                fp->drop.Invalidate(true);
                fp->drop.SetUseContentWidth(false, false);
                fp->drop.SetUseContentWidth(true, false);
                wxPoint pos = b->ClientToScreen(wxPoint(0, -6));
                fp->drop.Position(pos, wxSize(0, b->GetSize().y + b->FromDIP(12)));
                fp->drop.Popup();
            });
            m_flyout_keepalive.push_back(fo);
            sadd(b);
            auto* chev = new wxStaticText(m_toolbar, wxID_ANY, wxString::FromUTF8("\xE2\x96\xBE"));
            chev->SetForegroundColour(dp_sec_text());
            chev->SetFont(Label::Body_9);
            sadd(chev);
        };
        skbtn("design_select",    DesignSketchTool::Mode::Select,           _L("Select"),
              _L("Click to select; Shift to add; double-click for a whole loop"));
        skbtn("design_dimension", DesignSketchTool::Mode::Dimension, _L("Dimension"),
              _L("Click 2 points or a line / circle / arc to place a dimension"));
        dropdown("design_line", _L("Line / polyline"), {
            {"design_line",     DesignSketchTool::Mode::Line,     _L("Line"),     _L("Click start, then end — then set the exact length")},
            {"design_polyline", DesignSketchTool::Mode::Polyline, _L("Polyline"), _L("Click points; click the first point to close the loop, right-click or Enter to end the chain")} });
        dropdown("design_rect", _L("Rectangle"), {
            {"design_rect",         DesignSketchTool::Mode::CornerRect,  _L("Corner rectangle"),  _L("Click two opposite corners")},
            {"design_crect",        DesignSketchTool::Mode::CenterRect,  _L("Center rectangle"),  _L("Click center, then a corner")},
            {"design_rect_oblique", DesignSketchTool::Mode::ObliqueRect, _L("Oblique rectangle"), _L("Click two corners of one edge, then a point for the width")},
            {"design_rect_rounded", DesignSketchTool::Mode::RoundedRect, _L("Rounded rectangle"), _L("Click two opposite corners, then a point for the corner radius")} });
        dropdown("design_circle", _L("Circle"), {
            {"design_circle",    DesignSketchTool::Mode::CenterCircle,     _L("Center circle"),  _L("Click center, then radius")},
            {"design_circle2pt", DesignSketchTool::Mode::TwoPointCircle,   _L("2-point circle"), _L("Click two ends of the diameter")},
            {"design_circle3pt", DesignSketchTool::Mode::ThreePointCircle, _L("3-point circle"), _L("Click three points on the circle")} });
        dropdown("design_arc3pt", _L("Arc"), {
            {"design_arc3pt",     DesignSketchTool::Mode::ThreePointArc, _L("3-point arc"),      _L("Click start, end, then a point on the arc")},
            {"design_tangentarc", DesignSketchTool::Mode::TangentArc,    _L("Tangent arc"),      _L("Click start (on the last entity) then end")},
            {"design_arc_center", DesignSketchTool::Mode::CenterArc,     _L("Center-point arc"), _L("Click center, then start, then a point for the end angle")} });
        dropdown("design_slot", _L("Slot"), {
            {"design_slot",     DesignSketchTool::Mode::Slot,    _L("Slot"),     _L("Click two centerline ends, then a point for the width")},
            {"design_slot_arc", DesignSketchTool::Mode::ArcSlot, _L("Arc slot"), _L("Click center, start, end, then a point for the width")} });
        dropdown("design_ellipse", _L("Ellipse"), {
            {"design_ellipse",     DesignSketchTool::Mode::Ellipse,    _L("Ellipse"),        _L("Click center, a major-axis end, then a point for the minor axis")},
            {"design_ellipse_arc", DesignSketchTool::Mode::EllipseArc, _L("Elliptical arc"), _L("Click center, major-axis end, minor point, then arc start and end")} });
        skbtn("design_bspline", DesignSketchTool::Mode::BSpline, _L("Spline"),
              _L("Click control points; double-click, right-click or Enter to finish"));
        skbtn("design_point",   DesignSketchTool::Mode::Point,   _L("Point"),
              _L("Click to place a point"));
        {
            auto ensure_sketch = [this, select_tool] {
                if (m_ui_mode == UiMode::Sketch && m_viewport && !m_viewport->is_sketching())
                    select_tool(DesignSketchTool::Mode::Select,
                                _L("Select — click to select; Shift to add"));
            };
            auto* b_text = icon_btn("design_text", _L("Text — emboss text as a profile"));
            b_text->Bind(wxEVT_BUTTON, [this, ensure_sketch](wxCommandEvent&) {
                ensure_sketch(); on_add_text(); });
            sadd(b_text);
            auto* b_svg = icon_btn("design_svg", _L("SVG — import an outline as a profile"));
            b_svg->Bind(wxEVT_BUTTON, [this, ensure_sketch](wxCommandEvent&) {
                ensure_sketch(); on_import_svg(); });
            sadd(b_svg);
            m_verb_actions["btn:text"] = [this, ensure_sketch] { ensure_sketch(); on_add_text(); };
            m_verb_actions["btn:svg"]  = [this, ensure_sketch] { ensure_sketch(); on_import_svg(); };
        }
        dropdown("design_filletedge", _L("Fillet / chamfer"), {
            {"design_filletedge", DesignSketchTool::Mode::Fillet,  _L("Fillet"),  _L("Pick two lines, then drag the arrow or click the radius to set it")},
            {"design_chamfer",    DesignSketchTool::Mode::Chamfer, _L("Chamfer"), _L("Pick two lines, then drag the arrow or click the distance to set it")} });
        skbtn("design_offset", DesignSketchTool::Mode::Offset, _L("Offset"),
              _L("Pick an entity, then drag the arrow or click the distance; click empty to apply"));
        skbtn("design_mirror", DesignSketchTool::Mode::Mirror, _L("Mirror"),
              _L("Pick a mirror-axis line, then the entities to mirror; click empty to apply"));
        skbtn("design_trim",   DesignSketchTool::Mode::Trim,   _L("Trim"),
              _L("Click a segment to trim it back to its nearest intersection; Esc exits"));
        skbtn("design_extend", DesignSketchTool::Mode::Extend, _L("Extend"),
              _L("Click a line or arc to extend it to the nearest entity; Esc exits"));
        auto* b_constrain_sk = icon_btn("design_constrain",
            _L("Constrain — add geometric/dimensional relations"));
        b_constrain_sk->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { enter_constrain_inline(); });
        sadd(b_constrain_sk);
        dropdown("design_move", _L("Move / rotate / scale"), {
            {"design_move",   DesignSketchTool::Mode::Move,   _L("Move (translate)"),        _L("Pick entities, then drag the handle or click the distance; click empty to apply")},
            {"design_rotate", DesignSketchTool::Mode::Rotate, _L("Rotate (about centroid)"), _L("Pick entities, then drag around the pivot or click the angle; click empty to apply")},
            {"design_scale",  DesignSketchTool::Mode::Scale,  _L("Scale (about centroid)"),  _L("Pick entities, then drag the handle or click the factor; click empty to apply")} });
        dropdown("design_array", _L("Linear / polar array"), {
            {"design_array",      DesignSketchTool::Mode::Array,      _L("Linear array"),                 _L("Pick entities, drag the spacing handle, click the count; click empty to apply")},
            {"design_polararray", DesignSketchTool::Mode::PolarArray, _L("Polar array (about centroid)"), _L("Pick entities, drag the sweep handle, click the count; click empty to apply")} });

        auto arm_polygon = [this, select_tool] {
            push_polygon_params();
            select_tool(DesignSketchTool::Mode::Polygon,
                        wxString::Format(_L("Click center then a vertex — %d sides, %s"),
                                         m_poly_sides,
                                         m_poly_circumscribed ? _L("circumscribed") : _L("inscribed")));
        };
        for (int n : {3, 4, 5, 6, 8, 12})
            m_verb_actions["btn:poly#" + std::to_string(n)] =
                [this, arm_polygon, n] { m_poly_sides = n; arm_polygon(); };
        for (int c : {0, 1})
            m_verb_actions["btn:polyfit#" + std::to_string(c)] =
                [this, arm_polygon, c] { m_poly_circumscribed = (c == 1); arm_polygon(); };

        auto open_feature = [this](int key) {
            auto it = m_keys_feature.find(key);
            if (it != m_keys_feature.end() && it->second) it->second();
        };
        const int k_dress = SHIFT('F'), k_bool = SHIFT('B'), k_pat = SHIFT('N');
        m_verb_actions["btn:dress#0"] = [this, open_feature] {
            if (m_dressup_type) m_dressup_type->SetSelection(0); open_feature(k_dress); };
        m_verb_actions["btn:dress#1"] = [this, open_feature] {
            if (m_dressup_type) m_dressup_type->SetSelection(1); open_feature(k_dress); };
        for (int op = 0; op < 3; ++op)
            m_verb_actions["btn:bool#" + std::to_string(op)] = [this, open_feature, op] {
                if (m_bool_op) m_bool_op->SetSelection(op);
                open_feature(k_bool); };
        for (int t = 0; t < 2; ++t)
            m_verb_actions["btn:pat#" + std::to_string(t)] = [this, open_feature, t] {
                if (m_pattern_type) m_pattern_type->SetSelection(t);
                open_feature(k_pat); };

        auto* b_poly = icon_btn("design_polygon", _L("Polygon"));
        b_poly->Bind(wxEVT_BUTTON, [arm_polygon](wxCommandEvent&) { arm_polygon(); });
        sadd(b_poly);
        m_construction->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
            if (!m_viewport || !m_viewport->is_sketching())
                return;
            const int n = m_viewport->sketch_is_selecting()
                        ? m_viewport->toggle_sketch_construction_selection() : 0;
            if (n > 0) {
                m_construction->SetValue(!m_construction->GetValue());
                set_status(StatusKind::Info, wxString::Format(
                    _L_PLURAL("Converted %d entity between construction and real geometry",
                              "Converted %d entities between construction and real geometry", n),
                    n));
                return;
            }
            m_viewport->set_sketch_construction(m_construction->GetValue()); });
        sadd_bar(m_construction);
        add_sep(m_tb_sketch);
        auto* b_del = icon_btn("design_delete", _L("Delete selected"));
        b_del->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (m_viewport) m_viewport->delete_selected_sketch_entities(); });
        sadd_bar(b_del);
    }

    m_tb_relations = new wxBoxSizer(wxHORIZONTAL);
    auto cadd = [this](wxWindow* w) { m_tb_relations->Add(w, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2); };
    m_tb_relations->Add(caption(_L("CONSTRAIN")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    {
        auto cbtn = [&](const char* icon, const wxString& tip, SketchConstraintType type) {
            auto* b = icon_btn(icon, tip);
            b->Bind(wxEVT_BUTTON, [this, type](wxCommandEvent&) { apply_constraint(type); });
            cadd(b);
        };
        cbtn("design_c_horizontal",    _L_CONTEXT("Horizontal", "Sketch Constraint"),    SketchConstraintType::Horizontal);
        cbtn("design_c_vertical",      _L_CONTEXT("Vertical", "Sketch Constraint"),      SketchConstraintType::Vertical);
        cbtn("design_c_parallel",      _L_CONTEXT("Parallel", "Sketch Constraint"),      SketchConstraintType::Parallel);
        cbtn("design_c_perpendicular", _L_CONTEXT("Perpendicular", "Sketch Constraint"), SketchConstraintType::Perpendicular);
        cbtn("design_c_coincident",    _L_CONTEXT("Coincident", "Sketch Constraint"),    SketchConstraintType::Coincident);
        cbtn("design_c_equal",         _L_CONTEXT("Equal length", "Sketch Constraint"),  SketchConstraintType::EqualLength);
        cbtn("design_c_equal_radius", _L_CONTEXT("Equal radius", "Sketch Constraint"), SketchConstraintType::EqualRadius);
        cbtn("design_c_collinear",    _L_CONTEXT("Collinear", "Sketch Constraint"),    SketchConstraintType::Collinear);
        cbtn("design_c_concentric",    _L_CONTEXT("Concentric", "Sketch Constraint"),    SketchConstraintType::Concentric);
        cbtn("design_c_tangent",       _L_CONTEXT("Tangent", "Sketch Constraint"),       SketchConstraintType::Tangent);
        cbtn("design_c_midpoint",      _L_CONTEXT("Midpoint", "Sketch Constraint"),      SketchConstraintType::Midpoint);
        cbtn("design_c_symmetric",     _L_CONTEXT("Symmetric", "Sketch Constraint"),     SketchConstraintType::Symmetric);
        cbtn("design_c_sym_v", _L_CONTEXT("Symmetric about the vertical axis", "Sketch Constraint"),   SketchConstraintType::SymmetricAboutY);
        cbtn("design_c_sym_h", _L_CONTEXT("Symmetric about the horizontal axis", "Sketch Constraint"), SketchConstraintType::SymmetricAboutX);
        cbtn("design_c_angle",         _L_CONTEXT("Angle", "Sketch Constraint"),         SketchConstraintType::Angle);
        cbtn("design_c_radius",        _L_CONTEXT("Radius", "Sketch Constraint"),        SketchConstraintType::Radius);
        cbtn("design_c_diameter",      _L_CONTEXT("Diameter", "Sketch Constraint"),      SketchConstraintType::Diameter);
        cbtn("design_c_fix",           _L_CONTEXT("Fix point (anchor in place)", "Sketch Constraint"), SketchConstraintType::Fix);
        cbtn("design_c_dist_x", _L_CONTEXT("Horizontal distance", "Sketch Constraint"), SketchConstraintType::DistanceX);
        cbtn("design_c_dist_y", _L_CONTEXT("Vertical distance", "Sketch Constraint"),   SketchConstraintType::DistanceY);
    }

    m_tb_action = new wxBoxSizer(wxHORIZONTAL);
    {
        auto* ok = new ::Button(m_toolbar, _L("Confirm"));
        ok->SetStyle(ButtonStyle::Confirm, ButtonType::Window);
        ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { tool_confirm(); });
        m_confirm_btns.push_back(ok);
        auto* no = new ::Button(m_toolbar, _L("Cancel"));
        no->SetStyle(ButtonStyle::Regular, ButtonType::Window);
        no->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { tool_cancel(); });
        m_tb_action->Add(ok, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        m_tb_action->Add(no, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    }

    m_tb_doc = new wxBoxSizer(wxHORIZONTAL);
    {
        auto doc_btn = [this](const char* icon, const wxString& tip, bool has_dark_twin = false,
                              int cell_w = 40, int icon_px = 34) {
            const std::string name(icon);
            auto themed = [name, has_dark_twin] { return has_dark_twin && dp_dark() ? name + "_dark" : name; };
            auto* b = new ScalableButton(m_toolbar, wxID_ANY, themed(), "", FromDIP(wxSize(cell_w, 40)),
                                         wxDefaultPosition, wxBU_EXACTFIT | wxBORDER_NONE, false, icon_px);
            if (has_dark_twin)
                m_icon_refresh.push_back([b, themed] { b->SetBitmap_(themed()); });
            b->SetToolTip(tip);
            b->SetBackgroundColour(dp_ribbon_bg());
            b->Bind(wxEVT_ENTER_WINDOW, [b](wxMouseEvent& e) {
                if (b->IsEnabled()) { b->SetBackgroundColour(dp_ribbon_hover()); b->Refresh(); } e.Skip(); });
            b->Bind(wxEVT_LEAVE_WINDOW, [b](wxMouseEvent& e) {
                b->SetBackgroundColour(dp_ribbon_bg()); b->Refresh(); e.Skip(); });
            return b;
        };
        auto add_doc = [this](ScalableButton* b) {
            m_tb_doc->Add(b, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4); };

        auto* b_new = doc_btn("design_new", _L("New Design — clear the feature tree"));
        b_new->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_new_design(); });
        add_doc(b_new);
        auto* b_step = doc_btn("design_step", _L("Import STEP (editable B-rep solid)"));
        b_step->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_import_step(); });
        m_keys_feature[SHIFT('I')] = [this] { on_import_step(); };
        add_doc(b_step);
        auto* b_mesh = doc_btn("design_import_mesh", _L("Import mesh (STL/OBJ) as an editable B-rep solid"));
        b_mesh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_import_mesh(); });
        m_keys_feature[SHIFT('M')] = [this] { on_import_mesh(); };
        add_doc(b_mesh);
        auto* b_export = doc_btn("design_export", _L("Export STEP…"));
        b_export->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_export_step(); });
        add_doc(b_export);

        auto* b_sketch = doc_btn("design_new_sketch", _L("Create a new sketch"));
        b_sketch->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { start_sketch(); });
        tb_slot["sketch"].push_back(b_sketch);

        auto* b_place = doc_btn("toolbar_flatten", _L("Place on Face (F) — lay the picked face on the bed"),
                                true);
        b_place->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { place_on_face(); });
        tb_slot["place"].push_back(b_place);

        auto* b_section = doc_btn("split_parts", _L("Section View — hide part of the model to see inside. "
                                                   "PageUp/PageDown move the plane; Delete removes it."),
                                  true);
        b_section->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { toggle_section_view(); });
        tb_slot["section"].push_back(b_section);

        m_section_flip_btn = doc_btn("design_mirror", _L("Flip Section — show the opposite half"));
        m_section_flip_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { flip_section_view(); });
        m_section_flip_btn->Enable(false);
        tb_slot["flip"].push_back(m_section_flip_btn);

        m_tb_commit = new wxBoxSizer(wxHORIZONTAL);
        m_commit_btn = doc_btn("toolbar_add_plate", wxEmptyString);
        m_commit_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_commit(); });
        auto* b_mode = doc_btn("drop_down", _L("Choose how Commit to Plate sends the bodies"), false, 16, 16);
        m_tb_commit->Add(m_commit_btn, 0, wxALIGN_CENTER_VERTICAL);
        m_tb_commit->Add(b_mode, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

        struct CommitFlyout {
            std::vector<DropDown::Item> items;
            wxLongLong closed_ms;
            DropDown drop;
            CommitFlyout() : drop(items) {}
        };
        auto cf = std::make_shared<CommitFlyout>();
        cf->items.resize(2);
        cf->items[int(CommitMode::Assembly)].text = _L("Commit to Plate");
        cf->items[int(CommitMode::Assembly)].tip  = _L("All bodies become one object with a part per body, "
                                                       "keeping their relative positions");
        cf->items[int(CommitMode::Bodies)].text   = _L("Commit to Plate (as bodies)");
        cf->items[int(CommitMode::Bodies)].tip    = _L("Each body becomes its own object, placed on its own");
        CommitFlyout* cp = cf.get();
        auto refresh_rows = [this, cp] {
            for (size_t i = 0; i < cp->items.size(); ++i)
                cp->items[i].icon = create_scaled_bitmap(commit_icon(i == size_t(CommitMode::Bodies)), m_toolbar, 18);
        };
        refresh_rows();
        cp->drop.Create(m_commit_btn);
        cp->drop.SetUseContentWidth(true, false);
        cp->drop.Invalidate(true);
        m_commit_drop = &cp->drop;
        cp->drop.Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& e) {
            const bool bodies = e.GetInt() == int(CommitMode::Bodies);
            set_commit_mode(bodies ? CommitMode::Bodies : CommitMode::Assembly);
            wxGetApp().app_config->set("design_commit_mode", bodies ? "bodies" : "assembly");
        });
        cp->drop.Bind(EVT_DISMISS, [cp](wxCommandEvent&) { cp->closed_ms = wxGetLocalTimeMillis(); });
        b_mode->Bind(wxEVT_BUTTON, [this, b_mode, cp](wxCommandEvent&) {
            if (cp->drop.IsShown() || wxGetLocalTimeMillis() - cp->closed_ms < 300)
                return;
            m_commit_drop->Invalidate(true);
            m_commit_drop->SetUseContentWidth(false, false);
            m_commit_drop->SetUseContentWidth(true, false);
            m_commit_drop->SetSelection(int(m_commit_mode));
            wxPoint pos = b_mode->ClientToScreen(wxPoint(b_mode->GetSize().x, -6));
            pos.x -= m_commit_drop->GetSize().x;
            m_commit_drop->Position(pos, wxSize(0, b_mode->GetSize().y + b_mode->FromDIP(12)));
            m_commit_drop->Popup();
        });
        m_icon_refresh.push_back([this, refresh_rows] {
            refresh_rows();
            m_commit_drop->Invalidate(true);
            set_commit_mode(m_commit_mode);
        });
        m_flyout_keepalive.push_back(cf);
        set_commit_mode(wxGetApp().app_config->get("design_commit_mode") == "bodies" ? CommitMode::Bodies
                                                                                     : CommitMode::Assembly);
    }

    {
        auto put = [&](const char* id) {
            auto it = tb_slot.find(id);
            if (it == tb_slot.end()) return;
            for (size_t i = 0; i < it->second.size(); ++i)
                m_tb_feature->Add(it->second[i], 0,
                                  i == 0 ? (wxALIGN_CENTER_VERTICAL | wxRIGHT)
                                         : (wxALIGN_BOTTOM | wxBOTTOM | wxRIGHT),
                                  i == 0 ? 4 : 5);
        };
        put("sketch");    put("constrain");
        add_sep(m_tb_feature);
        put("material");  put("place");   put("placement"); put("plane");
        put("dressup");   put("section");
        put("hole");      put("boolean"); put("cut");
        put("surface");   put("color");   put("flip");
        put("pattern");
    }

    auto* tbrow = new wxBoxSizer(wxHORIZONTAL);
    tbrow->AddSpacer(8);
    tbrow->Add(m_tb_doc,       0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 5);
    add_sep(tbrow);
    tbrow->Add(m_tb_feature,   0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 5);
    tbrow->Add(m_tb_sketch,    0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 5);
    tbrow->Add(m_tb_relations, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 5);
    tbrow->AddStretchSpacer();
    tbrow->Add(m_tb_commit,    0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 5);
    tbrow->Add(m_tb_action,    0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 5);
    tbrow->AddSpacer(8);
    m_toolbar->SetSizer(tbrow);
    build_phase("toolbar");
    update_body_gates();

    auto card_header = [this](wxWindow* card, const char* icon, const wxString& title, wxStaticText*& out) -> wxSizer* {
        auto* h  = new wxBoxSizer(wxHORIZONTAL);
        auto* ic = new wxStaticBitmap(card, wxID_ANY, create_scaled_bitmap(icon, card, 18));
        m_icon_refresh.push_back([ic, card, name = std::string(icon)] {
            ic->SetBitmap(create_scaled_bitmap(name, card, 18)); });
        out = new wxStaticText(card, wxID_ANY, title);
        out->SetFont(Label::Head_14);
        h->Add(ic,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        h->Add(out, 0, wxALIGN_CENTER_VERTICAL);
        return h;
    };

    m_cards = make_card(m_form);
    auto* cards = new wxBoxSizer(wxVERTICAL);
    m_cards->SetSizer(cards);
    root->Add(m_cards, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    auto* form = two_col_form();

    m_shape = make_combo(m_cards);
    m_shape->Append(_L("Rectangle"));
    m_shape->Append(_L("Circle"));
    m_shape->SetSelection(0);
    form->Add(new wxStaticText(m_cards, wxID_ANY, _L("Shape")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_shape, 0, wxEXPAND);

    m_width = make_spin(m_cards, 20);
    form->Add(new wxStaticText(m_cards, wxID_ANY, _L("Width / X")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(spin_frame(m_width), 0, wxEXPAND);

    m_height = make_spin(m_cards, 20);
    form->Add(new wxStaticText(m_cards, wxID_ANY, _L("Height / Y")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(spin_frame(m_height), 0, wxEXPAND);

    m_radius = make_spin(m_cards, 10);
    form->Add(new wxStaticText(m_cards, wxID_ANY, _L("Radius")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(spin_frame(m_radius), 0, wxEXPAND);

    m_box_sketch = new wxBoxSizer(wxVERTICAL);
    m_box_sketch->Add(card_header(m_cards, "design_sketch", _L("Sketch"), m_hdr_sketch), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_sketch->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    m_box_sketch->Add(form, 0, wxEXPAND | wxALL, 12);
    cards->Add(m_box_sketch, 0, wxEXPAND);

    {
        auto* mform = two_col_form();
        m_move_dx = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        mform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Distance X")), 0, wxALIGN_CENTER_VERTICAL);
        mform->Add(spin_frame(m_move_dx), 0, wxEXPAND);
        m_move_dy = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        mform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Distance Y")), 0, wxALIGN_CENTER_VERTICAL);
        mform->Add(spin_frame(m_move_dy), 0, wxEXPAND);
        m_move_dz = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        mform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Distance Z")), 0, wxALIGN_CENTER_VERTICAL);
        mform->Add(spin_frame(m_move_dz), 0, wxEXPAND);

        m_move_axis = make_combo(m_cards);
        m_move_axis->Append(_L_CONTEXT("X", "Axis"));
        m_move_axis->Append(_L_CONTEXT("Y", "Axis"));
        m_move_axis->Append(_L_CONTEXT("Z", "Axis"));
        m_move_axis->SetSelection(2);
        mform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Rotation axis")), 0, wxALIGN_CENTER_VERTICAL);
        mform->Add(m_move_axis, 0, wxEXPAND);

        m_move_angle = make_spin(m_cards, 0.0, -360.0, 360.0);
        mform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Angle (°)")), 0, wxALIGN_CENTER_VERTICAL);
        mform->Add(spin_frame(m_move_angle), 0, wxEXPAND);

        for (wxSpinCtrlDouble* sp : { m_move_dx, m_move_dy, m_move_dz, m_move_angle })
            sp->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent& e) { apply_move_card(); e.Skip(); });
        m_move_axis->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& e) { apply_move_card(); e.Skip(); });

        m_box_move = new wxBoxSizer(wxVERTICAL);
        m_box_move->Add(card_header(m_cards, "design_move", _L("Move / Rotate"), m_hdr_move), 0,
                        wxLEFT | wxRIGHT | wxTOP, 12);
        m_box_move->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
        m_box_move->Add(mform, 0, wxEXPAND | wxALL, 12);
        cards->Add(m_box_move, 0, wxEXPAND);
    }

    {
    }

    m_box_extrude = new wxBoxSizer(wxVERTICAL);
    m_box_extrude->Add(card_header(m_cards, "design_extrude", _L_CONTEXT("Extrude", "Design"), m_hdr_extrude), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_extrude->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    m_extrude_sketch_label = new wxStaticText(m_cards, wxID_ANY, _L("Sketch: —"));
    m_box_extrude->Add(m_extrude_sketch_label, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    {
        auto* eform = two_col_form();

        m_distance = make_spin(m_cards, 10, -1000.0, 1000.0);
        eform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Extrude dist")), 0, wxALIGN_CENTER_VERTICAL);
        eform->Add(spin_frame(m_distance), 0, wxEXPAND);

        m_extrude_end = make_combo(m_cards);
        for (const char* s : { L("Blind"), L("Symmetric"), L("Two-sided"), L("Through all"),
                               L("Up to face"), L("Up to vertex") })
            m_extrude_end->Append(_L(s));
        m_extrude_end->SetSelection(0);
        eform->Add(new wxStaticText(m_cards, wxID_ANY, _L("End")), 0, wxALIGN_CENTER_VERTICAL);
        eform->Add(m_extrude_end, 0, wxEXPAND);

        m_distance2 = make_spin(m_cards, 5, 0.0, 100000.0);
        eform->Add(new wxStaticText(m_cards, wxID_ANY, _L("2nd dist")), 0, wxALIGN_CENTER_VERTICAL);
        eform->Add(spin_frame(m_distance2), 0, wxEXPAND);

        m_taper = make_spin(m_cards, 0.0, -89.0, 89.0);
        eform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Taper (°)")), 0, wxALIGN_CENTER_VERTICAL);
        eform->Add(spin_frame(m_taper), 0, wxEXPAND);

        m_mode = make_combo(m_cards);
        m_mode->Append(_L("New body"));
        m_mode->Append(_L_CONTEXT("Join", "Boolean Mode"));
        m_mode->Append(_L_CONTEXT("Cut", "Boolean Mode"));
        m_mode->Append(_L("Intersect"));
        m_mode->SetSelection(0);
        eform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Result")), 0, wxALIGN_CENTER_VERTICAL);
        eform->Add(m_mode, 0, wxEXPAND);

        m_flip = new CheckBox(m_cards);
        eform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Flip direction")), 0, wxALIGN_CENTER_VERTICAL);
        eform->Add(m_flip, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);

        m_box_extrude->Add(eform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    }
    cards->Add(m_box_extrude, 0, wxEXPAND);

    auto* dform = two_col_form();

    m_dressup_type = make_combo(m_cards);
    m_dressup_type->Append(_L("Fillet"));
    m_dressup_type->Append(_L("Chamfer"));
    m_dressup_type->SetSelection(0);
    dform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Type")), 0, wxALIGN_CENTER_VERTICAL);
    dform->Add(m_dressup_type, 0, wxEXPAND);

    m_dressup_edge_label = new wxStaticText(m_cards, wxID_ANY, _L("(no edge picked — group below)"));
    dform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Target")), 0, wxALIGN_CENTER_VERTICAL);
    dform->Add(m_dressup_edge_label, 0, wxALIGN_CENTER_VERTICAL);

    m_face_group = make_combo(m_cards);
    m_face_group->Append(_L("Top"));
    m_face_group->Append(_L("Bottom"));
    // TRN Fillet/Chamfer edge group used when no edge is picked: the side edges, as opposed to the top and bottom ones.
    m_face_group->Append(_L("Lateral"));
    m_face_group->Append(_L("All"));
    m_face_group->SetSelection(3);
    dform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Edges")), 0, wxALIGN_CENTER_VERTICAL);
    dform->Add(m_face_group, 0, wxEXPAND);

    m_dressup_size = make_spin(m_cards, 2.0);
    dform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Size (r/dist)")), 0, wxALIGN_CENTER_VERTICAL);
    dform->Add(spin_frame(m_dressup_size), 0, wxEXPAND);

    m_box_dressup = new wxBoxSizer(wxVERTICAL);
    m_box_dressup->Add(card_header(m_cards, "design_dressup", _L("Fillet / Chamfer"), m_hdr_dressup), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_dressup->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    m_box_dressup->Add(dform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    cards->Add(m_box_dressup, 0, wxEXPAND);

    auto* hform = two_col_form();

    m_hole_plane = make_combo(m_cards);
    m_hole_plane->Append(_L_CONTEXT("XY", "Axis"));
    m_hole_plane->Append(_L_CONTEXT("XZ", "Axis"));
    m_hole_plane->Append(_L_CONTEXT("YZ", "Axis"));
    m_hole_plane->SetSelection(0);
    m_hole_plane->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& e) {
        m_hole_on_face    = false;
        m_hole_has_bounds = false;
        set_hole_target_label(-1);
        update_hole_gizmo();
        refresh_preview();
        e.Skip();
    });
    hform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Hole plane")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(m_hole_plane, 0, wxEXPAND);

    m_hole_diameter = make_spin(m_cards, 6.0);
    hform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Diameter")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(spin_frame(m_hole_diameter), 0, wxEXPAND);

    m_hole_depth = make_spin(m_cards, 10.0);
    hform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Depth (blind)")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(spin_frame(m_hole_depth), 0, wxEXPAND);

    m_hole_x = make_spin(m_cards, 0.0, -1000.0, 1000.0);
    hform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Pos X")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(spin_frame(m_hole_x), 0, wxEXPAND);

    m_hole_y = make_spin(m_cards, 0.0, -1000.0, 1000.0);
    hform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Pos Y")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(spin_frame(m_hole_y), 0, wxEXPAND);

    m_hole_through = new CheckBox(m_cards);
    m_hole_through->SetValue(true);
    hform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Through")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(m_hole_through, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);

    m_hole_target_label = new wxStaticText(m_cards, wxID_ANY, _L("(none — uses Hole plane)"));
    hform->Add(new wxStaticText(m_cards, wxID_ANY, _L("On face")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(m_hole_target_label, 0, wxALIGN_CENTER_VERTICAL);

    m_box_hole = new wxBoxSizer(wxVERTICAL);
    m_box_hole->Add(card_header(m_cards, "design_hole", _L("Hole"), m_hdr_hole), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_hole->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    m_box_hole->Add(hform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    cards->Add(m_box_hole, 0, wxEXPAND);

    auto* tform = two_col_form();

    m_thread_plane = make_combo(m_cards);
    m_thread_plane->Append(_L_CONTEXT("XY", "Axis"));
    m_thread_plane->Append(_L_CONTEXT("XZ", "Axis"));
    m_thread_plane->Append(_L_CONTEXT("YZ", "Axis"));
    m_thread_plane->SetSelection(0);
    tform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Thread plane")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_plane, 0, wxEXPAND);

    m_thread_std = make_combo(m_cards);
    m_thread_std->Append(_L("Custom"));
    for (const ThreadSpec& s : thread_standards())
        m_thread_std->Append(s.name);
    m_thread_std->SetSelection(0);
    m_thread_std->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { apply_thread_standard(); });
    tform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Standard")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_std, 0, wxEXPAND);

    m_thread_radius = make_spin(m_cards, 10.0);
    tform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Diameter")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(spin_frame(m_thread_radius), 0, wxEXPAND);

    m_thread_pitch = make_spin(m_cards, 2.0);
    tform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Pitch")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(spin_frame(m_thread_pitch), 0, wxEXPAND);

    m_thread_height = make_spin(m_cards, 10.0);
    tform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Length")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(spin_frame(m_thread_height), 0, wxEXPAND);

    m_thread_depth = make_spin(m_cards, 1.0);
    tform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Thread depth")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(spin_frame(m_thread_depth), 0, wxEXPAND);

    m_thread_x = make_spin(m_cards, 0.0, -1000.0, 1000.0);
    tform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Pos X")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(spin_frame(m_thread_x), 0, wxEXPAND);

    m_thread_y = make_spin(m_cards, 0.0, -1000.0, 1000.0);
    tform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Pos Y")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(spin_frame(m_thread_y), 0, wxEXPAND);

    m_thread_internal = new CheckBox(m_cards);
    m_thread_internal->SetValue(false);
    m_thread_internal->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& e) {
        apply_thread_standard();
        e.Skip();
    });
    tform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Internal")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_internal, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);

    m_thread_target_label = new wxStaticText(m_cards, wxID_ANY, _L("(none — uses Thread plane)"));
    tform->Add(new wxStaticText(m_cards, wxID_ANY, _L("On face")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_target_label, 0, wxALIGN_CENTER_VERTICAL);

    m_box_thread = new wxBoxSizer(wxVERTICAL);
    m_box_thread->Add(card_header(m_cards, "design_thread", _L("Thread"), m_hdr_thread), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_thread->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    m_box_thread->Add(tform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    cards->Add(m_box_thread, 0, wxEXPAND);

    m_box_revolve = new wxBoxSizer(wxVERTICAL);
    m_box_revolve->Add(card_header(m_cards, "design_revolve", _L("Revolve"), m_hdr_revolve), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_revolve->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    m_revolve_sketch_label = new wxStaticText(m_cards, wxID_ANY, _L("Sketch: —"));
    m_box_revolve->Add(m_revolve_sketch_label, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    {
        auto* rform = two_col_form();

        m_revolve_angle = make_spin(m_cards, 360.0, 1.0, 360.0);
        rform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Angle (°)")), 0, wxALIGN_CENTER_VERTICAL);
        rform->Add(spin_frame(m_revolve_angle), 0, wxEXPAND);

        m_revolve_axis = make_combo(m_cards);
        m_revolve_axis->Append(_L("Plane X"));
        m_revolve_axis->Append(_L("Plane Y"));
        m_revolve_axis->SetSelection(0);
        rform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Axis")), 0, wxALIGN_CENTER_VERTICAL);
        rform->Add(m_revolve_axis, 0, wxEXPAND);

        m_revolve_mode = make_combo(m_cards);
        m_revolve_mode->Append(_L("New body"));
        m_revolve_mode->Append(_L_CONTEXT("Join", "Boolean Mode"));
        m_revolve_mode->Append(_L_CONTEXT("Cut", "Boolean Mode"));
        m_revolve_mode->Append(_L("Intersect"));
        m_revolve_mode->SetSelection(0);
        rform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Result")), 0, wxALIGN_CENTER_VERTICAL);
        rform->Add(m_revolve_mode, 0, wxEXPAND);

        m_revolve_flip = new CheckBox(m_cards);
        rform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Flip direction")), 0, wxALIGN_CENTER_VERTICAL);
        rform->Add(m_revolve_flip, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);

        m_box_revolve->Add(rform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    }
    cards->Add(m_box_revolve, 0, wxEXPAND);

    m_box_sweep = new wxBoxSizer(wxVERTICAL);
    m_box_sweep->Add(card_header(m_cards, "design_sweep", _L("Sweep"), m_hdr_sweep), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_sweep->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    m_sweep_profile_label = new wxStaticText(m_cards, wxID_ANY, _L("Profile: —"));
    m_box_sweep->Add(m_sweep_profile_label, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    {
        auto* sform = two_col_form();

        m_sweep_path = make_combo(m_cards);
        sform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Path")), 0, wxALIGN_CENTER_VERTICAL);
        sform->Add(m_sweep_path, 0, wxEXPAND);

        m_sweep_mode = make_combo(m_cards);
        m_sweep_mode->Append(_L("New body"));
        m_sweep_mode->Append(_L_CONTEXT("Join", "Boolean Mode"));
        m_sweep_mode->Append(_L_CONTEXT("Cut", "Boolean Mode"));
        m_sweep_mode->Append(_L("Intersect"));
        m_sweep_mode->SetSelection(0);
        sform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Result")), 0, wxALIGN_CENTER_VERTICAL);
        sform->Add(m_sweep_mode, 0, wxEXPAND);

        m_box_sweep->Add(sform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    }
    cards->Add(m_box_sweep, 0, wxEXPAND);

    m_box_pattern = new wxBoxSizer(wxVERTICAL);
    m_box_pattern->Add(card_header(m_cards, "design_pattern", _L_CONTEXT("Pattern", "Design"), m_hdr_pattern), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_pattern->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        auto* pform = two_col_form();

        m_pattern_type = make_combo(m_cards);
        m_pattern_type->Append(_L_CONTEXT("Linear", "Design"));
        m_pattern_type->Append(_L_CONTEXT("Circular", "Design"));
        m_pattern_type->SetSelection(0);
        pform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Type")), 0, wxALIGN_CENTER_VERTICAL);
        pform->Add(m_pattern_type, 0, wxEXPAND);

        m_pattern_count = make_spin(m_cards, 3, 1, 999);
        pform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Count")), 0, wxALIGN_CENTER_VERTICAL);
        pform->Add(spin_frame(m_pattern_count), 0, wxEXPAND);

        m_pattern_spacing = make_spin(m_cards, 20.0, 0.01, 100000.0);
        pform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Spacing")), 0, wxALIGN_CENTER_VERTICAL);
        pform->Add(spin_frame(m_pattern_spacing), 0, wxEXPAND);

        m_pattern_dir = make_combo(m_cards);
        m_pattern_dir->Append(_L("Plane X"));
        m_pattern_dir->Append(_L("Plane Y"));
        m_pattern_dir->SetSelection(0);
        pform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Direction")), 0, wxALIGN_CENTER_VERTICAL);
        pform->Add(m_pattern_dir, 0, wxEXPAND);

        m_pattern_angle = make_spin(m_cards, 360.0, 1.0, 360.0);
        pform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Total angle (°)")), 0, wxALIGN_CENTER_VERTICAL);
        pform->Add(spin_frame(m_pattern_angle), 0, wxEXPAND);

        m_box_pattern->Add(pform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    }
    cards->Add(m_box_pattern, 0, wxEXPAND);

    m_box_boolean = new wxBoxSizer(wxVERTICAL);
    m_box_boolean->Add(card_header(m_cards, "design_boolean", _L("Boolean"), m_hdr_boolean), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_boolean->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        auto* bform = two_col_form();

        m_bool_op = make_combo(m_cards);
        m_bool_op->Append(_L_CONTEXT("Join", "Boolean Mode"));
        m_bool_op->Append(_L("Subtract"));
        m_bool_op->Append(_L("Intersect"));
        m_bool_op->SetSelection(0);
        m_bool_op->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        bform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Operation")), 0, wxALIGN_CENTER_VERTICAL);
        bform->Add(m_bool_op, 0, wxEXPAND);

        m_bool_target = make_combo(m_cards);
        m_bool_target->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        bform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Target (kept)")), 0, wxALIGN_CENTER_VERTICAL);
        bform->Add(m_bool_target, 0, wxEXPAND);

        m_bool_tool = make_combo(m_cards);
        m_bool_tool->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        bform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Tool")), 0, wxALIGN_CENTER_VERTICAL);
        bform->Add(m_bool_tool, 0, wxEXPAND);

        m_bool_tol = make_spin(m_cards, 0.0, 0.0, 100.0);
        m_bool_tol->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        bform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Tolerance")), 0, wxALIGN_CENTER_VERTICAL);
        bform->Add(spin_frame(m_bool_tol), 0, wxEXPAND);

        m_box_boolean->Add(bform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

        m_bool_keep = new CheckBox(m_cards);
        m_bool_keep->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { refresh_preview(); });
        auto* keeprow = new wxBoxSizer(wxHORIZONTAL);
        keeprow->Add(new wxStaticText(m_cards, wxID_ANY, _L("Keep tool body")), 0, wxALIGN_CENTER_VERTICAL);
        keeprow->AddStretchSpacer();
        keeprow->Add(m_bool_keep, 0, wxALIGN_CENTER_VERTICAL);
        m_box_boolean->Add(keeprow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    }
    cards->Add(m_box_boolean, 0, wxEXPAND);

    m_box_cut = new wxBoxSizer(wxVERTICAL);
    m_box_cut->Add(card_header(m_cards, "design_cut", _L_CONTEXT("Cut", "Design"), m_hdr_cut), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_cut->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        auto* cform = two_col_form();

        m_cut_target = make_combo(m_cards);
        m_cut_target->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        cform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Body")), 0, wxALIGN_CENTER_VERTICAL);
        cform->Add(m_cut_target, 0, wxEXPAND);

        m_cut_plane = make_combo(m_cards);
        m_cut_plane->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        cform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Plane")), 0, wxALIGN_CENTER_VERTICAL);
        cform->Add(m_cut_plane, 0, wxEXPAND);

        m_cut_offset = make_spin(m_cards, 0.0, -10000.0, 10000.0);
        m_cut_offset->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        cform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Offset")), 0, wxALIGN_CENTER_VERTICAL);
        cform->Add(spin_frame(m_cut_offset), 0, wxEXPAND);

        m_box_cut->Add(cform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    }
    cards->Add(m_box_cut, 0, wxEXPAND);

    m_box_insert = new wxBoxSizer(wxVERTICAL);
    m_box_insert->Add(card_header(m_cards, "design_text", _L("Insert"), m_hdr_insert), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_insert->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    m_box_insert->Add(new wxStaticText(m_cards, wxID_ANY,
        _L("Drag a corner to size, the center to move.\nConfirm or Cancel in the toolbar above.")),
        0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
    cards->Add(m_box_insert, 0, wxEXPAND);

    m_box_plane = new wxBoxSizer(wxVERTICAL);
    m_box_plane->Add(card_header(m_cards, "design_plane", _L("Plane"), m_hdr_plane), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_plane->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        m_plane_type = make_combo(m_cards);
        for (const wxString& t : { _L("Offset"), _L("Angle"), _L("Midplane"),
                                   _L("Tangent"), _L("Two edges"), _L("Coincident") })
            m_plane_type->Append(t);
        m_plane_type->SetSelection(0);
        m_box_plane->Add(new wxStaticText(m_cards, wxID_ANY, _L("Plane type")), 0, wxLEFT | wxRIGHT | wxTOP, 12);
        m_box_plane->Add(m_plane_type, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

        auto* plform = two_col_form();

        m_plane_base = make_combo(m_cards);
        populate_plane_choices(m_plane_base);
        // TRN Plane tool: the reference plane (XY, XZ, YZ or a datum) the new plane is built from.
        plform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Base")), 0, wxALIGN_CENTER_VERTICAL);
        plform->Add(m_plane_base, 0, wxEXPAND);

        m_plane_offset = make_spin(m_cards, 20.0, -100000.0, 100000.0);
        plform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Offset")), 0, wxALIGN_CENTER_VERTICAL);
        plform->Add(spin_frame(m_plane_offset), 0, wxEXPAND);

        m_plane_tilt = make_spin(m_cards, 0.0, -180.0, 180.0);
        plform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Angle (°)")), 0, wxALIGN_CENTER_VERTICAL);
        plform->Add(spin_frame(m_plane_tilt), 0, wxEXPAND);

        m_plane_tilt_axis = make_combo(m_cards);
        m_plane_tilt_axis->Append(_L("Base X"));
        m_plane_tilt_axis->Append(_L("Base Y"));
        m_plane_tilt_axis->SetSelection(0);
        plform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Tilt axis")), 0, wxALIGN_CENTER_VERTICAL);
        plform->Add(m_plane_tilt_axis, 0, wxEXPAND);

        auto pick_row = [&](const wxString& label, ::Button*& btn, wxStaticText*& lbl, PlanePick target) {
            btn = new ::Button(m_cards, label);
            btn->SetStyle(ButtonStyle::Regular, ButtonType::Parameter);
            lbl = new wxStaticText(m_cards, wxID_ANY, _L("(none)"));
            btn->Bind(wxEVT_BUTTON, [this, target](wxCommandEvent&) { arm_plane_pick(target); });
            plform->Add(btn);
            plform->Add(lbl, 0, wxALIGN_CENTER_VERTICAL);
        };
        pick_row(_L("Pick Face A"), m_plane_pick_faceA, m_plane_faceA_lbl, PlanePick::FaceA);
        pick_row(_L("Pick Face B"), m_plane_pick_faceB, m_plane_faceB_lbl, PlanePick::FaceB);
        pick_row(_L("Pick Edge A"), m_plane_pick_edgeA, m_plane_edgeA_lbl, PlanePick::EdgeA);
        pick_row(_L("Pick Edge B"), m_plane_pick_edgeB, m_plane_edgeB_lbl, PlanePick::EdgeB);

        m_plane_usize = make_spin(m_cards, 60.0, 1.0, 100000.0);
        plform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Size U")), 0, wxALIGN_CENTER_VERTICAL);
        plform->Add(spin_frame(m_plane_usize), 0, wxEXPAND);
        m_plane_vsize = make_spin(m_cards, 60.0, 1.0, 100000.0);
        plform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Size V")), 0, wxALIGN_CENTER_VERTICAL);
        plform->Add(spin_frame(m_plane_vsize), 0, wxEXPAND);

        m_box_plane->Add(plform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    }
    cards->Add(m_box_plane, 0, wxEXPAND);

    m_box_loft = new wxBoxSizer(wxVERTICAL);
    m_box_loft->Add(card_header(m_cards, "design_loft", _L("Loft"), m_hdr_loft), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_loft->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    m_box_loft->Add(new wxStaticText(m_cards, wxID_ANY, _L("Profiles (check 2+, in order):")),
                    0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_loft_list = new wxCheckListBox(m_cards, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 120)));
    m_loft_list->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent&) { refresh_preview(); });
    m_box_loft->Add(m_loft_list, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    {
        auto* lform = two_col_form();

        m_loft_mode = make_combo(m_cards);
        m_loft_mode->Append(_L("New body"));
        m_loft_mode->Append(_L_CONTEXT("Join", "Boolean Mode"));
        m_loft_mode->Append(_L_CONTEXT("Cut", "Boolean Mode"));
        m_loft_mode->Append(_L("Intersect"));
        m_loft_mode->SetSelection(0);
        lform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Result")), 0, wxALIGN_CENTER_VERTICAL);
        lform->Add(m_loft_mode, 0, wxEXPAND);

        m_box_loft->Add(lform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    }
    m_loft_ruled = new CheckBox(m_cards);
    {
        auto* lrow = new wxBoxSizer(wxHORIZONTAL);
        lrow->Add(new wxStaticText(m_cards, wxID_ANY, _L("Ruled (straight) sections")), 0, wxALIGN_CENTER_VERTICAL);
        lrow->AddStretchSpacer();
        lrow->Add(m_loft_ruled, 0, wxALIGN_CENTER_VERTICAL);
        m_box_loft->Add(lrow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    }
    cards->Add(m_box_loft, 0, wxEXPAND);

    m_box_surf_extrude = new wxBoxSizer(wxVERTICAL);
    m_box_surf_extrude->Add(card_header(m_cards, "design_extrude", _L("Surface Extrude"), m_hdr_surf_extrude), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_surf_extrude->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    m_surf_extrude_sketch_label = new wxStaticText(m_cards, wxID_ANY, _L("Sketch: —"));
    m_box_surf_extrude->Add(m_surf_extrude_sketch_label, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    {
        auto* sform = two_col_form();
        m_surf_extrude_distance = make_spin(m_cards, 10);
        sform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Distance")), 0, wxALIGN_CENTER_VERTICAL);
        sform->Add(spin_frame(m_surf_extrude_distance), 0, wxEXPAND);
        m_box_surf_extrude->Add(sform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    }
    cards->Add(m_box_surf_extrude, 0, wxEXPAND);

    m_box_surf_revolve = new wxBoxSizer(wxVERTICAL);
    m_box_surf_revolve->Add(card_header(m_cards, "design_revolve", _L("Surface Revolve"), m_hdr_surf_revolve), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_surf_revolve->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    m_surf_revolve_sketch_label = new wxStaticText(m_cards, wxID_ANY, _L("Sketch: —"));
    m_box_surf_revolve->Add(m_surf_revolve_sketch_label, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    {
        auto* rform = two_col_form();
        m_surf_revolve_angle = make_spin(m_cards, 360.0, 1.0, 360.0);
        rform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Angle (°)")), 0, wxALIGN_CENTER_VERTICAL);
        rform->Add(spin_frame(m_surf_revolve_angle), 0, wxEXPAND);
        m_surf_revolve_axis = make_combo(m_cards);
        m_surf_revolve_axis->Append(_L("Plane X"));
        m_surf_revolve_axis->Append(_L("Plane Y"));
        m_surf_revolve_axis->SetSelection(0);
        rform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Axis")), 0, wxALIGN_CENTER_VERTICAL);
        rform->Add(m_surf_revolve_axis, 0, wxEXPAND);
        m_surf_revolve_flip = new CheckBox(m_cards);
        rform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Flip direction")), 0, wxALIGN_CENTER_VERTICAL);
        rform->Add(m_surf_revolve_flip, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
        m_box_surf_revolve->Add(rform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    }
    cards->Add(m_box_surf_revolve, 0, wxEXPAND);

    m_box_surf_loft = new wxBoxSizer(wxVERTICAL);
    m_box_surf_loft->Add(card_header(m_cards, "design_loft", _L("Surface Loft"), m_hdr_surf_loft), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_surf_loft->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    m_box_surf_loft->Add(new wxStaticText(m_cards, wxID_ANY, _L("Profiles (check 2+, in order):")),
                         0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_surf_loft_list = new wxCheckListBox(m_cards, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 120)));
    m_surf_loft_list->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent&) { refresh_preview(); });
    m_box_surf_loft->Add(m_surf_loft_list, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    m_surf_loft_ruled = new CheckBox(m_cards);
    {
        auto* lrow = new wxBoxSizer(wxHORIZONTAL);
        lrow->Add(new wxStaticText(m_cards, wxID_ANY, _L("Ruled (straight) sections")), 0, wxALIGN_CENTER_VERTICAL);
        lrow->AddStretchSpacer();
        lrow->Add(m_surf_loft_ruled, 0, wxALIGN_CENTER_VERTICAL);
        m_box_surf_loft->Add(lrow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    }
    cards->Add(m_box_surf_loft, 0, wxEXPAND);

    m_box_surf_fill = new wxBoxSizer(wxVERTICAL);
    m_box_surf_fill->Add(card_header(m_cards, "design_surface", _L("Surface Fill"), m_hdr_surf_fill), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_surf_fill->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    m_surf_fill_sketch_label = new wxStaticText(m_cards, wxID_ANY, _L("Sketch: —"));
    m_box_surf_fill->Add(m_surf_fill_sketch_label, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_surf_fill->Add(new wxStaticText(m_cards, wxID_ANY,
                          _L("Fills the sketch's closed boundary with a smooth face.")),
                         0, wxLEFT | wxRIGHT, 12);
    cards->Add(m_box_surf_fill, 0, wxEXPAND);

    m_box_surf_offset = new wxBoxSizer(wxVERTICAL);
    m_box_surf_offset->Add(card_header(m_cards, "design_offset", _L("Surface Offset"), m_hdr_surf_offset), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_surf_offset->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        auto* oform = two_col_form();
        m_surf_offset_body = make_combo(m_cards);
        m_surf_offset_body->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        oform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Sheet body")), 0, wxALIGN_CENTER_VERTICAL);
        oform->Add(m_surf_offset_body, 0, wxEXPAND);
        m_surf_offset_distance = make_spin(m_cards, 1.0, -100000.0, 100000.0);
        m_surf_offset_distance->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        oform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Offset")), 0, wxALIGN_CENTER_VERTICAL);
        oform->Add(spin_frame(m_surf_offset_distance), 0, wxEXPAND);
        m_box_surf_offset->Add(oform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        m_box_surf_offset->Add(new wxStaticText(m_cards, wxID_ANY,
                                  _L("Offsets a sheet body's shell. Positive = outward, negative = inward.")),
                               0, wxLEFT | wxRIGHT, 12);
    }
    cards->Add(m_box_surf_offset, 0, wxEXPAND);

    m_box_surf_thicken = new wxBoxSizer(wxVERTICAL);
    m_box_surf_thicken->Add(card_header(m_cards, "design_thicken", _L("Thicken Surface"), m_hdr_surf_thicken), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_surf_thicken->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        auto* tform = two_col_form();
        m_surf_thicken_body = make_combo(m_cards);
        m_surf_thicken_body->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        tform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Sheet body")), 0, wxALIGN_CENTER_VERTICAL);
        tform->Add(m_surf_thicken_body, 0, wxEXPAND);
        m_surf_thicken_thickness = make_spin(m_cards, 1.0, 0.01, 100000.0);
        m_surf_thicken_thickness->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        tform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Thickness")), 0, wxALIGN_CENTER_VERTICAL);
        tform->Add(spin_frame(m_surf_thicken_thickness), 0, wxEXPAND);
        m_surf_thicken_flip = new CheckBox(m_cards);
        m_surf_thicken_flip->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { refresh_preview(); });
        tform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Flip direction")), 0, wxALIGN_CENTER_VERTICAL);
        tform->Add(m_surf_thicken_flip, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
        m_box_surf_thicken->Add(tform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        m_box_surf_thicken->Add(new wxStaticText(m_cards, wxID_ANY,
                                  _L("Thickens a sheet body into a solid. Flip reverses the direction.")),
                               0, wxLEFT | wxRIGHT, 12);
    }
    cards->Add(m_box_surf_thicken, 0, wxEXPAND);

    auto* sform = two_col_form();
    m_shell_thickness = make_spin(m_cards, 2.0, 0.01, 100000.0);
    sform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Thickness")), 0, wxALIGN_CENTER_VERTICAL);
    sform->Add(spin_frame(m_shell_thickness), 0, wxEXPAND);
    m_shell_face_label = new wxStaticText(m_cards, wxID_ANY, _L("(all faces — closed hollow)"));
    sform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Open face")), 0, wxALIGN_CENTER_VERTICAL);
    sform->Add(m_shell_face_label, 0, wxALIGN_CENTER_VERTICAL);

    m_box_shell = new wxBoxSizer(wxVERTICAL);
    m_box_shell->Add(card_header(m_cards, "design_shell", _L_CONTEXT("Shell", "Design"), m_hdr_shell), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_shell->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    m_box_shell->Add(new wxStaticText(m_cards, wxID_ANY,
                        _L("Pick a solid face to open it, then set the wall thickness.")),
                     0, wxLEFT | wxRIGHT, 12);
    m_box_shell->Add(sform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    cards->Add(m_box_shell, 0, wxEXPAND);

    auto* drform = two_col_form();
    m_draft_angle = make_spin(m_cards, 5.0, -89.0, 89.0);
    drform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Angle (°)")), 0, wxALIGN_CENTER_VERTICAL);
    drform->Add(spin_frame(m_draft_angle), 0, wxEXPAND);
    m_draft_face_label = new wxStaticText(m_cards, wxID_ANY, _L("(pick a side face)"));
    drform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Face")), 0, wxALIGN_CENTER_VERTICAL);
    drform->Add(m_draft_face_label, 0, wxALIGN_CENTER_VERTICAL);

    m_box_draft = new wxBoxSizer(wxVERTICAL);
    m_box_draft->Add(card_header(m_cards, "design_draft", _L("Draft"), m_hdr_draft), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_draft->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    m_box_draft->Add(new wxStaticText(m_cards, wxID_ANY,
                        _L("Pick a side face, then set the draft angle. The face pivots about the body base.")),
                     0, wxLEFT | wxRIGHT, 12);
    m_box_draft->Add(drform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    cards->Add(m_box_draft, 0, wxEXPAND);

    m_box_transform = new wxBoxSizer(wxVERTICAL);
    m_box_transform->Add(card_header(m_cards, "design_move", _L("Transform"), m_hdr_transform), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_transform->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        auto* xform = two_col_form();
        m_xf_body = make_combo(m_cards);
        m_xf_body->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        xform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Body")), 0, wxALIGN_CENTER_VERTICAL);
        xform->Add(m_xf_body, 0, wxEXPAND);
        m_xf_dx = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        m_xf_dx->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        xform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Translate X")), 0, wxALIGN_CENTER_VERTICAL);
        xform->Add(spin_frame(m_xf_dx), 0, wxEXPAND);
        m_xf_dy = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        m_xf_dy->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        xform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Translate Y")), 0, wxALIGN_CENTER_VERTICAL);
        xform->Add(spin_frame(m_xf_dy), 0, wxEXPAND);
        m_xf_dz = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        m_xf_dz->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        xform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Translate Z")), 0, wxALIGN_CENTER_VERTICAL);
        xform->Add(spin_frame(m_xf_dz), 0, wxEXPAND);
        m_xf_axis = make_combo(m_cards);
        m_xf_axis->Append(_L_CONTEXT("X", "Axis"));
        m_xf_axis->Append(_L_CONTEXT("Y", "Axis"));
        m_xf_axis->Append(_L_CONTEXT("Z", "Axis"));
        m_xf_axis->SetSelection(2);
        m_xf_axis->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        xform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Rotate axis")), 0, wxALIGN_CENTER_VERTICAL);
        xform->Add(m_xf_axis, 0, wxEXPAND);
        m_xf_angle = make_spin(m_cards, 0.0, -360.0, 360.0);
        m_xf_angle->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        xform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Angle (°)")), 0, wxALIGN_CENTER_VERTICAL);
        xform->Add(spin_frame(m_xf_angle), 0, wxEXPAND);
        m_xf_pivot_x = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        m_xf_pivot_x->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        xform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Pivot X")), 0, wxALIGN_CENTER_VERTICAL);
        xform->Add(spin_frame(m_xf_pivot_x), 0, wxEXPAND);
        m_xf_pivot_y = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        m_xf_pivot_y->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        xform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Pivot Y")), 0, wxALIGN_CENTER_VERTICAL);
        xform->Add(spin_frame(m_xf_pivot_y), 0, wxEXPAND);
        m_xf_pivot_z = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        m_xf_pivot_z->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        xform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Pivot Z")), 0, wxALIGN_CENTER_VERTICAL);
        xform->Add(spin_frame(m_xf_pivot_z), 0, wxEXPAND);
        m_box_transform->Add(xform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        m_xf_copy = new CheckBox(m_cards);
        m_xf_copy->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { refresh_preview(); });
        auto* cpro = new wxBoxSizer(wxHORIZONTAL);
        cpro->Add(new wxStaticText(m_cards, wxID_ANY, _L("Keep original (make a copy)")), 0, wxALIGN_CENTER_VERTICAL);
        cpro->AddStretchSpacer();
        cpro->Add(m_xf_copy, 0, wxALIGN_CENTER_VERTICAL);
        m_box_transform->Add(cpro, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        m_box_transform->Add(new wxStaticText(m_cards, wxID_ANY,
                               _L("Translate and/or rotate the selected body. Rotation is applied about the pivot, then translation follows.")),
                            0, wxLEFT | wxRIGHT, 12);
    }
    cards->Add(m_box_transform, 0, wxEXPAND);

    m_box_mirror = new wxBoxSizer(wxVERTICAL);
    m_box_mirror->Add(card_header(m_cards, "design_mirror", _L("Mirror"), m_hdr_mirror), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_mirror->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        auto* mform = two_col_form();
        m_mirror_body = make_combo(m_cards);
        m_mirror_body->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        mform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Body")), 0, wxALIGN_CENTER_VERTICAL);
        mform->Add(m_mirror_body, 0, wxEXPAND);
        m_mirror_plane = make_combo(m_cards);
        m_mirror_plane->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        mform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Mirror plane")), 0, wxALIGN_CENTER_VERTICAL);
        mform->Add(m_mirror_plane, 0, wxEXPAND);
        m_box_mirror->Add(mform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        m_mirror_keep = new CheckBox(m_cards);
        m_mirror_keep->SetValue(true);
        m_mirror_keep->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { refresh_preview(); });
        auto* mkrow = new wxBoxSizer(wxHORIZONTAL);
        mkrow->Add(new wxStaticText(m_cards, wxID_ANY, _L("Keep original")), 0, wxALIGN_CENTER_VERTICAL);
        mkrow->AddStretchSpacer();
        mkrow->Add(m_mirror_keep, 0, wxALIGN_CENTER_VERTICAL);
        m_box_mirror->Add(mkrow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        m_box_mirror->Add(new wxStaticText(m_cards, wxID_ANY,
                            _L("Reflect the selected body about a plane. The mirror is always a new body; keeping the original gives you both.")),
                         0, wxLEFT | wxRIGHT, 12);
    }
    cards->Add(m_box_mirror, 0, wxEXPAND);

    m_box_thicken = new wxBoxSizer(wxVERTICAL);
    m_box_thicken->Add(card_header(m_cards, "design_thicken", _L("Thicken"), m_hdr_thicken), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_thicken->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        auto* tform = two_col_form();
        m_thicken_body = make_combo(m_cards);
        m_thicken_body->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        tform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Body")), 0, wxALIGN_CENTER_VERTICAL);
        tform->Add(m_thicken_body, 0, wxEXPAND);
        m_thicken_face_label = new wxStaticText(m_cards, wxID_ANY, _L("(pick a solid face)"));
        tform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Face")), 0, wxALIGN_CENTER_VERTICAL);
        tform->Add(m_thicken_face_label, 0, wxALIGN_CENTER_VERTICAL);
        m_thicken_thickness = make_spin(m_cards, 2.0, 0.01, 100000.0);
        m_thicken_thickness->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        tform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Thickness")), 0, wxALIGN_CENTER_VERTICAL);
        tform->Add(spin_frame(m_thicken_thickness), 0, wxEXPAND);
        m_box_thicken->Add(tform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        m_thicken_flip = new CheckBox(m_cards);
        m_thicken_flip->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { refresh_preview(); });
        auto* trow = new wxBoxSizer(wxHORIZONTAL);
        trow->Add(new wxStaticText(m_cards, wxID_ANY, _L("Flip direction")), 0, wxALIGN_CENTER_VERTICAL);
        trow->AddStretchSpacer();
        trow->Add(m_thicken_flip, 0, wxALIGN_CENTER_VERTICAL);
        m_box_thicken->Add(trow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        m_box_thicken->Add(new wxStaticText(m_cards, wxID_ANY,
                              _L("Pick a solid face and offset it into a new thin body. Not for sheet bodies — use Thicken Surface for those.")),
                           0, wxLEFT | wxRIGHT, 12);
    }
    cards->Add(m_box_thicken, 0, wxEXPAND);

    m_box_rib = new wxBoxSizer(wxVERTICAL);
    m_box_rib->Add(card_header(m_cards, "design_rib", _L_CONTEXT("Rib", "Design"), m_hdr_rib), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_rib->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        auto* rform = two_col_form();
        m_rib_body = make_combo(m_cards);
        m_rib_body->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        rform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Target body")), 0, wxALIGN_CENTER_VERTICAL);
        rform->Add(m_rib_body, 0, wxEXPAND);
        m_rib_sketch = make_combo(m_cards);
        m_rib_sketch->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        rform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Sketch")), 0, wxALIGN_CENTER_VERTICAL);
        rform->Add(m_rib_sketch, 0, wxEXPAND);
        m_rib_entity = new wxSpinCtrl(m_cards, wxID_ANY, "", wxDefaultPosition, FromDIP(wxSize(90, -1)),
                                      wxSP_ARROW_KEYS | wxBORDER_SIMPLE);
        m_rib_entity->SetRange(0, 999);
        m_rib_entity->SetValue(0);
        m_rib_entity->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { refresh_preview(); });
        rform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Entity index")), 0, wxALIGN_CENTER_VERTICAL);
        rform->Add(m_rib_entity, 0, wxEXPAND);
        m_rib_thickness = make_spin(m_cards, 2.0, 0.01, 100000.0);
        m_rib_thickness->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        rform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Thickness")), 0, wxALIGN_CENTER_VERTICAL);
        rform->Add(spin_frame(m_rib_thickness), 0, wxEXPAND);
        m_rib_depth = make_spin(m_cards, 10.0, 0.01, 100000.0);
        m_rib_depth->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        rform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Depth")), 0, wxALIGN_CENTER_VERTICAL);
        rform->Add(spin_frame(m_rib_depth), 0, wxEXPAND);
        m_box_rib->Add(rform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        m_box_rib->Add(new wxStaticText(m_cards, wxID_ANY,
                          _L("Grow a thin wall from an open sketch Line entity, fused to the target body. Entity index is the position of the open Line in the sketch.")),
                       0, wxLEFT | wxRIGHT, 12);
    }
    cards->Add(m_box_rib, 0, wxEXPAND);

    m_box_project = new wxBoxSizer(wxVERTICAL);
    m_box_project->Add(card_header(m_cards, "design_sketch", _L_CONTEXT("Project", "Design"), m_hdr_project), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_project->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        auto* pform = two_col_form();
        m_proj_source_body = make_combo(m_cards);
        m_proj_source_body->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        pform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Source body")), 0, wxALIGN_CENTER_VERTICAL);
        pform->Add(m_proj_source_body, 0, wxEXPAND);
        m_proj_face_label = new wxStaticText(m_cards, wxID_ANY, _L("(all edges)"));
        pform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Face")), 0, wxALIGN_CENTER_VERTICAL);
        pform->Add(m_proj_face_label, 0, wxALIGN_CENTER_VERTICAL);
        m_proj_plane = make_combo(m_cards);
        populate_plane_choices(m_proj_plane);
        m_proj_plane->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        pform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Target plane")), 0, wxALIGN_CENTER_VERTICAL);
        pform->Add(m_proj_plane, 0, wxEXPAND);
        m_box_project->Add(pform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        m_box_project->Add(new wxStaticText(m_cards, wxID_ANY,
                              _L("Project the edges of a body onto a plane as sketch entities. Pick a face to project only its edges, or leave the face empty to project the whole body.")),
                           0, wxLEFT | wxRIGHT, 12);
    }
    cards->Add(m_box_project, 0, wxEXPAND);

    m_box_delete_face = new wxBoxSizer(wxVERTICAL);
    m_box_delete_face->Add(card_header(m_cards, "design_delete", _L("Delete Face"), m_hdr_delete_face), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_delete_face->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        auto* dform = two_col_form();
        m_del_face_body = make_combo(m_cards);
        m_del_face_body->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        dform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Body")), 0, wxALIGN_CENTER_VERTICAL);
        dform->Add(m_del_face_body, 0, wxEXPAND);
        m_del_face_add_btn = new ::Button(m_cards, _L("Add picked face"));
        m_del_face_add_btn->SetStyle(ButtonStyle::Regular, ButtonType::Parameter);
        m_del_face_add_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (m_sel_solid_face < 0) {
                set_status(StatusKind::Error, _L("Click a face on the body first, then Add picked face"));
                return;
            }
            if (std::find(m_del_faces.begin(), m_del_faces.end(), m_sel_solid_face) != m_del_faces.end()) {
                set_status(StatusKind::Info, wxString::Format(_L("Face %d is already in the list"), m_sel_solid_face));
                return;
            }
            {
                m_del_faces.push_back(m_sel_solid_face);
                wxString s;
                for (size_t i = 0; i < m_del_faces.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += wxString::Format(_L("Face %d"), m_del_faces[i]);
                }
                m_del_face_list->SetLabel(s.empty() ? _L("(none)") : s);
                m_del_face_list->GetParent()->Layout();
                m_del_face_list->Refresh();
                refresh_preview();
            }
        });
        dform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Faces to delete")), 0, wxALIGN_CENTER_VERTICAL);
        dform->Add(m_del_face_add_btn, 0, wxALIGN_CENTER_VERTICAL);
        m_del_face_list = new wxStaticText(m_cards, wxID_ANY, _L("(none)"));
        dform->Add(0, 0);
        dform->Add(m_del_face_list, 0, wxALIGN_CENTER_VERTICAL);
        m_box_delete_face->Add(dform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        m_box_delete_face->Add(new wxStaticText(m_cards, wxID_ANY,
                                  _L("Pick a solid face, then click 'Add picked face' to accumulate faces. Confirm to remove all listed faces and heal the solid.")),
                               0, wxLEFT | wxRIGHT, 12);
    }
    cards->Add(m_box_delete_face, 0, wxEXPAND);

    m_box_helix = new wxBoxSizer(wxVERTICAL);
    m_box_helix->Add(card_header(m_cards, "design_thread", _L("Helix"), m_hdr_helix), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_helix->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        auto* hform = two_col_form();
        m_helix_plane = make_combo(m_cards);
        populate_plane_choices(m_helix_plane);
        m_helix_plane->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
        hform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Plane")), 0, wxALIGN_CENTER_VERTICAL);
        hform->Add(m_helix_plane, 0, wxEXPAND);
        m_helix_radius = make_spin(m_cards, 10.0, 0.01, 10000.0);
        m_helix_radius->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        hform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Radius")), 0, wxALIGN_CENTER_VERTICAL);
        hform->Add(spin_frame(m_helix_radius), 0, wxEXPAND);
        m_helix_pitch = make_spin(m_cards, 5.0, 0.01, 10000.0);
        m_helix_pitch->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        hform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Pitch")), 0, wxALIGN_CENTER_VERTICAL);
        hform->Add(spin_frame(m_helix_pitch), 0, wxEXPAND);
        m_helix_height = make_spin(m_cards, 20.0, 0.01, 10000.0);
        m_helix_height->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        hform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Height")), 0, wxALIGN_CENTER_VERTICAL);
        hform->Add(spin_frame(m_helix_height), 0, wxEXPAND);
        m_box_helix->Add(hform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        m_helix_left_handed = new CheckBox(m_cards);
        m_helix_left_handed->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { refresh_preview(); });
        auto* hrow = new wxBoxSizer(wxHORIZONTAL);
        hrow->Add(new wxStaticText(m_cards, wxID_ANY, _L("Left-handed")), 0, wxALIGN_CENTER_VERTICAL);
        hrow->AddStretchSpacer();
        hrow->Add(m_helix_left_handed, 0, wxALIGN_CENTER_VERTICAL);
        m_box_helix->Add(hrow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        m_helix_taper = make_spin(m_cards, 0.0, -89.0, 89.0);
        m_helix_taper->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
        auto* tvalform = two_col_form();
        tvalform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Taper (°)")), 0, wxALIGN_CENTER_VERTICAL);
        tvalform->Add(spin_frame(m_helix_taper), 0, wxEXPAND);
        m_box_helix->Add(tvalform, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
        m_box_helix->Add(new wxStaticText(m_cards, wxID_ANY,
                            _L("A helical curve (spring path). Use as a sweep path to build springs, coils, or augers.")),
                         0, wxLEFT | wxRIGHT, 12);
    }
    cards->Add(m_box_helix, 0, wxEXPAND);

    m_box_axis = new wxBoxSizer(wxVERTICAL);
    m_box_axis->Add(card_header(m_cards, "design_line", _L("Axis"), m_hdr_axis), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_axis->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        m_axis_type = make_combo(m_cards);
        for (const wxString& t : { _L("Two points"), _L("Face normal"), _L("Cylinder centerline"),
                                   _L("Plane intersection"), _L("Along edge") })
            m_axis_type->Append(t);
        m_axis_type->SetSelection(0);
        m_box_axis->Add(new wxStaticText(m_cards, wxID_ANY, _L("Axis type")), 0, wxLEFT | wxRIGHT | wxTOP, 12);
        m_box_axis->Add(m_axis_type, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

        auto* axform = two_col_form();

        auto ax_pick = [&](const wxString& label, ::Button*& btn, wxStaticText*& lbl, AxisPick target) {
            btn = new ::Button(m_cards, label);
            btn->SetStyle(ButtonStyle::Regular, ButtonType::Parameter);
            lbl = new wxStaticText(m_cards, wxID_ANY, _L("(none)"));
            btn->Bind(wxEVT_BUTTON, [this, target](wxCommandEvent&) { arm_axis_pick(target); });
            axform->Add(btn);
            axform->Add(lbl, 0, wxALIGN_CENTER_VERTICAL);
        };
        ax_pick(_L("Pick Face"),  m_axis_pick_face, m_axis_face_lbl, AxisPick::Face);
        ax_pick(_L("Pick Edge"),  m_axis_pick_edge, m_axis_edge_lbl, AxisPick::Edge);

        m_axis_plane_a = make_combo(m_cards);
        populate_plane_choices(m_axis_plane_a);
        axform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Plane A")), 0, wxALIGN_CENTER_VERTICAL);
        axform->Add(m_axis_plane_a, 0, wxEXPAND);
        m_axis_plane_b = make_combo(m_cards);
        populate_plane_choices(m_axis_plane_b);
        axform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Plane B")), 0, wxALIGN_CENTER_VERTICAL);
        axform->Add(m_axis_plane_b, 0, wxEXPAND);

        m_axis_p1x = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        m_axis_p1y = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        m_axis_p1z = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        axform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Point 1 X")), 0, wxALIGN_CENTER_VERTICAL);
        axform->Add(spin_frame(m_axis_p1x), 0, wxEXPAND);
        axform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Point 1 Y")), 0, wxALIGN_CENTER_VERTICAL);
        axform->Add(spin_frame(m_axis_p1y), 0, wxEXPAND);
        axform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Point 1 Z")), 0, wxALIGN_CENTER_VERTICAL);
        axform->Add(spin_frame(m_axis_p1z), 0, wxEXPAND);

        m_axis_p2x = make_spin(m_cards, 10.0, -100000.0, 100000.0);
        m_axis_p2y = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        m_axis_p2z = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        axform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Point 2 X")), 0, wxALIGN_CENTER_VERTICAL);
        axform->Add(spin_frame(m_axis_p2x), 0, wxEXPAND);
        axform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Point 2 Y")), 0, wxALIGN_CENTER_VERTICAL);
        axform->Add(spin_frame(m_axis_p2y), 0, wxEXPAND);
        axform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Point 2 Z")), 0, wxALIGN_CENTER_VERTICAL);
        axform->Add(spin_frame(m_axis_p2z), 0, wxEXPAND);

        m_box_axis->Add(axform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    }
    cards->Add(m_box_axis, 0, wxEXPAND);

    m_box_coordsys = new wxBoxSizer(wxVERTICAL);
    m_box_coordsys->Add(card_header(m_cards, "design_point", _L("Coordinate system"), m_hdr_coordsys), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_coordsys->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        m_coordsys_type = make_combo(m_cards);
        for (const wxString& t : { _L("Point (world)"), _L("Face + direction") })
            m_coordsys_type->Append(t);
        m_coordsys_type->SetSelection(0);
        m_box_coordsys->Add(new wxStaticText(m_cards, wxID_ANY, _L("Type")), 0, wxLEFT | wxRIGHT | wxTOP, 12);
        m_box_coordsys->Add(m_coordsys_type, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

        m_cs_body = make_combo(m_cards);
        m_cs_body->Append(_L("(all)"));
        m_cs_body->SetSelection(0);
        m_cs_body->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) {
            if (m_viewport) m_viewport->set_xray_focus(m_cs_body->GetSelection() - 1);
        });
        m_box_coordsys->Add(new wxStaticText(m_cards, wxID_ANY, _L("Body")), 0, wxLEFT | wxRIGHT | wxTOP, 12);
        m_box_coordsys->Add(m_cs_body, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        auto* body_hint = new wxStaticText(m_cards, wxID_ANY,
            _L("Restrict picking to one body — the others go see-through and stop catching clicks"));
        body_hint->SetForegroundColour(dp_sec_text());
        m_box_coordsys->Add(body_hint, 0, wxLEFT | wxRIGHT | wxTOP, 12);

        auto* csform = two_col_form();

        m_cs_x = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        m_cs_y = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        m_cs_z = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        csform->Add(new wxStaticText(m_cards, wxID_ANY, _L_CONTEXT("X", "Axis")), 0, wxALIGN_CENTER_VERTICAL);
        csform->Add(spin_frame(m_cs_x), 0, wxEXPAND);
        csform->Add(new wxStaticText(m_cards, wxID_ANY, _L_CONTEXT("Y", "Axis")), 0, wxALIGN_CENTER_VERTICAL);
        csform->Add(spin_frame(m_cs_y), 0, wxEXPAND);
        csform->Add(new wxStaticText(m_cards, wxID_ANY, _L_CONTEXT("Z", "Axis")), 0, wxALIGN_CENTER_VERTICAL);
        csform->Add(spin_frame(m_cs_z), 0, wxEXPAND);

        auto cs_pick = [&](const wxString& label, ::Button*& btn, wxStaticText*& lbl, CoordSysPick target) {
            btn = new ::Button(m_cards, label);
            btn->SetStyle(ButtonStyle::Regular, ButtonType::Parameter);
            lbl = new wxStaticText(m_cards, wxID_ANY, _L("(none)"));
            btn->Bind(wxEVT_BUTTON, [this, target](wxCommandEvent&) { arm_coordsys_pick(target); });
            csform->Add(btn);
            csform->Add(lbl, 0, wxALIGN_CENTER_VERTICAL);
        };
        cs_pick(_L("Pick Face"), m_cs_pick_face, m_cs_face_lbl, CoordSysPick::Face);

        m_cs_pick_edge = new ::Button(m_cards, _L("Edge (sets in-plane direction)"));
        m_cs_pick_edge->SetStyle(ButtonStyle::Regular, ButtonType::Parameter);
        m_cs_edge_lbl = new wxStaticText(m_cards, wxID_ANY, _L("(none)"));
        m_cs_pick_edge->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { arm_coordsys_pick(CoordSysPick::Edge); });
        csform->Add(m_cs_pick_edge);
        csform->Add(m_cs_edge_lbl, 0, wxALIGN_CENTER_VERTICAL);
        auto* edge_hint = new wxStaticText(m_cards, wxID_ANY,
            _L("Without an edge the frame takes X from the face's first edge — pick one to choose it yourself"));
        edge_hint->SetForegroundColour(dp_sec_text());
        csform->Add(0, 0);
        csform->Add(edge_hint, 0, wxALIGN_CENTER_VERTICAL);

        m_cs_hx = make_spin(m_cards, 1.0, -100000.0, 100000.0);
        m_cs_hy = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        m_cs_hz = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        csform->Add(new wxStaticText(m_cards, wxID_ANY, _L("X direction X")), 0, wxALIGN_CENTER_VERTICAL);
        csform->Add(spin_frame(m_cs_hx), 0, wxEXPAND);
        csform->Add(new wxStaticText(m_cards, wxID_ANY, _L("X direction Y")), 0, wxALIGN_CENTER_VERTICAL);
        csform->Add(spin_frame(m_cs_hy), 0, wxEXPAND);
        csform->Add(new wxStaticText(m_cards, wxID_ANY, _L("X direction Z")), 0, wxALIGN_CENTER_VERTICAL);
        csform->Add(spin_frame(m_cs_hz), 0, wxEXPAND);

        m_box_coordsys->Add(csform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    }
    cards->Add(m_box_coordsys, 0, wxEXPAND);

    m_box_mate = new wxBoxSizer(wxVERTICAL);
    m_box_mate->Add(card_header(m_cards, "design_c_coincident", _L("Mate"), m_hdr_mate), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_mate->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        auto* mform = two_col_form();

        m_mate_kind = make_combo(m_cards);
        m_mate_kind->Append(_L("Fastened — 6 DOF locked, B driven onto A exactly"));
        m_mate_kind->Append(_L("Planar — z aligned, offset distance; free in-plane slide"));
        m_mate_kind->Append(_L("Revolute — axes collinear; free rotation about z"));
        m_mate_kind->Append(_L("Slider — orientation locked; free axial slide"));
        m_mate_kind->Append(_L("Cylindrical — axes collinear; free spin + axial slide"));
        m_mate_kind->SetSelection(0);
        mform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Kind")), 0, wxALIGN_CENTER_VERTICAL);
        mform->Add(m_mate_kind, 0, wxEXPAND);

        m_mate_cs_a = make_combo(m_cards);
        mform->Add(new wxStaticText(m_cards, wxID_ANY, _L("A (fixed)")), 0, wxALIGN_CENTER_VERTICAL);
        mform->Add(m_mate_cs_a, 0, wxEXPAND);

        m_mate_cs_b = make_combo(m_cards);
        mform->Add(new wxStaticText(m_cards, wxID_ANY, _L("B (moves)")), 0, wxALIGN_CENTER_VERTICAL);
        mform->Add(m_mate_cs_b, 0, wxEXPAND);

        m_offset_label = new wxStaticText(m_cards, wxID_ANY, _L("Offset"));
        m_mate_offset = make_spin(m_cards, 0.0, -100000.0, 100000.0);
        mform->Add(m_offset_label, 0, wxALIGN_CENTER_VERTICAL);
        mform->Add(spin_frame(m_mate_offset), 0, wxEXPAND);

        m_angle_label = new wxStaticText(m_cards, wxID_ANY, _L("Angle \u00b0"));
        m_mate_angle = make_spin(m_cards, 0.0, -360.0, 360.0);
        mform->Add(m_angle_label, 0, wxALIGN_CENTER_VERTICAL);
        mform->Add(spin_frame(m_mate_angle), 0, wxEXPAND);

        m_mate_flip = new CheckBox(m_cards);
        auto* frow = new wxBoxSizer(wxHORIZONTAL);
        frow->Add(new wxStaticText(m_cards, wxID_ANY, _L("Flip (oppose z axes)")), 0, wxALIGN_CENTER_VERTICAL);
        frow->AddStretchSpacer();
        frow->Add(m_mate_flip, 0, wxALIGN_CENTER_VERTICAL);
        mform->Add(0, 0);
        mform->Add(frow, 0, wxEXPAND);

        m_mate_kind->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& e) {
            const int kind = m_mate_kind->GetSelection();
            switch (kind) {
            case 0:
                m_offset_label->SetLabel(_L("Offset"));
                m_angle_label->SetLabel(_L("Angle \u00b0"));
                break;
            case 1:
                m_offset_label->SetLabel(_L("Plane distance"));
                m_angle_label->SetLabel(_L("Angle \u00b0"));
                break;
            case 2:
                m_offset_label->SetLabel(_L("Axial position"));
                m_angle_label->SetLabel(_L("Angle \u00b0"));
                break;
            case 3:
                m_offset_label->SetLabel(_L("Axial position"));
                m_angle_label->SetLabel(_L("Angle \u00b0"));
                break;
            case 4:
                m_offset_label->SetLabel(_L("Axial position"));
                m_angle_label->SetLabel(_L("Angle \u00b0"));
                break;
            }
            m_cards->Layout(); m_form->Layout(); m_form->FitInside();
            refresh_preview();
            e.Skip();
        });

        m_box_mate->Add(mform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    }
    cards->Add(m_box_mate, 0, wxEXPAND);
    m_mate_cs_a->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
    m_mate_cs_b->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { refresh_preview(); });
    m_mate_offset->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
    m_mate_angle->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { refresh_preview(); });
    m_mate_flip->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { refresh_preview(); });

    m_box_expr = new wxBoxSizer(wxVERTICAL);
    {
        wxStaticText* expr_hdr = nullptr;
        m_box_expr->Add(card_header(m_cards, "design_constrain", _L("Expression"), expr_hdr), 0,
                        wxLEFT | wxRIGHT | wxTOP, 12);
        m_box_expr->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);

        auto* eform = two_col_form();

        m_expr_field = new ComboBox(m_cards, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                    wxSize(FromDIP(90), FromDIP(24)), 0, nullptr, 0);
        m_expr_field->SetMinSize(wxSize(FromDIP(90), FromDIP(24)));
        eform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Field")), 0, wxALIGN_CENTER_VERTICAL);
        eform->Add(m_expr_field, 0, wxEXPAND);

        m_expr_text = new wxTextCtrl(m_cards, wxID_ANY, "", wxDefaultPosition,
                                     wxSize(FromDIP(120), FromDIP(24)),
                                     wxTE_PROCESS_ENTER | wxBORDER_SIMPLE);
        m_expr_text->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { on_set_expr(); });
        eform->Add(new wxStaticText(m_cards, wxID_ANY, _L("Expr")), 0, wxALIGN_CENTER_VERTICAL);
        eform->Add(m_expr_text, 0, wxEXPAND);

        m_box_expr->Add(eform, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

        auto* brow = new wxBoxSizer(wxHORIZONTAL);
        m_expr_set_btn   = new ::Button(m_cards, _L("Set"));
        m_expr_clear_btn = new ::Button(m_cards, _L("Clear"));
        m_expr_set_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Parameter);
        m_expr_clear_btn->SetStyle(ButtonStyle::Regular, ButtonType::Parameter);
        m_expr_set_btn->Bind(wxEVT_BUTTON,   [this](wxCommandEvent&) { on_set_expr(); });
        m_expr_clear_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_clear_expr(); });
        brow->Add(m_expr_set_btn,   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        brow->Add(m_expr_clear_btn, 0, wxALIGN_CENTER_VERTICAL);
        brow->AddStretchSpacer();
        m_box_expr->Add(brow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

        m_expr_status = new wxStaticText(m_cards, wxID_ANY, _L("(no bindings)"));
        m_expr_status->SetForegroundColour(dp_sec_text());
        m_box_expr->Add(m_expr_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
    }
    cards->Add(m_box_expr, 0, wxEXPAND);

    m_box_value = new wxBoxSizer(wxVERTICAL);
    {
        m_box_value->Add(card_header(m_cards, "design_constrain", _L("Value"), m_value_label), 0, wxLEFT | wxRIGHT | wxTOP, 12);
        m_box_value->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
        auto* vrow = new wxBoxSizer(wxHORIZONTAL);
        m_value_input = new wxTextCtrl(m_cards, wxID_ANY, "", wxDefaultPosition,
                                       FromDIP(wxSize(90, -1)), wxTE_PROCESS_ENTER);
        m_value_input->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { confirm_value(); });
        vrow->Add(new wxStaticText(m_cards, wxID_ANY, _L("Value")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        vrow->Add(m_value_input, 0, wxALIGN_CENTER_VERTICAL);
        m_box_value->Add(vrow, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    }
    cards->Add(m_box_value, 0, wxEXPAND);

    m_box_sketch_session = new wxBoxSizer(wxVERTICAL);
    m_box_sketch_session->Add(card_header(m_cards, "design_sketch", _L("Sketch"), m_hdr_sketch_session),
                              0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_sketch_session->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    {
        m_sketch_hint = new wxStaticText(m_cards, wxID_ANY, _L("Pick a tool, or press Menu for the list."));
        m_sketch_hint->SetForegroundColour(dp_sec_text());
        m_box_sketch_session->Add(m_sketch_hint, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 12);
    }
    cards->Add(m_box_sketch_session, 0, wxEXPAND);

    m_box_constraints = new wxBoxSizer(wxVERTICAL);
    m_box_constraints->Add(card_header(m_cards, "design_constrain", _L("Constraints"), m_hdr_constraints),
                           0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_constraints->Add(new wxStaticLine(m_cards), 0, wxEXPAND | wxALL, 8);
    m_constraint_rows = new wxBoxSizer(wxVERTICAL);
    m_box_constraints->Add(m_constraint_rows, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    cards->Add(m_box_constraints, 0, wxEXPAND);

    for (wxWindow* w : m_cards->GetChildren())
        if (auto* t = dynamic_cast<wxStaticText*>(w))
            t->Wrap(240);

    m_tree_box = make_card(m_form);
    auto* tree_inner = new wxBoxSizer(wxVERTICAL);
    m_tree_box->SetSizer(tree_inner);
    root->Add(m_tree_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    m_hdr_tree_row = new wxBoxSizer(wxHORIZONTAL);
    m_hdr_tree_row->Add(card_header(m_tree_box, "design_sketch", _L("Feature tree"), m_hdr_tree), 0,
                        wxALIGN_CENTER_VERTICAL);
    m_hdr_tree_row->AddStretchSpacer();
    tree_inner->Add(m_hdr_tree_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
              FromDIP(SidebarProps::ContentMargin()));
    tree_inner->Add(new wxStaticLine(m_tree_box), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
              FromDIP(SidebarProps::TitlebarMargin()));
    m_pinned = new DesignRowList(m_tree_box, 2, false, wxBORDER_NONE);
    m_pinned->SetBackgroundColour(dp_panel_bg());
    tree_inner->Add(m_pinned, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 13);
    m_tree = new DesignRowList(m_tree_box, 9);
    m_tree->SetBackgroundColour(dp_panel_bg());
    tree_inner->Add(m_tree, 0, wxEXPAND | wxALL, 12);

    m_pinned->on_action = [this](int row, int) {
        if (row == kBedRow)
            toggle_bed();
        else if (row == kOriginRow)
            toggle_origin();
    };
    m_pinned->on_menu = [this](int row, const wxPoint& screen) {
        const bool bed = (row == kBedRow);
        wxMenu     menu;
        const int  id_toggle = wxWindow::NewControlId();
        menu.Append(id_toggle, bed ? bed_toggle_text(m_show_bed) : origin_toggle_text(m_show_origin));
        menu.Bind(wxEVT_MENU, [this, bed](wxCommandEvent&) { bed ? toggle_bed() : toggle_origin(); }, id_toggle);
        m_pinned->PopupMenu(&menu, m_pinned->ScreenToClient(screen));
    };

    m_tree->on_select = [this] {
        if (!m_viewport) return;
        if (m_parts && m_tree->selection() != wxNOT_FOUND) m_parts->unselect();
        const int sel = tree_selection();
        if (sel != wxNOT_FOUND && m_active == Tool::None)
            drop_solid_pick();
        request_feature_highlight();
        if (const std::string* why = (sel >= 0) ? mate_conflict_reason(sel) : nullptr) {
            set_status(StatusKind::Error, wxString::Format(_L("%s. Click the eye icon to suppress this mate."), wxString::FromUTF8(*why)));
        } else if (sel >= 0 && sel < int(m_doc.features.size())) {
            set_status(StatusKind::Info, wxString::Format(
                _L("%s selected — F2 or right-click renames it, double-click edits it"),
                wxString::FromUTF8(m_doc.features[sel].name)));
        }
    };

    m_tree->on_activate = [this] {
        if (tree_selection() != wxNOT_FOUND) on_edit_feature();
    };

    m_tree->on_action = [this](int row, int id) {
        if (m_parts) m_parts->unselect();
        switch (id) {
        case RowZoom:       zoom_to_feature(row);   break;
        case RowEdit:       on_edit_feature();      break;
        case RowVisibility: on_toggle_visibility(); break;
        case RowDelete:     on_delete_feature();    break;
        }
    };

    m_tree->on_menu = [this](int row, const wxPoint& screen) {
        wxMenu menu;
        const int id_rename = wxWindow::NewControlId();
        const int id_edit   = wxWindow::NewControlId();
        const int id_up     = wxWindow::NewControlId();
        const int id_down   = wxWindow::NewControlId();
        const int id_vis    = wxWindow::NewControlId();
        const int id_zoom   = wxWindow::NewControlId();
        const int id_art    = wxWindow::NewControlId();
        const int id_del    = wxWindow::NewControlId();
        menu.Append(id_rename, _L("Rename\tF2"));
        menu.Append(id_edit,   _L("Edit"));
        const bool art = row < int(m_doc.features.size()) &&
                         !m_doc.features[row].imported_regions.empty();
        if (art) menu.Append(id_art, _L("Scale artwork"));
        menu.AppendSeparator();
        menu.Append(id_up,     _L("Move up"));
        menu.Append(id_down,   _L("Move down"));
        menu.Append(id_vis,    _L("Show / hide"));
        const bool zoom = can_zoom_to_feature(row);
        if (zoom) menu.Append(id_zoom, _L("Zoom to selection"));
        menu.AppendSeparator();
        menu.Append(id_del,    _L("Delete"));
        menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_verb_actions["btn:rename"](); }, id_rename);
        menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { on_edit_feature(); },      id_edit);
        menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { on_move_feature(-1); },    id_up);
        menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { on_move_feature(+1); },    id_down);
        menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { on_toggle_visibility(); }, id_vis);
        menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { on_delete_feature(); },    id_del);
        if (art)
            menu.Bind(wxEVT_MENU, [this, row](wxCommandEvent&) { on_transform_imported(row); }, id_art);
        if (zoom)
            menu.Bind(wxEVT_MENU, [this, row](wxCommandEvent&) { zoom_to_feature(row); }, id_zoom);
        m_tree->PopupMenu(&menu, m_tree->ScreenToClient(screen));
    };

    m_tree->on_rename = [this](int row, const wxString& name) {
        if (row < 0 || row >= int(m_doc.features.size())) return;
        m_doc.features[row].name = std::string(name.ToUTF8().data());
        refresh_tree();
    };

    {
        wxBoxSizer* trow = m_hdr_tree_row;
        auto* up   = sidebar_icon_btn(m_tree_box, "design_moveup", _L("Move up"));
        up->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_move_feature(-1); });
        auto* down = sidebar_icon_btn(m_tree_box, "design_movedown", _L("Move down"));
        down->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_move_feature(+1); });
        m_btn_interfere = sidebar_icon_btn(m_tree_box, "color_palette", _L("Check interference — find overlapping bodies"));
        m_btn_interfere->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_check_interference(); });
        const int gap = FromDIP(SidebarProps::ElementSpacing());
        trow->Add(up,   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
        trow->Add(down, 0, wxALIGN_CENTER_VERTICAL);
        trow->AddSpacer(8);
        trow->Add(new wxStaticLine(m_tree_box, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(1, 22)), wxLI_VERTICAL),
                  0, wxALIGN_CENTER_VERTICAL);
        trow->AddSpacer(8);
        trow->Add(m_btn_interfere, 0, wxALIGN_CENTER_VERTICAL);
    }

    m_parts_box = make_card(m_form);
    auto* parts_inner = new wxBoxSizer(wxVERTICAL);
    m_parts_box->SetSizer(parts_inner);
    root->Add(m_parts_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    m_parts_hdr = new wxBoxSizer(wxHORIZONTAL);
    m_parts_hdr->Add(card_header(m_parts_box, "design_extrude", _L("Bodies"), m_parts_label), 0,
                     wxALIGN_CENTER_VERTICAL);
    {
        auto* bbool = sidebar_icon_btn(m_parts_box, "design_boolean", _L("Boolean — join, subtract or intersect with another body"));
        bbool->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_boolean_tool(); });
        auto* bcol  = sidebar_icon_btn(m_parts_box, "color_palette", _L("Color"));
        bcol->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_set_body_color(); });
        const int bgap = FromDIP(SidebarProps::ElementSpacing());
        m_parts_hdr->AddStretchSpacer(1);
        m_parts_hdr->Add(bbool, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, bgap);
        m_parts_hdr->Add(bcol,  0, wxALIGN_CENTER_VERTICAL);
    }
    parts_inner->Add(m_parts_hdr, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
              FromDIP(SidebarProps::ContentMargin()));
    m_parts_rule = new wxStaticLine(m_parts_box);
    parts_inner->Add(m_parts_rule, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
              FromDIP(SidebarProps::TitlebarMargin()));
    m_parts_hdr->ShowItems(false);
    m_parts_rule->Hide();
    m_parts = new DesignRowList(m_parts_box, 6);
    m_parts->SetBackgroundColour(dp_panel_bg());
    parts_inner->Add(m_parts, 0, wxEXPAND | wxALL, 12);
    m_parts->Hide();
    m_parts_label->Hide();
    auto apply_body_row = [this](int b) {
        if (!m_viewport || b < 0) return;
        if (m_tree) m_tree->unselect();
        m_viewport->select_body(b);
        m_sel_solid_body   = b;
        m_sel_solid_face   = m_sel_solid_edge = -1;
        m_sel_solid_vertex = false;
        m_pick_face = m_pick_face_body = -1;
        set_status(StatusKind::Info, wxString::Format(_L("Body %d selected — right-click for what applies to it"), b + 1));
    };
    m_parts->on_select = [this, apply_body_row] { apply_body_row(tree_body_selection()); };
    m_parts->on_action = [this, apply_body_row](int row, int id) {
        apply_body_row(row);
        switch (id) {
        case RowZoom:       zoom_to_body(row);      break;
        case RowMove:       on_move_body();         break;
        case RowVisibility: on_toggle_visibility(); break;
        case RowDelete:     on_delete_body();       break;
        }
    };
    m_parts->on_menu = [this, apply_body_row](int row, const wxPoint& screen) {
        apply_body_row(row);
        show_offer_menu(screen);
    };
    m_parts->on_rename = [this](int b, const wxString& name) {
        if (b < 0 || b >= int(m_doc.bodies.size())) return;
        m_doc.bodies[b].has_user_name = true;
        m_doc.bodies[b].user_name     = std::string(name.ToUTF8().data());
        sync_recipe_to_model();
        refresh_parts();
    };

    m_var_box = make_card(m_form);
    auto* var_inner = new wxBoxSizer(wxVERTICAL);
    m_var_box->SetSizer(var_inner);
    {
        auto* var_hdr = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText* var_hdr_title = nullptr;
        var_hdr->Add(card_header(m_var_box, "design_constrain", _L("Variables"), var_hdr_title), 0,
                     wxALIGN_CENTER_VERTICAL);
        var_hdr->AddStretchSpacer();
        m_btn_add_var  = sidebar_icon_btn(m_var_box, "add",           _L("Add variable"));
        m_btn_edit_var = sidebar_icon_btn(m_var_box, "design_edit",   _L("Edit variable"));
        m_btn_del_var  = sidebar_icon_btn(m_var_box, "design_delete", _L("Delete variable"));
        m_btn_add_var->Bind(wxEVT_BUTTON,  [this](wxCommandEvent&) { on_add_variable(); });
        m_btn_edit_var->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_edit_variable(); });
        m_btn_del_var->Bind(wxEVT_BUTTON,  [this](wxCommandEvent&) { on_remove_variable(); });
        const int vhgap = FromDIP(SidebarProps::ElementSpacing());
        var_hdr->Add(m_btn_add_var,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, vhgap);
        var_hdr->Add(m_btn_edit_var, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, vhgap);
        var_hdr->Add(m_btn_del_var,  0, wxALIGN_CENTER_VERTICAL);
        var_inner->Add(var_hdr, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
                       FromDIP(SidebarProps::ContentMargin()));
        var_inner->Add(new wxStaticLine(m_var_box), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
                       FromDIP(SidebarProps::TitlebarMargin()));

        m_var_list = new wxListCtrl(m_var_box, wxID_ANY, wxDefaultPosition,
                                    wxSize(-1, FromDIP(64)), wxLC_REPORT | wxLC_SINGLE_SEL);
        m_var_list->SetBackgroundColour(dp_panel_bg());
        m_var_list->AppendColumn(_L("Name"),       wxLIST_FORMAT_LEFT, FromDIP(90));
        m_var_list->AppendColumn(_L("Expression"), wxLIST_FORMAT_LEFT, FromDIP(120));
        var_inner->Add(m_var_list, 0, wxEXPAND | wxALL, FromDIP(SidebarProps::ContentMargin()));
    }
    root->Add(m_var_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    const int cm = FromDIP(SidebarProps::ContentMargin());

    m_status = new wxStaticText(m_form, wxID_ANY, "");
    m_status->Hide();
    root->Add(m_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    m_dof_status = new wxStaticText(m_form, wxID_ANY, "");
    {
        wxFont f = m_dof_status->GetFont();
        f.SetWeight(wxFONTWEIGHT_BOLD);
        m_dof_status->SetFont(f);
    }
    root->Add(m_dof_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);


    m_shape->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& e) { on_shape_changed(); e.Skip(); });
    on_shape_changed();

    m_form->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent& e) { refresh_preview(); e.Skip(); });
    m_form->Bind(wxEVT_COMBOBOX,         [this](wxCommandEvent& e)    { refresh_preview(); e.Skip(); });
    m_form->Bind(wxEVT_CHECKBOX,       [this](wxCommandEvent& e)    { refresh_preview(); e.Skip(); });
    m_form->Bind(wxEVT_TOGGLEBUTTON,   [this](wxCommandEvent& e)    { refresh_preview(); e.Skip(); });

    m_form->SetSizer(root);

    root->Show(m_parts_box, false, false);
    root->Show(m_cards,     false, false);
    cards->Show(m_box_sketch,  false, true);
    cards->Show(m_box_move,    false, true);
    cards->Show(m_box_extrude, false, true);
    cards->Show(m_box_revolve, false, true);
    cards->Show(m_box_sweep, false, true);
    cards->Show(m_box_pattern, false, true);
    cards->Show(m_box_plane, false, true);
    cards->Show(m_box_axis, false, true);
    cards->Show(m_box_coordsys, false, true);
    cards->Show(m_box_loft, false, true);
    cards->Show(m_box_draft, false, true);
    cards->Show(m_box_boolean, false, true);
    cards->Show(m_box_cut,     false, true);
    cards->Show(m_box_insert,  false, true);
    cards->Show(m_box_dressup, false, true);
    cards->Show(m_box_hole,    false, true);
    cards->Show(m_box_thread,  false, true);
    cards->Show(m_box_shell,   false, true);
    cards->Show(m_box_surf_extrude, false, true);
    cards->Show(m_box_surf_revolve, false, true);
    cards->Show(m_box_surf_loft,    false, true);
    cards->Show(m_box_surf_fill,    false, true);
    cards->Show(m_box_surf_offset,  false, true);
    cards->Show(m_box_surf_thicken, false, true);
    cards->Show(m_box_value,   false, true);
    cards->Show(m_box_sketch_session, false, true);
    cards->Show(m_box_constraints,    false, true);
    cards->Show(m_box_expr,          false, true);
    cards->Show(m_box_transform,   false, true);
    cards->Show(m_box_mirror,      false, true);
    cards->Show(m_box_thicken,     false, true);
    cards->Show(m_box_rib,         false, true);
    cards->Show(m_box_project,     false, true);
    cards->Show(m_box_delete_face, false, true);
    cards->Show(m_box_helix,       false, true);
    cards->Show(m_box_mate,        false, true);

    m_form->FitInside();
    m_form->SetScrollRate(0, FromDIP(20));
    m_form->SetMinSize(FromDIP(wxSize(264, -1)));

    build_phase("sidebar and tool cards");
    auto* view_col = new wxPanel(body_panel, wxID_ANY);
    view_col->SetBackgroundColour(dp_panel_bg());
    m_viewport = new DesignCanvas(view_col);
    build_phase("3D canvas");

    m_viewport->set_on_sketch_commit([this](const SketchProfile& prof, const SketchPlane& plane) {
        m_doc.checkpoint();
        m_feature_counter++;
        m_doc.add_sketch_profile(prof, plane, feature_name(_L("Sketch")));
        m_doc.recompute();
        set_status(StatusKind::Info, _L("Sketch created — select it, then right-click to Extrude"));
        refresh_tree();
    });

    m_viewport->set_on_sketch_entities_commit(
        [this](const std::vector<SketchEntity>& ents,
               const std::vector<SketchEntityConstraintDef>& cons,
               const SketchPlane& plane) {
            if (ents.empty()) {
                set_status(StatusKind::Error, _L("Sketch empty — nothing committed"));
                return;
            }
            m_doc.checkpoint();
            if (m_edit_index >= 0 && m_edit_index < int(m_doc.features.size()) &&
                m_doc.features[m_edit_index].type == CadFeatureType::Sketch) {
                CadFeature edited = m_doc.features[m_edit_index];
                edited.entities           = ents;
                edited.entity_constraints = cons;
                edited.plane              = plane;
                if (m_doc.replace_feature(m_edit_index, edited)) {
                    if (!cons.empty()) m_doc.solve_sketch_feature(m_edit_index);
                    m_doc.recompute();
                    set_status(StatusKind::Info, _L("Sketch updated"));
                    m_edit_index = -1;
                    refresh_tree();
                    sync_sketch_display();
                }
                return;
            }
            m_feature_counter++;
            const int sk = m_doc.add_sketch_entities(ents, plane,
                               feature_name(_L("Sketch")), cons);
            if (!cons.empty()) m_doc.solve_sketch_feature(sk);
            m_doc.recompute();
            set_status(StatusKind::Info, cons.empty()
                ? _L("Sketch created — select it, then right-click to Extrude")
                : wxString::Format(_L("Sketch created (%zu driving dims) — select it, then right-click to Extrude"),
                                   cons.size()));
            refresh_tree();
            sync_sketch_display();
        });

    m_viewport->set_on_cursor_metrics([this](double len, double ang_deg, bool locked) {
        double a = ang_deg; if (a < 0.0) a += 360.0;
        wxString metrics = wxString::Format(_L("Length %.2f mm, angle %.1f°"), len, a);
        if (locked) metrics += "  " + _L("(locked)");
        set_status(m_sketch_step.IsEmpty() ? metrics
                                           : m_sketch_step + L"   ·   " + metrics);
        m_status->Refresh();
    });

    m_viewport->set_on_sketch_step([this](DesignSketchTool::Mode mode, int step, int picks) {
        on_sketch_step(int(mode), step, picks);
    });

    m_viewport->set_on_sketch_constraints_changed([this]() {
        CallAfter([this]() { rebuild_constraint_list(); });
    });

    m_viewport->set_on_solve_state([this](int dof, bool ok, bool has_constraints) {
        m_dof_last = dof; m_dof_last_ok = ok; m_dof_last_has = has_constraints;
        apply_dof_status(dof, ok, has_constraints);
        update_undo_redo_buttons();
    });

    m_viewport->set_on_sketch_face_selected([this](int region) {
        if (!m_viewport) return;
        m_viewport->finish_sketch();
        m_extrude_sketch_ref = resolve_extrude_sketch();
        if (m_extrude_sketch_ref < 0) {
            set_status(StatusKind::Error, _L("Could not resolve the sketch to extrude"));
            return;
        }
        set_ui_mode(UiMode::Feature);
        open_tool(Tool::Extrude);
        m_sel_solid_face = m_sel_solid_edge = -1;
        m_pick_face = m_pick_face_body = -1;
        m_viewport->set_loop_pick(m_extrude_sketch_ref, region);
        set_status(StatusKind::Info, _L("Face selected — set the depth and Confirm"));
    });

    m_viewport->set_on_display_sketch_selected([this](int feat, int region, int entity) {
        if (feat < 0 || feat >= int(m_doc.features.size())) return;
        m_sel_solid_face = m_sel_solid_edge = -1;
        m_pick_face = m_pick_face_body = -1;
        if (m_active == Tool::Rib && entity >= 0) {
            if (m_rib_sketch != nullptr)
                for (unsigned i = 0; i < m_rib_sketch->GetCount(); ++i)
                    if (int(reinterpret_cast<intptr_t>(m_rib_sketch->GetClientData(i))) == feat) {
                        m_rib_sketch->SetSelection(i);
                        break;
                    }
            if (m_rib_entity != nullptr) m_rib_entity->SetValue(entity);
            refresh_preview();
        }
        if (m_active == Tool::Sweep && m_sweep_path != nullptr) {
            for (unsigned i = 0; i < m_sweep_path->GetCount(); ++i) {
                if (int(reinterpret_cast<intptr_t>(m_sweep_path->GetClientData(i))) != feat) continue;
                m_sweep_path->SetSelection(i);
                m_sweep_path_ref = feat;
                refresh_preview();
                break;
            }
        }
        if (m_active == Tool::Loft && m_loft_list != nullptr) {
            for (size_t i = 0; i < m_loft_sketch_idx.size() && i < m_loft_list->GetCount(); ++i) {
                if (m_loft_sketch_idx[i] != feat) continue;
                m_loft_list->Check(unsigned(i), !m_loft_list->IsChecked(unsigned(i)));
                refresh_preview();
                break;
            }
        }
        set_tree_selection(feat);
        set_status(StatusKind::Info, region >= 0
            ? _L("Region selected — right-click to Extrude, or double-click to edit")
            : _L("Sketch selected — right-click to Extrude, or double-click to edit"));
    });

    m_viewport->set_on_display_sketch_activated([this](int feat) {
        if (feat < 0 || feat >= int(m_doc.features.size())) return;
        set_tree_selection(feat);
        on_edit_feature();
    });

    m_viewport->set_on_solid_selection_changed([this](int level, int body, int face, int edge) {
        if (level >= 1 && m_active == Tool::None && m_tree) m_tree->unselect();
        m_sel_solid_body = (level >= 1) ? body : -1;
        m_sel_solid_face = (level == 2) ? face : -1;
        m_sel_solid_edge = (level == 3) ? edge : -1;
        m_sel_solid_edges = (level == 3) ? m_viewport->selected_solid_edges() : std::vector<int>();
        m_sel_solid_vertex = (level == 4);
        m_pick_face_body = (level >= 1) ? body : -1;
        m_pick_face      = (level >= 1) ? face : -1;
        if (level >= 1) {
            m_viewport->clear_loop_pick();
            m_plane_picked = false;
        }
        if (sketch_plane_pick_live()) {
            wxString on;
            if (level >= 1 && sketch_plane_target(on)) {
                start_sketch_on_target(true);
                return;
            }
            set_status(StatusKind::Info, level >= 1 && m_pick_face >= 0
                ? _L("That face is not flat — click a flat face or a reference plane")
                : sketch_plane_prompt());
            m_status->Refresh();
            return;
        }
        if (m_active == Tool::Dressup) { sync_dressup_target(); update_fillet_gizmo(); refresh_preview(); }
        if (m_active == Tool::Boolean && m_sel_solid_body >= 0) {
            ComboBox* dst   = (m_bool_next_slot == 0) ? m_bool_target : m_bool_tool;
            ComboBox* other = (m_bool_next_slot == 0) ? m_bool_tool   : m_bool_target;
            const int row   = m_sel_solid_body;
            if (dst != nullptr && row < int(dst->GetCount())) {
                const int prev = dst->GetSelection();
                dst->SetSelection(row);
                if (other != nullptr && other->GetSelection() == row && prev >= 0 && prev != row)
                    other->SetSelection(prev);
                m_bool_next_slot ^= 1;
                refresh_preview();
            }
        }
        if (m_active == Tool::Mirror && m_sel_solid_body >= 0 && m_mirror_body != nullptr &&
            m_sel_solid_body < int(m_mirror_body->GetCount())) {
            m_mirror_body->SetSelection(m_sel_solid_body);
            refresh_preview();
        }
        if (m_active == Tool::Shell) {
            m_shell_face_label->SetLabel(m_sel_solid_face >= 0
                ? wxString::Format(_L("Face %d"), m_sel_solid_face)
                : _L("(all faces — closed hollow)"));
            refresh_preview();
        }
        if (m_active == Tool::Thicken) {
            m_thicken_face_label->SetLabel(m_sel_solid_face >= 0
                ? wxString::Format(_L("Face %d"), m_sel_solid_face)
                : _L("(pick a solid face)"));
            refresh_preview();
        }
        if (m_active == Tool::Draft) {
            m_draft_face_label->SetLabel(m_sel_solid_face >= 0
                ? wxString::Format(_L("Face %d"), m_sel_solid_face)
                : _L("(pick a side face)"));
            refresh_preview();
        }
        if (m_active == Tool::Hole && face >= 0 && body >= 0 && body < int(m_doc.bodies.size())) {
            const TopoDS_Face fc = GeometryEngine::face_by_index(m_doc.bodies[body].shape, face);
            if (!fc.IsNull()) {
                m_hole_face_plane = face_plane_inward(fc);
                m_hole_on_face    = true;
                m_hole_face_body  = body;
                set_hole_target_label(face);
                m_hole_has_bounds = GeometryEngine::face_plane_bounds(
                    fc, m_hole_face_plane.origin, m_hole_face_plane.x_axis,
                    m_hole_face_plane.y_axis, m_hole_umin, m_hole_umax, m_hole_vmin, m_hole_vmax);
                if (m_hole_x) m_hole_x->SetValue(0.0);
                if (m_hole_y) m_hole_y->SetValue(0.0);
                if (m_hole_plane) m_hole_plane->SetSelection(index_from_plane(m_hole_face_plane));
                refresh_preview();
            }
        }
        if (m_active == Tool::Thread && body >= 0 && body < int(m_doc.bodies.size())) {
            const TopoDS_Shape& shape = m_doc.bodies[body].shape;
            GeometryEngine::CylinderFace cf;
            if (face >= 0)
                cf = GeometryEngine::cylinder_of_face(GeometryEngine::face_by_index(shape, face));
            const bool from_face = cf.ok;
            if (!cf.ok && edge >= 0)
                cf = GeometryEngine::circle_of_edge(GeometryEngine::edge_by_index(shape, edge));
            if (cf.ok) {
                SketchPlane p; p.origin = cf.base; p.normal = cf.axis;
                const Vec3d ref = std::abs(cf.axis.z()) < 0.9 ? Vec3d(0, 0, 1) : Vec3d(1, 0, 0);
                p.x_axis = ref.cross(cf.axis).normalized();
                p.y_axis = cf.axis.cross(p.x_axis).normalized();
                m_thread_face_plane = p;
                m_thread_on_face    = true;
                m_thread_face_body  = m_sel_solid_body;
                set_thread_target_label(from_face ? face : -1, from_face ? -1 : edge);
                if (m_thread_internal) m_thread_internal->SetValue(cf.internal);
                infer_thread_spec(2.0 * cf.radius, cf.internal);
                if (m_thread_height && cf.height > 1e-6) m_thread_height->SetValue(cf.height);
                refresh_preview();
            }
        }
        if (m_active == Tool::Plane && m_plane_pick != PlanePick::None) {
            bool got = false;
            switch (m_plane_pick) {
            case PlanePick::FaceA: if (m_sel_solid_face >= 0) { m_pl_faceA_body = m_sel_solid_body; m_pl_faceA = m_sel_solid_face; got = true; } break;
            case PlanePick::FaceB: if (m_sel_solid_face >= 0) { m_pl_faceB_body = m_sel_solid_body; m_pl_faceB = m_sel_solid_face; got = true; } break;
            case PlanePick::EdgeA: if (m_sel_solid_edge >= 0) { m_pl_edgeA_body = m_sel_solid_body; m_pl_edgeA = m_sel_solid_edge; got = true; } break;
            case PlanePick::EdgeB: if (m_sel_solid_edge >= 0) { m_pl_edgeB_body = m_sel_solid_body; m_pl_edgeB = m_sel_solid_edge; got = true; } break;
            default: break;
            }
            if (got) { m_plane_pick = PlanePick::None; refresh_plane_labels(); }
            if (got && m_viewport) m_viewport->set_escalate_on_repick(true);
        }
        if (m_active == Tool::Axis && m_axis_pick != AxisPick::None) {
            bool got = false;
            switch (m_axis_pick) {
            case AxisPick::Face: if (m_sel_solid_face >= 0) { m_ax_face_body = m_sel_solid_body; m_ax_face = m_sel_solid_face; got = true; } break;
            case AxisPick::Edge: if (m_sel_solid_edge >= 0) { m_ax_face_body = m_sel_solid_body; m_ax_edge = m_sel_solid_edge; got = true; } break;
            default: break;
            }
            if (got) { m_axis_pick = AxisPick::None; refresh_axis_labels(); }
            if (got && m_viewport) m_viewport->set_escalate_on_repick(true);
        }
        if (m_active == Tool::CoordSys && m_coordsys_pick != CoordSysPick::None) {
            bool got = false;
            switch (m_coordsys_pick) {
            case CoordSysPick::Face: if (m_sel_solid_face >= 0) { m_cs_face_body = m_sel_solid_body; m_cs_face = m_sel_solid_face; got = true; } break;
            case CoordSysPick::Edge: if (m_sel_solid_edge >= 0) { m_cs_face_body = m_sel_solid_body; m_cs_edge = m_sel_solid_edge; got = true; } break;
            default: break;
            }
            if (got) {
                if (m_coordsys_type && m_coordsys_type->GetSelection() != (int)CoordSysType::FaceAndDirection)
                    m_coordsys_type->SetSelection((int)CoordSysType::FaceAndDirection);
                m_coordsys_pick = CoordSysPick::None;
                refresh_coordsys_labels();
            }
            if (got && m_viewport) m_viewport->set_escalate_on_repick(true);
        }
        const int nb = int(m_doc.bodies.size());
        const bool bodytag = nb > 1;
        set_status(level == 4 ? (bodytag ? wxString::Format(_L("Body %d: vertex selected — click again for the whole body"), body + 1)
                                         : _L("Vertex selected — click again for the whole body"))
                 : level == 1 ? (bodytag ? wxString::Format(_L("Body %d selected (whole body) — right-click for what applies to it"), body + 1)
                                         : _L("Whole body selected — right-click for what applies to it"))
                 : level == 2 ? (bodytag ? wxString::Format(_L("Body %d: face %d selected — right-click to push/pull it, or click again for the whole body"), body + 1, face)
                                         : wxString::Format(_L("Face %d selected — right-click to push/pull it, or click again for the whole body"), face))
                 : level == 3 && m_sel_solid_edges.size() > 1
                              ? (bodytag ? wxString::Format(_L_PLURAL("Body %d: %zu edge selected — Shift+click adds or removes one, Fillet/Chamfer dresses them all",
                                                                      "Body %d: %zu edges selected — Shift+click adds or removes one, Fillet/Chamfer dresses them all",
                                                                      unsigned(m_sel_solid_edges.size())), body + 1, m_sel_solid_edges.size())
                                         : wxString::Format(_L_PLURAL("%zu edge selected — Shift+click adds or removes one, Fillet/Chamfer dresses them all",
                                                                      "%zu edges selected — Shift+click adds or removes one, Fillet/Chamfer dresses them all",
                                                                      unsigned(m_sel_solid_edges.size())), m_sel_solid_edges.size()))
                 : level == 3 ? (bodytag ? wxString::Format(_L("Body %d: edge %d selected — Shift+click to add edges, or click again for the whole body"), body + 1, edge)
                                         : wxString::Format(_L("Edge %d selected — Shift+click to add edges, or click again for the whole body"), edge))
                              : _L("Nothing selected"));
        m_status->Refresh();
    });
    m_viewport->set_on_empty_pick([this] {
        if (m_active != Tool::None) return;
        const bool row = deselect_rows();
        if (drop_plane_pick() || row)
            set_status(StatusKind::Info, _L("Nothing selected"));
    });

    m_viewport->set_on_extrude_depth_changed([this](double depth, bool second) {
        if (m_active == Tool::SurfaceExtrude) {
            if (m_surf_extrude_distance) m_surf_extrude_distance->SetValue(depth);
        } else if (m_active == Tool::Thicken) {
            if (m_thicken_thickness) m_thicken_thickness->SetValue(depth);
        } else if (m_active == Tool::Rib) {
            if (m_rib_depth) m_rib_depth->SetValue(depth);
        } else if (m_active == Tool::SurfaceOffset) {
            if (m_surf_offset_distance) m_surf_offset_distance->SetValue(depth);
        } else if (m_active == Tool::ThickenSurface) {
            if (m_surf_thicken_thickness) m_surf_thicken_thickness->SetValue(depth);
        } else if (second) { if (m_distance2) m_distance2->SetValue(depth); }
        else               { if (m_distance)  m_distance->SetValue(depth); }
        refresh_preview();
    });

    m_viewport->set_on_datum_size_changed([this](double u, double v) {
        if (m_plane_usize) m_plane_usize->SetValue(u);
        if (m_plane_vsize) m_plane_vsize->SetValue(v);
        if (m_edit_index >= 0 && m_edit_index < int(m_doc.features.size()) &&
            m_doc.features[m_edit_index].type == CadFeatureType::Plane) {
            m_doc.features[m_edit_index].plane_u_size = u;
            m_doc.features[m_edit_index].plane_v_size = v;
            refresh_datum_planes();
        }
        m_viewport->request_repaint();
    });

    m_viewport->set_on_datum_offset_changed([this](double off) {
        if (m_plane_offset) m_plane_offset->SetValue(off);
        if (m_edit_index >= 0 && m_edit_index < int(m_doc.features.size()) &&
            m_doc.features[m_edit_index].type == CadFeatureType::Plane) {
            m_doc.features[m_edit_index].plane_offset = off;
            refresh_datum_planes();
        }
        m_viewport->request_repaint();
    });

    m_viewport->set_on_helix_changed([this](double radius, double pitch, double height) {
        if (m_helix_radius) m_helix_radius->SetValue(radius);
        if (m_helix_pitch)  m_helix_pitch->SetValue(pitch);
        if (m_helix_height) m_helix_height->SetValue(height);
        update_helix_gizmo();
        m_viewport->request_repaint();
    });

    m_viewport->set_on_rib_thickness_changed([this](double thickness) {
        if (m_rib_thickness) m_rib_thickness->SetValue(thickness);
        refresh_preview();
    });

    m_viewport->set_on_datum_base_picked([this](int base) {
        if (m_active == Tool::Plane) {
            if (m_plane_base && base >= 0 && base < int(m_plane_base->GetCount()))
                m_plane_base->SetSelection(base);
            m_pl_faceA_body = m_pl_faceA = -1;
            refresh_plane_labels();
            refresh_preview();
        } else {
            if (base >= 0) {
                m_ref_plane = base;
                m_plane_picked = true;
                m_pick_face = m_pick_face_body = -1;
            }
            if (base >= 0 && sketch_plane_pick_live()) {
                start_sketch_on_target(true);
                return;
            }
            const wxString nm = ref_plane_name(base);
            set_status(StatusKind::Ok,
                wxString::Format(_L("%s plane selected — right-click to sketch on it, "
                                    "or click an object to select it"), nm));
            m_status->Refresh();
        }
    });
    m_viewport->set_selected_base([this] {
        if (m_active == Tool::Plane)
            return m_plane_base != nullptr && m_pl_faceA < 0 ? m_plane_base->GetSelection() : -1;
        const bool face = (m_sel_solid_face >= 0 && m_sel_solid_body >= 0) || (m_pick_face >= 0 && m_pick_face_body >= 0);
        return m_plane_picked && !face ? m_ref_plane : -1;
    });

    m_viewport->set_on_body_move_changed([this](int body, const Transform3d& xform) {
        sync_body_xform();
        if (body < 0 || body >= int(m_body_xform.size())) return;
        m_body_xform[body] = xform;
        feed_bodies();
        if (body == m_xf_gizmo_body && m_active == Tool::Transform) {
            const Transform3d d = xform * m_xf_gizmo_base.inverse();
            const Vec3d       t = d.translation();
            if (m_xf_dx) m_xf_dx->SetValue(t.x());
            if (m_xf_dy) m_xf_dy->SetValue(t.y());
            if (m_xf_dz) m_xf_dz->SetValue(t.z());
            const Eigen::AngleAxisd aa(Eigen::Quaterniond(d.linear()).normalized());
            int ax = 0; double best = std::abs(aa.axis().x());
            if (std::abs(aa.axis().y()) > best) { ax = 1; best = std::abs(aa.axis().y()); }
            if (std::abs(aa.axis().z()) > best) { ax = 2; best = std::abs(aa.axis().z()); }
            const double sign = (aa.axis()[ax] < 0.0) ? -1.0 : 1.0;
            if (m_xf_axis)  m_xf_axis->SetSelection(ax);
            if (m_xf_angle) m_xf_angle->SetValue(sign * aa.angle() * 180.0 / M_PI);
        }
        const int nb = int(m_doc.bodies.size());
        const Vec3d t = xform.translation();
        set_status(StatusKind::Info, nb > 1
            ? wxString::Format(_L("Body %d placed at (%.1f, %.1f, %.1f) mm — drag arrows to move, rings to rotate"),
                               body + 1, t.x(), t.y(), t.z())
            : wxString::Format(_L("Placed at (%.1f, %.1f, %.1f) mm — drag arrows to move, rings to rotate"),
                               t.x(), t.y(), t.z()));
    });

    m_viewport->set_on_fillet_radius_changed([this](double radius) {
        if (m_dressup_size) m_dressup_size->SetValue(radius);
        refresh_preview();
    });

    m_viewport->set_on_hole_changed([this](double x, double y, double diameter, double depth) {
        if (m_hole_x)        m_hole_x->SetValue(x);
        if (m_hole_y)        m_hole_y->SetValue(y);
        if (m_hole_diameter) m_hole_diameter->SetValue(diameter);
        if (m_hole_depth)    m_hole_depth->SetValue(depth);
        refresh_preview();
    });

    m_viewport->set_on_thread_changed([this](double x, double y, double radius, double height) {
        if (m_thread_x)      m_thread_x->SetValue(x);
        if (m_thread_y)      m_thread_y->SetValue(y);
        if (m_thread_radius) m_thread_radius->SetValue(2.0 * radius);
        if (m_thread_height) m_thread_height->SetValue(height);
        refresh_preview();
    });

    m_viewport->set_on_shell_thickness_changed([this](double thickness) {
        if (m_shell_thickness) m_shell_thickness->SetValue(thickness);
        refresh_preview();
    });

    m_viewport->set_on_revolve_angle_changed([this](double angle) {
        if (m_revolve_angle) m_revolve_angle->SetValue(angle);
        refresh_preview();
    });

    m_viewport->set_on_draft_angle_changed([this](double angle) {
        if (m_draft_angle) m_draft_angle->SetValue(angle);
        refresh_preview();
    });

    m_viewport->set_on_cut_offset_changed([this](double v) {
        if (m_cut_offset) m_cut_offset->SetValue(v);
        refresh_preview();
    });

    m_viewport->set_on_pattern_changed([this](double value) {
        if (m_pattern_type && m_pattern_type->GetSelection() == 1) {
            if (m_pattern_angle) m_pattern_angle->SetValue(value);
        } else if (m_pattern_spacing) {
            m_pattern_spacing->SetValue(value);
        }
        refresh_preview();
    });

    m_viewport->set_on_sketch_exit([this]() {
        if (m_active == Tool::Insert) { finalize_insert(); return; }
        if (m_viewport) m_viewport->cancel_sketch();
        m_edit_index = -1;
        set_ui_mode(UiMode::Feature);
        sync_sketch_display();
        refresh_tree();
        set_status(StatusKind::Info, _L("Tool exited"));
    });

    m_viewport->set_on_sketch_notice([this](const std::string& msg, bool error) {
        set_status(error ? StatusKind::Error : StatusKind::Info, wxString::FromUTF8(msg));
    });
    m_viewport->set_on_sketch_exit_refused([this]() {
        set_status(StatusKind::Info, _L("Sketch kept — Confirm to commit it, Cancel to discard"));
    });

    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
        const int  key  = e.GetKeyCode();
        const bool ctrl = e.ControlDown() || e.CmdDown();
        const bool sketching = (m_ui_mode == UiMode::Sketch) && m_viewport && m_viewport->is_sketching();
        const bool sketch_mode = (m_ui_mode == UiMode::Sketch);
        const bool in_text = (dynamic_cast<wxTextCtrl*>(wxWindow::FindFocus()) != nullptr)
                             || (m_viewport && m_viewport->inline_busy());
        if (getenv("ORCA_CAD_KEYTRACE")) {
            wxWindow* fw = wxWindow::FindFocus();
            fprintf(stderr, "[KEYTRACE] key=%d ui_mode=%d is_sketching=%d in_text=%d inline_busy=%d focus=%s\n",
                    key, int(m_ui_mode), (m_viewport && m_viewport->is_sketching()) ? 1 : 0, in_text ? 1 : 0,
                    (m_viewport && m_viewport->inline_busy()) ? 1 : 0,
                    fw ? wxString(fw->GetClassInfo()->GetClassName()).utf8_str().data() : "(none)");
            fflush(stderr);
        }

        if (m_viewport && m_viewport->inline_busy() && !m_viewport->inline_has_focus()) {
            if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER || key == WXK_TAB) {
                m_viewport->inline_commit();
                return;
            }
        }

        if (key == WXK_ESCAPE) { escape(); return; }

        if (!in_text && !ctrl && (key == WXK_RETURN || key == WXK_NUMPAD_ENTER)) {
            if (sketch_mode && m_viewport && m_viewport->is_sketching()) {
                if (m_viewport->sketch_confirm_pending()) { update_undo_redo_buttons(); return; }
                if (m_viewport->sketch_disarm_tool()) { set_status(_L("Select")); return; }
            }
            if (confirm_enabled()) { tool_confirm(); return; }
        }

        if (!in_text && !ctrl
            && (key == WXK_MENU || key == WXK_WINDOWS_MENU || (key == WXK_F10 && e.ShiftDown()))) {
            CallAfter([this] { show_offer_menu(offer_anchor()); });
            return;
        }

        if (!in_text && ctrl && (key == 'Z' || key == 'z' || key == WXK_CONTROL_Z ||
                                 key == 'Y' || key == 'y' || key == WXK_CONTROL_Y)) {
            const bool redo = (key == 'Y' || key == 'y' || key == WXK_CONTROL_Y) || e.ShiftDown();
            do_undo_redo(redo);
            return;
        }
        if (!in_text && (key == WXK_DELETE || (key == WXK_BACK && sketching))) {
            if (sketching) {
                if (!m_viewport->delete_selected_sketch_entities())
                    set_status(_L("Select something to delete"));
                return;
            }
            if (m_ui_mode == UiMode::Feature && m_active == Tool::None
                && tree_selection() != wxNOT_FOUND) { on_delete_feature(); return; }
        }
        if (!in_text && !sketch_mode && m_section_on) {
            if (key == WXK_PAGEUP || key == WXK_PAGEDOWN) {
                m_section_cut_z += (key == WXK_PAGEUP ? 2.0 : -2.0);
                if (m_viewport) m_viewport->set_section_plane(true, m_section_cut_z, m_section_upper);
                return;
            }
            if (key == 'F' || key == 'f') { flip_section_view(); return; }
        }
        const bool inline_field_only = (dynamic_cast<wxTextCtrl*>(wxWindow::FindFocus()) == nullptr)
                                       && m_viewport && m_viewport->inline_busy();
        if (in_text && inline_field_only && sketch_mode && !ctrl) {
            const int up2 = (key >= 'a' && key <= 'z') ? key - 'a' + 'A' : key;
            auto it2 = m_keys_sketch.find(up2);
            if (it2 != m_keys_sketch.end()) { it2->second(); return; }
        }
        if (!in_text && ctrl && e.ShiftDown()) {
            const int up = (key >= 'a' && key <= 'z') ? key - 'a' + 'A' : key;
            auto it = m_keys_feature.find(up | SC_SHIFT | SC_CTRL);
            if (it != m_keys_feature.end()) { it->second(); return; }
        }
        if (!in_text && !ctrl) {
            const int up = (key >= 'a' && key <= 'z') ? key - 'a' + 'A' : key;
            if (sketch_mode) {
                auto it = m_keys_sketch.find(up);
                if (it != m_keys_sketch.end()) { it->second(); return; }
            } else {
                auto it = m_keys_feature.find(up | (e.ShiftDown() ? SC_SHIFT : 0));
                if (it != m_keys_feature.end()) { it->second(); return; }
            }
        }
        e.Skip();
    });

    m_viewport->set_on_context_menu([this](const wxPoint& p) { show_offer_menu(p); });

    m_viewport->set_on_imported_transform([this](int feat, Vec2d off, double sx, double sy) {
        if (feat < 0 || feat >= int(m_doc.features.size())) return;
        CadFeature& f = m_doc.features[feat];
        if (f.imported_regions.empty()) return;
        f.import_offset  = off;
        f.import_scale_x = sx;
        f.import_scale_y = sy;
        m_doc.recompute();
        sync_sketch_display();
    });

    auto* vcol = new wxBoxSizer(wxVERTICAL);
    m_sketch_banner = new wxPanel(view_col, wxID_ANY);
    m_sketch_banner->SetBackgroundColour(wxColour(0, 122, 116));
    m_sketch_banner_txt = new wxStaticText(m_sketch_banner, wxID_ANY, wxString());
    m_sketch_banner_txt->SetForegroundColour(*wxWHITE);
    {
        wxFont f = m_sketch_banner_txt->GetFont();
        f.SetWeight(wxFONTWEIGHT_BOLD);
        m_sketch_banner_txt->SetFont(f);
        auto* bs = new wxBoxSizer(wxHORIZONTAL);
        bs->Add(m_sketch_banner_txt, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(6));
        m_sketch_banner->SetSizer(bs);
    }
    m_sketch_banner->Hide();
    vcol->Add(m_sketch_banner, 0, wxEXPAND);
    vcol->Add(m_viewport, 1, wxEXPAND);
    view_col->SetSizer(vcol);

    m_aui.init(body_panel);
    m_aui.AddPane(m_form, AuiMgr::sidebar_pane_info().MinSize(m_form->GetMinSize()));
    m_aui.AddPane(view_col, wxAuiPaneInfo().Name("main").CenterPane().PaneBorder(false));
    m_default_layout = m_aui.SavePerspective();
    load_window_layout();
    m_aui.track_docked_size(m_form);
    m_viewport->set_sidebar_collapse(
        [this] {
            const wxAuiPaneInfo& sidebar = m_aui.GetPane(m_form);
            if (!sidebar.IsOk() || sidebar.IsFloating())
                return CollapseSide::None;
            return sidebar.dock_direction == wxAUI_DOCK_RIGHT ? CollapseSide::Right : CollapseSide::Left;
        },
        [this] { CallAfter([this] { collapse_sidebar(!m_sidebar_collapsed); }); });
    collapse_sidebar(m_sidebar_collapsed);

    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(m_toolbar, 0, wxEXPAND);
    outer->Add(new wxStaticLine(this, wxID_ANY), 0, wxEXPAND);
    outer->Add(body_panel, 1, wxEXPAND);
    SetSizer(outer);

    {
        std::vector<std::string> dead;
        for (int i = 0; i < kOfferVerbCount; ++i) {
            const OfferVerb& v = kOfferVerbs[i];
            if (v.action == nullptr || *v.action == '\0')
                continue;
            const std::string a(v.action);
            bool ok = false;
            if (a.rfind("key:", 0) == 0) {
                const std::string k = a.substr(4);
                if (k.size() >= 3 && k[0] == 'S' && k[1] == '+') {
                    auto it = m_keys_feature.find(int(k[2]) | SC_SHIFT);
                    ok = it != m_keys_feature.end() && bool(it->second);
                } else if (!k.empty()) {
                    auto it = m_keys_sketch.find(int(k[0]));
                    ok = it != m_keys_sketch.end() && bool(it->second);
                }
            } else {
                auto it = m_verb_actions.find(a);
                ok = it != m_verb_actions.end() && bool(it->second);
            }
            if (!ok)
                dead.emplace_back(std::string(v.id) + " -> " + a);
        }
        for (const std::string& d : dead)
            BOOST_LOG_TRIVIAL(error) << "Design offer: wired verb has no action: " << d;
        assert(dead.empty());
    }

    m_show_origin = wxGetApp().app_config->get_bool("design_show_origin");
    m_show_bed    = wxGetApp().app_config->get_bool("design_show_bed");
    if (m_viewport) m_viewport->set_show_bed(m_show_bed);
    set_ui_mode(UiMode::Feature);
    refresh_pinned();
    build_phase("wiring");
    BOOST_LOG_TRIVIAL(info) << "Design tab build: total " << build_clock.Time() << " ms";
}

void DesignPanel::set_active_tool_btn(ScalableButton* b)
{
    m_active_tool_btn = b;
    const wxColour bg = dp_ribbon_bg(), teal(0x00, 0x96, 0x88);
    for (auto* btn : m_tool_btns) {
        if (btn == nullptr) continue;
        btn->SetBackgroundColour(btn == b ? teal : bg);
        btn->Refresh();
    }
}

void DesignPanel::apply_dof_status(int dof, bool ok, bool has_constraints)
{
    if (!m_dof_status) return;
    if (!has_constraints) {
        m_dof_status->SetLabel(wxString());
    } else if (!ok) {
        m_dof_status->SetForegroundColour(wxColour(235, 80, 80));
        m_dof_status->SetLabel(_L("✗ Conflicting constraints"));
    } else if (dof == 0) {
        m_dof_status->SetForegroundColour(wxColour(80, 200, 110));
        m_dof_status->SetLabel(_L("✓ Fully constrained"));
    } else if (dof > 0) {
        m_dof_status->SetForegroundColour(dp_ctl_text());
        m_dof_status->SetLabel(wxString::Format(_L_PLURAL("%d degree of freedom", "%d degrees of freedom", dof), dof));
    } else {
        m_dof_status->SetLabel(wxString());
    }
    m_dof_status->Show(!m_dof_status->GetLabel().IsEmpty());
    m_dof_status->Refresh();
    update_cards_frame(); m_form->Layout();
}

void DesignPanel::set_ui_mode(UiMode m)
{
    if (m != UiMode::Feature) end_body_move(true);
    m_ui_mode = m;
    m_choosing_sketch_plane = false;
    if (m != UiMode::Sketch) m_sketch_on.clear();
    if (m == UiMode::Sketch && m_viewport != nullptr) m_viewport->clear_loop_pick();
    if (m == UiMode::Feature && m_dof_status != nullptr) {
        m_dof_status->SetLabel(wxString());
        m_dof_status->Show(false);
    }
    if (m == UiMode::Constrain)
        apply_dof_status(m_dof_last, m_dof_last_ok, m_dof_last_has);
    wxSizer* s = m_toolbar->GetSizer();
    s->Show(m_tb_feature,   m == UiMode::Feature,   true);
    s->Show(m_tb_sketch,    m == UiMode::Sketch,    true);
    s->Show(m_tb_relations, m == UiMode::Sketch || m == UiMode::Constrain, true);
    m_toolbar->Layout();
    m_toolbar->FitInside();
    set_active_tool_btn(nullptr);
    if (m_box_sketch_session != nullptr && m_form != nullptr && m_cards->GetSizer() != nullptr) {
        if (m == UiMode::Sketch && m_hdr_sketch_session != nullptr)
            m_hdr_sketch_session->SetLabel(wxString::Format(_L("Sketch %d"), m_feature_counter + 1));
        m_cards->GetSizer()->Show(m_box_sketch_session, m == UiMode::Sketch, true);
        update_cards_frame(); m_form->Layout();
        m_form->FitInside();
    }
    if (m_box_constraints != nullptr && m_form != nullptr && m_cards->GetSizer() != nullptr) {
        if (m == UiMode::Constrain || m == UiMode::Sketch)
            rebuild_constraint_list();
        else
            m_cards->GetSizer()->Show(m_box_constraints, false, true);
        update_cards_frame(); m_form->Layout();
        m_form->FitInside();
    }
    update_action_bar();
    update_reference_planes();

    if (m_sketch_banner != nullptr) {
        const bool sketching = (m == UiMode::Sketch);
        if (sketching && m_sketch_banner_txt != nullptr)
            m_sketch_banner_txt->SetLabel(
                wxString::Format(_L("Editing: Sketch %d   ·   N = look normal to the plane   ·   "
                                    "Confirm or Cancel in the toolbar"),
                                 m_feature_counter + 1));
        m_sketch_banner->Show(sketching);
        m_sketch_banner->GetParent()->Layout();
    }
}

void DesignPanel::on_shape_changed()
{
    bool rect = (m_shape->GetSelection() == 0);
    m_width->Enable(rect);
    m_height->Enable(rect);
    m_radius->Enable(!rect);
}

void DesignPanel::set_status_ok()
{
    set_status(wxString::Format(_L("OK — %zu triangles"),
                                        m_doc.display_mesh.its.indices.size()));
    if (m_viewport != nullptr) {
        if (body_move_pending()) end_body_move(true);
        else                     m_viewport->clear_move_gizmo();
        rebuild_disp_meshes();
        m_viewport->set_solid_pick(&m_doc.bodies, &m_disp_pick_mesh,
                                   &m_doc.display_tri_face, &m_doc.display_tri_body,
                                   &m_body_visible, &m_body_xform);
        feed_bodies();
    }
    sync_sketch_display();
}

void DesignPanel::sync_sketch_display()
{
    if (m_viewport == nullptr) return;
    const int n = int(m_doc.features.size());
    std::vector<bool> consumed(n, false);
    std::vector<std::vector<SketchEntity>> consumed_loops;
    for (const CadFeature& f : m_doc.features) {
        if (f.type != CadFeatureType::Extrude || !f.enabled) continue;
        if (f.sketch_ref >= 0 && f.sketch_ref < n)        consumed[f.sketch_ref] = true;
        else if (f.sketch_ref < 0 && !f.entities.empty()) consumed_loops.push_back(f.entities);
    }
    auto same_loop = [](const std::vector<SketchEntity>& a, const std::vector<SketchEntity>& b) {
        if (a.size() != b.size() || a.empty()) return false;
        auto eq = [](const Vec2d& u, const Vec2d& v) { return (u - v).squaredNorm() < 1e-10; };
        for (size_t k = 0; k < a.size(); ++k) {
            const SketchEntity& x = a[k]; const SketchEntity& y = b[k];
            if (x.type != y.type || !eq(x.p0, y.p0) || !eq(x.p1, y.p1) ||
                !eq(x.center, y.center) || std::abs(x.radius - y.radius) > 1e-7) return false;
        }
        return true;
    };

    std::vector<DesignSketchTool::DisplaySketch> ds;
    for (int i = 0; i < n; ++i) {
        const CadFeature& f = m_doc.features[i];
        if (f.type != CadFeatureType::Sketch || consumed[i] || !f.enabled)
            continue;
        if (!f.entities.empty()) {
            if (consumed_loops.empty()) {
                ds.push_back({ f.entities, f.plane, i });
            } else {
                std::vector<char> drop(f.entities.size(), 0);
                for (const std::vector<int>& loop : m_viewport->region_entity_indices_with_holes(f.entities)) {
                    std::vector<SketchEntity> es;
                    for (int ei : loop)
                        if (ei >= 0 && ei < int(f.entities.size())) es.push_back(f.entities[ei]);
                    bool gone = false;
                    for (const std::vector<SketchEntity>& c : consumed_loops)
                        if (same_loop(es, c)) { gone = true; break; }
                    if (gone)
                        for (int ei : loop)
                            if (ei >= 0 && ei < int(drop.size())) drop[ei] = 1;
                }
                std::vector<SketchEntity> shown;
                for (int ei = 0; ei < int(f.entities.size()); ++ei)
                    if (!drop[ei]) shown.push_back(f.entities[ei]);
                if (!shown.empty()) ds.push_back({ std::move(shown), f.plane, i });
            }
        } else if (!f.imported_regions.empty()) {
            const auto regions = transform_regions(f.imported_regions, f.import_offset,
                                                    f.import_scale_x, f.import_scale_y);
            std::vector<SketchEntity> lines;
            for (const auto& region : regions)
                for (const auto& contour : region) {
                    const int m = int(contour.size());
                    for (int k = 0; k < m; ++k) {
                        SketchEntity e;
                        e.type = SketchEntity::Type::Line;
                        e.p0   = contour[k];
                        e.p1   = contour[(k + 1) % m];
                        lines.push_back(e);
                    }
                }
            if (!lines.empty())
                ds.push_back({ std::move(lines), f.plane, i });
        }
    }
    m_viewport->set_display_sketches(std::move(ds));
}

void DesignPanel::on_add_text()
{
    open_text_dialog(-1);
}

void DesignPanel::open_text_dialog(int feat)
{
    if (m_text_dlg != nullptr) { m_text_dlg->Raise(); return; }
    end_body_move(true);
    end_sketch_plane_choice();
    m_text_editing = feat >= 0 && feat < int(m_doc.features.size()) && m_doc.features[feat].is_text();
    m_text_feat    = m_text_editing ? feat : -1;
    m_text_face_body = -1;
    m_text_offset    = Vec2d(0, 0);

    DesignTextDialog::Spec initial;
    if (m_text_editing) {
        const CadFeature& f = m_doc.features[feat];
        initial = { wxString::FromUTF8(f.text_string), f.text_font, f.text_height };
        m_doc.checkpoint();
    } else {
        if (m_viewport && m_viewport->is_sketching()) {
            m_text_plane = m_viewport->mcp_sketch_tool().plane();
            if (m_viewport->live_sketch_has_work()) {
                m_viewport->sketch_confirm_pending();
                m_viewport->finish_sketch();
            } else {
                m_viewport->cancel_sketch();
            }
            m_edit_index = -1;
            set_ui_mode(UiMode::Feature);
            sync_sketch_display();
            refresh_tree();
        } else if (m_sel_solid_face >= 0 && m_sel_solid_body >= 0 && m_sel_solid_body < int(m_doc.bodies.size())) {
            const TopoDS_Face face = GeometryEngine::face_by_index(m_doc.bodies[m_sel_solid_body].shape, m_sel_solid_face);
            if (!face.IsNull()) {
                m_text_plane     = SketchPlane::from_face(face);
                m_text_offset    = m_text_plane.project(GeometryEngine::face_centroid_world(face), m_text_plane.normal);
                m_text_face_body = m_sel_solid_body;
            } else {
                m_text_plane = plane_from_choice(m_ref_plane);
            }
        } else {
            m_text_plane = plane_from_choice(m_ref_plane);
        }
    }

    m_text_dlg = new DesignTextDialog(this, m_text_editing ? &initial : nullptr);
    m_text_dlg->on_change = [this] { text_dialog_changed(); };
    m_text_dlg->on_accept = [this] { text_dialog_done(true); };
    m_text_dlg->on_cancel = [this] { text_dialog_done(false); };
    m_text_dlg->Show();
    set_status(StatusKind::Info, m_text_editing ? _L("Edit the text — Enter applies, Esc keeps it as it was")
                                                : _L("Type the text — it appears in the view as you type; Enter inserts, Esc cancels"));
}

void DesignPanel::text_dialog_changed()
{
    if (m_text_dlg == nullptr) return;
    const ImportRegions& regions = m_text_dlg->regions();
    const DesignTextDialog::Spec sp = m_text_dlg->spec();
    const bool have = m_text_feat >= 0 && m_text_feat < int(m_doc.features.size());
    if (regions.empty()) {
        if (!m_text_editing && have) {
            m_doc.undo();
            m_text_feat = -1;
            m_feature_counter--;
            refresh_tree();
            sync_sketch_display();
        }
        return;
    }
    if (!have) {
        m_doc.checkpoint();
        m_feature_counter++;
        CadFeature f;
        f.type             = CadFeatureType::Sketch;
        f.name             = feature_name(_L("Text"));
        f.plane            = m_text_plane;
        f.import_offset    = m_text_offset;
        f.import_on_face   = m_text_face_body >= 0;
        f.import_face_body = m_text_face_body;
        m_doc.features.push_back(f);
        m_text_feat = int(m_doc.features.size()) - 1;
        refresh_tree();
        set_tree_selection(m_text_feat);
    }
    CadFeature& f = m_doc.features[m_text_feat];
    f.imported_regions = regions;
    f.text_string      = std::string(sp.text.ToUTF8().data());
    f.text_font        = sp.font;
    f.text_height      = sp.height;
    sync_sketch_display();
}

void DesignPanel::text_dialog_done(bool accepted)
{
    DesignTextDialog* dlg = m_text_dlg;
    m_text_dlg = nullptr;
    if (dlg != nullptr) dlg->Destroy();
    const int  feat    = m_text_feat;
    const bool editing = m_text_editing;
    m_text_feat    = -1;
    m_text_editing = false;
    const bool have = feat >= 0 && feat < int(m_doc.features.size());

    if (!accepted) {
        if (have) m_doc.undo();
        refresh_tree();
        sync_sketch_display();
        set_status(StatusKind::Info, editing ? _L("Text unchanged") : _L("Text canceled"));
        return;
    }
    if (!have) return;
    m_sel_solid_face = m_sel_solid_edge = m_sel_solid_body = -1;
    m_doc.recompute();
    refresh_tree();
    set_tree_selection(feat);
    sync_sketch_display();
    if (editing) {
        set_status_ok();
        return;
    }
    on_transform_imported(feat);
    m_insert_feat = feat;
    open_insert_card(wxString::FromUTF8(m_doc.features[feat].name));
}

void DesignPanel::on_import_svg()
{
    wxFileDialog dlg(this, _L("Import SVG"), wxEmptyString, wxEmptyString,
                     _L("SVG files (*.svg)|*.svg|All files|*.*"),
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK)
        return;
    const std::string path(dlg.GetPath().ToUTF8().data());
    add_imported_sketch(svg_to_regions(path, 1.0), _L("SVG"));
}

static std::atomic<int> s_doc_worker_busy{0};

static void run_off_ui_thread(wxWindow* parent, const wxString& message, const std::function<void()>& work)
{
    struct Busy { Busy() { ++s_doc_worker_busy; } ~Busy() { --s_doc_worker_busy; } } busy;
    std::atomic<bool> done{false};
    std::thread worker([&work, &done]() {
        work();
        done.store(true, std::memory_order_release);
    });

    wxWindowDisabler disabler;
    std::unique_ptr<wxProgressDialog> dlg;
    int elapsed_ms = 0;
    while (!done.load(std::memory_order_acquire)) {
        if (dlg == nullptr && elapsed_ms >= 300)
            dlg = std::make_unique<wxProgressDialog>(_L("Design"), message, 100, parent,
                                                     wxPD_AUTO_HIDE | wxPD_SMOOTH);
        if (dlg != nullptr)
            dlg->Pulse();
        wxYield();
        wxMilliSleep(30);
        elapsed_ms += 30;
    }
    worker.join();
}

bool DesignPanel::mcp_busy(bool sketch_method, std::string& why) const
{
    if (s_doc_worker_busy.load() > 0) { why = "the model is being rebuilt; try again when it finishes"; return true; }
    if (m_active != Tool::None || m_edit_index >= 0) { why = "a feature card is open in the Design tab"; return true; }
    if (m_text_dlg != nullptr) { why = "the Text dialog is open in the Design tab"; return true; }
    if (m_ui_mode == UiMode::Constrain) { why = "the Design tab is constraining a sketch"; return true; }
    if (!sketch_method && m_ui_mode == UiMode::Sketch) { why = "a sketch is open in the Design tab"; return true; }
    return false;
}

void DesignPanel::sync_recipe_to_model()
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr) return;
    std::string recipe = m_doc.features.empty() ? std::string() : m_doc.serialize_recipe();
    if (recipe == plater->model().cad_recipe)
        return;
    plater->model().cad_recipe = std::move(recipe);

    Slic3r::put_other_changes();
}

bool DesignPanel::recompute_guarded(const wxString& message)
{
    bool ok = false;
    run_off_ui_thread(this, message, [this, &ok]() {
        try {
            ok = m_doc.recompute();
        } catch (const std::exception& e) {
            m_doc.error = *e.what() ? e.what() : "OCCT failure";
            ok = false;
        }
    });
    if (ok) sync_recipe_to_model();
    return ok;
}

void DesignPanel::on_import_step()
{
    wxFileDialog dlg(this, _L("Import STEP"), wxEmptyString, wxEmptyString,
                     _L("STEP files (*.step;*.stp)|*.step;*.stp|All files|*.*"),
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK)
        return;
    const std::string path(dlg.GetPath().ToUTF8().data());
    std::string err;
    std::vector<TopoDS_Shape> solids;
    run_off_ui_thread(this, _L("Reading STEP…"), [&]() {
        try {
            solids = GeometryEngine::read_step_solids(path, err);
        } catch (const std::exception& e) {
            err = *e.what() ? e.what() : "OCCT failure";
        }
    });
    if (solids.empty()) {
        set_status(StatusKind::Error, err.empty() ? _L("No solids found in STEP")
                                       : (wxString::Format(_L("STEP import failed: %s"), kernel_error_text(err))));
        return;
    }
    m_doc.checkpoint();
    for (const TopoDS_Shape& s : solids) {
        m_feature_counter++;
        CadFeature f;
        f.type           = CadFeatureType::Import;
        f.name           = feature_name("STEP");
        f.imported_solid = s;
        f.mode           = BooleanMode::New;
        m_doc.features.push_back(f);
    }
    bool rebuilt = false;
    run_off_ui_thread(this, _L("Rebuilding model…"), [&]() {
        try {
            rebuilt = m_doc.recompute();
        } catch (const std::exception& e) {
            m_doc.error = *e.what() ? e.what() : "OCCT failure";
        }
    });
    if (!rebuilt) {
        set_status(StatusKind::Error, wxString::Format(_L("STEP import failed: %s"), kernel_error_text(m_doc.error)));
        return;
    }
    set_ui_mode(UiMode::Feature);
    refresh_tree();
    set_tree_selection(int(m_doc.features.size()) - 1);
    set_status_ok();
    set_status(StatusKind::Info, wxString::Format(
        _L_PLURAL("Imported %d solid — pick a face or edge, then Fillet / Cut / Shell to modify",
                  "Imported %d solids — pick a face or edge, then Fillet / Cut / Shell to modify",
                  unsigned(solids.size())),
        int(solids.size())));
}

void DesignPanel::on_import_mesh()
{
    wxFileDialog dlg(this, _L("Import mesh"), wxEmptyString, wxEmptyString,
                     _L("Mesh files (*.stl;*.obj)|*.stl;*.obj|All files|*.*"),
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK)
        return;
    const std::string path(dlg.GetPath().ToUTF8().data());

    auto fail = [this](const wxString& msg) {
        set_status(StatusKind::Error, msg);
    };

    TriangleMesh mesh;
    const std::string ext = boost::algorithm::to_lower_copy(
        boost::filesystem::path(path).extension().string());
    if (ext == ".stl") {
        if (!mesh.ReadSTLFile(path.c_str())) { fail(_L("Could not read the STL file")); return; }
    } else if (ext == ".obj") {
        ObjInfo     obj_info;
        std::string obj_err;
        if (!load_obj(path.c_str(), &mesh, obj_info, obj_err)) {
            fail(wxString::Format(_L("Could not read the OBJ file: %s"), wxString::FromUTF8(obj_err)));
            return;
        }
    } else {
        fail(_L("Unsupported mesh format (STL and OBJ are supported)"));
        return;
    }
    if (mesh.its.indices.empty()) { fail(_L("The mesh contains no triangles")); return; }

    if (mesh.its.indices.size() > MESH_IMPORT_TRIANGLE_WARN) {
        const wxString q = wxString::Format(
            _L("This mesh has %d triangles. Every triangle becomes a B-rep face before coplanar "
               "merging, so importing it may take a long time and leave a body that is slow to "
               "edit. Decimating the mesh first is usually better.\n\nImport anyway?"),
            int(mesh.its.indices.size()));
        MessageDialog dlg(this, q, _L("Large mesh"), wxYES_NO | wxICON_WARNING);
        if (dlg.ShowModal() != wxID_YES)
            return;
    }

    wxBusyCursor busy;
    GeometryEngine::MeshBrepStats stats;
    TopoDS_Shape shape;
    try {
        shape = GeometryEngine::mesh_to_brep(mesh.its, MESH_IMPORT_TOLERANCE,
                                             MESH_IMPORT_MERGE_ANGLE_DEG, stats);
    } catch (const std::exception& e) {
        fail(wxString::Format(_L("Mesh conversion failed: %s"), kernel_error_text(e.what())));
        return;
    }
    if (shape.IsNull()) { fail(_L("Mesh conversion produced no geometry")); return; }

    m_doc.checkpoint();
    m_feature_counter++;
    CadFeature f;
    f.type           = CadFeatureType::Import;
    f.name           = feature_name(_L("Mesh"));
    f.imported_solid = shape;
    f.mode           = BooleanMode::New;
    m_doc.features.push_back(f);

    if (!recompute_guarded(_L("Rebuilding model…"))) {
        fail(wxString::Format(_L("Mesh import failed: %s"), kernel_error_text(m_doc.error)));
        return;
    }
    set_ui_mode(UiMode::Feature);
    refresh_tree();
    set_tree_selection(int(m_doc.features.size()) - 1);
    set_status_ok();

    if (stats.is_solid) {
        set_status(StatusKind::Info, wxString::Format(
            _L("Imported solid — %d triangles → %d faces, volume %.2f mm³. Pick a face or edge, "
               "then Fillet / Cut / Shell to modify"),
            stats.kept_tris, stats.faces_final, stats.volume));
    } else {
        set_status(StatusKind::Warning, wxString::Format(
            _L("Imported as an open shell (not watertight). Boundary edges: %d, non-manifold "
               "edges: %d, triangles: %d → faces: %d. The source mesh has holes or duplicated "
               "geometry; boolean features may fail on it"),
            stats.boundary_edges, stats.nonmanifold_edges, stats.kept_tris, stats.faces_final));
    }
    m_status->Refresh();
}

void DesignPanel::add_imported_sketch(
    const std::vector<std::vector<std::vector<Vec2d>>>& regions,
    const wxString& base_name)
{
    if (regions.empty()) {
        set_status(StatusKind::Error, _L("No importable geometry found"));
        return;
    }
    if (m_viewport && m_viewport->is_sketching() && m_viewport->add_sketch_regions(regions)) {
        set_status(StatusKind::Info, wxString::Format(_L("%s added to the sketch — Confirm to commit it"),
                                            base_name));
        return;
    }
    m_doc.checkpoint();
    m_feature_counter++;
    CadFeature f;
    f.type            = CadFeatureType::Sketch;
    f.name            = feature_name(base_name);
    f.imported_regions = regions;

    bool on_face = false;
    if (m_sel_solid_face >= 0 && m_sel_solid_body >= 0 && m_sel_solid_body < int(m_doc.bodies.size())) {
        const TopoDS_Face face =
            GeometryEngine::face_by_index(m_doc.bodies[m_sel_solid_body].shape, m_sel_solid_face);
        if (!face.IsNull()) {
            f.plane = SketchPlane::from_face(face);
            const Vec3d cw   = GeometryEngine::face_centroid_world(face);
            const Vec2d c_uv = f.plane.project(cw, f.plane.normal);
            Vec2d lo(1e30, 1e30), hi(-1e30, -1e30);
            for (const auto& reg : regions)
                for (const auto& loop : reg)
                    for (const Vec2d& p : loop) { lo = lo.cwiseMin(p); hi = hi.cwiseMax(p); }
            f.import_offset    = c_uv - 0.5 * (lo + hi);
            f.import_on_face   = true;
            f.import_face_body = m_sel_solid_body;
            on_face = true;
        }
    }
    if (!on_face) {
        f.plane = plane_from_choice(m_ref_plane);
    }

    m_sel_solid_face = m_sel_solid_edge = m_sel_solid_body = -1;

    m_doc.features.push_back(f);
    m_doc.recompute();
    refresh_tree();
    const int newidx = int(m_doc.features.size()) - 1;
    set_tree_selection(newidx);
    sync_sketch_display();
    on_transform_imported(newidx);
    m_insert_feat = newidx;
    open_insert_card(base_name);
}

void DesignPanel::open_insert_card(const wxString& base_name)
{
    end_body_move(true);
    m_active = Tool::Insert;
    end_sketch_plane_choice();
    if (m_hdr_insert) m_hdr_insert->SetLabel(base_name);
    wxSizer* s = m_cards->GetSizer();
    s->Show(m_box_insert, true, true);
    update_cards_frame(); m_form->Layout();
    m_form->FitInside();
    update_action_bar();
    set_status(StatusKind::Info, base_name + _L(" — drag to place/size, then Confirm"));
}

void DesignPanel::finalize_insert()
{
    const int feat = m_insert_feat;
    m_insert_feat = -1;
    if (m_viewport) m_viewport->cancel_sketch();
    close_tool();
    set_ui_mode(UiMode::Feature);
    if (feat >= 0 && feat < int(m_doc.features.size())) set_tree_selection(feat);
    sync_sketch_display();
    refresh_tree();
    set_status_ok();
}

void DesignPanel::cancel_insert()
{
    m_insert_feat = -1;
    if (m_viewport) m_viewport->cancel_sketch();
    m_doc.undo();
    close_tool();
    set_ui_mode(UiMode::Feature);
    sync_sketch_display();
    refresh_tree();
    set_status(StatusKind::Info, _L("Insert canceled"));
}

void DesignPanel::on_transform_imported(int feat_idx)
{
    if (feat_idx < 0 || feat_idx >= int(m_doc.features.size()) || !m_viewport)
        return;
    const CadFeature& f = m_doc.features[feat_idx];
    if (f.imported_regions.empty())
        return;
    end_body_move(true);
    end_sketch_plane_choice();
    m_viewport->begin_imported_transform(feat_idx, f.imported_regions, f.plane,
                                         f.import_offset, f.import_scale_x, f.import_scale_y);
    set_status(StatusKind::Info, _L("Drag a corner to scale, the center to move — Enter or ✓ when done, Esc to discard"));
}

void DesignPanel::on_add_sketch()
{
    SketchShape shape = (m_shape->GetSelection() == 1) ? SketchShape::Circle
                                                        : SketchShape::Rectangle;
    wxString where;
    SketchPlane plane = sketch_plane_from_selection(where);
    m_feature_counter++;
    m_doc.add_sketch(shape, plane, m_width->GetValue(), m_height->GetValue(),
                     m_radius->GetValue(), feature_name(_L("Sketch")));
    m_doc.recompute();
    set_status(StatusKind::Info, wxString::Format(_L("Sketch added (plane: %s) — select it, then right-click to Extrude"), where));
    refresh_tree();
}

bool DesignPanel::extrude_uses_loop() const
{
    return m_viewport != nullptr
        && m_viewport->loop_pick_region() >= 0
        && m_extrude_sketch_ref >= 0
        && m_extrude_sketch_ref == m_viewport->loop_pick_feature()
        && m_extrude_sketch_ref < int(m_doc.features.size())
        && !m_viewport->selected_loop_entities().empty();
}

std::string DesignPanel::feature_name(const wxString& kind) const
{
    return std::string((kind + wxString::Format(" %d", m_feature_counter)).ToUTF8().data());
}

void DesignPanel::on_add_extrude()
{
    BooleanMode mode = static_cast<BooleanMode>(m_mode->GetSelection());
    m_feature_counter++;
    const std::string name = feature_name(_L("Extrude"));
    int idx = -1;
    if (m_extrude_face_src >= 0) {
        idx = m_doc.add_extrude_face(m_extrude_face_src, m_distance->GetValue(), false, mode, name);
        m_extrude_face_src = -1;
    } else if (extrude_uses_loop()) {
        if (::getenv("ORCA_CAD_PICK_TRACE"))
            std::fprintf(stderr, "[pick] on_add_extrude: feat=%d reg=%d ents=%zu\n",
                         m_extrude_sketch_ref, m_viewport->loop_pick_region(),
                         m_viewport->selected_loop_entities().size());
        idx = m_doc.add_extrude_entities(m_viewport->selected_loop_entities(),
                                         m_doc.features[m_extrude_sketch_ref].plane,
                                         m_distance->GetValue(), false, mode, name);
        m_viewport->clear_loop_pick();
    } else {
        idx = m_doc.add_extrude(m_extrude_sketch_ref, m_distance->GetValue(), false, mode, name);
    }
    if (idx >= 0 && idx < int(m_doc.features.size())) {
        CadFeature& f = m_doc.features[idx];
        f.extrude_end = static_cast<ExtrudeEnd>(m_extrude_end->GetSelection());
        f.distance2   = m_distance2->GetValue();
        f.taper_deg   = m_taper->GetValue();
        f.flip        = m_flip->GetValue();
        f.up_to_face  = (f.extrude_end == ExtrudeEnd::UpToFace) ? m_sel_solid_face : -1;
        f.target_body = m_sel_solid_body >= 0 ? m_sel_solid_body : m_extrude_auto_body;
        if (m_extrude_sketch_ref >= 0 && m_extrude_sketch_ref < int(m_doc.features.size())
            && m_doc.features[m_extrude_sketch_ref].import_on_face)
            f.target_body = m_doc.features[m_extrude_sketch_ref].import_face_body;
    }
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

void DesignPanel::on_add_dressup()
{
    if (m_doc.body.IsNull()) {
        set_status(_L("Add a solid (sketch + extrude) first"));
        return;
    }
    FaceGroup fg = static_cast<FaceGroup>(m_face_group->GetSelection());
    double    sz = m_dressup_size->GetValue();
    bool      fillet = (m_dressup_type->GetSelection() == 0);

    m_feature_counter++;
    int didx = -1;
    const std::vector<int> edges = dressup_edges();
    if (!edges.empty()) {
        if (fillet)
            didx = m_doc.add_fillet(sz, edges, feature_name(_L("Fillet")));
        else
            didx = m_doc.add_chamfer(sz, edges, feature_name(_L("Chamfer")));
    } else if (fillet)
        didx = m_doc.add_fillet(sz, fg, feature_name(_L("Fillet")));
    else
        didx = m_doc.add_chamfer(sz, fg, feature_name(_L("Chamfer")));
    if (didx >= 0 && didx < int(m_doc.features.size()))
        m_doc.features[didx].target_body = m_sel_solid_body;

    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();

    refresh_tree();
}

void DesignPanel::set_hole_target_label(int face)
{
    if (m_hole_target_label)
        m_hole_target_label->SetLabel(face >= 0 ? wxString::Format(_L("Face %d"), face)
                                                : _L("(none — uses Hole plane)"));
}

void DesignPanel::set_thread_target_label(int face, int edge)
{
    if (!m_thread_target_label) return;
    m_thread_target_label->SetLabel(
        face >= 0 ? wxString::Format(_L("Face %d"), face)
      : edge >= 0 ? wxString::Format(_L("Edge %d"), edge)
                  : _L("(none — uses Thread plane)"));
}

SketchPlane DesignPanel::hole_plane() const
{
    if (m_hole_on_face) return m_hole_face_plane;
    SketchPlane p = plane_from_index(m_hole_plane->GetSelection());
    p.origin += m_doc.modeling_origin;
    return p;
}

void DesignPanel::on_add_hole()
{
    if (m_doc.body.IsNull()) {
        set_status(_L("Add a solid (sketch + extrude) first"));
        return;
    }
    SketchPlane plane   = hole_plane();
    double      dia     = m_hole_diameter->GetValue();
    double      depth   = m_hole_depth->GetValue();
    bool        through = m_hole_through->GetValue();
    double      px      = m_hole_x->GetValue();
    double      py      = m_hole_y->GetValue();

    m_feature_counter++;
    const int hidx = m_doc.add_hole(dia, depth, through, px, py, plane,
                                    feature_name(_L("Hole")));
    if (m_hole_on_face && hidx >= 0 && hidx < int(m_doc.features.size()))
        m_doc.features[hidx].target_body = m_hole_face_body;

    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();

    refresh_tree();
}

SketchPlane DesignPanel::thread_plane() const
{
    if (m_thread_on_face) return m_thread_face_plane;
    SketchPlane p = plane_from_index(m_thread_plane->GetSelection());
    p.origin += m_doc.modeling_origin;
    return p;
}

void DesignPanel::apply_thread_standard()
{
    if (!m_thread_std) return;
    const int sel = m_thread_std->GetSelection();
    if (sel <= 0) return;

    const ThreadSpec* s = find_thread_standard(
        m_thread_std->GetString(sel).utf8_string());
    if (!s) return;

    const bool internal = m_thread_internal && m_thread_internal->GetValue();
    if (m_thread_pitch) m_thread_pitch->SetValue(s->pitch_mm);
    if (m_thread_depth) m_thread_depth->SetValue(internal ? s->internal_depth_mm() : s->thread_depth_mm());
    if (m_thread_radius) m_thread_radius->SetValue(s->major_diameter_mm);

    if (m_status)
        set_status(wxString::Format(_L("Thread standard: %s  (pitch %.3g mm)"),
                                            m_thread_std->GetString(sel), s->pitch_mm));
}

void DesignPanel::infer_thread_spec(double diameter, bool internal)
{
    const auto& stds = thread_standards();
    int best = -1; double bestErr = 1e30;
    for (int i = 0; i < int(stds.size()); ++i) {
        const double ref = internal ? stds[i].minor_diameter_mm() : stds[i].major_diameter_mm;
        const double e = std::abs(ref - diameter);
        if (e < bestErr) { bestErr = e; best = i; }
    }
    if (best < 0) { if (m_thread_radius) m_thread_radius->SetValue(diameter); return; }
    const ThreadSpec& s = stds[best];
    if (m_thread_std)    m_thread_std->SetSelection(best + 1);
    if (m_thread_radius) m_thread_radius->SetValue(s.major_diameter_mm);
    if (m_thread_pitch)  m_thread_pitch->SetValue(s.pitch_mm);
    if (m_thread_depth)  m_thread_depth->SetValue(internal ? s.internal_depth_mm() : s.thread_depth_mm());
}

void DesignPanel::on_add_thread()
{
    bool internal = m_thread_internal->GetValue();
    if (internal && m_doc.body.IsNull()) {
        set_status(StatusKind::Warning, _L("Thread needs a solid body — add or import one first"));
        return;
    }
    SketchPlane plane = thread_plane();

    m_feature_counter++;
    const int tidx = m_doc.add_thread(m_thread_radius->GetValue() * 0.5, m_thread_pitch->GetValue(),
                     m_thread_height->GetValue(), m_thread_depth->GetValue(),
                     internal, m_thread_x->GetValue(), m_thread_y->GetValue(),
                     plane, feature_name(_L("Thread")));
    if (m_thread_on_face && tidx >= 0 && tidx < int(m_doc.features.size()))
        m_doc.features[tidx].target_body = m_thread_face_body;

    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();

    refresh_tree();
}

void DesignPanel::on_add_revolve()
{
    if (m_revolve_sketch_ref < 0 || m_revolve_sketch_ref >= int(m_doc.features.size())) {
        set_status(_L("Pick a sketch profile to revolve first"));
        return;
    }
    const BooleanMode mode = static_cast<BooleanMode>(m_revolve_mode->GetSelection());
    if (mode != BooleanMode::New && m_doc.body.IsNull()) {
        set_status(StatusKind::Warning, _L("Revolve needs a solid body — add or import one first"));
        return;
    }
    m_feature_counter++;
    int axis = 0, axis_entity = -1;
    read_revolve_axis(m_revolve_axis, m_revolve_axis_ents, axis, axis_entity);
    const int idx = m_doc.add_revolve(m_revolve_sketch_ref, m_revolve_angle->GetValue(), axis,
                                      m_revolve_flip->GetValue(), mode, feature_name(_L("Revolve")));
    m_doc.features[idx].revolve_axis_entity = axis_entity;

    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();

    refresh_tree();
}

void DesignPanel::on_add_sweep()
{
    if (m_sweep_profile_ref < 0 || m_sweep_profile_ref >= int(m_doc.features.size())) {
        set_status(_L("Pick a profile sketch to sweep first"));
        return;
    }
    const int sel = m_sweep_path->GetSelection();
    const int path_ref = (sel != wxNOT_FOUND)
        ? int(reinterpret_cast<intptr_t>(m_sweep_path->GetClientData(sel))) : -1;
    if (path_ref < 0) {
        set_status(_L("Pick a path sketch for the sweep"));
        return;
    }
    const BooleanMode mode = static_cast<BooleanMode>(m_sweep_mode->GetSelection());
    if (mode != BooleanMode::New && m_doc.body.IsNull()) {
        set_status(StatusKind::Warning, _L("Sweep needs a solid body — add or import one first"));
        return;
    }
    m_feature_counter++;
    m_doc.add_sweep(m_sweep_profile_ref, path_ref, mode,
                    feature_name(_L("Sweep")));

    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();

    refresh_tree();
}

void DesignPanel::on_add_loft()
{
    std::vector<int> refs;
    for (unsigned i = 0; i < m_loft_list->GetCount(); ++i)
        if (m_loft_list->IsChecked(i) && i < m_loft_sketch_idx.size())
            refs.push_back(m_loft_sketch_idx[i]);
    if (refs.size() < 2) {
        set_status(_L("Check at least two profile sketches to loft"));
        return;
    }
    const BooleanMode mode = static_cast<BooleanMode>(m_loft_mode->GetSelection());
    if (mode != BooleanMode::New && m_doc.body.IsNull()) {
        set_status(StatusKind::Warning, _L("Loft needs a solid body — add or import one first"));
        return;
    }
    m_feature_counter++;
    m_doc.add_loft(refs, m_loft_ruled->GetValue(), mode,
                   feature_name(_L("Loft")));

    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();

    refresh_tree();
}

void DesignPanel::on_add_surface_extrude()
{
    if (m_surf_extrude_sketch_ref < 0 || m_surf_extrude_sketch_ref >= int(m_doc.features.size())) {
        set_status(_L("Pick a sketch profile to extrude first"));
        return;
    }
    m_feature_counter++;
    m_doc.add_surface_extrude(m_surf_extrude_sketch_ref, m_surf_extrude_distance->GetValue(),
                              feature_name(_L("Surface Extrude")));
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

void DesignPanel::on_add_surface_revolve()
{
    if (m_surf_revolve_sketch_ref < 0 || m_surf_revolve_sketch_ref >= int(m_doc.features.size())) {
        set_status(_L("Pick a sketch profile to revolve first"));
        return;
    }
    m_feature_counter++;
    int axis = 0, axis_entity = -1;
    read_revolve_axis(m_surf_revolve_axis, m_surf_revolve_axis_ents, axis, axis_entity);
    const int idx = m_doc.add_surface_revolve(m_surf_revolve_sketch_ref, m_surf_revolve_angle->GetValue(),
                                              axis, feature_name(_L("Surface Revolve")));
    m_doc.features[idx].revolve_axis_entity = axis_entity;
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

void DesignPanel::on_add_surface_loft()
{
    std::vector<int> refs;
    for (unsigned i = 0; i < m_surf_loft_list->GetCount(); ++i)
        if (m_surf_loft_list->IsChecked(i) && i < m_surf_loft_sketch_idx.size())
            refs.push_back(m_surf_loft_sketch_idx[i]);
    if (refs.size() < 2) {
        set_status(_L("Check at least two profile sketches to loft"));
        return;
    }
    m_feature_counter++;
    m_doc.add_surface_loft(refs, m_surf_loft_ruled->GetValue(),
                           feature_name(_L("Surface Loft")));
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

void DesignPanel::on_add_surface_fill()
{
    if (m_surf_fill_sketch_ref < 0 || m_surf_fill_sketch_ref >= int(m_doc.features.size())) {
        set_status(_L("Pick a sketch to fill first"));
        return;
    }
    m_feature_counter++;
    m_doc.add_surface_fill(m_surf_fill_sketch_ref,
                           feature_name(_L("Surface Fill")));
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

void DesignPanel::on_add_surface_offset()
{
    const int sel = sheet_choice_body(m_surf_offset_body);
    if (sel < 0 || sel >= int(m_doc.bodies.size())) {
        set_status(_L("Select a sheet body first"));
        return;
    }
    m_feature_counter++;
    m_doc.add_surface_offset(sel, m_surf_offset_distance->GetValue(),
                             feature_name(_L("Surface Offset")));
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

void DesignPanel::on_add_thicken_surface()
{
    const int sel = sheet_choice_body(m_surf_thicken_body);
    if (sel < 0 || sel >= int(m_doc.bodies.size())) {
        set_status(_L("Select a sheet body first"));
        return;
    }
    m_feature_counter++;
    m_doc.add_thicken_surface(sel, m_surf_thicken_thickness->GetValue(),
                              m_surf_thicken_flip->GetValue(),
                              feature_name(_L("Thicken Surface")));
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

static Transform3d xf_compose(const Vec3d& t, const Vec3d& axis, const Vec3d& pivot, double deg)
{
    Transform3d r = Transform3d::Identity();
    if (std::abs(deg) > 1e-12 && axis.norm() > 1e-9)
        r = Transform3d(Eigen::Translation3d(pivot))
          * Transform3d(Eigen::AngleAxisd(deg * M_PI / 180.0, axis.normalized()))
          * Transform3d(Eigen::Translation3d(-pivot));
    return Transform3d(Eigen::Translation3d(t)) * r;
}

void DesignPanel::xf_live_preview()
{
    if (m_active != Tool::Transform || m_xf_dx == nullptr) return;
    if (m_xf_copy && m_xf_copy->GetValue()) return;
    sync_body_xform();
    const int sel = m_xf_body ? m_xf_body->GetSelection() : wxNOT_FOUND;
    const int b   = (sel != wxNOT_FOUND) ? sel : int(m_body_xform.size()) - 1;
    if (b < 0 || b >= int(m_body_xform.size())) return;

    if (m_xf_prev_body != b) {
        xf_clear_preview();
        m_xf_prev_body = b;
        m_xf_prev_base = (b == m_xf_gizmo_body) ? m_xf_gizmo_base : m_body_xform[b];
    }
    const int   ax   = m_xf_axis->GetSelection();
    const Vec3d axis = (ax == 0) ? Vec3d::UnitX() : (ax == 1) ? Vec3d::UnitY() : Vec3d::UnitZ();
    const Vec3d pivot(m_xf_pivot_x->GetValue(), m_xf_pivot_y->GetValue(), m_xf_pivot_z->GetValue());
    const Transform3d want = xf_compose(Vec3d(m_xf_dx->GetValue(), m_xf_dy->GetValue(),
                                              m_xf_dz->GetValue()),
                                        axis, pivot, m_xf_angle->GetValue());
    Transform3d undo = Transform3d::Identity();
    if (m_edit_index >= 0 && m_edit_index < int(m_doc.features.size())) {
        const CadFeature& f = m_doc.features[m_edit_index];
        if (f.type == CadFeatureType::Transform && !f.xf_copy)
            undo = xf_compose(f.xf_translate, f.xf_axis, f.xf_pivot, f.xf_angle_deg).inverse();
    }
    m_body_xform[b] = want * undo * m_xf_prev_base;
    feed_bodies();
}

void DesignPanel::xf_clear_preview()
{
    if (m_xf_prev_body < 0) return;
    sync_body_xform();
    if (m_xf_prev_body < int(m_body_xform.size()))
        m_body_xform[m_xf_prev_body] = m_xf_prev_base;
    m_xf_prev_body = -1;
    feed_bodies();
}

void DesignPanel::on_add_transform()
{
    xf_clear_preview();
    if (m_xf_gizmo_body >= 0) {
        sync_body_xform();
        if (m_xf_gizmo_body < int(m_body_xform.size()))
            m_body_xform[m_xf_gizmo_body] = m_xf_gizmo_base;
        if (m_viewport) m_viewport->clear_move_gizmo();
        feed_bodies();
        m_xf_gizmo_body = -1;
    }
    if (m_doc.bodies.empty()) {
        set_status(StatusKind::Warning, _L("Transform needs a body — add or import one first"));
        return;
    }
    const int sel = m_xf_body->GetSelection();
    const int target = (sel != wxNOT_FOUND) ? sel : -1;
    const Vec3d trans(m_xf_dx->GetValue(), m_xf_dy->GetValue(), m_xf_dz->GetValue());
    const int ax = m_xf_axis->GetSelection();
    const Vec3d axis = (ax == 0) ? Vec3d(1, 0, 0) : (ax == 1) ? Vec3d(0, 1, 0) : Vec3d(0, 0, 1);
    const Vec3d pivot(m_xf_pivot_x->GetValue(), m_xf_pivot_y->GetValue(), m_xf_pivot_z->GetValue());
    m_feature_counter++;
    m_doc.add_transform(target, trans, axis, pivot, m_xf_angle->GetValue(), m_xf_copy->GetValue(),
                        feature_name(_L("Transform")));
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

void DesignPanel::on_add_mirror()
{
    if (m_doc.bodies.empty()) {
        set_status(StatusKind::Warning, _L("Mirror needs a body — add or import one first"));
        return;
    }
    const int sel = m_mirror_body->GetSelection();
    const int target = (sel != wxNOT_FOUND) ? sel : -1;
    const BooleanMode mode = m_mirror_keep->GetValue() ? BooleanMode::New : BooleanMode::Add;
    m_feature_counter++;
    m_doc.add_mirror(plane_from_choice(m_mirror_plane->GetSelection()), target, mode,
                     feature_name(_L("Mirror")));
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

void DesignPanel::on_add_thicken()
{
    if (m_doc.bodies.empty()) {
        set_status(StatusKind::Warning, _L("Thicken needs a solid body — add or import one first"));
        return;
    }
    if (m_sel_solid_face < 0) {
        set_status(StatusKind::Error, _L("Pick a solid face to thicken first"));
        return;
    }
    const int sel = m_thicken_body->GetSelection();
    const int target = (sel != wxNOT_FOUND) ? sel : -1;
    m_feature_counter++;
    m_doc.add_thicken(target, m_sel_solid_face, m_thicken_thickness->GetValue(),
                      m_thicken_flip->GetValue(), feature_name(_L("Thicken")));
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

void DesignPanel::on_add_rib()
{
    if (m_doc.bodies.empty()) {
        set_status(StatusKind::Warning, _L("Rib needs a solid body — add or import one first"));
        return;
    }
    const int bsel = m_rib_body->GetSelection();
    const int target = (bsel != wxNOT_FOUND) ? bsel : -1;
    const int ssel = m_rib_sketch->GetSelection();
    const int sketch_ref = (ssel != wxNOT_FOUND)
                               ? int(reinterpret_cast<intptr_t>(m_rib_sketch->GetClientData(ssel))) : -1;
    if (sketch_ref < 0) {
        set_status(_L("Pick a sketch with an open line first"));
        return;
    }
    m_feature_counter++;
    m_doc.add_rib(sketch_ref, m_rib_entity->GetValue(), m_rib_thickness->GetValue(),
                  m_rib_depth->GetValue(), target, feature_name(_L("Rib")));
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

void DesignPanel::on_add_project()
{
    if (m_doc.bodies.empty()) {
        set_status(StatusKind::Warning, _L("Project needs a body — add or import one first"));
        return;
    }
    const int sel = m_proj_source_body->GetSelection();
    const int src_body = (sel != wxNOT_FOUND) ? sel : -1;
    const int face = (m_sel_solid_face >= 0) ? m_sel_solid_face : -1;
    m_feature_counter++;
    m_doc.add_project_edges(src_body, {}, face,
                            plane_from_choice(m_proj_plane->GetSelection()),
                            feature_name(_L("Project")));
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

void DesignPanel::on_add_delete_face()
{
    if (m_doc.bodies.empty()) {
        set_status(StatusKind::Warning, _L("Delete Face needs a body — add or import one first"));
        return;
    }
    if (m_del_faces.empty()) {
        set_status(StatusKind::Error, _L("Add at least one face to delete first"));
        return;
    }
    const int sel = m_del_face_body->GetSelection();
    const int target = (sel != wxNOT_FOUND) ? sel : -1;
    m_feature_counter++;
    m_doc.add_delete_face(target, m_del_faces, feature_name(_L("Delete Face")));
    m_del_faces.clear();
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

void DesignPanel::on_add_helix()
{
    m_feature_counter++;
    m_doc.add_helix(plane_from_choice(m_helix_plane->GetSelection()),
                    m_helix_radius->GetValue(), m_helix_pitch->GetValue(),
                    m_helix_height->GetValue(), m_helix_left_handed->GetValue(),
                    m_helix_taper->GetValue(), feature_name(_L("Helix")));
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

void DesignPanel::on_add_mate()
{
    const int sel_a = m_mate_cs_a->GetSelection();
    const int sel_b = m_mate_cs_b->GetSelection();
    if (sel_a == wxNOT_FOUND || sel_b == wxNOT_FOUND) {
        set_status(StatusKind::Warning, _L("Mate needs two coordinate systems — create them first"));
        return;
    }
    const int cs_a = int(reinterpret_cast<intptr_t>(m_mate_cs_a->GetClientData(sel_a)));
    const int cs_b = int(reinterpret_cast<intptr_t>(m_mate_cs_b->GetClientData(sel_b)));
    if (cs_a == cs_b) {
        set_status(StatusKind::Error, _L("Mate: A and B must be different coordinate systems"));
        return;
    }
    m_feature_counter++;
    int idx = m_doc.add_mate(m_mate_kind->GetSelection(), cs_a, cs_b,
                             m_mate_offset->GetValue(), m_mate_angle->GetValue(),
                             m_mate_flip->GetValue(),
                             feature_name(_L("Mate")));
    if (idx < 0) {
        set_status(StatusKind::Error, _L("Mate rejected"));
        return;
    }
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

void DesignPanel::on_check_interference()
{
    if (m_doc.bodies.size() < 2) {
        set_status(StatusKind::Info, _L("Interference needs at least two bodies"));
        return;
    }
    const auto pairs = m_doc.check_interference();
    if (pairs.empty()) {
        set_status(StatusKind::Ok, _L("No interference found"));
        return;
    }
    auto body_name = [this](int b) {
        if (b >= 0 && b < int(m_doc.bodies.size()) && !m_doc.bodies[b].name.empty())
            return wxString::FromUTF8(m_doc.bodies[b].name);
        return wxString::Format(_L("Body %d"), b + 1);
    };
    auto sorted = pairs;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.volume > b.volume; });
    wxString list;
    const size_t shown = std::min<size_t>(sorted.size(), 3);
    for (size_t k = 0; k < shown; ++k)
        list += (k ? ", " : "") + wxString::Format(_L("%s ↔ %s %.2f mm³"), body_name(sorted[k].body_a),
                                                   body_name(sorted[k].body_b), sorted[k].volume);
    if (sorted.size() > shown)
        list += wxString::FromUTF8(", …");
    set_status(StatusKind::Warning, wxString::Format(_L_PLURAL("%zu overlapping pair: %s", "%zu overlapping pairs: %s",
                                                               unsigned(sorted.size())), sorted.size(), list));
}

void DesignPanel::on_mass_properties()
{
    if (m_sel_solid_body < 0 || m_sel_solid_body >= int(m_doc.bodies.size())) {
        set_status(StatusKind::Info, _L("Select a body to measure it"));
        return;
    }
    const auto mp = GeometryEngine::mass_properties(m_doc.bodies[m_sel_solid_body].shape);
    if (!mp.valid) {
        set_status(StatusKind::Error, _L("This body could not be measured"));
        return;
    }
    wxString name = wxString::Format(_L("Body %d"), m_sel_solid_body + 1);
    if (!m_doc.bodies[m_sel_solid_body].name.empty())
        name = wxString::FromUTF8(m_doc.bodies[m_sel_solid_body].name);
    if (!mp.is_solid)
        set_status(StatusKind::Info, wxString::Format(_L("%s: sheet body, area %.2f mm², no volume — thicken it to get one"),
                                                      name, mp.surface_area));
    else
        set_status(StatusKind::Info, wxString::Format(_L("%s: volume %.2f mm³, area %.2f mm²"),
                                                      name, mp.volume, mp.surface_area));
}

void DesignPanel::populate_sheet_body_choices(ComboBox* c) const
{
    if (!c) return;
    const int keep = c->GetSelection();
    c->Clear();
    for (size_t i = 0; i < m_doc.bodies.size(); ++i) {
        if (!CadDocument::is_sheet_shape(m_doc.bodies[i].shape)) continue;
        const std::string& n = m_doc.bodies[i].name;
        combo_append_index(c, n.empty() ? wxString::Format(_L("Body %zu"), i + 1)
                                        : wxString::FromUTF8(n), int(i));
    }
    if (c->GetCount() > 0)
        c->SetSelection(std::min(std::max(keep, 0), int(c->GetCount()) - 1));
}

int DesignPanel::sheet_choice_body(ComboBox* c)
{
    if (!c) return -1;
    const int sel = c->GetSelection();
    if (sel == wxNOT_FOUND) return -1;
    return int(reinterpret_cast<intptr_t>(c->GetClientData(sel)));
}

void DesignPanel::select_sheet_choice(ComboBox* c, int body)
{
    if (!c) return;
    for (unsigned i = 0; i < c->GetCount(); ++i) {
        if (int(reinterpret_cast<intptr_t>(c->GetClientData(i))) == body) { c->SetSelection(int(i)); return; }
    }
    if (c->GetCount() > 0) c->SetSelection(0);
}

void DesignPanel::on_add_pattern()
{
    if (m_doc.bodies.empty()) {
        set_status(StatusKind::Warning, _L("Pattern needs a solid body — add or import one first"));
        return;
    }
    const bool circular = (m_pattern_type->GetSelection() == 1);
    const int  target   = (m_sel_solid_body >= 0 && m_sel_solid_body < int(m_doc.bodies.size()))
                          ? m_sel_solid_body : -1;
    m_feature_counter++;
    m_doc.add_pattern(circular, int(m_pattern_count->GetValue()),
                      m_pattern_spacing->GetValue(), m_pattern_dir->GetSelection(),
                      m_pattern_angle->GetValue(), target,
                      feature_name(_L("Pattern")));

    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();

    refresh_tree();
}

int DesignPanel::selected_body_default() const
{
    const int n = int(m_doc.bodies.size());
    if (n <= 0) return 0;
    return (m_sel_solid_body >= 0 && m_sel_solid_body < n) ? m_sel_solid_body : 0;
}

void DesignPanel::populate_body_choices(int as_of_feature)
{
    const std::vector<CadBody>* src = &m_doc.bodies;
    std::vector<CadBody> as_of;
    if (as_of_feature >= 0 && as_of_feature <= int(m_doc.features.size())) {
        CadDocument tmp = m_doc;
        tmp.features.resize(as_of_feature);
        if (tmp.recompute() && !tmp.bodies.empty()) { as_of = tmp.bodies; src = &as_of; }
    }
    auto fill = [&](ComboBox* c, int def) {
        if (!c) return;
        c->Clear();
        for (size_t i = 0; i < src->size(); ++i) {
            const std::string& n = (*src)[i].name;
            c->Append(n.empty() ? wxString::Format(_L("Body %zu"), i + 1) : wxString::FromUTF8(n));
        }
        if (c->GetCount() > 0)
            c->SetSelection(std::min(def, int(c->GetCount()) - 1));
    };
    const int picked = selected_body_default();
    fill(m_bool_target, picked);
    fill(m_bool_tool, picked == 0 ? 1 : 0);
    fill(m_cut_target, picked);
}

void DesignPanel::fill_body_choice(ComboBox* c, int as_of_feature, int want)
{
    if (!c) return;
    const std::vector<CadBody>* src = &m_doc.bodies;
    std::vector<CadBody> as_of;
    if (as_of_feature >= 0 && as_of_feature <= int(m_doc.features.size())) {
        CadDocument tmp = m_doc;
        tmp.features.resize(as_of_feature);
        if (tmp.recompute() && !tmp.bodies.empty()) { as_of = tmp.bodies; src = &as_of; }
    }
    c->Clear();
    for (size_t i = 0; i < src->size(); ++i) {
        const std::string& n = (*src)[i].name;
        c->Append(n.empty() ? wxString::Format(_L("Body %zu"), i + 1) : wxString::FromUTF8(n));
    }
    if (want >= 0 && want < int(c->GetCount())) c->SetSelection(want);
    else if (c->GetCount() > 0)                 c->SetSelection(0);
}

void DesignPanel::on_add_boolean()
{
    if (m_doc.bodies.size() < 2) {
        set_status(StatusKind::Warning, _L("Boolean needs two bodies — add or import a second one"));
        return;
    }
    const int sel = m_bool_op->GetSelection();
    const BooleanMode op = (sel == 1) ? BooleanMode::Cut
                         : (sel == 2) ? BooleanMode::Intersect
                                      : BooleanMode::Add;
    m_feature_counter++;
    m_doc.add_boolean(op, m_bool_target->GetSelection(), m_bool_tool->GetSelection(),
                      m_bool_keep->GetValue(), m_bool_tol->GetValue(), -1, -1,
                      feature_name(_L("Boolean")));
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

void DesignPanel::on_add_cut()
{
    if (m_doc.bodies.empty()) {
        set_status(StatusKind::Warning, _L("Cut needs a solid body — add or import one first"));
        return;
    }
    m_feature_counter++;
    m_doc.add_cut(plane_from_choice(m_cut_plane->GetSelection()), m_cut_offset->GetValue(),
                   false,  true,  true,
                  m_cut_target->GetSelection(), feature_name(_L("Cut")));
    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();
    refresh_tree();
}

void DesignPanel::populate_plane_choices(ComboBox* c) const
{
    if (!c) return;
    const int keep = c->GetSelection();
    c->Clear();
    c->Append(_L_CONTEXT("XY", "Axis")); c->Append(_L_CONTEXT("XZ", "Axis")); c->Append(_L_CONTEXT("YZ", "Axis"));
    for (const auto& dp : m_doc.resolve_datum_planes())
        c->Append(wxString::FromUTF8(dp.first));
    c->SetSelection((keep >= 0 && keep < int(c->GetCount())) ? keep : 0);
}

wxString DesignPanel::ref_plane_name(int row) const
{
    if (row == 1) return "XZ";
    if (row == 2) return "YZ";
    if (row >= 3) {
        const auto datums = m_doc.resolve_datum_planes();
        const int di = row - 3;
        if (di < int(datums.size())) return wxString::FromUTF8(datums[di].first);
    }
    return "XY";
}

SketchPlane DesignPanel::plane_from_choice(int row) const
{
    if (row < 3) {
        SketchPlane p = plane_from_index(row);
        p.origin += m_doc.modeling_origin;
        return p;
    }
    auto datums = m_doc.resolve_datum_planes();
    const int di = row - 3;
    if (di >= 0 && di < int(datums.size())) return datums[di].second;
    SketchPlane p = SketchPlane::XY(); p.origin += m_doc.modeling_origin; return p;
}

SketchPlane DesignPanel::sketch_plane_from_selection(wxString& what) const
{
    SketchPlane p;
    const int fb = (m_sel_solid_face >= 0 && m_sel_solid_body >= 0) ? m_sel_solid_body : m_pick_face_body;
    const int fi = (m_sel_solid_face >= 0 && m_sel_solid_body >= 0) ? m_sel_solid_face : m_pick_face;
    if (fb >= 0 && fi >= 0 && m_doc.plane_of_face(fb, fi, p)) {
        what = (m_doc.bodies.size() > 1)
             ? wxString::Format(_L("Picked face (Body %d)"), fb + 1)
             : _L("Picked face");
        return p;
    }
    what = ref_plane_name(m_ref_plane);
    return plane_from_choice(m_ref_plane);
}

bool DesignPanel::sketch_plane_target(wxString& what) const
{
    const int fb = (m_sel_solid_face >= 0 && m_sel_solid_body >= 0) ? m_sel_solid_body : m_pick_face_body;
    const int fi = (m_sel_solid_face >= 0 && m_sel_solid_body >= 0) ? m_sel_solid_face : m_pick_face;
    SketchPlane p;
    if (fb >= 0 && fi >= 0 && m_doc.plane_of_face(fb, fi, p)) {
        what = (m_doc.bodies.size() > 1)
             ? wxString::Format(_L("Picked face (Body %d)"), fb + 1)
             : _L("Picked face");
        return true;
    }
    if (m_plane_picked) { what = ref_plane_name(m_ref_plane); return true; }
    return false;
}

bool DesignPanel::sketch_map_applies() const
{
    return m_ui_mode == UiMode::Sketch || (m_viewport && m_viewport->is_sketching());
}

int DesignPanel::offer_selection_kind() const
{
    if (sketch_map_applies()) {
        const int n = m_viewport ? m_viewport->sketch_selection_count() : 0;
        if (n >= 2) return int(OfferSel::Sk2Ent);
        if (n == 1) {
            SketchEntity::Type t = SketchEntity::Type::Line;
            if (m_viewport && m_viewport->sketch_first_selected_type(t)) {
                switch (t) {
                case SketchEntity::Type::Line:  return int(OfferSel::SkLine);
                case SketchEntity::Type::Point: return int(OfferSel::SkPoint);
                default:                        return int(OfferSel::SkArc);
                }
            }
        }
        return int(OfferSel::SkNone);
    }

    const int nb = int(m_doc.bodies.size());
    if (m_sel_solid_vertex && m_sel_solid_body >= 0 && m_sel_solid_body < nb)
        return int(OfferSel::Vertex);
    if (m_sel_solid_edge >= 0 && m_sel_solid_body >= 0 && m_sel_solid_body < nb) {
        const TopoDS_Edge e = GeometryEngine::edge_by_index(m_doc.bodies[m_sel_solid_body].shape,
                                                            m_sel_solid_edge);
        return int(GeometryEngine::circle_of_edge(e).ok ? OfferSel::EdgeCirc : OfferSel::EdgeStr);
    }
    if (m_sel_solid_face >= 0 && m_sel_solid_body >= 0 && m_sel_solid_body < nb) {
        SketchPlane p;
        if (m_doc.plane_of_face(m_sel_solid_body, m_sel_solid_face, p))
            return int(OfferSel::FacePlanar);
        const TopoDS_Face f = GeometryEngine::face_by_index(m_doc.bodies[m_sel_solid_body].shape,
                                                            m_sel_solid_face);
        return int(GeometryEngine::cylinder_of_face(f).ok ? OfferSel::FaceCyl : OfferSel::FaceOther);
    }
    if (const int lf = m_viewport ? m_viewport->loop_pick_feature() : -1;
        lf >= 0 && lf < int(m_doc.features.size()) && m_doc.features[lf].type == CadFeatureType::Sketch
        && m_viewport->loop_pick_region() >= 0)
        return int(OfferSel::SkLoop);
    if (m_sel_solid_body >= 0 && m_sel_solid_body < nb)
        return int(CadDocument::is_sheet_shape(m_doc.bodies[m_sel_solid_body].shape)
                   ? OfferSel::BodySheet : OfferSel::BodySolid);
    return int(OfferSel::None);
}

void DesignPanel::run_offer_action(const char* action)
{
    if (!action || !*action)
        return;
    const std::string a(action);
    if (a.rfind("key:", 0) == 0) {
        const std::string k = a.substr(4);
        if (k.size() >= 3 && k[0] == 'S' && k[1] == '+') {
            auto it = m_keys_feature.find(int(k[2]) | SC_SHIFT);
            if (it != m_keys_feature.end() && it->second) it->second();
        } else if (!k.empty()) {
            auto it = m_keys_sketch.find(int(k[0]));
            if (it != m_keys_sketch.end() && it->second) it->second();
        }
        return;
    }
    auto it = m_verb_actions.find(a);
    if (it != m_verb_actions.end() && it->second)
        it->second();
}

bool DesignPanel::mcp_run_verb(const char* verb_id)
{
    if (!verb_id) return false;
    for (int i = 0; i < kOfferVerbCount; ++i) {
        const OfferVerb& v = kOfferVerbs[i];
        if (std::string(v.id) == verb_id) {
            if (v.action == nullptr) return false;
            run_offer_action(v.action);
            return true;
        }
    }
    return false;
}

wxPoint DesignPanel::offer_anchor() const
{
    const wxPoint mouse = wxGetMousePosition();
    if (!m_viewport)
        return mouse;
    const wxRect r = m_viewport->GetScreenRect();
    if (r.Contains(mouse))
        return mouse;
    return wxPoint(r.x + r.width / 2, r.y + r.height / 2);
}

wxString DesignPanel::kernel_error_text(const std::string& err)
{
    struct Map { const char* needle; const char* sentence; };
    static const Map kMap[] = {
        { "fuse failed",                  L("the new shape could not be joined to the body") },
        { "cut failed",                   L("the new shape could not be cut from the body") },
        { "intersect failed",             L("the shapes could not be intersected") },
        { "no solid-producing features",  L("nothing in the history makes a solid yet") },
        { "sketch profile wire failed",   L("the sketch profile is not a clean closed loop") },
        { "does not intersect",           L("the tool does not touch the body") },
        { "unknown identifier",           L("an expression uses a variable that does not exist") },
        { "division by zero",             L("an expression divides by zero") },
        { "variable cycle",               L("two variables are defined in terms of each other") },
    };
    if (err.empty())
        return _L("a geometry operation failed");
    for (const Map& m : kMap)
        if (err.find(m.needle) != std::string::npos)
            return _(m.sentence);
    if (err.find("OCCT") != std::string::npos || err.find("BRep") != std::string::npos
        || err.find("Standard_") != std::string::npos || err.find("StdFail") != std::string::npos)
        return _L("a geometry operation failed");
    return wxString::FromUTF8(err);
}

void DesignPanel::set_status(StatusKind kind, const wxString& body)
{
    if (m_status == nullptr) return;
    wxColour colour;
    const wchar_t* glyph = nullptr;
    switch (kind) {
    case StatusKind::Info:    colour = wxNullColour;              break;
    case StatusKind::Ok:      colour = wxColour(120, 210, 120);   glyph = L"\u2713 "; break;
    case StatusKind::Warning: colour = wxColour(220, 160, 60);    glyph = L"\u26A0 "; break;
    case StatusKind::Error:   colour = wxColour(235, 110, 110);   glyph = L"\u2717 "; break;
    }
    m_status->SetForegroundColour(colour);
    wxString text = body;
    if (glyph != nullptr && !text.IsEmpty() && !text.StartsWith(L"\u2713") && !text.StartsWith(L"\u2717")
        && !text.StartsWith(L"\u26A0"))
        text = wxString(glyph) + text;
    m_status->SetLabel(text);
    if (m_viewport != nullptr) {
        m_viewport->set_status_text(text, m_status->UseForegroundColour() ? m_status->GetForegroundColour()
                                                                          : wxNullColour);
    }
}


static wxString sketch_step_prompt(DesignSketchTool::Mode m, int step, int picks)
{
    using Mode = DesignSketchTool::Mode;
    auto pick_more = [](const wxString& lead, int n) {
        return n <= 0 ? lead
                      : lead + wxString::Format(_L("  ·  %d picked"), n);
    };
    switch (m) {
    case Mode::Select:
        return picks > 0
            ? wxString::Format(_L_PLURAL("%d selected  ·  Del removes it  ·  Shift-click adds  ·  "
                                         "double-click takes the whole loop",
                                         "%d selected  ·  Del removes them  ·  Shift-click adds  ·  "
                                         "double-click takes the whole loop", picks), picks)
            : _L("Select — click an entity to pick it  ·  drag an endpoint or center to move it  ·  "
                 "Shift-click adds  ·  Del removes");
    case Mode::Constrain:
        return picks > 0
            ? wxString::Format(_L("%d picked  ·  now choose a constraint (Horizontal, Parallel, "
                                  "Tangent, Equal…)"), picks)
            : _L("Constrain — click one or two entities, then choose a constraint");
    case Mode::Dimension:
        return step == 0 ? _L("Dimension — click an entity, or the first of two points")
                         : _L("Dimension — click the second point");
    case Mode::Line:
        return step == 0 ? _L("Line — click the start point")
                         : _L("Line — click the end point; its length and angle can then be typed");
    case Mode::Polyline:
        return step == 0 ? _L("Polyline — click the first point")
                         : pick_more(_L("Polyline — click the next point  ·  click the start point "
                                        "to close it  ·  right-click, double-click or Enter ends the chain"), step);
    case Mode::CornerRect:
        return step == 0 ? _L("Rectangle — click one corner")
                         : _L("Rectangle — click the opposite corner");
    case Mode::CenterRect:
        return step == 0 ? _L("Center rectangle — click the center")
                         : _L("Center rectangle — click a corner");
    case Mode::ObliqueRect:
        return step == 0 ? _L("Oblique rectangle — click the start of the base edge")
             : step == 1 ? _L("Oblique rectangle — click the end of the base edge (this sets the angle)")
                         : _L("Oblique rectangle — click to set the width");
    case Mode::RoundedRect:
        return step == 0 ? _L("Rounded rectangle — click one corner")
             : step == 1 ? _L("Rounded rectangle — click the opposite corner")
                         : _L("Rounded rectangle — click to set the corner radius");
    case Mode::CenterCircle:
        return step == 0 ? _L("Circle — click the center")
                         : _L("Circle — click to set the radius; it can then be typed");
    case Mode::TwoPointCircle:
        return step == 0 ? _L("Circle (2 points) — click one end of the diameter")
                         : _L("Circle (2 points) — click the other end of the diameter");
    case Mode::ThreePointCircle:
        return step == 0 ? _L("Circle (3 points) — click the first point on the circle")
             : step == 1 ? _L("Circle (3 points) — click the second point")
                         : _L("Circle (3 points) — click the third point");
    case Mode::ThreePointArc:
        return step == 0 ? _L("Arc — click the start point")
             : step == 1 ? _L("Arc — click the end point")
                         : _L("Arc — click a point the arc passes through");
    case Mode::TangentArc:
        return step == 0 ? _L("Tangent arc — click the endpoint it leaves from")
                         : _L("Tangent arc — click its far end");
    case Mode::CenterArc:
        return step == 0 ? _L("Center arc — click the center")
             : step == 1 ? _L("Center arc — click the start point (this sets the radius)")
                         : _L("Center arc — click the end point");
    case Mode::Slot:
        return step == 0 ? _L("Slot — click one end of the centerline")
             : step == 1 ? _L("Slot — click the other end of the centerline")
                         : _L("Slot — click to set the width");
    case Mode::ArcSlot:
        return step == 0 ? _L("Arc slot — click the center the slot curves about")
             : step == 1 ? _L("Arc slot — click the start of the centerline (this sets the radius)")
             : step == 2 ? _L("Arc slot — click the end of the centerline")
                         : _L("Arc slot — click to set the width");
    case Mode::Polygon:
        return step == 0 ? _L("Polygon — click the center")
                         : _L("Polygon — click a vertex (this sets size and orientation)");
    case Mode::Ellipse:
        return step == 0 ? _L("Ellipse — click the center")
             : step == 1 ? _L("Ellipse — click the end of the major axis")
                         : _L("Ellipse — click a point on the minor axis");
    case Mode::EllipseArc:
        return step == 0 ? _L("Elliptical arc — click the center")
             : step == 1 ? _L("Elliptical arc — click the end of the major axis")
             : step == 2 ? _L("Elliptical arc — click a point on the minor axis")
             : step == 3 ? _L("Elliptical arc — click where the arc starts")
                         : _L("Elliptical arc — click where the arc ends");
    case Mode::BSpline:
        return step == 0 ? _L("Spline — click the first control point")
                         : pick_more(_L("Spline — click the next control point  ·  right-click, "
                                        "double-click or Enter finishes the curve"), step);
    case Mode::Point:
        return _L("Point — click to place one; the tool stays armed for more");
    case Mode::Trim:
        return _L("Trim — click a segment where it crosses another entity  ·  Esc to exit");
    case Mode::Extend:
        return _L("Extend — click a line or arc to grow it out to the nearest entity  ·  "
                  "Esc to exit");
    case Mode::Fillet:
        return step == 0 ? _L("Fillet — click the first of two lines that meet")
             : step == 1 ? _L("Fillet — click the second line")
                         : _L("Fillet — drag the arrow, or click the number to type the radius  ·  "
                              "Enter applies  ·  Esc cancels");
    case Mode::Chamfer:
        return step == 0 ? _L("Chamfer — click the first of two lines that meet")
             : step == 1 ? _L("Chamfer — click the second line")
                         : _L("Chamfer — drag the arrow, or click the number to type the setback  ·  "
                              "Enter applies  ·  Esc cancels");
    case Mode::Offset:
        return step == 0 ? _L("Offset — click a curve; its whole outline is offset")
                         : _L("Offset — drag the arrow to either side, or click the number to type "
                              "the distance  ·  Enter applies  ·  Esc cancels");
    case Mode::Mirror:
        return step == 0
            ? _L("Mirror — first click the LINE to mirror about (a construction line works)")
            : picks == 0
                ? _L("Mirror — axis set  ·  now click the entities to mirror  ·  Esc cancels")
                : wxString::Format(_L("Mirror — axis set  ·  %d to mirror  ·  click another to add or "
                                      "remove it  ·  Enter applies"), picks);
    case Mode::Move:
        return step == 0 ? _L("Move — click the entities to move")
                         : pick_more(_L("Move — drag the handle, or click the number to type the "
                                        "distance  ·  Enter applies"), picks);
    case Mode::Rotate:
        return step == 0 ? _L("Rotate — click the entities to rotate")
                         : pick_more(_L("Rotate — drag the handle, or click the number to type the "
                                        "angle  ·  Enter applies"), picks);
    case Mode::Scale:
        return step == 0 ? _L("Scale — click the entities to scale")
                         : pick_more(_L("Scale — drag the handle, or click the number to type the "
                                        "factor  ·  Enter applies"), picks);
    case Mode::Array:
        return step == 0 ? _L("Array — click the entities to repeat")
                         : pick_more(_L("Array — drag the handle to set the step, click the count to "
                                        "type it  ·  Enter applies"), picks);
    case Mode::PolarArray:
        return step == 0 ? _L("Polar array — click the entities to repeat")
                         : pick_more(_L("Polar array — drag the handle to set the sweep, click the "
                                        "count to type it  ·  Enter applies"), picks);
    case Mode::TransformArt:
        return _L("Drag a corner to scale, the center to move  ·  Enter or ✓ when done, Esc to discard");
    }
    return wxString();
}

void DesignPanel::on_sketch_step(int mode, int step, int picks)
{
    wxString text = sketch_step_prompt(DesignSketchTool::Mode(mode), step, picks);
    if (step == 0 && picks == 0 && DesignSketchTool::Mode(mode) != DesignSketchTool::Mode::Select
        && !text.IsEmpty())
        text += _L("  ·  Esc goes back to Select");
    if (DesignSketchTool::Mode(mode) == DesignSketchTool::Mode::Select && m_viewport != nullptr &&
        m_viewport->sketch_constraint_count() > 0 && !text.IsEmpty())
        text += _L("  ·  click a constraint badge to remove it");
    m_sketch_step = text;
    if (text.IsEmpty() || m_status == nullptr) return;
    set_status(StatusKind::Info, text);
}

wxMenuItem* DesignPanel::append_offer_item(wxMenu* menu, int id, const wxString& text,
                                           const OfferVerb& v)
{
    auto* item = new wxMenuItem(menu, id, text);
    if (v.icon != nullptr && *v.icon != '\0') {
        const wxBitmap bmp = create_scaled_bitmap(v.icon, this, 16);
        if (bmp.IsOk())
            item->SetBitmap(bmp);
    }
    menu->Append(item);
    return item;
}

static void offer_trace(const char* fmt, ...)
{
    static const bool on = std::getenv("ORCA_CAD_KEYTRACE") != nullptr;
    if (!on) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[OFFER] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    fflush(stderr);
}

wxString DesignPanel::offer_header(int kind) const
{
    const int nb = int(m_doc.bodies.size());
    auto body = [&]() {
        if (m_sel_solid_body < 0 || m_sel_solid_body >= nb) return wxString();
        const std::string& n = m_doc.bodies[m_sel_solid_body].name;
        return n.empty() ? wxString::Format(_L("Body %d"), m_sel_solid_body + 1) : wxString::FromUTF8(n);
    };
    switch (OfferSel(kind)) {
    case OfferSel::None:       return _L("Nothing selected");
    case OfferSel::BodySolid:  return wxString::Format(_L("%s — solid body"), body());
    case OfferSel::BodySheet:  return wxString::Format(_L("%s — surface body"), body());
    case OfferSel::FacePlanar: return wxString::Format(_L("Flat face %d of %s"), m_sel_solid_face, body());
    case OfferSel::FaceCyl:    return wxString::Format(_L("Cylindrical face %d of %s"), m_sel_solid_face, body());
    case OfferSel::FaceOther:  return wxString::Format(_L("Face %d of %s"), m_sel_solid_face, body());
    case OfferSel::EdgeStr:
    case OfferSel::EdgeCirc:
        if (dressup_edges().size() > 1)
            return wxString::Format(_L_PLURAL("%zu edge of %s", "%zu edges of %s", dressup_edges().size()), dressup_edges().size(), body());
        return OfferSel(kind) == OfferSel::EdgeStr
            ? wxString::Format(_L("Straight edge %d of %s"), m_sel_solid_edge, body())
            : wxString::Format(_L("Circular edge %d of %s"), m_sel_solid_edge, body());
    case OfferSel::Vertex:     return wxString::Format(_L("Vertex of %s"), body());
    case OfferSel::SkLoop:     return _L("Sketch profile");
    case OfferSel::SkNone:     return _L("Sketch — nothing selected");
    case OfferSel::SkLine:     return _L("Sketch line");
    case OfferSel::SkArc:      return _L("Sketch curve");
    case OfferSel::SkPoint:    return _L("Sketch point");
    case OfferSel::Sk2Ent: {
        const int n = m_viewport ? m_viewport->sketch_selection_count() : 0;
        return wxString::Format(_L("%d sketch entities"), n);
    }
    default:                   return wxString();
    }
}

void DesignPanel::show_offer_menu(const wxPoint& screen_pos)
{
    const int      kind = offer_selection_kind();
    const uint32_t bit  = offer_bit(OfferSel(kind));
    const bool     sketching = sketch_map_applies();
    offer_trace("open kind=%d sketching=%d bodies=%d", kind, sketching ? 1 : 0,
                int(m_doc.bodies.size()));

    const int bodies = int(m_doc.bodies.size());
    int sketches = 0;
    for (const auto& f : m_doc.features)
        if (f.type == CadFeatureType::Sketch) ++sketches;
    bool sheet = false;
    for (const auto& b : m_doc.bodies)
        if (CadDocument::is_sheet_shape(b.shape)) { sheet = true; break; }

    auto applies = [&](const OfferVerb& v) {
        return (v.accepts & bit) && v.need_bodies <= bodies && v.need_sketches <= sketches
               && (!v.need_sheet || sheet);
    };
    auto tr = [](const char* s) {
        return wxGetTranslation(wxString::FromUTF8(s), SLIC3R_APP_KEY);
    };
    auto label = [&](const OfferVerb& v) {
        wxString s = I18N::translate(v.name, "Design");
        if (v.key && *v.key) s += "\t" + wxString::FromUTF8(v.key);
        return s;
    };

    wxMenu menu;
    std::vector<const OfferVerb*> bound;
    const int base = wxID_HIGHEST + 4200;

    {
        const wxString head = offer_header(kind);
        if (!head.empty()) {
            menu.Append(wxID_ANY, head)->Enable(false);
            menu.AppendSeparator();
        }
    }

    std::vector<int> rows;
    for (const bool flat : {true, false})
        for (int row = 0; row < kOfferRowCount; ++row)
            if (kOfferRowFlat[row] == flat) rows.push_back(row);
    bool flat_items = false;

    for (const int row : rows) {
        std::vector<const OfferVerb*> live, family;
        for (int i = 0; i < kOfferVerbCount; ++i) {
            const OfferVerb& v = kOfferVerbs[i];
            if (v.row != row || v.sketch_mode != sketching) continue;
            family.push_back(&v);
            if (applies(v)) live.push_back(&v);
        }
        if (family.empty())
            continue;

        if (kOfferRowFlat[row]) {
            for (const OfferVerb* v : family) {
                if (!(v->accepts & bit)) continue;
                const int id = base + int(bound.size());
                if (applies(*v)) {
                    offer_trace("row=%d %s -> %s%s", row, kOfferRowNames[row], v->id,
                                v->action ? "" : " (no GUI route)");
                    append_offer_item(&menu, id, label(*v), *v)->Enable(v->action != nullptr);
                    bound.push_back(v);
                } else {
                    offer_trace("row=%d %s DISABLED (%s)", row, kOfferRowNames[row],
                                v->refusal ? v->refusal : "blocked");
                    wxString s = tr(v->name);
                    if (v->refusal) s += wxString::FromUTF8("   —   ") + tr(v->refusal);
                    append_offer_item(&menu, id, s, *v)->Enable(false);
                    bound.push_back(nullptr);
                }
                flat_items = true;
            }
            continue;
        }
        if (flat_items) {
            menu.AppendSeparator();
            flat_items = false;
        }
        const wxString fam = tr(kOfferRowNames[row]);

        if (live.empty()) {
            const char* why = nullptr;
            for (const OfferVerb* v : family)
                if ((v->accepts & bit) && v->refusal) { why = v->refusal; break; }
            wxString s = fam;
            if (why)
                s += wxString::FromUTF8("   —   ") + tr(why);
            else if (OfferSel(kind) == OfferSel::None)
                s += wxString::FromUTF8("   —   ") + _L("select something first");
            offer_trace("row=%d %s DISABLED (%s)", row, kOfferRowNames[row],
                        why ? why : "no verb accepts this selection");
            menu.Append(base + int(bound.size()), s)->Enable(false);
            bound.push_back(nullptr);
        } else if (live.size() == 1 && std::none_of(family.begin(), family.end(), [&](const OfferVerb* v) {
                       return (v->accepts & bit) && !applies(*v); })) {
            offer_trace("row=%d %s -> %s%s", row, kOfferRowNames[row], live[0]->id,
                        live[0]->action ? "" : " (no GUI route)");
            append_offer_item(&menu, base + int(bound.size()), label(*live[0]), *live[0])
                ->Enable(live[0]->action != nullptr);
            bound.push_back(live[0]);
        } else {
            auto* sub = new wxMenu();
            std::vector<std::pair<std::string, wxMenu*>> groups;
            for (const OfferVerb* v : family) {
                const bool ok = applies(*v);
                if (!ok && !(v->accepts & bit)) continue;
                wxMenu* target = sub;
                if (v->family && *v->family) {
                    auto it = std::find_if(groups.begin(), groups.end(),
                                           [&](const auto& g) { return g.first == v->family; });
                    if (it == groups.end()) {
                        auto* g = new wxMenu();
                        groups.emplace_back(v->family, g);
                        sub->AppendSubMenu(g, tr(v->family));
                        target = g;
                    } else {
                        target = it->second;
                    }
                }
                if (!ok) {
                    offer_trace("row=%d %s ~ %s BLOCKED", row, kOfferRowNames[row], v->id);
                    wxString s = tr(v->name);
                    if (v->refusal) s += wxString::FromUTF8("   —   ") + tr(v->refusal);
                    append_offer_item(target, base + int(bound.size()), s, *v)->Enable(false);
                    bound.push_back(nullptr);
                    continue;
                }
                offer_trace("row=%d %s > %s%s%s%s", row, kOfferRowNames[row],
                            (v->family && *v->family) ? v->family : "",
                            (v->family && *v->family) ? " > " : "", v->id,
                            v->action ? "" : " (no GUI route)");
                append_offer_item(target, base + int(bound.size()), label(*v), *v)
                    ->Enable(v->action != nullptr);
                bound.push_back(v);
            }
            menu.AppendSubMenu(sub, fam);
        }
    }

    bool             mate_ghost = false;
    std::vector<int> cs_features;
    for (int i = 0; i < int(m_doc.features.size()); ++i)
        if (m_doc.features[i].type == CadFeatureType::CoordSys && m_doc.features[i].enabled)
            cs_features.push_back(i);
    if (cs_features.size() >= 2) {
        int cs_a = -1, cs_b = -1;
        if (m_active == Tool::Mate && m_mate_cs_a && m_mate_cs_b
            && m_mate_cs_a->GetSelection() != wxNOT_FOUND
            && m_mate_cs_b->GetSelection() != wxNOT_FOUND) {
            cs_a = int(reinterpret_cast<intptr_t>(m_mate_cs_a->GetClientData(m_mate_cs_a->GetSelection())));
            cs_b = int(reinterpret_cast<intptr_t>(m_mate_cs_b->GetClientData(m_mate_cs_b->GetSelection())));
        }
        if (cs_a < 0 || cs_b < 0) { cs_a = cs_features[0]; cs_b = cs_features[1]; }

        const int mate_base = base + 500;

        menu.AppendSeparator();
        const wxString name_a = wxString::FromUTF8(m_doc.features[cs_a].name);
        const wxString name_b = wxString::FromUTF8(m_doc.features[cs_b].name);
        menu.Append(mate_base, wxString::Format(_L("Mate: %s  →  %s"), name_a, name_b))->Enable(false);

        auto mate_kind_name = [](int kind) -> wxString {
            switch (kind) {
            case 0:  return _L("Fastened");
            case 1:  return _L("Planar");
            case 2:  return _L("Revolute");
            case 3:  return _L("Slider");
            default: return _L("Cylindrical");
            }
        };
        const std::vector<CadDocument::MateOption> opts = m_doc.mate_options(cs_a, cs_b);
        for (int i = 0; i < int(opts.size()); ++i) {
            const CadDocument::MateOption& o = opts[i];
            wxString label = mate_kind_name(o.kind);
            if (!o.viable)
                label += wxString::FromUTF8("   —   ") + wxString::FromUTF8(o.reason);
            menu.Append(mate_base + 1 + i, label)->Enable(o.viable);
        }

        auto drop_ghost = [this, &mate_ghost]() {
            if (!mate_ghost) return;
            m_viewport->clear_preview();
            m_viewport->set_body_hidden(false);
            m_viewport->repaint_now();
            mate_ghost = false;
        };
        menu.Bind(wxEVT_MENU_HIGHLIGHT,
                  [this, cs_a, cs_b, opts, &mate_ghost, drop_ghost](wxMenuEvent& e) {
            const int i = e.GetMenuId() - (mate_base + 1);
            if (i < 0 || i >= int(opts.size()) || !opts[i].viable) { drop_ghost(); return; }
            std::string err;
            mate_ghost = show_mate_ghost(opts[i].kind, cs_a, cs_b, 0.0, 0.0, false, err);
            m_viewport->repaint_now();
        });

        menu.Bind(wxEVT_MENU, [this, cs_a, cs_b, opts, drop_ghost](wxCommandEvent& e) {
            const int i = e.GetId() - (mate_base + 1);
            if (i < 0 || i >= int(opts.size()) || !opts[i].viable) return;
            drop_ghost();
            m_doc.checkpoint();
            int idx = m_doc.add_mate(opts[i].kind, cs_a, cs_b, 0.0, 0.0, false,
                                     "Mate" + std::to_string(++m_feature_counter));
            if (idx < 0) {
                set_status(StatusKind::Error, _L("Mate rejected"));
                return;
            }
            if (!recompute_guarded(_L("Rebuilding model…")))
                set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
            else
                set_status_ok();
            refresh_tree();
        });
    }

    menu.Bind(wxEVT_MENU_HIGHLIGHT, [this, &bound](wxMenuEvent& e) {
        const int i = e.GetMenuId() - base;
        if (i < 0 || i >= int(bound.size()) || bound[i] == nullptr || bound[i]->hint == nullptr)
            return;
        set_status(StatusKind::Info, wxGetTranslation(wxString::FromUTF8(bound[i]->hint), SLIC3R_APP_KEY));
        m_status->Update();
    }, base, base + 499);
    menu.Bind(wxEVT_MENU, [this, &bound](wxCommandEvent& e) {
        const int i = e.GetId() - base;
        if (i >= 0 && i < int(bound.size()) && bound[i])
            run_offer_action(bound[i]->action);
    }, base, base + 499);
    PopupMenu(&menu, ScreenToClient(screen_pos));
    if (mate_ghost) {
        m_viewport->clear_preview();
        m_viewport->set_body_hidden(false);
        m_viewport->request_repaint();
    }
}

void DesignPanel::apply_plane_refs(CadFeature& f) const
{
    f.plane_type       = (PlaneType)m_plane_type->GetSelection();
    f.plane_face_body  = m_pl_faceA_body;  f.plane_face  = m_pl_faceA;
    f.plane_face2_body = m_pl_faceB_body;  f.plane_face2 = m_pl_faceB;
    f.plane_edge_body  = m_pl_edgeA_body;  f.plane_edge  = m_pl_edgeA;
    f.plane_edge2_body = m_pl_edgeB_body;  f.plane_edge2 = m_pl_edgeB;
    f.plane_u_size     = m_plane_usize->GetValue();
    f.plane_v_size     = m_plane_vsize->GetValue();
}

void DesignPanel::refresh_plane_labels()
{
    auto txt = [](int idx) { return idx >= 0 ? wxString::Format("#%d", idx) : wxString(_L("(none)")); };
    if (m_plane_faceA_lbl) m_plane_faceA_lbl->SetLabel(txt(m_pl_faceA));
    if (m_plane_faceB_lbl) m_plane_faceB_lbl->SetLabel(txt(m_pl_faceB));
    if (m_plane_edgeA_lbl) m_plane_edgeA_lbl->SetLabel(txt(m_pl_edgeA));
    if (m_plane_edgeB_lbl) m_plane_edgeB_lbl->SetLabel(txt(m_pl_edgeB));
}

void DesignPanel::reset_plane_refs()
{
    m_pl_faceA_body = m_pl_faceA = -1;  m_pl_faceB_body = m_pl_faceB = -1;
    m_pl_edgeA_body = m_pl_edgeA = -1;  m_pl_edgeB_body = m_pl_edgeB = -1;
    m_plane_pick = PlanePick::None;
    if (m_viewport) m_viewport->set_escalate_on_repick(true);
    refresh_plane_labels();
}

void DesignPanel::arm_plane_pick(PlanePick target)
{
    m_plane_pick = target;
    if (m_viewport) m_viewport->set_escalate_on_repick(false);
    const bool face = (target == PlanePick::FaceA || target == PlanePick::FaceB);
    set_status(StatusKind::Info, face ? _L("Click a solid FACE in the viewport")
                            : _L("Click a solid EDGE in the viewport"));
}

void DesignPanel::apply_axis_refs(CadFeature& f) const
{
    f.axis_type = (AxisType)m_axis_type->GetSelection();
    f.axis_face = m_ax_face;
    f.axis_edge = m_ax_edge;
    f.axis_plane_a = m_axis_plane_a->GetSelection();
    f.axis_plane_b = m_axis_plane_b->GetSelection();
    f.axis_p1 = Vec3d(m_axis_p1x->GetValue(), m_axis_p1y->GetValue(), m_axis_p1z->GetValue());
    f.axis_p2 = Vec3d(m_axis_p2x->GetValue(), m_axis_p2y->GetValue(), m_axis_p2z->GetValue());
    f.axis_body = m_ax_face_body;
}

void DesignPanel::refresh_axis_labels()
{
    auto txt = [](int idx) { return idx >= 0 ? wxString::Format("#%d", idx) : wxString(_L("(none)")); };
    if (m_axis_face_lbl) m_axis_face_lbl->SetLabel(txt(m_ax_face));
    if (m_axis_edge_lbl) m_axis_edge_lbl->SetLabel(txt(m_ax_edge));
}

void DesignPanel::reset_axis_refs()
{
    m_ax_face_body = m_ax_face = -1;
    m_ax_edge = -1;
    m_axis_pick = AxisPick::None;
    if (m_viewport) m_viewport->set_escalate_on_repick(true);
    refresh_axis_labels();
}

void DesignPanel::arm_axis_pick(AxisPick target)
{
    m_axis_pick = target;
    if (m_viewport) m_viewport->set_escalate_on_repick(false);
    set_status(StatusKind::Info, target == AxisPick::Face ? _L("Click a solid FACE in the viewport")
                                                : _L("Click a solid EDGE in the viewport"));
}

void DesignPanel::apply_coordsys_refs(CadFeature& f) const
{
    f.coordsys_type  = (CoordSysType)m_coordsys_type->GetSelection();
    f.coordsys_point = Vec3d(m_cs_x->GetValue(), m_cs_y->GetValue(), m_cs_z->GetValue());
    f.coordsys_body  = m_cs_face_body;
    f.coordsys_face  = m_cs_face;
    f.coordsys_edge  = m_cs_edge;
    f.coordsys_x_hint = Vec3d(m_cs_hx->GetValue(), m_cs_hy->GetValue(), m_cs_hz->GetValue());
}

void DesignPanel::refresh_coordsys_labels()
{
    auto txt = [](int idx) { return idx >= 0 ? wxString::Format("#%d", idx) : wxString(_L("(none)")); };
    if (m_cs_face_lbl) m_cs_face_lbl->SetLabel(txt(m_cs_face));
    if (m_cs_edge_lbl) m_cs_edge_lbl->SetLabel(txt(m_cs_edge));
}

void DesignPanel::refresh_cs_body_choice()
{
    if (!m_cs_body) return;
    const int keep = m_cs_body->GetSelection();
    m_cs_body->Clear();
    m_cs_body->Append(_L("(all)"));
    for (size_t b = 0; b < m_doc.bodies.size(); ++b)
        m_cs_body->Append(wxString::Format(_L("Body %d"), int(b) + 1));
    const int sel = (keep > 0 && keep < int(m_cs_body->GetCount())) ? keep : 0;
    m_cs_body->SetSelection(sel);
    if (m_viewport != nullptr && m_active == Tool::CoordSys)
        m_viewport->set_xray_focus(sel - 1);
}

void DesignPanel::reset_coordsys_refs()
{
    m_cs_face_body = m_cs_face = -1;
    m_cs_edge = -1;
    m_coordsys_pick = CoordSysPick::None;
    if (m_viewport) m_viewport->set_escalate_on_repick(true);
    refresh_coordsys_labels();
    if (m_cs_body) m_cs_body->SetSelection(0);
    if (m_viewport) m_viewport->set_xray_focus(-1);
    refresh_cs_body_choice();
}

void DesignPanel::arm_coordsys_pick(CoordSysPick target)
{
    m_coordsys_pick = target;
    if (m_viewport) m_viewport->set_escalate_on_repick(false);
    set_status(StatusKind::Info, target == CoordSysPick::Face ? _L("Click a solid FACE in the viewport")
                                                    : _L("Click a solid EDGE in the viewport"));
}

bool DesignPanel::on_add_plane()
{
    auto refuse = [this](const wxString& why) {
        set_status(StatusKind::Error, why);
    };
    switch ((PlaneType)m_plane_type->GetSelection()) {
    case PlaneType::Angle:
        if (m_pl_edgeA < 0) { refuse(_L("An angled plane needs an edge to tilt about — pick Edge A")); return false; }
        break;
    case PlaneType::Midplane:
        if (m_pl_faceA < 0 || m_pl_faceB < 0) { refuse(_L("A midplane needs two faces — pick Face A and Face B")); return false; }
        if (m_pl_faceA == m_pl_faceB && m_pl_faceA_body == m_pl_faceB_body) {
            refuse(_L("Face A and Face B are the same face — a midplane needs two different faces"));
            return false;
        }
        break;
    case PlaneType::Tangent:
        if (m_pl_faceA < 0) { refuse(_L("A tangent plane needs a cylindrical face — pick Face A")); return false; }
        break;
    case PlaneType::TwoEdges:
        if (m_pl_edgeA < 0 || m_pl_edgeB < 0) { refuse(_L("This plane needs two edges — pick Edge A and Edge B")); return false; }
        break;
    default:
        break;
    }

    m_feature_counter++;
    int idx = m_doc.add_plane(m_plane_base->GetSelection(), m_plane_offset->GetValue(),
                    m_plane_tilt->GetValue(), m_plane_tilt_axis->GetSelection(),
                    feature_name(_L("Plane")));
    if (idx >= 0 && idx < int(m_doc.features.size())) apply_plane_refs(m_doc.features[idx]);
    m_doc.recompute();
    set_status(StatusKind::Info, _L("Plane added — pick it as a sketch plane"));
    refresh_tree();
    return true;
}

void DesignPanel::on_add_axis()
{
    m_feature_counter++;
    int idx = m_doc.add_axis((AxisType)m_axis_type->GetSelection(),
                             feature_name(_L("Axis")));
    if (idx >= 0 && idx < int(m_doc.features.size())) apply_axis_refs(m_doc.features[idx]);
    m_doc.recompute();
    set_status(StatusKind::Info, _L("Axis added"));
    refresh_tree();
}

void DesignPanel::on_add_coordsys()
{
    m_feature_counter++;
    Vec3d pt(m_cs_x->GetValue(), m_cs_y->GetValue(), m_cs_z->GetValue());
    int idx = m_doc.add_coordsys((CoordSysType)m_coordsys_type->GetSelection(), pt,
                                 feature_name(_L("Coordinate system")));
    if (idx >= 0 && idx < int(m_doc.features.size())) apply_coordsys_refs(m_doc.features[idx]);
    m_doc.recompute();
    set_status(StatusKind::Info, _L("Coordinate system added"));
    refresh_tree();
}

void DesignPanel::on_add_shell()
{
    if (m_doc.body.IsNull()) {
        set_status(StatusKind::Warning, _L("Shell needs a solid body — add or import one first"));
        return;
    }
    const int face = (m_sel_solid_face >= 0) ? m_sel_solid_face : -1;

    m_feature_counter++;
    m_doc.add_shell(m_shell_thickness->GetValue(), face, m_sel_solid_body,
                    feature_name(_L("Shell")));

    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();

    refresh_tree();
}

void DesignPanel::on_add_draft()
{
    if (m_doc.body.IsNull()) {
        set_status(StatusKind::Warning, _L("Draft needs a solid body — add or import one first"));
        return;
    }
    if (m_sel_solid_face < 0) {
        set_status(StatusKind::Error, _L("Draft needs a picked face — click a side face first"));
        return;
    }

    m_feature_counter++;
    m_doc.add_draft(m_draft_angle->GetValue(), m_sel_solid_face, m_sel_solid_body,
                    feature_name(_L("Draft")));

    if (!recompute_guarded(_L("Rebuilding model…")))
        set_status(StatusKind::Error, wxString::Format(_L("The model could not be rebuilt: %s"), kernel_error_text(m_doc.error)));
    else
        set_status_ok();

    refresh_tree();
}

const char* DesignPanel::tree_icon_for(CadFeatureType t)
{
    switch (t) {
    case CadFeatureType::Sketch:
    case CadFeatureType::Plane:
    case CadFeatureType::Axis:
    case CadFeatureType::CoordSys:
    case CadFeatureType::Project:        return "design_sketch";
    case CadFeatureType::Fillet:
    case CadFeatureType::Chamfer:
    case CadFeatureType::Mate:           return "design_dressup";
    case CadFeatureType::Hole:           return "design_hole";
    case CadFeatureType::Thread:
    case CadFeatureType::Helix:          return "design_thread";
    case CadFeatureType::Shell:
    case CadFeatureType::Draft:
    case CadFeatureType::DeleteFace:     return "design_shell";
    case CadFeatureType::Extrude:
    case CadFeatureType::Revolve:
    case CadFeatureType::Sweep:
    case CadFeatureType::Loft:
    case CadFeatureType::Pattern:
    case CadFeatureType::Import:
    case CadFeatureType::Boolean:
    case CadFeatureType::Cut:
    case CadFeatureType::Transform:
    case CadFeatureType::Mirror:
    case CadFeatureType::Thicken:
    case CadFeatureType::Rib:
    case CadFeatureType::SurfaceExtrude:
    case CadFeatureType::SurfaceRevolve:
    case CadFeatureType::SurfaceLoft:
    case CadFeatureType::SurfaceFill:
    case CadFeatureType::ThickenSurface:
    case CadFeatureType::SurfaceOffset:  return "design_extrude";
    }
    return "design_sketch";
}

const std::string* DesignPanel::mate_conflict_reason(int feature) const
{
    for (const auto& c : m_doc.mate_conflicts)
        if (c.first == feature) return &c.second;
    return nullptr;
}

wxString DesignPanel::idle_hint() const
{
    return m_doc.features.empty()
        ? _L("Nothing yet — import a STEP or a mesh from the toolbar,\n"
             "or press Sketch and click a reference plane to start a sketch.")
        : _L("No solid yet — select a sketch and right-click it to Extrude.");
}

void DesignPanel::on_tab_hidden()
{
    if (m_viewport)
        m_viewport->leave_viewport();
    update_sidebar_pane();
}

void DesignPanel::on_tab_shown()
{
    if (m_viewport)
        m_viewport->enter_viewport();

    if (m_active == Tool::None && m_doc.display_mesh.its.indices.empty())
        set_status(idle_hint());

    wxStopWatch show_clock;
    if (m_viewport) m_viewport->refresh_bed();

    hydrate_from_model();
    update_reference_planes();
    if (show_clock.Time() > 100)
        BOOST_LOG_TRIVIAL(info) << "Design tab shown: bed, project recipe and planes in " << show_clock.Time() << " ms";
    m_laid_out = true;
    update_sidebar_pane();
    if (m_viewport) m_viewport->force_repaint();
}

void DesignPanel::refresh_icons()
{
    std::function<void(wxWindow*)> walk = [&walk](wxWindow* w) {
        if (auto* b = dynamic_cast<ScalableButton*>(w))
            b->msw_rescale();
        else if (auto* b = dynamic_cast<::Button*>(w)) {
            b->Rescale();
            if (b->GetName() == "design_icon_btn")
                style_sidebar_icon_btn(b);
        } else if (auto* l = dynamic_cast<DesignRowList*>(w))
            l->Rescale();
        else if (auto* c = dynamic_cast<::CheckBox*>(w))
            c->Rescale();
        else if (auto* c = dynamic_cast<::ComboBox*>(w))
            c->Rescale();
        for (wxWindow* child : w->GetChildren())
            walk(child);
    };
    walk(this);
    for (auto& refresh : m_icon_refresh)
        refresh();
}

void DesignPanel::msw_rescale()
{
    refresh_icons();
    if (m_viewport) m_viewport->Refresh();
    Layout();
}

void DesignPanel::on_sys_color_changed()
{
    m_aui.apply_color_mode();
    const bool to_dark = dp_dark();
    std::function<void(wxWindow*)> walk = [&walk, to_dark](wxWindow* w) {
        if (w->UseBackgroundColour())
            w->SetBackgroundColour(dp_retheme(w->GetBackgroundColour(), to_dark));
        if (w->UseForegroundColour())
            w->SetForegroundColour(dp_retheme(w->GetForegroundColour(), to_dark));
        for (wxWindow* child : w->GetChildren())
            walk(child);
    };
    walk(this);
    wxGetApp().UpdateDarkUIWin(this);
    refresh_icons();
    refresh_tree();
    refresh_pinned();
    Refresh();
}

void DesignPanel::hydrate_from_model()
{
    if (!m_doc.features.empty()) return;
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr) return;
    if (!m_doc.origin_from_recipe) {
        m_viewport->refresh_bed();
        const Vec2d bc = m_viewport->bed_center();
        m_doc.modeling_origin = Vec3d(bc.x(), bc.y(), 0.0);
        m_doc.auto_close_loops = wxGetApp().is_auto_close_sketch_loops();
        Slic3r::set_sketch_auto_close(m_doc.auto_close_loops);
    }
    const std::string& blob = plater->model().cad_recipe;
    if (!blob.empty()) load_recipe(blob);
}

void DesignPanel::unbind_canvas_event_handlers()
{
    if (m_viewport) m_viewport->unbind_canvas_event_handlers();
}

void DesignPanel::reset_canvas_volumes()
{
    if (m_viewport) m_viewport->reset_canvas_volumes();
}

void DesignPanel::shutdown()
{
    wxAuiPaneInfo& sidebar = m_aui.GetPane(m_form);
    if (!sidebar.IsOk())
        return;
    sidebar.Show(!m_sidebar_collapsed);
    wxGetApp().app_config->set("design_window_layout", m_aui.SavePerspective().utf8_string());
    if (sidebar.IsFloating())
        m_aui.DetachPane(m_form);
}

void DesignPanel::reset_window_layout()
{
    load_default_layout();
    collapse_sidebar(false);
    update_sidebar_pane(true);
}

void DesignPanel::load_window_layout()
{
    const wxString saved = wxString::FromUTF8(wxGetApp().app_config->get("design_window_layout"));
    if (saved.empty() || !m_aui.LoadPerspective(saved, false)) {
        if (!saved.empty())
            BOOST_LOG_TRIVIAL(warning) << "Design tab: failed to restore the saved window layout";
        load_default_layout();
    }
    wxAuiPaneInfo& sidebar = m_aui.GetPane(m_form);
    m_sidebar_collapsed = !sidebar.IsShown();
    if ((m_aui.GetFlags() & wxAUI_MGR_ALLOW_FLOATING) == 0)
        sidebar.Dock().Floatable(false);
    update_sidebar_pane(true);
}

void DesignPanel::load_default_layout()
{
    m_aui.LoadPerspective(m_default_layout, false);
    Plater* plater = wxGetApp().plater();
    const Sidebar::DockingState prepare = plater != nullptr ? plater->get_sidebar_docking_state() : Sidebar::None;
    if (prepare == Sidebar::None)
        return;
    wxAuiPaneInfo& sidebar = m_aui.GetPane(m_form);
    if (prepare == Sidebar::Right)
        sidebar.Right();
    const int width = plater->sidebar().GetSize().GetWidth();
    if (width >= sidebar.min_size.GetWidth())
        sidebar.BestSize(width, sidebar.best_size.GetHeight());
}

void DesignPanel::collapse_sidebar(bool collapse)
{
    m_sidebar_collapsed = collapse;
    if (m_viewport)
        m_viewport->set_sidebar_collapse_tooltip(wxGetApp().shortcuts().with_key(
            (collapse ? _L("Expand sidebar") : _L("Collapse sidebar")).utf8_string(), Shortcut::CollapseSidebar));
    update_sidebar_pane();
}

void DesignPanel::update_sidebar_pane(bool force_update)
{
    wxAuiPaneInfo& sidebar = m_aui.GetPane(m_form);
    const bool     show    = !m_sidebar_collapsed && m_laid_out && (sidebar.IsDocked() || IsShownOnScreen());
    if (sidebar.IsOk() && sidebar.IsShown() != show) {
        sidebar.Show(show);
        force_update = true;
    }
    if (force_update)
        m_aui.Update();
}

void DesignPanel::load_recipe(const std::string& blob)
{
    if (blob.empty()) return;
    if (!m_doc.deserialize_recipe(blob)) {
        set_status(StatusKind::Error, m_doc.error.empty()
                   ? _L("Could not restore the CAD model from this project")
                   : wxString::Format(_L("Could not restore the CAD model: %s"), wxString::FromUTF8(m_doc.error)));
        return;
    }
    m_feature_counter = int(m_doc.features.size());
    drop_selection();
    feed_bodies();
    refresh_tree();
    set_status_ok();
}

void DesignPanel::refresh_tree()
{
    if (!m_doc.features.empty()) sync_recipe_to_model();

    const int keep = m_tree->selection();

    refresh_datum_planes();
    update_reference_planes();
    std::vector<DesignRowList::Row> rows;
    rows.reserve(m_doc.features.size());
    for (size_t fi = 0; fi < m_doc.features.size(); ++fi) {
        const CadFeature& f = m_doc.features[fi];
        DesignRowList::Row row;
        row.icon  = tree_icon_for(f.type);
        row.label = wxString::FromUTF8(f.name);
        row.colour = !f.enabled                            ? dp_item_dim()
                   : mate_conflict_reason(int(fi)) != nullptr ? wxColour(235, 110, 110)
                                                              : dp_item_text();
        row.actions = {
            { RowZoom, can_zoom_to_feature(int(fi)) ? "design_zoom" : "", _L("Zoom to selection") },
            { RowEdit, "design_edit", _L("Edit") },
            eye_action(f.enabled),
            { RowDelete, "design_delete", _L("Delete") },
        };
        rows.push_back(std::move(row));
    }
    m_tree->set_rows(std::move(rows));
    refresh_parts();
    m_tree->select(keep);
    request_feature_highlight();
    if (m_form && m_form->GetSizer()) { update_cards_frame(); m_form->Layout(); m_form->FitInside(); }
}

void DesignPanel::refresh_parts()
{
    if (m_parts == nullptr) return;
    const int keep = tree_body_selection();

    sync_body_visible();
    std::vector<DesignRowList::Row> rows;
    rows.reserve(m_doc.bodies.size());
    for (size_t b = 0; b < m_doc.bodies.size(); ++b) {
        const bool vis = b >= m_body_visible.size() || m_body_visible[b];
        const wxString bname = m_doc.bodies[b].has_user_name
                             ? wxString::FromUTF8(m_doc.bodies[b].user_name)
                             : wxString::FromUTF8(m_doc.bodies[b].name);
        DesignRowList::Row row;
        row.label = bname.IsEmpty() ? wxString::Format(_L("Body %zu"), b + 1)
                                    : wxString::Format(_L("Body %zu — %s"), b + 1, bname);
        row.edit_text = bname;
        row.colour  = vis ? dp_item_text() : dp_item_dim();
        row.actions = {
            { RowZoom, "design_zoom", _L("Zoom to selection") },
            { RowMove, "design_move", _L("Move") },
            eye_action(vis),
            { RowDelete, "design_delete", _L("Delete") },
        };
        rows.push_back(std::move(row));
    }
    m_parts->set_rows(std::move(rows));

    const bool any = m_parts->GetItemCount() > 0;
    m_parts->Show(any);
    if (m_parts_label) m_parts_label->Show(any);
    if (m_parts_hdr)   m_parts_hdr->ShowItems(any);
    if (m_parts_rule)  m_parts_rule->Show(any);
    if (m_parts_box && m_form && m_form->GetSizer()) m_form->GetSizer()->Show(m_parts_box, any, false);

    if (any)
        m_parts->select(keep);
    if (m_form && m_form->GetSizer()) { update_cards_frame(); m_form->Layout(); m_form->FitInside(); }
}

int DesignPanel::tree_body_selection() const
{
    return m_parts == nullptr ? -1 : m_parts->selection();
}

bool DesignPanel::deselect_rows()
{
    const bool any = (m_tree && m_tree->selection() != wxNOT_FOUND) || tree_body_selection() >= 0;
    if (m_tree) m_tree->unselect();
    if (m_parts) m_parts->unselect();
    return any;
}

bool DesignPanel::drop_plane_pick()
{
    if (!m_plane_picked) return false;
    m_plane_picked = false;
    if (m_viewport) m_viewport->request_repaint();
    return true;
}

void DesignPanel::update_section_flip_btn()
{
    if (m_section_flip_btn) m_section_flip_btn->Enable(m_section_on);
}

void DesignPanel::toggle_section_view()
{
    if (!m_viewport) return;
    m_section_on = !m_section_on;
    if (m_section_on) {
        m_section_cut_z = m_viewport->model_mid_z();
        m_section_upper = false;
        m_viewport->set_section_plane(true, m_section_cut_z, m_section_upper);
        set_status(_L("Section view on — hides half the model to see inside; "
                              "PageUp / PageDown move the plane, Flip shows the other half"));
    } else {
        m_viewport->set_section_plane(false, 0.0);
        set_status(_L("Section view off"));
    }
    m_status->Refresh();
    update_section_flip_btn();
}

void DesignPanel::flip_section_view()
{
    if (!m_viewport || !m_section_on) return;
    m_section_upper = !m_section_upper;
    m_viewport->set_section_plane(true, m_section_cut_z, m_section_upper);
    set_status(StatusKind::Info, m_section_upper ? _L("Section view — showing the upper half")
                                                 : _L("Section view — showing the lower half"));
}

void DesignPanel::sync_body_visible()
{
    m_body_visible.resize(m_doc.bodies.size(), true);
}

void DesignPanel::sync_body_xform()
{
    m_body_xform.resize(m_doc.bodies.size(), Transform3d::Identity());
}

void DesignPanel::rebuild_disp_meshes()
{
    sync_body_visible();
    sync_body_xform();
    const std::vector<TriangleMesh>& src = m_doc.display_body_meshes;

    bool any = false;
    for (const Transform3d& t : m_body_xform)
        if (!t.isApprox(Transform3d::Identity())) { any = true; break; }

    if (!any) {
        m_disp_body_meshes = src;
        m_disp_pick_mesh   = m_doc.display_mesh;
        return;
    }

    m_disp_body_meshes.clear();
    m_disp_body_meshes.reserve(src.size());
    m_disp_pick_mesh = TriangleMesh{};
    for (size_t b = 0; b < src.size(); ++b) {
        TriangleMesh m = src[b];
        if (b < m_body_xform.size()) m.transform(m_body_xform[b]);
        m_disp_pick_mesh.merge(m);
        m_disp_body_meshes.push_back(std::move(m));
    }
}

void DesignPanel::feed_bodies()
{
    update_body_gates();
    if (m_viewport == nullptr) return;
    rebuild_disp_meshes();
    m_viewport->set_bodies(&m_disp_body_meshes, m_body_visible);
    if (m_hl_generation != m_doc.topo_generation)
        request_feature_highlight();
}

void DesignPanel::request_feature_highlight()
{
    if (m_hl_pending) return;
    m_hl_pending = true;
    CallAfter([this] { update_feature_highlight(); });
}

void DesignPanel::update_feature_highlight()
{
    m_hl_pending = false;
    if (m_viewport == nullptr) return;
    if (s_doc_worker_busy.load() > 0) return;
    const int  sel    = tree_selection();
    const bool wanted = sel >= 0 && sel < int(m_doc.features.size()) && m_active == Tool::None;
    m_viewport->set_highlight_faces(wanted ? feature_faces(sel) : std::vector<std::pair<int, int>>{});
}

const std::vector<std::pair<int, int>>& DesignPanel::feature_faces(int f)
{
    if (f < 0 || f >= int(m_doc.features.size())) {
        static const std::vector<std::pair<int, int>> none;
        return none;
    }
    if (f != m_hl_feature || m_hl_generation != m_doc.topo_generation) {
        std::vector<std::pair<int, int>> faces;
        const CadFeature& feat = m_doc.features[f];
        if (feat.enabled && CadDocument::produces_body(feat.type))
            run_off_ui_thread(this, _L("Finding the feature's faces…"), [this, f, &faces] {
                try {
                    faces = m_doc.faces_made_by(f);
                } catch (...) {
                    faces.clear();
                }
            });
        m_hl_faces      = std::move(faces);
        m_hl_feature    = f;
        m_hl_generation = m_doc.topo_generation;
    }
    return m_hl_faces;
}

bool DesignPanel::can_zoom_to_feature(int f) const
{
    if (f < 0 || f >= int(m_doc.features.size()))
        return false;
    const CadFeature& feat = m_doc.features[f];
    return feat.type == CadFeatureType::Sketch || (feat.enabled && CadDocument::produces_body(feat.type));
}

void DesignPanel::zoom_to_feature(int f)
{
    if (s_doc_worker_busy.load() > 0) {
        set_status(StatusKind::Info, _L("The model is being rebuilt — zoom again when it finishes"));
        return;
    }
    if (m_viewport == nullptr || !can_zoom_to_feature(f))
        return;
    const CadFeature& feat = m_doc.features[f];
    const wxString    name = wxString::FromUTF8(feat.name);
    BoundingBoxf3     box;
    if (feat.type == CadFeatureType::Sketch) {
        const auto add = [&box, &feat](const Vec2d& p) { box.merge(feat.plane.to_world(p)); };
        if (!feat.entities.empty()) {
            box = DesignSketchTool::sketch_box(feat.entities, feat.plane);
        } else if (!feat.imported_regions.empty()) {
            for (const auto& region : transform_regions(feat.imported_regions, feat.import_offset,
                                                        feat.import_scale_x, feat.import_scale_y))
                for (const auto& contour : region)
                    for (const Vec2d& p : contour)
                        add(p);
        } else if (!feat.profile.points.empty()) {
            for (const Vec2d& p : feat.profile.points)
                add(p);
        } else {
            const Vec2d h = feat.shape == SketchShape::Circle ? Vec2d(feat.radius, feat.radius)
                                                              : Vec2d(feat.width, feat.height) * 0.5;
            for (const Vec2d& p : { Vec2d(-h.x(), -h.y()), Vec2d(h.x(), -h.y()), Vec2d(h.x(), h.y()), Vec2d(-h.x(), h.y()) })
                add(p);
        }
    } else {
        box = m_viewport->faces_box(feature_faces(f));
    }
    if (!m_viewport->zoom_to_box(box))
        set_status(StatusKind::Info, wxString::Format(_L("%s has nothing on show to zoom to"), name));
}

void DesignPanel::zoom_to_body(int b)
{
    if (m_viewport == nullptr)
        return;
    BoundingBoxf3 box;
    if (b >= 0 && b < int(m_disp_body_meshes.size()) && !m_disp_body_meshes[b].empty())
        box = m_disp_body_meshes[b].bounding_box();
    if (!m_viewport->zoom_to_box(box))
        set_status(StatusKind::Error, _L("That body has no display mesh yet — recompute first"));
}

void DesignPanel::zoom_to_selection()
{
    if (m_viewport == nullptr)
        return;
    const OfferSel kind = OfferSel(offer_selection_kind());
    if (kind == OfferSel::BodySolid || kind == OfferSel::BodySheet) {
        zoom_to_body(m_sel_solid_body);
        return;
    }
    if (!m_viewport->zoom_to_box(m_viewport->selection_box()))
        set_status(StatusKind::Info, _L("Select something to zoom to"));
}

void DesignPanel::on_boolean_tool()
{
    if (m_doc.bodies.size() < 2) {
        set_status(StatusKind::Warning, _L("Boolean needs two bodies — add or import a second one"));
        return;
    }
    populate_body_choices();
    open_tool(Tool::Boolean);
}

void DesignPanel::on_move_body()
{
    const int b = m_sel_solid_body;
    if (m_viewport == nullptr) return;
    if (m_active != Tool::None || m_ui_mode != UiMode::Feature || m_text_dlg != nullptr) {
        set_status(StatusKind::Info, _L("Finish or cancel what is open first"));
        return;
    }
    if (b < 0 || b >= int(m_doc.display_body_meshes.size())) {
        set_status(StatusKind::Error, b < 0 ? _L("Select a body first — click it in the viewport or the Bodies list")
                                 : _L("That body has no display mesh yet — recompute first"));
        return;
    }
    const wxString hint = _L("Drag the arrows to move, the rings to rotate — then Confirm (Esc cancels)");
    if (body_move_pending() && b == m_move_body) {
        set_status(StatusKind::Info, hint);
        return;
    }
    end_sketch_plane_choice();
    sync_body_xform();
    const Transform3d base = m_body_xform[b];
    const BoundingBoxf3 bb = m_doc.display_body_meshes[b].bounding_box();
    const Vec3d pivot = base * bb.center();
    const double radius = (base.linear() * (bb.size() * 0.5)).norm();
    m_viewport->begin_move_body(b, pivot, base, radius);
    m_move_body = b;
    m_move_prev = base;
    if (m_move_dx) m_move_dx->SetValue(0.0);
    if (m_move_dy) m_move_dy->SetValue(0.0);
    if (m_move_dz) m_move_dz->SetValue(0.0);
    if (m_move_angle) m_move_angle->SetValue(0.0);
    show_move_card(true);
    update_action_bar();
    set_status(StatusKind::Info, hint);
}

void DesignPanel::end_body_move(bool keep)
{
    if (!body_move_pending()) return;
    if (!keep) {
        sync_body_xform();
        if (m_move_body < int(m_body_xform.size()))
            m_body_xform[m_move_body] = m_move_prev;
        feed_bodies();
    }
    m_viewport->clear_move_gizmo();
    m_move_body = -1;
    show_move_card(false);
    update_action_bar();
}

void DesignPanel::arm_transform_gizmo()
{
    int b = tree_body_selection();
    if (b < 0) b = m_sel_solid_body;
    if (b < 0 && m_xf_body) b = m_xf_body->GetSelection();
    if (b < 0 || b >= int(m_doc.display_body_meshes.size())) return;
    if (m_xf_body && b < int(m_xf_body->GetCount())) m_xf_body->SetSelection(b);
    sync_body_xform();
    const Transform3d base = m_body_xform[b];
    const BoundingBoxf3 bb = m_doc.display_body_meshes[b].bounding_box();
    const Vec3d pivot = base * bb.center();
    const double radius = (base.linear() * (bb.size() * 0.5)).norm();
    if (m_viewport) m_viewport->begin_move_body(b, pivot, base, radius);
    m_xf_gizmo_body = b;
    m_xf_gizmo_base = base;
    if (m_xf_pivot_x) m_xf_pivot_x->SetValue(pivot.x());
    if (m_xf_pivot_y) m_xf_pivot_y->SetValue(pivot.y());
    if (m_xf_pivot_z) m_xf_pivot_z->SetValue(pivot.z());
    if (m_xf_dx) m_xf_dx->SetValue(0.0);
    if (m_xf_dy) m_xf_dy->SetValue(0.0);
    if (m_xf_dz) m_xf_dz->SetValue(0.0);
    if (m_xf_angle) m_xf_angle->SetValue(0.0);
    set_status(StatusKind::Info, _L("Drag the arrows to move, the rings to rotate — the numbers follow"));
}

void DesignPanel::on_set_body_color()
{
    int b = tree_body_selection();
    if (b < 0) b = m_sel_solid_body;
    if (b < 0 || b >= int(m_doc.bodies.size())) {
        set_status(StatusKind::Error, _L("Select a body first"));
        return;
    }

    const ColorRGBA cur = (m_viewport != nullptr) ? m_viewport->body_color(b)
                                                  : m_doc.bodies[b].color;
    wxColourData data;
    data.SetColour(wxColour(cur.r_uchar(), cur.g_uchar(), cur.b_uchar()));
    wxColourDialog dlg(this, &data);
    if (dlg.ShowModal() != wxID_OK) return;

    const wxColour picked = dlg.GetColourData().GetColour();
    m_doc.bodies[b].has_color = true;
    m_doc.bodies[b].color = ColorRGBA((unsigned char)picked.Red(), (unsigned char)picked.Green(),
                                      (unsigned char)picked.Blue(), (unsigned char)255);
    feed_bodies();

    set_status(StatusKind::Info, wxString::Format(_L("Body %d color set"), b + 1));
}

bool DesignPanel::place_on_face()
{
    const int b = m_sel_solid_body;
    if (b < 0 || b >= int(m_doc.bodies.size()) || m_sel_solid_face < 0
        || b >= int(m_doc.display_body_meshes.size())) {
        set_status(StatusKind::Error, _L("Click a face on the solid, then press F"));
        return false;
    }
    const TopoDS_Face face = GeometryEngine::face_by_index(m_doc.bodies[b].shape, m_sel_solid_face);
    if (face.IsNull()) return false;
    end_sketch_plane_choice();
    sync_body_xform();
    const Transform3d old_x = m_body_xform[b];
    const Vec3d n = (old_x.linear() * GeometryEngine::face_normal_world(face)).normalized();
    if (!n.allFinite() || n.norm() < 0.5) return false;
    const Transform3d R(Eigen::Quaterniond().setFromTwoVectors(n, -Vec3d::UnitZ()));
    const Vec3d c = old_x * m_doc.display_body_meshes[b].bounding_box().center();
    Transform3d x = Eigen::Translation3d(c) * R * Eigen::Translation3d(-c) * old_x;
    TriangleMesh probe = m_doc.display_body_meshes[b];
    probe.transform(x);
    x = Transform3d(Eigen::Translation3d(0.0, 0.0, -probe.bounding_box().min.z())) * x;
    m_body_xform[b] = x;
    set_status_ok();
    set_status(StatusKind::Info, _L("Placed on face — body laid flat on the bed"));
    return true;
}

int DesignPanel::tree_selection() const
{
    return m_tree->selection();
}

void DesignPanel::set_tree_selection(int row)
{
    if (row >= 0 && row < int(m_tree->GetItemCount()))
        m_tree->select(row);
}

void DesignPanel::drop_selection()
{
    if (m_viewport != nullptr)
        m_viewport->clear_loop_pick();
    drop_solid_pick();
}

void DesignPanel::drop_solid_pick()
{
    m_sel_solid_body = m_sel_solid_face = m_sel_solid_edge = -1;
    m_sel_solid_edges.clear();
    m_sel_solid_vertex = false;
    m_pick_face = m_pick_face_body = -1;
    if (m_viewport != nullptr)
        m_viewport->clear_solid_pick();
}

bool DesignPanel::begin_renumber()
{
    if (m_ui_mode != UiMode::Feature || m_text_dlg != nullptr || m_active == Tool::Insert
        || body_move_pending()) {
        set_status(StatusKind::Info, _L("Finish or cancel what is open first"));
        return false;
    }
    if (m_active != Tool::None || m_edit_index >= 0) cancel_tool();
    m_doc.checkpoint();
    drop_selection();
    return true;
}

void DesignPanel::after_tree_edit(bool ok)
{
    end_sketch_plane_choice();
    update_undo_redo_buttons();
    refresh_tree();
    refresh_variables();
    if (!ok) {
        set_status(StatusKind::Error, wxString::Format(_L("Edit rejected: %s"), kernel_error_text(m_doc.error)));
        if (m_viewport != nullptr) m_viewport->request_repaint();
        return;
    }
    sync_recipe_to_model();
    if (m_doc.display_mesh.its.indices.empty()) {
        if (m_viewport != nullptr) m_viewport->clear_mesh();
        sync_sketch_display();
        set_status(idle_hint());
    } else {
        if (m_viewport != nullptr) feed_bodies();
        set_status_ok();
    }
    if (m_viewport != nullptr) m_viewport->request_repaint();
    m_status->Refresh();
}

void DesignPanel::on_new_design()
{
    if (m_doc.features.empty() && m_doc.bodies.empty()) { set_status_ok(); return; }
    MessageDialog dlg(this,
        _L("Erase all features and bodies and start a new design? This cannot be undone."),
        _L("New Design"), wxYES_NO | wxICON_EXCLAMATION);
    if (dlg.ShowModal() != wxID_YES) return;
    clear_document();
}

void DesignPanel::clear_document()
{
    tool_cancel();
    end_body_move(true);
    m_doc.clear();
    m_doc.auto_close_loops = wxGetApp().is_auto_close_sketch_loops();
    Slic3r::set_sketch_auto_close(m_doc.auto_close_loops);
    drop_selection();
    m_edit_index = -1;
    m_body_xform.clear();
    if (m_viewport) { m_viewport->clear_move_gizmo(); m_viewport->clear_mesh(); }
    after_tree_edit(true);
    update_action_bar();
    set_status_ok();
}

void DesignPanel::on_delete_body()
{
    const int nb = int(m_doc.bodies.size());
    if (m_sel_solid_body < 0 || m_sel_solid_body >= nb) {
        set_status(StatusKind::Error, _L("Select a body first"));
        return;
    }
    const int src = m_doc.bodies[m_sel_solid_body].source_feature;
    if (src < 0 || src >= int(m_doc.features.size())) {
        set_status(StatusKind::Error, _L("This body has no feature to delete — use New Design to start over"));
        return;
    }
    const std::string& raw = m_doc.features[src].name;
    const wxString fname = raw.empty() ? wxString::Format(_L("feature %d"), src + 1)
                                       : wxString::FromUTF8(raw);
    if (!begin_renumber()) return;
    const bool ok = m_doc.remove_feature(src);
    after_tree_edit(ok);
    if (ok)
        set_status(StatusKind::Ok, wxString::Format(_L("Deleted %s, the feature this body was made from — Ctrl+Z restores it"), fname));
}

void DesignPanel::on_delete_feature()
{
    if (tree_body_selection() >= 0) {
        set_status(StatusKind::Error, _L("Select the FEATURE that created this body (or use New Design)"));
        return;
    }
    int sel = tree_selection();
    if (sel == wxNOT_FOUND) {
        set_status(StatusKind::Info, _L("Select a feature in the tree first"));
        return;
    }
    if (!begin_renumber()) return;
    after_tree_edit(m_doc.remove_feature(sel));
}

void DesignPanel::on_toggle_visibility()
{
    const int bsel = tree_body_selection();
    if (bsel >= 0) {
        sync_body_visible();
        if (bsel < int(m_body_visible.size())) {
            const bool now_visible = !m_body_visible[bsel];
            m_body_visible[bsel] = now_visible;
            if (m_viewport != nullptr) {
                feed_bodies();
                m_viewport->set_solid_pick(&m_doc.bodies, &m_disp_pick_mesh,
                                           &m_doc.display_tri_face, &m_doc.display_tri_body,
                                           &m_body_visible, &m_body_xform);
            }
            refresh_tree();
            if (m_parts != nullptr) m_parts->select(bsel);
            set_status(StatusKind::Info, wxString::Format(now_visible ? _L("Body %d shown")
                                                            : _L("Body %d hidden"), bsel + 1));
        }
        return;
    }

    if (body_move_pending()) {
        set_status(StatusKind::Info, _L("Finish or cancel what is open first"));
        return;
    }
    int sel = tree_selection();
    if (sel == wxNOT_FOUND || sel >= int(m_doc.features.size())) {
        set_status(StatusKind::Error, _L("Select a feature in the tree first"));
        return;
    }
    const bool shown = !m_doc.features[sel].enabled;
    m_doc.checkpoint();
    if (!m_doc.set_feature_enabled(sel, shown)) { after_tree_edit(false); return; }
    refresh_tree();
    set_tree_selection(sel);
    if (m_viewport != nullptr) {
        if (m_doc.display_mesh.its.indices.empty()) {
            m_viewport->clear_mesh();
        } else {
            feed_bodies();
            m_viewport->set_solid_pick(&m_doc.bodies, &m_disp_pick_mesh,
                                       &m_doc.display_tri_face, &m_doc.display_tri_body,
                                       &m_body_visible, &m_body_xform);
        }
    }
    sync_sketch_display();
    set_status(StatusKind::Info, shown ? _L("Feature shown") : _L("Feature hidden"));
}

void DesignPanel::on_move_feature(int delta)
{
    int sel = tree_selection();
    if (sel == wxNOT_FOUND) {
        set_status(StatusKind::Info, _L("Select a feature in the tree first"));
        return;
    }
    int target = sel + delta;
    if (target < 0 || target >= int(m_doc.features.size()))
        return;
    if (!begin_renumber()) return;
    if (m_doc.move_feature(sel, delta)) {
        after_tree_edit(true);
        set_tree_selection(target);
    } else {
        after_tree_edit(false);
    }
}

bool DesignPanel::enter_constrain_inline()
{
    if (m_viewport && m_viewport->is_sketching())
        m_viewport->finish_sketch();
    const int sk = resolve_extrude_sketch();
    if (sk < 0) {
        set_status(StatusKind::Error, _L("Draw a sketch first, then Constrain"));
        return false;
    }
    set_tree_selection(sk);
    on_begin_constrain(sk);
    const bool entered = m_viewport &&
        (m_viewport->is_constraining() || m_viewport->is_constraining_entities());
    if (entered) set_ui_mode(UiMode::Constrain);
    return entered;
}

void DesignPanel::on_begin_constrain(int sel_override)
{
    int sel = (sel_override >= 0) ? sel_override : tree_selection();
    if (sel == wxNOT_FOUND || sel >= int(m_doc.features.size())) {
        sel = m_viewport ? m_viewport->loop_pick_feature() : wxNOT_FOUND;
        set_tree_selection(sel);
    }
    if (sel == wxNOT_FOUND || sel >= int(m_doc.features.size())) {
        set_status(StatusKind::Error, _L("Select a sketch in the tree first"));
        return;
    }
    CadFeature& f = m_doc.features[sel];
    if (f.type != CadFeatureType::Sketch) {
        set_status(StatusKind::Error, _L("Selected feature is not a sketch"));
        return;
    }

    if (!f.entities.empty()) {
        m_constrain_feat = sel;
        if (m_viewport) m_viewport->begin_constrain_entities(f.entities, f.plane);
        set_status(StatusKind::Info, _L("Pick 1-2 lines, then a constraint; right-click exits"));
        return;
    }

    if (f.profile.points.size() < 3) {
        set_status(StatusKind::Error, _L("Selected feature is not a sketch"));
        return;
    }
    m_constrain_feat = sel;
    if (f.constraints.empty())
        f.constraints.push_back(SketchConstraintDef{SketchConstraintType::Fix, 0, -1, -1, -1, 0.0});
    if (m_viewport) m_viewport->begin_constrain(f.profile, f.plane);
    set_status(StatusKind::Info, _L("Pick 1-2 entities, then a constraint or dimension; right-click exits"));
}

static wxString constraint_reject_text(ConstraintReject reason, SketchConstraintType type)
{
    using T = SketchConstraintType;
    switch (reason) {
    case ConstraintReject::NeedOneEntity:        return _L("Pick an entity first");
    case ConstraintReject::NeedTwoEntities:      return _L("Pick two entities first");
    case ConstraintReject::NeedALine:            return _L("Horizontal and Vertical apply to a line");
    case ConstraintReject::NeedTwoLines:
        if (type == T::Angle)     return _L("Angle applies between two lines");
        if (type == T::Collinear) return _L("Collinear needs two lines");
        return _L("Parallel, perpendicular and equal length apply to two lines");
    case ConstraintReject::NeedTwoRounds:
        if (type == T::EqualRadius) return _L("Equal radius needs two circles or arcs");
        return _L("Concentric needs two circles or arcs");
    case ConstraintReject::NeedTangentPair:      return _L("Tangent needs a line and a circle/arc, or two circles/arcs");
    case ConstraintReject::NeedJoinablePoints:   return _L("This constraint needs two entities with a point to join");
    case ConstraintReject::NeedMeasurablePoints: return _L("This dimension needs two entities with a point to measure between");
    case ConstraintReject::NeedPointAndLine:     return _L("Midpoint needs a point and a line");
    case ConstraintReject::NeedTwoPointsOrLines:
        if (type == T::Symmetric) return _L("Symmetric needs two points or two lines + an axis");
        return _L("Symmetric needs two points or two lines");
    case ConstraintReject::NeedAxisLine:         return _L("Symmetric: pick two entities, then an axis line");
    case ConstraintReject::NeedRound:            return _L("Radius/Diameter needs a circle or arc");
    case ConstraintReject::Unsupported:          return _L("Unsupported constraint");
    case ConstraintReject::None:
    default:                                     return wxString();
    }
}

static void write_plan_value(std::vector<SketchEntityConstraintDef>& defs, double v)
{
    for (auto& d : defs)
        d.value = (d.type == SketchConstraintType::Angle) ? v * M_PI / 180.0 : v;
}

void DesignPanel::apply_entity_constraint(SketchConstraintType type)
{
    int e0 = -1, e1 = -1;
    m_viewport->selected_constrain_entities(e0, e1);
    const int e2 = m_viewport->selected_constrain_axis();

    CadFeature& feat = m_doc.features[m_constrain_feat];
    const ConstraintPlan plan = plan_entity_constraint(feat.entities, e0, e1, e2, type);

    auto fail = [this](const wxString& msg) {
        set_status(StatusKind::Error, msg);
    };

    switch (plan.kind) {
    case ConstraintPlan::Kind::Reject:
        fail(constraint_reject_text(plan.reason, type));
        return;
    case ConstraintPlan::Kind::AskValue:
        m_viewport->open_inline_value(plan.prefill, [this, plan](double v) {
            std::vector<SketchEntityConstraintDef> defs = plan.defs;
            write_plan_value(defs, v);
            commit_entity_constraints(defs);
        });
        return;
    case ConstraintPlan::Kind::Apply:
        commit_entity_constraints(plan.defs);
        return;
    }
}

void DesignPanel::apply_live_constraint(SketchConstraintType type)
{
    const std::vector<int>& sel  = m_viewport->sketch_selection();
    const int e0 = sel.size() > 0 ? sel[0] : -1;
    const int e1 = sel.size() > 1 ? sel[1] : -1;
    const int e2 = sel.size() > 2 ? sel[2] : -1;

    const ConstraintPlan plan = plan_entity_constraint(
        m_viewport->sketch_entities(), e0, e1, e2, type);

    auto fail = [this](const wxString& msg) {
        set_status(StatusKind::Error, msg);
    };
    auto commit = [this, fail](std::vector<SketchEntityConstraintDef> defs, double v) {
        write_plan_value(defs, v);
        if (!m_viewport->try_add_sketch_constraints(defs)) {
            fail(_L("Constraint rejected (over-constrained)"));
            return;
        }
        m_viewport->request_repaint();
        set_status(_L("Applied constraint  ·  its badge is on the sketch — click the badge to remove it"));
        m_status->Refresh();
    };

    switch (plan.kind) {
    case ConstraintPlan::Kind::Reject:
        fail(wxString::Format(_L_PLURAL("%s  —  %d selected. Press Esc for the Select tool, click one "
                                        "line, then Shift- or Ctrl-click the other.",
                                        "%s  —  %d selected. Press Esc for the Select tool, click one "
                                        "line, then Shift- or Ctrl-click the other.", unsigned(sel.size())),
                              constraint_reject_text(plan.reason, type), int(sel.size())));
        return;
    case ConstraintPlan::Kind::AskValue:
        m_viewport->open_inline_value(plan.prefill, [plan, commit](double v) {
            commit(plan.defs, v);
        });
        return;
    case ConstraintPlan::Kind::Apply:
        commit(plan.defs, 0.0);
        return;
    }
}

void DesignPanel::commit_entity_constraints(const std::vector<SketchEntityConstraintDef>& defs)
{
    if (m_constrain_feat < 0 || m_constrain_feat >= int(m_doc.features.size()) ||
        !m_viewport || defs.empty())
        return;
    CadFeature& feat = m_doc.features[m_constrain_feat];
    const std::vector<SketchEntity> saved = feat.entities;
    const size_t before = feat.entity_constraints.size();
    m_doc.checkpoint();
    for (const auto& d : defs) feat.entity_constraints.push_back(d);
    if (!m_doc.solve_sketch_feature(m_constrain_feat)) {
        feat.entity_constraints.resize(before);
        feat.entities = saved;
        m_doc.abandon_checkpoint();
        set_status(StatusKind::Error, _L("Constraint rejected (over-constrained)"));
        return;
    }
    m_doc.recompute();
    sync_recipe_to_model();
    update_undo_redo_buttons();
    m_viewport->update_constrain_entities(m_doc.features[m_constrain_feat].entities);
    if (!m_doc.display_mesh.its.indices.empty())
        feed_bodies();
    set_status(StatusKind::Info, _L("Applied constraint"));

    refresh_constrain_dof();
    rebuild_constraint_list();
}

void DesignPanel::refresh_constrain_dof()
{
    if (!m_dof_status || m_constrain_feat < 0 || m_constrain_feat >= int(m_doc.features.size()))
        return;
    const CadFeature& feat = m_doc.features[m_constrain_feat];
    std::vector<SketchEntity> ents = feat.entities;
    const SketchSolveResult r = sketch_solve(ents, feat.entity_constraints);
    if (!r.ok) {
        m_dof_status->SetForegroundColour(wxColour(235, 80, 80));
        m_dof_status->SetLabel(_L("✗ Conflicting constraints")); m_dof_status->Show(!m_dof_status->GetLabel().IsEmpty());
    } else if (r.dof == 0) {
        m_dof_status->SetForegroundColour(wxColour(80, 200, 110));
        m_dof_status->SetLabel(_L("✓ Fully constrained")); m_dof_status->Show(!m_dof_status->GetLabel().IsEmpty());
    } else if (r.dof > 0) {
        m_dof_status->SetForegroundColour(dp_ctl_text());
        m_dof_status->SetLabel(wxString::Format(_L_PLURAL("%d degree of freedom", "%d degrees of freedom", r.dof), r.dof)); m_dof_status->Show(!m_dof_status->GetLabel().IsEmpty());
    } else {
        m_dof_status->SetLabel(wxString()); m_dof_status->Show(!m_dof_status->GetLabel().IsEmpty());
    }
    m_dof_status->Refresh();
    update_cards_frame(); m_form->Layout();
}

wxString DesignPanel::constraint_label(const SketchEntityConstraintDef& d) const
{
    using T = SketchConstraintType;
    const CadFeature* feat = (m_constrain_feat >= 0 && m_constrain_feat < int(m_doc.features.size()))
                                 ? &m_doc.features[m_constrain_feat] : nullptr;
    auto tag = [&](int ei, SketchPointRole r) -> wxString {
        if (ei < 0) return wxString();
        char c = 'E';
        if (feat && ei < int(feat->entities.size())) {
            switch (feat->entities[ei].type) {
            case SketchEntity::Type::Line:       c = 'L'; break;
            case SketchEntity::Type::Circle:     c = 'C'; break;
            case SketchEntity::Type::Arc:        c = 'A'; break;
            case SketchEntity::Type::Point:      c = 'P'; break;
            case SketchEntity::Type::Ellipse:
            case SketchEntity::Type::EllipseArc: c = 'E'; break;
            case SketchEntity::Type::BSpline:    c = 'B'; break;
            }
        }
        wxString s; s << wxUniChar(c) << ei;
        if (r == SketchPointRole::P1)     s += wxString::FromUTF8("·P1");
        // TRN Design sketch: abbreviation of "center", appended to a point tag such as "L3·Ctr"
        else if (r == SketchPointRole::Center) s += _L("·Ctr");
        else if (r == SketchPointRole::P0)     s += wxString::FromUTF8("·P0");
        return s;
    };
    auto two = [&](const wxString& name) {
        return d.eb >= 0 ? wxString::Format(wxString::FromUTF8("%s %s — %s"), name, tag(d.ea, d.ra), tag(d.eb, d.rb))
                         : wxString::Format("%s %s", name, tag(d.ea, d.ra));
    };
    switch (d.type) {
    case T::Fix:           return wxString::Format(_L("Fix %s"), tag(d.ea, d.ra));
    case T::Coincident:    return two(_L_CONTEXT("Coincident", "Sketch Constraint"));
    case T::Horizontal:    return two(_L_CONTEXT("Horizontal", "Sketch Constraint"));
    case T::Vertical:      return two(_L_CONTEXT("Vertical", "Sketch Constraint"));
    case T::Distance:      return wxString::Format("%s = %s", two(_L("Distance")), en_format(d.value));
    case T::LockX:         return wxString::Format(_L("Lock X %s"), tag(d.ea, d.ra));
    case T::LockY:         return wxString::Format(_L("Lock Y %s"), tag(d.ea, d.ra));
    case T::EqualLength:   return two(_L_CONTEXT("Equal", "Sketch Constraint"));
    case T::Parallel:      return two(_L_CONTEXT("Parallel", "Sketch Constraint"));
    case T::Perpendicular: return two(_L_CONTEXT("Perpendicular", "Sketch Constraint"));
    case T::Concentric:    return two(_L_CONTEXT("Concentric", "Sketch Constraint"));
    case T::Tangent:       return two(_L_CONTEXT("Tangent", "Sketch Constraint"));
    case T::Midpoint:      return two(_L_CONTEXT("Midpoint", "Sketch Constraint"));
    case T::Symmetric:     return wxString::Format(_L("Symmetric %s — %s / %s"),
                                                   tag(d.ea, d.ra), tag(d.eb, d.rb), tag(d.ec, d.rc));
    case T::SymmetricAboutY: return wxString::Format(_L("Symmetric about Y axis %s — %s"),
                                                     tag(d.ea, d.ra), tag(d.eb, d.rb));
    case T::SymmetricAboutX: return wxString::Format(_L("Symmetric about X axis %s — %s"),
                                                     tag(d.ea, d.ra), tag(d.eb, d.rb));
    case T::Angle:         return wxString::Format(wxString::FromUTF8("%s = %s°"), two(_L_CONTEXT("Angle", "Sketch Constraint")), en_format(d.value * 180.0 / M_PI, 1));
    case T::Radius:        return wxString::Format("%s %s = %s", _L_CONTEXT("Radius", "Sketch Constraint"),   tag(d.ea, d.ra), en_format(d.value));
    case T::Diameter:      return wxString::Format("%s %s = %s", _L_CONTEXT("Diameter", "Sketch Constraint"), tag(d.ea, d.ra), en_format(d.value));
    case T::PointOnLine:   return two(_L("On line"));
    case T::PointOnObject: return two(_L("On edge"));
    }
    return _L("Constraint");
}

bool DesignPanel::live_constraint_scope() const
{
    return m_viewport != nullptr && m_viewport->is_sketching() &&
           !m_viewport->is_constraining() && !m_viewport->is_constraining_entities();
}

void DesignPanel::rebuild_constraint_list()
{
    if (m_constraint_rows == nullptr || m_form == nullptr)
        return;
    m_constraint_rows->Clear(true );
    m_constraint_sel = -1;

    const bool live   = live_constraint_scope();
    const bool active = (m_constrain_feat >= 0 && m_constrain_feat < int(m_doc.features.size()));
    const std::vector<SketchEntityConstraintDef> empty;
    const std::vector<SketchEntityConstraintDef>& cons =
        live   ? m_viewport->sketch_constraints()
      : active ? m_doc.features[m_constrain_feat].entity_constraints : empty;

    if (m_hdr_constraints)
        m_hdr_constraints->SetLabel(wxString::Format(_L("Constraints (%d)"), int(cons.size())));

    if (cons.empty()) {
        auto* none = new wxStaticText(m_cards, wxID_ANY, _L("No constraints yet"));
        none->SetForegroundColour(dp_sec_text());
        m_constraint_rows->Add(none, 0, wxTOP, 4);
    }
    for (int i = 0; i < int(cons.size()); ++i) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* del = sidebar_icon_btn(m_cards, "design_delete", _L("Delete constraint"), 16);
        del->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { CallAfter([this, i] { delete_constraint(i); }); });
        auto* lbl = new wxStaticText(m_cards, wxID_ANY, constraint_label(cons[i]));
        lbl->SetCursor(wxCursor(wxCURSOR_HAND));
        lbl->Bind(wxEVT_LEFT_UP, [this, i](wxMouseEvent&) { highlight_constraint_entities(i); });
        row->Add(del, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        row->Add(lbl, 1, wxALIGN_CENTER_VERTICAL);
        m_constraint_rows->Add(row, 0, wxEXPAND | wxTOP, 2);
    }

    if (m_viewport)
        m_viewport->set_constraint_glyphs(cons);

    m_cards->GetSizer()->Show(m_box_constraints,
                              m_ui_mode == UiMode::Constrain || live, true);
    update_cards_frame(); m_form->Layout();
    m_form->FitInside();
}

void DesignPanel::highlight_constraint_entities(int idx)
{
    if (!m_viewport)
        return;
    const bool live = live_constraint_scope();
    if (!live && (m_constrain_feat < 0 || m_constrain_feat >= int(m_doc.features.size())))
        return;
    const auto& cons = live ? m_viewport->sketch_constraints()
                            : m_doc.features[m_constrain_feat].entity_constraints;
    if (idx < 0 || idx >= int(cons.size()))
        return;
    if (m_constraint_sel == idx) {
        m_constraint_sel = -1;
        m_viewport->set_constraint_highlight({});
        return;
    }
    m_constraint_sel = idx;
    const SketchEntityConstraintDef& d = cons[idx];
    std::vector<int> ents;
    for (int e : { d.ea, d.eb, d.ec })
        if (e >= 0) ents.push_back(e);
    m_viewport->set_constraint_highlight(std::move(ents));
}

void DesignPanel::delete_constraint(int idx)
{
    if (live_constraint_scope()) {
        if (!m_viewport->remove_sketch_constraint(idx))
            return;
        set_status(StatusKind::Info, _L("Constraint deleted"));
        return;
    }
    if (m_constrain_feat < 0 || m_constrain_feat >= int(m_doc.features.size()) || !m_viewport)
        return;
    CadFeature& feat = m_doc.features[m_constrain_feat];
    if (idx < 0 || idx >= int(feat.entity_constraints.size()))
        return;
    m_doc.checkpoint();
    feat.entity_constraints.erase(feat.entity_constraints.begin() + idx);
    m_doc.solve_sketch_feature(m_constrain_feat);
    m_doc.recompute();
    sync_recipe_to_model();
    update_undo_redo_buttons();
    m_viewport->set_constraint_highlight({});
    m_viewport->update_constrain_entities(m_doc.features[m_constrain_feat].entities);
    if (!m_doc.display_mesh.its.indices.empty())
        feed_bodies();
    set_status(StatusKind::Info, _L("Constraint deleted"));
    refresh_constrain_dof();
    rebuild_constraint_list();
}

void DesignPanel::apply_edit_op(EditOp op)
{
    if (m_constrain_feat < 0 || m_constrain_feat >= int(m_doc.features.size()) || !m_viewport ||
        !m_viewport->is_constraining_entities()) {
        set_status(StatusKind::Error, _L("Press Constrain on a sketch first"));
        return;
    }
    auto fail = [this](const wxString& msg) {
        set_status(StatusKind::Error, msg);
    };

    int e0 = -1, e1 = -1;
    m_viewport->selected_constrain_entities(e0, e1);
    CadFeature& feat = m_doc.features[m_constrain_feat];
    const int n = int(feat.entities.size());
    if (e0 < 0 || e0 >= n) { fail(_L("Pick an entity first")); return; }
    using Type = SketchEntity::Type;

    switch (op) {
    case EditOp::Mirror: {
        if (e1 < 0 || e1 >= n) { fail(_L("Pick the entity, then a mirror-axis line")); return; }
        const SketchEntity& axis = feat.entities[e1];
        if (axis.type != Type::Line) { fail(_L("Mirror axis must be a line")); return; }
        auto out = SketchEngine::mirror_entities({ feat.entities[e0] }, axis.p0, axis.p1);
        if (out.empty()) { fail(_L("Mirror produced nothing")); return; }
        const int mi = n;
        for (auto& m : out) feat.entities.push_back(m);

        {
            using R  = SketchPointRole;
            using CT = SketchConstraintType;
            const std::vector<SketchEntity> saved_ents = feat.entities;
            const size_t cons_before = feat.entity_constraints.size();
            SketchEntityConstraintDef d; d.type = CT::Symmetric; d.ea = e0; d.eb = mi; d.ec = e1;
            const Type st = feat.entities[e0].type;
            if (st == Type::Line) {
                d.ra = R::P0; d.rb = R::P0; feat.entity_constraints.push_back(d);
                d.ra = R::P1; d.rb = R::P1; feat.entity_constraints.push_back(d);
            } else if (st == Type::Arc || st == Type::Circle) {
                d.ra = R::Center; d.rb = R::Center; feat.entity_constraints.push_back(d);
            } else if (st == Type::Point) {
                d.ra = R::P0; d.rb = R::P0; feat.entity_constraints.push_back(d);
            }
            if (feat.entity_constraints.size() != cons_before &&
                !m_doc.solve_sketch_feature(m_constrain_feat)) {
                feat.entity_constraints.resize(cons_before);
                feat.entities = saved_ents;
            }
        }
        break;
    }
    case EditOp::Offset: {
        const int a = e0;
        request_value(_L("Offset distance (+left / -right of direction)"), 1.0, -100000.0, 100000.0,
            [this, a](double d) {
                CadFeature& f = m_doc.features[m_constrain_feat];
                if (a >= int(f.entities.size())) return;
                auto out = SketchEngine::offset_entities({ f.entities[a] }, d);
                if (out.empty()) {
                    set_status(StatusKind::Error, _L("Offset collapsed the entity")); return;
                }
                const int ni = int(f.entities.size());
                for (auto& o : out) f.entities.push_back(o);

                {
                    using CT = SketchConstraintType;
                    const Type st = f.entities[a].type;
                    SketchEntityConstraintDef d2; d2.ea = a; d2.eb = ni;
                    bool emit = true;
                    if (st == Type::Line)                              d2.type = CT::Parallel;
                    else if (st == Type::Arc || st == Type::Circle)    d2.type = CT::Concentric;
                    else                                               emit = false;
                    if (emit) {
                        const size_t cbefore = f.entity_constraints.size();
                        f.entity_constraints.push_back(d2);
                        if (!m_doc.solve_sketch_feature(m_constrain_feat))
                            f.entity_constraints.resize(cbefore);
                    }
                }
                after_edit_op();
            });
        return;
    }
    case EditOp::Fillet: {
        if (e1 < 0 || e1 >= n) { fail(_L("Pick two lines to fillet")); return; }
        if (feat.entities[e0].type != Type::Line || feat.entities[e1].type != Type::Line) {
            fail(_L("Fillet needs two lines")); return;
        }
        const int a = e0, b = e1;
        request_value(_L("Fillet radius"), 1.0, 0.001, 100000.0, [this, a, b](double r) {
            CadFeature& f = m_doc.features[m_constrain_feat];
            if (a >= int(f.entities.size()) || b >= int(f.entities.size())) return;
            SketchEntity a_out, b_out, arc_out;
            if (!SketchEngine::fillet_lines(f.entities[a], f.entities[b], r, a_out, b_out, arc_out)) {
                set_status(StatusKind::Error, _L("Fillet failed (parallel lines or radius too large)")); return;
            }
            f.entities[a] = a_out;
            f.entities[b] = b_out;
            const int arc = int(f.entities.size());
            f.entities.push_back(arc_out);

            {
                using R  = SketchPointRole;
                using CT = SketchConstraintType;
                auto role_near = [](const SketchEntity& ln, const Vec2d& p) -> R {
                    return ((ln.p0 - p).squaredNorm() <= (ln.p1 - p).squaredNorm()) ? R::P0 : R::P1;
                };
                const R ra = role_near(f.entities[a], arc_out.p0);
                const R rb = role_near(f.entities[b], arc_out.p1);

                auto refs = [](const SketchEntityConstraintDef& d, int e, R r) {
                    return (d.ea == e && d.ra == r) || (d.eb == e && d.rb == r);
                };
                auto self_len = [](const SketchEntityConstraintDef& d, int e) {
                    return d.type == CT::Distance && d.ea == e && d.eb == e;
                };
                auto& cs = f.entity_constraints;
                cs.erase(std::remove_if(cs.begin(), cs.end(),
                    [&](const SketchEntityConstraintDef& d) {
                        return (d.type == CT::Coincident && refs(d, a, ra) && refs(d, b, rb))
                            || self_len(d, a) || self_len(d, b);
                    }), cs.end());

                auto coin = [&](R arc_role, int ln, R ln_role) {
                    SketchEntityConstraintDef d; d.type = CT::Coincident;
                    d.ea = arc; d.ra = arc_role; d.eb = ln; d.rb = ln_role; return d;
                };
                auto tang = [&](int ln) {
                    SketchEntityConstraintDef d; d.type = CT::Tangent; d.ea = arc; d.eb = ln; return d;
                };
                const std::vector<std::vector<SketchEntityConstraintDef>> ladder = {
                    { coin(R::P0, a, ra), coin(R::P1, b, rb), tang(a), tang(b) },
                    { coin(R::P0, a, ra), coin(R::P1, b, rb), tang(a) },
                    { coin(R::P0, a, ra), coin(R::P1, b, rb) },
                };
                const std::vector<SketchEntity> saved = f.entities;
                const size_t cbefore = f.entity_constraints.size();
                for (const auto& set : ladder) {
                    for (const auto& d : set) f.entity_constraints.push_back(d);
                    if (m_doc.solve_sketch_feature(m_constrain_feat)) break;
                    f.entity_constraints.resize(cbefore);
                    f.entities = saved;
                }
            }
            after_edit_op();
        });
        return;
    }

    case EditOp::Chamfer: {
        if (e1 < 0 || e1 >= n) { fail(_L("Pick two lines to chamfer")); return; }
        if (feat.entities[e0].type != Type::Line || feat.entities[e1].type != Type::Line) {
            fail(_L("Chamfer needs two lines")); return;
        }
        const int a = e0, b = e1;
        request_value(_L("Chamfer distance"), 1.0, 0.001, 100000.0, [this, a, b](double d) {
            CadFeature& f = m_doc.features[m_constrain_feat];
            if (a >= int(f.entities.size()) || b >= int(f.entities.size())) return;
            SketchEntity a_out, b_out, seg_out;
            if (!SketchEngine::chamfer_lines(f.entities[a], f.entities[b], d, a_out, b_out, seg_out)) {
                set_status(StatusKind::Error, _L("Chamfer failed (parallel lines or distance too large)")); return;
            }
            f.entities[a] = a_out;
            f.entities[b] = b_out;
            const int seg = int(f.entities.size());
            f.entities.push_back(seg_out);

            {
                using R  = SketchPointRole;
                using CT = SketchConstraintType;
                auto role_near = [](const SketchEntity& ln, const Vec2d& p) -> R {
                    return ((ln.p0 - p).squaredNorm() <= (ln.p1 - p).squaredNorm()) ? R::P0 : R::P1;
                };
                const R ra = role_near(f.entities[a], seg_out.p0);
                const R rb = role_near(f.entities[b], seg_out.p1);

                auto refs = [](const SketchEntityConstraintDef& dd, int e, R r) {
                    return (dd.ea == e && dd.ra == r) || (dd.eb == e && dd.rb == r);
                };
                auto self_len = [](const SketchEntityConstraintDef& dd, int e) {
                    return dd.type == CT::Distance && dd.ea == e && dd.eb == e;
                };
                auto& cs = f.entity_constraints;
                cs.erase(std::remove_if(cs.begin(), cs.end(),
                    [&](const SketchEntityConstraintDef& dd) {
                        return (dd.type == CT::Coincident && refs(dd, a, ra) && refs(dd, b, rb))
                            || self_len(dd, a) || self_len(dd, b);
                    }), cs.end());

                auto coin = [&](R seg_role, int ln, R ln_role) {
                    SketchEntityConstraintDef dd; dd.type = CT::Coincident;
                    dd.ea = seg; dd.ra = seg_role; dd.eb = ln; dd.rb = ln_role; return dd;
                };
                const std::vector<SketchEntity> saved = f.entities;
                const size_t cbefore = f.entity_constraints.size();
                f.entity_constraints.push_back(coin(R::P0, a, ra));
                f.entity_constraints.push_back(coin(R::P1, b, rb));
                if (!m_doc.solve_sketch_feature(m_constrain_feat)) {
                    f.entity_constraints.resize(cbefore);
                    f.entities = saved;
                }
            }
            after_edit_op();
        });
        return;
    }

    case EditOp::Trim:
    case EditOp::Extend: {
        const Type st = feat.entities[e0].type;
        const bool subject_ok = (op == EditOp::Trim)
            ? (st == Type::Line || st == Type::Arc || st == Type::Circle)
            : (st == Type::Line || st == Type::Arc);
        if (!subject_ok) {
            fail(op == EditOp::Trim ? _L("Trim works on lines, arcs and circles")
                                    : _L("Extend works on lines and arcs"));
            return;
        }
        Vec2d pick;
        if (!m_viewport->pick0_point(pick)) { fail(_L("Pick the edge to trim/extend")); return; }
        std::vector<SketchEntity> others;
        others.reserve(n > 0 ? n - 1 : 0);
        for (int i = 0; i < n; ++i)
            if (i != e0) others.push_back(feat.entities[i]);
        const SketchEntity before = feat.entities[e0];
        const bool ok = (op == EditOp::Trim)
            ? SketchEngine::trim_entity(feat.entities[e0], others, pick)
            : SketchEngine::extend_entity(feat.entities[e0], others, pick);
        if (!ok) { fail(op == EditOp::Trim ? _L("Nothing to trim at the pick")
                                           : _L("No edge to extend to")); return; }

        if (st == Type::Line || st == Type::Arc) {
            using R  = SketchPointRole;
            using CT = SketchConstraintType;
            const SketchEntity& aft = feat.entities[e0];
            R     moved = R::P0;
            Vec2d P;
            if (st == Type::Line) {
                const bool p0_moved = (before.p0 - aft.p0).squaredNorm()
                                    > (before.p1 - aft.p1).squaredNorm();
                moved = p0_moved ? R::P0 : R::P1;
                P     = p0_moved ? aft.p0 : aft.p1;
            } else {
                const bool start_moved = std::abs(before.start_angle - aft.start_angle)
                                       > std::abs(before.end_angle - aft.end_angle);
                moved = start_moved ? R::P0 : R::P1;
                const double ang = start_moved ? aft.start_angle : aft.end_angle;
                P = aft.center + aft.radius * Vec2d(std::cos(ang), std::sin(ang));
            }

            auto refs = [&](const SketchEntityConstraintDef& d, R r) {
                return (d.ea == e0 && d.ra == r) || (d.eb == e0 && d.rb == r);
            };
            auto& cs = feat.entity_constraints;
            cs.erase(std::remove_if(cs.begin(), cs.end(),
                [&](const SketchEntityConstraintDef& d) {
                    if (d.type == CT::Distance && d.ea == e0 && d.eb == e0) return true;
                    return (d.type == CT::Coincident || d.type == CT::PointOnLine
                         || d.type == CT::PointOnObject) && refs(d, moved);
                }), cs.end());

            int cutter = -1;
            const double tol = 1e-5;
            for (int i = 0; i < int(feat.entities.size()); ++i) {
                if (i == e0) continue;
                const SketchEntity& o = feat.entities[i];
                if (o.type == Type::Line) {
                    Vec2d dv = o.p1 - o.p0;
                    const double L2 = dv.dot(dv);
                    if (L2 < 1e-18) continue;
                    const double t = (P - o.p0).dot(dv) / L2;
                    if (t < -1e-6 || t > 1.0 + 1e-6) continue;
                    if ((o.p0 + t * dv - P).norm() < tol) { cutter = i; break; }
                } else if (o.type == Type::Circle) {
                    if (std::abs((P - o.center).norm() - o.radius) < tol) { cutter = i; break; }
                }
            }

            if (cutter >= 0) {
                const size_t cbefore = feat.entity_constraints.size();
                SketchEntityConstraintDef d; d.type = CT::PointOnObject;
                d.ea = e0; d.ra = moved; d.eb = cutter;
                feat.entity_constraints.push_back(d);
                if (!m_doc.solve_sketch_feature(m_constrain_feat))
                    feat.entity_constraints.resize(cbefore);
            }
        }
        break;
    }
    case EditOp::Array: {
        const int a = e0;
        request_value(_L("Array count (incl. original)"), 3.0, 2.0, 200.0,
            [this, a](double cnt_d) {
                const int count = std::max(2, int(cnt_d + 0.5));
                request_value(_L("Spacing (mm)"), 20.0, -100000.0, 100000.0,
                    [this, a, count](double sp) {
                        CadFeature& f = m_doc.features[m_constrain_feat];
                        if (a >= int(f.entities.size())) return;
                        using Type = SketchEntity::Type;
                        const SketchEntity src = f.entities[a];
                        const Type src_type = src.type;
                        Vec2d dir(1.0, 0.0);
                        if (src_type == Type::Line) {
                            const Vec2d t = src.p1 - src.p0;
                            if (t.norm() > 1e-9) {
                                const Vec2d u = t.normalized();
                                dir = Vec2d(-u.y(), u.x());
                            }
                        }
                        auto copies = SketchEngine::array_entities(
                            { src }, count, sp * dir, 0.0, Vec2d(0, 0));
                        if (copies.empty()) {
                            set_status(StatusKind::Error, _L("Array produced nothing")); return;
                        }
                        const int base = int(f.entities.size());
                        for (auto& c : copies) f.entities.push_back(c);

                        if (src_type == Type::Line) {
                            using CT = SketchConstraintType;
                            const size_t cb = f.entity_constraints.size();
                            auto build_web = [&](bool with_equal) {
                                f.entity_constraints.resize(cb);
                                for (int k = 0; k < int(copies.size()); ++k) {
                                    SketchEntityConstraintDef dp; dp.type = CT::Parallel;
                                    dp.ea = a; dp.eb = base + k;
                                    f.entity_constraints.push_back(dp);
                                    if (with_equal) {
                                        SketchEntityConstraintDef de; de.type = CT::EqualLength;
                                        de.ea = a; de.eb = base + k;
                                        f.entity_constraints.push_back(de);
                                    }
                                }
                            };
                            build_web(true);
                            if (!m_doc.solve_sketch_feature(m_constrain_feat)) {
                                build_web(false);
                                if (!m_doc.solve_sketch_feature(m_constrain_feat))
                                    f.entity_constraints.resize(cb);
                            }
                        } else if (src_type == Type::Arc || src_type == Type::Circle) {
                            using CT = SketchConstraintType;
                            const size_t cb = f.entity_constraints.size();
                            for (int k = 0; k < int(copies.size()); ++k) {
                                SketchEntityConstraintDef dr; dr.type = CT::Radius;
                                dr.ea = base + k; dr.value = src.radius;
                                f.entity_constraints.push_back(dr);
                            }
                            if (!m_doc.solve_sketch_feature(m_constrain_feat))
                                f.entity_constraints.resize(cb);
                        }
                        after_edit_op();
                    });
            });
        return;
    }
    case EditOp::Move: {
        const int a = e0;
        request_value(_L("Move dX (mm)"), 20.0, -100000.0, 100000.0,
            [this, a](double dx) {
                request_value(_L("Move dY (mm)"), 0.0, -100000.0, 100000.0,
                    [this, a, dx](double dy) {
                        CadFeature& f = m_doc.features[m_constrain_feat];
                        if (a >= int(f.entities.size())) return;
                        auto out = SketchEngine::transform_entities(
                            { f.entities[a] }, Vec2d(dx, dy), 0.0, 1.0, Vec2d(0, 0));
                        if (out.empty()) {
                            set_status(StatusKind::Error, _L("Move produced nothing")); return;
                        }
                        f.entities[a] = out[0];

                        using CT = SketchConstraintType;
                        auto refs_a = [&](const SketchEntityConstraintDef& d) {
                            return d.ea == a || d.eb == a || d.ec == a;
                        };
                        auto& cs = f.entity_constraints;
                        cs.erase(std::remove_if(cs.begin(), cs.end(),
                            [&](const SketchEntityConstraintDef& d) {
                                if (!refs_a(d)) return false;
                                switch (d.type) {
                                case CT::Coincident: case CT::PointOnLine: case CT::PointOnObject:
                                case CT::Concentric: case CT::Symmetric:   case CT::Midpoint:
                                case CT::Fix:        case CT::LockX:        case CT::LockY:
                                    return true;
                                case CT::Distance:
                                    return !(d.ea == a && d.eb == a);
                                default:
                                    return false;
                                }
                            }), cs.end());

                        m_doc.solve_sketch_feature(m_constrain_feat);
                        after_edit_op();
                    });
            });
        return;
    }
    case EditOp::Rotate: {
        const int a = e0;
        request_value(_L("Rotate angle (deg)"), 45.0, -360.0, 360.0,
            [this, a](double deg) {
                CadFeature& f = m_doc.features[m_constrain_feat];
                if (a >= int(f.entities.size())) return;
                auto centroid_of = [](const SketchEntity& e) -> Vec2d {
                    using T = SketchEntity::Type;
                    switch (e.type) {
                    case T::Line:    return 0.5 * (e.p0 + e.p1);
                    case T::Arc: case T::Circle: case T::Ellipse: case T::EllipseArc:
                                     return e.center;
                    case T::BSpline:
                        if (!e.ctrl.empty()) {
                            Vec2d s(0, 0); for (auto& p : e.ctrl) s += p;
                            return s / double(e.ctrl.size());
                        }
                        return 0.5 * (e.p0 + e.p1);
                    default:         return e.p0;
                    }
                };
                const Vec2d piv = centroid_of(f.entities[a]);
                auto out = SketchEngine::transform_entities(
                    { f.entities[a] }, Vec2d(0, 0), deg * M_PI / 180.0, 1.0, piv);
                if (out.empty()) {
                    set_status(StatusKind::Error, _L("Rotate produced nothing")); return;
                }
                f.entities[a] = out[0];

                using CT = SketchConstraintType;
                auto refs_a = [&](const SketchEntityConstraintDef& d) {
                    return d.ea == a || d.eb == a || d.ec == a;
                };
                auto& cs = f.entity_constraints;
                cs.erase(std::remove_if(cs.begin(), cs.end(),
                    [&](const SketchEntityConstraintDef& d) {
                        if (!refs_a(d)) return false;
                        switch (d.type) {
                        case CT::EqualLength: case CT::Radius: case CT::Diameter:
                            return false;
                        case CT::Distance:
                            return !(d.ea == a && d.eb == a);
                        default:
                            return true;
                        }
                    }), cs.end());

                m_doc.solve_sketch_feature(m_constrain_feat);
                after_edit_op();
            });
        return;
    }
    case EditOp::Scale: {
        const int a = e0;
        request_value(_L("Scale factor"), 2.0, 0.01, 1000.0,
            [this, a](double sf) {
                CadFeature& f = m_doc.features[m_constrain_feat];
                if (a >= int(f.entities.size())) return;
                auto centroid_of = [](const SketchEntity& e) -> Vec2d {
                    using T = SketchEntity::Type;
                    switch (e.type) {
                    case T::Line:    return 0.5 * (e.p0 + e.p1);
                    case T::Arc: case T::Circle: case T::Ellipse: case T::EllipseArc:
                                     return e.center;
                    case T::BSpline:
                        if (!e.ctrl.empty()) {
                            Vec2d s(0, 0); for (auto& p : e.ctrl) s += p;
                            return s / double(e.ctrl.size());
                        }
                        return 0.5 * (e.p0 + e.p1);
                    default:         return e.p0;
                    }
                };
                const Vec2d piv = centroid_of(f.entities[a]);
                auto out = SketchEngine::transform_entities(
                    { f.entities[a] }, Vec2d(0, 0), 0.0, sf, piv);
                if (out.empty()) {
                    set_status(StatusKind::Error, _L("Scale produced nothing")); return;
                }
                f.entities[a] = out[0];

                using CT = SketchConstraintType;
                auto refs_a = [&](const SketchEntityConstraintDef& d) {
                    return d.ea == a || d.eb == a || d.ec == a;
                };
                auto& cs = f.entity_constraints;
                cs.erase(std::remove_if(cs.begin(), cs.end(),
                    [&](const SketchEntityConstraintDef& d) {
                        if (!refs_a(d)) return false;
                        switch (d.type) {
                        case CT::Horizontal: case CT::Vertical: case CT::Parallel:
                        case CT::Perpendicular: case CT::Angle:
                            return false;
                        default:
                            return true;
                        }
                    }), cs.end());

                m_doc.solve_sketch_feature(m_constrain_feat);
                after_edit_op();
            });
        return;
    }
    case EditOp::PolarArray: {
        const int a = e0;
        request_value(_L("Polar count (incl. original)"), 6.0, 2.0, 200.0,
            [this, a](double cnt_d) {
                const int count = std::max(2, int(cnt_d + 0.5));
                request_value(_L("Total sweep (deg)"), 360.0, -360.0, 360.0,
                    [this, a, count](double sweep_deg) {
                        CadFeature& f = m_doc.features[m_constrain_feat];
                        if (a >= int(f.entities.size())) return;
                        using Type = SketchEntity::Type;
                        const SketchEntity src = f.entities[a];
                        const Type src_type = src.type;
                        auto centroid_of = [](const SketchEntity& e) -> Vec2d {
                            using T = SketchEntity::Type;
                            switch (e.type) {
                            case T::Line:    return 0.5 * (e.p0 + e.p1);
                            case T::Arc: case T::Circle: case T::Ellipse: case T::EllipseArc:
                                             return e.center;
                            case T::BSpline:
                                if (!e.ctrl.empty()) {
                                    Vec2d s(0, 0); for (auto& p : e.ctrl) s += p;
                                    return s / double(e.ctrl.size());
                                }
                                return 0.5 * (e.p0 + e.p1);
                            default:         return e.p0;
                            }
                        };
                        const Vec2d  piv        = centroid_of(src);
                        const double angle_step = (sweep_deg * M_PI / 180.0) / double(count);
                        auto copies = SketchEngine::array_entities(
                            { src }, count, Vec2d(0, 0), angle_step, piv);
                        if (copies.empty()) {
                            set_status(StatusKind::Error, _L("Polar array produced nothing")); return;
                        }
                        const int base = int(f.entities.size());
                        for (auto& c : copies) f.entities.push_back(c);

                        if (src_type == Type::Line) {
                            using CT = SketchConstraintType;
                            const size_t cb = f.entity_constraints.size();
                            for (int k = 0; k < int(copies.size()); ++k) {
                                SketchEntityConstraintDef de; de.type = CT::EqualLength;
                                de.ea = a; de.eb = base + k;
                                f.entity_constraints.push_back(de);
                            }
                            if (!m_doc.solve_sketch_feature(m_constrain_feat))
                                f.entity_constraints.resize(cb);
                        } else if (src_type == Type::Arc || src_type == Type::Circle) {
                            using CT = SketchConstraintType;
                            const size_t cb = f.entity_constraints.size();
                            auto build_web = [&](bool with_concentric) {
                                f.entity_constraints.resize(cb);
                                for (int k = 0; k < int(copies.size()); ++k) {
                                    if (with_concentric) {
                                        SketchEntityConstraintDef dc; dc.type = CT::Concentric;
                                        dc.ea = a; dc.eb = base + k;
                                        f.entity_constraints.push_back(dc);
                                    }
                                    SketchEntityConstraintDef dr; dr.type = CT::Radius;
                                    dr.ea = base + k; dr.value = src.radius;
                                    f.entity_constraints.push_back(dr);
                                }
                            };
                            build_web(true);
                            if (!m_doc.solve_sketch_feature(m_constrain_feat)) {
                                build_web(false);
                                if (!m_doc.solve_sketch_feature(m_constrain_feat))
                                    f.entity_constraints.resize(cb);
                            }
                        }
                        after_edit_op();
                    });
            });
        return;
    }
    }

    after_edit_op();
}

void DesignPanel::after_edit_op()
{
    if (m_constrain_feat < 0 || m_constrain_feat >= int(m_doc.features.size()) || !m_viewport)
        return;
    m_doc.recompute();
    m_viewport->set_constraint_highlight({});
    m_viewport->update_constrain_entities(m_doc.features[m_constrain_feat].entities);
    sync_sketch_display();
    if (!m_doc.display_mesh.its.indices.empty())
        feed_bodies();
    set_status(StatusKind::Info, _L("Applied edit"));
    refresh_constrain_dof();
    rebuild_constraint_list();
}

void DesignPanel::request_value(const wxString& label, double def, double mn, double mx,
                                std::function<void(double)> cont,
                                std::function<void()> on_cancel)
{
    m_value_cont = std::move(cont);
    m_value_cancel = std::move(on_cancel);
    m_value_min = mn;
    m_value_max = mx;
    m_value_label->SetLabel(label);
    m_value_input->ChangeValue(en_format(def));
    m_cards->GetSizer()->Show(m_box_value, true, true);
    update_cards_frame(); m_form->Layout();
    m_form->FitInside();
    m_value_input->SetFocus();
    m_value_input->SetSelection(-1, -1);
    set_status(StatusKind::Info, label + _L(" — type a value, press Enter (or Confirm)"));
}

void DesignPanel::confirm_value()
{
    if (!m_value_cont) { cancel_value(); return; }
    double v = 0.0;
    if (!en_parse(m_value_input->GetValue(), v)) { m_value_input->SetFocus(); return; }
    v = std::min(std::max(v, m_value_min), m_value_max);
    auto cont = m_value_cont;
    m_value_cont = nullptr;
    m_value_cancel = nullptr;
    m_cards->GetSizer()->Show(m_box_value, false, true);
    update_cards_frame(); m_form->Layout();
    m_form->FitInside();
    cont(v);
}

void DesignPanel::cancel_value()
{
    const bool was_open = (m_value_cont != nullptr);
    m_value_cont = nullptr;
    auto on_cancel = m_value_cancel;
    m_value_cancel = nullptr;
    if (m_box_value)
        m_cards->GetSizer()->Show(m_box_value, false, true);
    update_cards_frame(); m_form->Layout();
    m_form->FitInside();
    if (was_open) {
        set_status(StatusKind::Info, wxString());
    }
    if (on_cancel)
        on_cancel();
}

void DesignPanel::apply_constraint(SketchConstraintType type)
{
    if (m_viewport && m_viewport->is_sketching() &&
        !m_viewport->is_constraining() && !m_viewport->is_constraining_entities()) {
        apply_live_constraint(type);
        return;
    }

    if (m_constrain_feat < 0 || m_constrain_feat >= int(m_doc.features.size()) ||
        m_viewport == nullptr) {
        set_status(StatusKind::Error, _L("Press Constrain on a sketch first"));
        return;
    }

    if (m_viewport->is_constraining_entities()) {
        apply_entity_constraint(type);
        return;
    }

    if (!m_viewport->is_constraining()) {
        set_status(StatusKind::Error, _L("Press Constrain on a sketch first"));
        return;
    }
    int a = -1, b = -1;
    if (!m_viewport->selected_segment(a, b)) {
        set_status(StatusKind::Error, _L("Pick a segment in the viewport first"));
        return;
    }
    CadFeature& feat = m_doc.features[m_constrain_feat];
    const std::vector<Vec2d> saved_pts = feat.profile.points;
    m_doc.checkpoint();
    feat.constraints.push_back(SketchConstraintDef{type, a, b, -1, -1, 0.0});
    if (!m_doc.solve_sketch_feature(m_constrain_feat)) {
        feat.constraints.pop_back();
        feat.profile.points = saved_pts;
        m_doc.abandon_checkpoint();
        set_status(StatusKind::Error, _L("Constraint rejected (over-constrained)"));
        return;
    }
    m_doc.recompute();
    sync_recipe_to_model();
    update_undo_redo_buttons();
    m_viewport->update_constrain_profile(m_doc.features[m_constrain_feat].profile.points);
    if (!m_doc.display_mesh.its.indices.empty())
        feed_bodies();
    set_status(StatusKind::Info, type == SketchConstraintType::Horizontal ? _L("Applied Horizontal")
                                                                : _L("Applied Vertical"));
}

void DesignPanel::reset_edit_state()
{
    m_edit_index = -1;
}

int DesignPanel::resolve_extrude_sketch() const
{
    int sel = tree_selection();
    if (sel != wxNOT_FOUND && sel < int(m_doc.features.size()) &&
        m_doc.features[sel].type == CadFeatureType::Sketch)
        return sel;
    for (int i = int(m_doc.features.size()) - 1; i >= 0; --i)
        if (m_doc.features[i].type == CadFeatureType::Sketch) return i;
    return -1;
}

void DesignPanel::load_feature_into_dialog(const CadFeature& f)
{
    switch (f.type) {
    case CadFeatureType::Sketch:
        m_shape->SetSelection(f.shape == SketchShape::Circle ? 1 : 0);
        m_width->SetValue(f.width);
        m_height->SetValue(f.height);
        m_radius->SetValue(f.radius);
        break;
    case CadFeatureType::Extrude:
        m_distance->SetValue(f.distance);
        m_mode->SetSelection(static_cast<int>(f.mode));
        m_extrude_end->SetSelection(static_cast<int>(f.extrude_end));
        m_distance2->SetValue(f.distance2);
        m_taper->SetValue(f.taper_deg);
        m_flip->SetValue(f.flip);
        m_extrude_sketch_ref = f.sketch_ref;
        m_sel_solid_body = f.target_body;
        if (m_extrude_sketch_ref >= 0 && m_extrude_sketch_ref < int(m_doc.features.size()))
            m_extrude_sketch_label->SetLabel(_L("Sketch: ") +
                wxString::FromUTF8(m_doc.features[m_extrude_sketch_ref].name));
        break;
    case CadFeatureType::Fillet:
    case CadFeatureType::Chamfer:
        m_dressup_type->SetSelection(f.type == CadFeatureType::Fillet ? 0 : 1);
        m_dressup_size->SetValue(f.dressup_size);
        m_face_group->SetSelection(static_cast<int>(f.face_group));
        m_sel_solid_edges = f.dressup_edge_ids();
        m_sel_solid_edge  = m_sel_solid_edges.empty() ? -1 : m_sel_solid_edges.back();
        m_sel_solid_body = f.target_body;
        sync_dressup_target();
        break;
    case CadFeatureType::Hole:
        m_hole_plane->SetSelection(index_from_plane(f.plane));
        m_hole_diameter->SetValue(f.hole_diameter);
        m_hole_depth->SetValue(f.hole_depth);
        m_hole_through->SetValue(f.hole_through);
        m_hole_x->SetValue(f.hole_x);
        m_hole_y->SetValue(f.hole_y);
        m_hole_on_face    = !is_base_plane(f.plane, m_doc.modeling_origin);
        m_hole_face_plane = f.plane;
        m_hole_face_body  = m_hole_on_face ? f.target_body : -1;
        m_hole_has_bounds = false;
        if (m_hole_target_label)
            m_hole_target_label->SetLabel(m_hole_on_face
                ? (f.target_body >= 0 ? wxString::Format(_L("On a face of Body %d"), f.target_body + 1)
                                      : _L("On a stored face"))
                : _L("(none — uses Hole plane)"));
        break;
    case CadFeatureType::Thread:
        m_thread_plane->SetSelection(index_from_plane(f.plane));
        m_thread_radius->SetValue(f.thread_radius * 2.0);
        m_thread_pitch->SetValue(f.thread_pitch);
        m_thread_height->SetValue(f.thread_height);
        m_thread_depth->SetValue(f.thread_depth);
        m_thread_internal->SetValue(f.thread_internal);
        m_thread_x->SetValue(f.thread_x);
        m_thread_y->SetValue(f.thread_y);
        if (m_thread_std) m_thread_std->SetSelection(0);
        m_thread_on_face    = !is_base_plane(f.plane, m_doc.modeling_origin);
        m_thread_face_plane = f.plane;
        m_thread_face_body  = m_thread_on_face ? f.target_body : -1;
        if (m_thread_target_label)
            m_thread_target_label->SetLabel(m_thread_on_face
                ? (f.target_body >= 0 ? wxString::Format(_L("On a face of Body %d"), f.target_body + 1)
                                      : _L("On a stored face"))
                : _L("(none — uses Thread plane)"));
        break;
    case CadFeatureType::Shell:
        m_shell_thickness->SetValue(f.shell_thickness);
        m_sel_solid_face = f.shell_face;
        m_shell_face_label->SetLabel(f.shell_face >= 0
            ? wxString::Format(_L("Face %d"), f.shell_face)
            : _L("(all faces — closed hollow)"));
        break;
    case CadFeatureType::Revolve:
        m_revolve_angle->SetValue(f.revolve_angle);
        m_revolve_mode->SetSelection(static_cast<int>(f.mode));
        m_revolve_flip->SetValue(f.flip);
        m_revolve_sketch_ref = f.sketch_ref;
        fill_revolve_axes(m_revolve_axis, m_revolve_axis_ents, f.sketch_ref, f.revolve_axis, f.revolve_axis_entity);
        break;
    case CadFeatureType::Sweep:
        m_sweep_profile_ref = f.sketch_ref;
        m_sweep_path_ref    = f.sweep_path_ref;
        m_sweep_mode->SetSelection(static_cast<int>(f.mode));
        break;
    case CadFeatureType::Pattern:
        m_pattern_type->SetSelection(f.pattern_circular ? 1 : 0);
        m_pattern_count->SetValue(f.pattern_count);
        m_pattern_spacing->SetValue(f.pattern_spacing);
        m_pattern_dir->SetSelection(f.pattern_dir);
        m_pattern_angle->SetValue(f.pattern_angle);
        break;
    case CadFeatureType::Plane:
        populate_plane_choices(m_plane_base);
        m_plane_base->SetSelection(f.plane_base);
        m_plane_offset->SetValue(f.plane_offset);
        m_plane_tilt->SetValue(f.plane_angle_tilt);
        m_plane_tilt_axis->SetSelection(f.plane_axis);
        m_plane_type->SetSelection((int)f.plane_type);
        m_pl_faceA_body = f.plane_face_body;  m_pl_faceA = f.plane_face;
        m_pl_faceB_body = f.plane_face2_body; m_pl_faceB = f.plane_face2;
        m_pl_edgeA_body = f.plane_edge_body;  m_pl_edgeA = f.plane_edge;
        m_pl_edgeB_body = f.plane_edge2_body; m_pl_edgeB = f.plane_edge2;
        m_plane_usize->SetValue(f.plane_u_size);
        m_plane_vsize->SetValue(f.plane_v_size);
        m_plane_pick = PlanePick::None;
        if (m_viewport) m_viewport->set_escalate_on_repick(true);
    if (m_viewport) m_viewport->set_escalate_on_repick(true);
        refresh_plane_labels();
        break;
    case CadFeatureType::Loft:
        m_loft_refs = f.loft_profile_refs;
        m_loft_ruled->SetValue(f.loft_ruled);
        m_loft_mode->SetSelection(static_cast<int>(f.mode));
        break;
    case CadFeatureType::Draft:
        m_draft_angle->SetValue(f.draft_angle);
        m_sel_solid_face = f.draft_face;
        m_draft_face_label->SetLabel(f.draft_face >= 0
            ? wxString::Format(_L("Face %d"), f.draft_face)
            : _L("(pick a side face)"));
        break;
    case CadFeatureType::Axis:
        m_axis_type->SetSelection((int)f.axis_type);
        m_ax_face_body = f.axis_body; m_ax_face = f.axis_face;
        m_ax_edge = f.axis_edge;
        populate_plane_choices(m_axis_plane_a);
        populate_plane_choices(m_axis_plane_b);
        m_axis_plane_a->SetSelection(f.axis_plane_a >= 0 ? f.axis_plane_a : 0);
        m_axis_plane_b->SetSelection(f.axis_plane_b >= 0 ? f.axis_plane_b : 0);
        m_axis_p1x->SetValue(f.axis_p1.x()); m_axis_p1y->SetValue(f.axis_p1.y()); m_axis_p1z->SetValue(f.axis_p1.z());
        m_axis_p2x->SetValue(f.axis_p2.x()); m_axis_p2y->SetValue(f.axis_p2.y()); m_axis_p2z->SetValue(f.axis_p2.z());
        m_axis_pick = AxisPick::None;
        if (m_viewport) m_viewport->set_escalate_on_repick(true);
    if (m_viewport) m_viewport->set_escalate_on_repick(true);
        refresh_axis_labels();
        break;
    case CadFeatureType::CoordSys:
        m_coordsys_type->SetSelection((int)f.coordsys_type);
        m_cs_x->SetValue(f.coordsys_point.x());
        m_cs_y->SetValue(f.coordsys_point.y());
        m_cs_z->SetValue(f.coordsys_point.z());
        m_cs_face_body = f.coordsys_body; m_cs_face = f.coordsys_face;
        m_cs_edge = f.coordsys_edge;
        m_cs_hx->SetValue(f.coordsys_x_hint.x());
        m_cs_hy->SetValue(f.coordsys_x_hint.y());
        m_cs_hz->SetValue(f.coordsys_x_hint.z());
        m_coordsys_pick = CoordSysPick::None;
        if (m_viewport) m_viewport->set_escalate_on_repick(true);
    if (m_viewport) m_viewport->set_escalate_on_repick(true);
        refresh_coordsys_labels();
        refresh_cs_body_choice();
        if (m_cs_body && f.coordsys_body >= 0) {
            m_cs_body->SetSelection(f.coordsys_body + 1);
            if (m_viewport) m_viewport->set_xray_focus(f.coordsys_body);
        }
        break;
    case CadFeatureType::Boolean:
        populate_body_choices(m_edit_index);
        m_bool_op->SetSelection(f.mode == BooleanMode::Cut       ? 1
                              : f.mode == BooleanMode::Intersect ? 2 : 0);
        if (f.target_body >= 0 && f.target_body < int(m_bool_target->GetCount()))
            m_bool_target->SetSelection(f.target_body);
        if (f.bool_tool_body >= 0 && f.bool_tool_body < int(m_bool_tool->GetCount()))
            m_bool_tool->SetSelection(f.bool_tool_body);
        m_bool_keep->SetValue(f.bool_keep_tool);
        m_bool_tol->SetValue(f.bool_tolerance);
        break;
    case CadFeatureType::Cut:
        populate_body_choices(m_edit_index);
        if (f.target_body >= 0 && f.target_body < int(m_cut_target->GetCount()))
            m_cut_target->SetSelection(f.target_body);
        else if (m_cut_target->GetCount() > 0)
            m_cut_target->SetSelection(0);
        populate_plane_choices(m_cut_plane);
        m_cut_plane->SetSelection(index_from_plane(f.plane));
        m_cut_offset->SetValue(f.cut_offset);
        break;
    case CadFeatureType::SurfaceExtrude:
        m_surf_extrude_distance->SetValue(f.distance);
        m_surf_extrude_sketch_ref = f.sketch_ref;
        if (m_surf_extrude_sketch_ref >= 0 && m_surf_extrude_sketch_ref < int(m_doc.features.size()))
            m_surf_extrude_sketch_label->SetLabel(_L("Sketch: ") +
                wxString::FromUTF8(m_doc.features[m_surf_extrude_sketch_ref].name));
        break;
    case CadFeatureType::SurfaceRevolve:
        m_surf_revolve_angle->SetValue(f.revolve_angle);
        m_surf_revolve_flip->SetValue(f.flip);
        m_surf_revolve_sketch_ref = f.sketch_ref;
        fill_revolve_axes(m_surf_revolve_axis, m_surf_revolve_axis_ents, f.sketch_ref, f.revolve_axis, f.revolve_axis_entity);
        if (m_surf_revolve_sketch_ref >= 0 && m_surf_revolve_sketch_ref < int(m_doc.features.size()))
            m_surf_revolve_sketch_label->SetLabel(_L("Sketch: ") +
                wxString::FromUTF8(m_doc.features[m_surf_revolve_sketch_ref].name));
        break;
    case CadFeatureType::SurfaceLoft:
        m_surf_loft_refs = f.loft_profile_refs;
        m_surf_loft_ruled->SetValue(f.loft_ruled);
        break;
    case CadFeatureType::SurfaceFill:
        m_surf_fill_sketch_ref = f.sketch_ref;
        if (m_surf_fill_sketch_ref >= 0 && m_surf_fill_sketch_ref < int(m_doc.features.size()))
            m_surf_fill_sketch_label->SetLabel(_L("Sketch: ") +
                wxString::FromUTF8(m_doc.features[m_surf_fill_sketch_ref].name));
        break;
    case CadFeatureType::SurfaceOffset:
        populate_sheet_body_choices(m_surf_offset_body);
        m_surf_offset_distance->SetValue(f.plane_offset);
        select_sheet_choice(m_surf_offset_body, f.target_body);
        break;
    case CadFeatureType::ThickenSurface:
        populate_sheet_body_choices(m_surf_thicken_body);
        m_surf_thicken_thickness->SetValue(f.thicken_thickness);
        m_surf_thicken_flip->SetValue(f.thicken_flip);
        select_sheet_choice(m_surf_thicken_body, f.target_body);
        break;
    case CadFeatureType::Transform: {
        fill_body_choice(m_xf_body, m_edit_index, f.target_body);
        m_xf_dx->SetValue(f.xf_translate.x());
        m_xf_dy->SetValue(f.xf_translate.y());
        m_xf_dz->SetValue(f.xf_translate.z());
        const int ax = (std::abs(f.xf_axis.x()) > 0.5) ? 0 : (std::abs(f.xf_axis.y()) > 0.5) ? 1 : 2;
        m_xf_axis->SetSelection(ax);
        m_xf_angle->SetValue(f.xf_angle_deg);
        m_xf_pivot_x->SetValue(f.xf_pivot.x());
        m_xf_pivot_y->SetValue(f.xf_pivot.y());
        m_xf_pivot_z->SetValue(f.xf_pivot.z());
        m_xf_copy->SetValue(f.xf_copy);
        break;
    }
    case CadFeatureType::Mirror: {
        fill_body_choice(m_mirror_body, m_edit_index, f.target_body);
        populate_plane_choices(m_mirror_plane);
        m_mirror_plane->SetSelection(index_from_plane(f.plane));
        m_mirror_keep->SetValue(f.mirror_keep_original);
        break;
    }
    case CadFeatureType::Thicken: {
        fill_body_choice(m_thicken_body, m_edit_index, f.target_body);
        m_sel_solid_face = f.thicken_face;
        m_thicken_face_label->SetLabel(f.thicken_face >= 0
            ? wxString::Format(_L("Face %d"), f.thicken_face)
            : _L("(pick a solid face)"));
        m_thicken_thickness->SetValue(f.thicken_thickness);
        m_thicken_flip->SetValue(f.thicken_flip);
        break;
    }
    case CadFeatureType::Rib: {
        fill_body_choice(m_rib_body, m_edit_index, f.target_body);
        {
            m_rib_sketch->Clear();
            int pre_sel = wxNOT_FOUND;
            for (int i = 0; i < int(m_doc.features.size()); ++i) {
                if (m_doc.features[i].type == CadFeatureType::Sketch) {
                    const int pos = combo_append_index(m_rib_sketch,
                                                       wxString::FromUTF8(m_doc.features[i].name), i);
                    if (i == f.rib_sketch_ref) pre_sel = pos;
                }
            }
            if (pre_sel != wxNOT_FOUND) m_rib_sketch->SetSelection(pre_sel);
            else if (m_rib_sketch->GetCount() > 0) m_rib_sketch->SetSelection(0);
        }
        m_rib_entity->SetValue(f.rib_entity);
        m_rib_thickness->SetValue(f.rib_thickness);
        m_rib_depth->SetValue(f.rib_depth);
        break;
    }
    case CadFeatureType::Project: {
        fill_body_choice(m_proj_source_body, m_edit_index, f.project_source_body);
        populate_plane_choices(m_proj_plane);
        m_proj_plane->SetSelection(index_from_plane(f.plane));
        m_sel_solid_face = f.project_face;
        m_proj_face_label->SetLabel(f.project_face >= 0
            ? wxString::Format(_L("Face %d"), f.project_face)
            : _L("(all edges)"));
        break;
    }
    case CadFeatureType::DeleteFace: {
        fill_body_choice(m_del_face_body, m_edit_index, f.target_body);
        m_del_faces = f.delete_faces;
        {
            wxString s;
            for (size_t i = 0; i < m_del_faces.size(); ++i) {
                if (i > 0) s += ", ";
                s += wxString::Format(_L("Face %d"), m_del_faces[i]);
            }
            m_del_face_list->SetLabel(s.empty() ? _L("(none)") : s);
        }
        break;
    }
    case CadFeatureType::Helix:
        populate_plane_choices(m_helix_plane);
        m_helix_plane->SetSelection(index_from_plane(f.plane));
        m_helix_radius->SetValue(f.helix_radius);
        m_helix_pitch->SetValue(f.helix_pitch);
        m_helix_height->SetValue(f.helix_height);
        m_helix_left_handed->SetValue(f.helix_left_handed);
        m_helix_taper->SetValue(f.helix_taper_deg);
        break;
    case CadFeatureType::Mate:
        m_mate_kind->SetSelection(f.mate_kind);
        m_mate_offset->SetValue(f.mate_offset);
        m_mate_angle->SetValue(f.mate_angle);
        m_mate_flip->SetValue(f.mate_flip);
        break;
    default: break;
    }
}

void DesignPanel::on_edit_feature()
{
    int sel = tree_selection();
    if (sel == wxNOT_FOUND) {
        set_status(StatusKind::Info, _L("Select a feature in the tree first"));
        return;
    }
    const CadFeature& f = m_doc.features[sel];
    reset_edit_state();

    switch (f.type) {
    case CadFeatureType::Sketch:
        if (f.is_text()) {
            open_text_dialog(sel);
            break;
        }
        if (!f.imported_regions.empty()) {
            m_doc.checkpoint();
            on_transform_imported(sel);
            m_insert_feat = sel;
            open_insert_card(wxString::FromUTF8(f.name));
            break;
        }
        m_edit_index = sel;
        if (!f.entities.empty()) {
            set_ui_mode(UiMode::Sketch);
            if (m_viewport) {
                m_viewport->set_display_sketches({});
                m_viewport->edit_sketch(f.entities, f.entity_constraints, f.plane);
            }
            set_status(StatusKind::Info, _L("Editing sketch — drag a handle or double-click a dimension to edit it"));
        } else {
            load_feature_into_dialog(f);
            open_tool(Tool::Sketch);
        }
        break;
    case CadFeatureType::Extrude:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Extrude);
        break;
    case CadFeatureType::Fillet:
    case CadFeatureType::Chamfer:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Dressup);
        break;
    case CadFeatureType::Hole:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Hole);
        break;
    case CadFeatureType::Thread:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Thread);
        break;
    case CadFeatureType::Shell:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Shell);
        break;
    case CadFeatureType::Revolve:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Revolve);
        break;
    case CadFeatureType::Sweep:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Sweep);
        break;
    case CadFeatureType::Pattern:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Pattern);
        break;
    case CadFeatureType::Plane:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Plane);
        break;
    case CadFeatureType::Axis:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Axis);
        break;
    case CadFeatureType::CoordSys:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::CoordSys);
        break;
    case CadFeatureType::Loft:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Loft);
        break;
    case CadFeatureType::Draft:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Draft);
        break;
    case CadFeatureType::Boolean:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Boolean);
        break;
    case CadFeatureType::SurfaceExtrude:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::SurfaceExtrude);
        break;
    case CadFeatureType::SurfaceRevolve:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::SurfaceRevolve);
        break;
    case CadFeatureType::SurfaceLoft:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::SurfaceLoft);
        break;
    case CadFeatureType::SurfaceFill:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::SurfaceFill);
        break;
    case CadFeatureType::SurfaceOffset:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::SurfaceOffset);
        break;
    case CadFeatureType::ThickenSurface:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::ThickenSurface);
        break;
    case CadFeatureType::Transform:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Transform);
        break;
    case CadFeatureType::Mirror:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Mirror);
        break;
    case CadFeatureType::Thicken:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Thicken);
        break;
    case CadFeatureType::Rib:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Rib);
        break;
    case CadFeatureType::Project:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Project);
        break;
    case CadFeatureType::DeleteFace:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::DeleteFace);
        break;
    case CadFeatureType::Helix:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Helix);
        break;
    case CadFeatureType::Mate:
        m_edit_index = sel;
        m_mate_kind->SetSelection(f.mate_kind);
        m_mate_offset->SetValue(f.mate_offset);
        m_mate_angle->SetValue(f.mate_angle);
        m_mate_flip->SetValue(f.mate_flip);
        open_tool(Tool::Mate);
        {
            int pre_a = wxNOT_FOUND, pre_b = wxNOT_FOUND;
            for (unsigned i = 0; i < m_mate_cs_a->GetCount(); ++i) {
                const auto* cd = m_mate_cs_a->GetClientData(i);
                if (cd && int(reinterpret_cast<intptr_t>(cd)) == f.mate_cs_a) pre_a = int(i);
            }
            for (unsigned i = 0; i < m_mate_cs_b->GetCount(); ++i) {
                const auto* cd = m_mate_cs_b->GetClientData(i);
                if (cd && int(reinterpret_cast<intptr_t>(cd)) == f.mate_cs_b) pre_b = int(i);
            }
            if (pre_a != wxNOT_FOUND) m_mate_cs_a->SetSelection(pre_a);
            if (pre_b != wxNOT_FOUND) m_mate_cs_b->SetSelection(pre_b);
        }
        break;
    case CadFeatureType::Cut:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Cut);
        break;
    case CadFeatureType::Import:
        set_status(StatusKind::Info, _L("An imported solid has no parameters — use Transform to move or rotate it"));
        break;
    default:
        set_status(StatusKind::Info, _L("This feature type can't be edited yet"));
        break;
    }
}

void DesignPanel::on_export_step()
{
    if (m_active != Tool::None)
        confirm_tool();
    if (m_doc.bodies.empty()) {
        set_status(StatusKind::Info, _L("Nothing to export — add a feature first"));
        return;
    }
    wxFileDialog dlg(this, _L("Export STEP"), wxEmptyString, "model.step",
                     _L("STEP files (*.step;*.stp)|*.step;*.stp"),
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK)
        return;
    sync_body_xform();
    std::string err;
    const bool ok = m_doc.export_step(dlg.GetPath().ToUTF8().data(), m_body_xform, err);
    set_status(ok ? StatusKind::Ok : StatusKind::Error,
               ok ? _L("Exported STEP")
                  : wxString::Format(_L("STEP export failed: %s"), wxString::FromUTF8(err)));
    m_status->Refresh();
}

void DesignPanel::set_commit_mode(CommitMode mode)
{
    m_commit_mode = mode;
    const bool bodies = mode == CommitMode::Bodies;
    if (m_commit_btn) {
        m_commit_btn->SetBitmap_(commit_icon(bodies));
        m_commit_btn->SetToolTip(bodies ? _L("Commit to Plate (as bodies) — send each body to Prepare as its own object")
                                        : _L("Commit to Plate — send the solid to Prepare"));
    }
    if (m_commit_drop)
        m_commit_drop->SetSelection(int(mode));
}

void DesignPanel::on_commit()
{
    if (m_active != Tool::None)
        confirm_tool();
    end_sketch_plane_choice();

    if (m_doc.display_mesh.its.indices.empty()) {
        set_status(_L("Nothing to commit — add a feature first"));
        return;
    }
    ObjectList* obj_list = wxGetApp().obj_list();
    if (obj_list == nullptr)
        return;

    sync_body_visible();
    rebuild_disp_meshes();
    if (m_disp_body_meshes.size() > 1) {
        std::vector<std::pair<const TriangleMesh*, wxString>> parts;
        for (size_t b = 0; b < m_disp_body_meshes.size(); ++b) {
            if (b < m_body_visible.size() && !m_body_visible[b]) continue;
            if (m_disp_body_meshes[b].its.indices.empty()) continue;
            parts.emplace_back(&m_disp_body_meshes[b], wxString::FromUTF8("Design Body " + std::to_string(b + 1)));
        }
        if (parts.empty()) {
            set_status(_L("All bodies hidden — show one before committing"));
            return;
        }
        if (m_commit_mode == CommitMode::Assembly && parts.size() > 1)
            obj_list->load_mesh_object(parts, "Design Assembly");
        else
            for (const auto& [mesh, name] : parts)
                obj_list->load_mesh_object(*mesh, name);
    } else {
        obj_list->load_mesh_object(m_disp_pick_mesh, "Design Body");
    }

    sync_recipe_to_model();

    if (wxGetApp().mainframe != nullptr)
        wxGetApp().mainframe->select_tab(TAB_ID_PREPARE);
}

CadFeature DesignPanel::build_candidate(Tool t) const
{
    CadFeature f;
    const bool editing = (m_edit_index >= 0 && m_edit_index < int(m_doc.features.size()));
    if (editing)
        f = m_doc.features[m_edit_index];
    switch (t) {
    case Tool::Sketch:
        f.type   = CadFeatureType::Sketch;
        f.shape  = (m_shape->GetSelection() == 0) ? SketchShape::Rectangle : SketchShape::Circle;
        if (!editing) { wxString where; f.plane = sketch_plane_from_selection(where); }
        f.width  = m_width->GetValue();
        f.height = m_height->GetValue();
        f.radius = m_radius->GetValue();
        break;
    case Tool::Extrude:
        f.type        = CadFeatureType::Extrude;
        f.distance    = m_distance->GetValue();
        f.symmetric   = false;
        f.extrude_end = static_cast<ExtrudeEnd>(m_extrude_end->GetSelection());
        f.distance2   = m_distance2->GetValue();
        f.taper_deg   = m_taper->GetValue();
        f.flip        = m_flip->GetValue();
        f.mode       = (m_mode->GetSelection() == 0) ? BooleanMode::New
                     : (m_mode->GetSelection() == 1) ? BooleanMode::Add
                     : (m_mode->GetSelection() == 2) ? BooleanMode::Cut
                                                     : BooleanMode::Intersect;
        if (!editing) {
            f.up_to_face = (f.extrude_end == ExtrudeEnd::UpToFace) ? m_sel_solid_face : -1;
            if (m_extrude_face_src >= 0) {
                f.extrude_src_face = m_extrude_face_src;
                f.sketch_ref       = -1;
            } else if (extrude_uses_loop()) {
                f.sketch_ref = -1;
                f.entities   = m_viewport->selected_loop_entities();
                f.plane      = m_doc.features[m_extrude_sketch_ref].plane;
            } else {
                f.sketch_ref = m_extrude_sketch_ref;
                if (m_extrude_sketch_ref >= 0 && m_extrude_sketch_ref < int(m_doc.features.size())
                    && m_doc.features[m_extrude_sketch_ref].import_on_face)
                    f.target_body = m_doc.features[m_extrude_sketch_ref].import_face_body;
            }
        }
        break;
    case Tool::Dressup:
        f.type         = (m_dressup_type->GetSelection() == 0) ? CadFeatureType::Fillet
                                                               : CadFeatureType::Chamfer;
        f.dressup_size = m_dressup_size->GetValue();
        f.face_group   = static_cast<FaceGroup>(m_face_group->GetSelection());
        {
            const std::vector<int> edges = dressup_edges();
            f.dressup_edge  = edges.empty() ? -1 : edges.front();
            f.dressup_edges = edges.size() > 1 ? edges : std::vector<int>();
        }
        break;
    case Tool::Hole:
        f.type          = CadFeatureType::Hole;
        f.plane         = hole_plane();
        f.hole_diameter = m_hole_diameter->GetValue();
        f.hole_depth    = m_hole_depth->GetValue();
        f.hole_through  = m_hole_through->GetValue();
        f.hole_x        = m_hole_x->GetValue();
        f.hole_y        = m_hole_y->GetValue();
        if (m_hole_on_face) f.target_body = m_hole_face_body;
        break;
    case Tool::Thread:
        f.type            = CadFeatureType::Thread;
        f.plane           = thread_plane();
        f.thread_radius   = m_thread_radius->GetValue() * 0.5;
        f.thread_pitch    = m_thread_pitch->GetValue();
        f.thread_height   = m_thread_height->GetValue();
        f.thread_depth    = m_thread_depth->GetValue();
        f.thread_internal = m_thread_internal->GetValue();
        f.thread_x        = m_thread_x->GetValue();
        f.thread_y        = m_thread_y->GetValue();
        if (!editing) f.thread_major_nominal = true;
        if (m_thread_on_face) f.target_body = m_thread_face_body;
        break;
    case Tool::Shell:
        f.type            = CadFeatureType::Shell;
        f.shell_thickness = m_shell_thickness->GetValue();
        f.shell_face      = (m_sel_solid_face >= 0) ? m_sel_solid_face : -1;
        break;
    case Tool::Draft:
        f.type        = CadFeatureType::Draft;
        f.draft_angle = m_draft_angle->GetValue();
        f.draft_face  = (m_sel_solid_face >= 0) ? m_sel_solid_face : -1;
        break;
    case Tool::Revolve:
        f.type          = CadFeatureType::Revolve;
        f.sketch_ref    = m_revolve_sketch_ref;
        f.revolve_angle = m_revolve_angle->GetValue();
        read_revolve_axis(m_revolve_axis, m_revolve_axis_ents, f.revolve_axis, f.revolve_axis_entity);
        f.flip          = m_revolve_flip->GetValue();
        f.mode          = static_cast<BooleanMode>(m_revolve_mode->GetSelection());
        break;
    case Tool::Sweep: {
        f.type           = CadFeatureType::Sweep;
        f.sketch_ref     = m_sweep_profile_ref;
        const int sel    = m_sweep_path ? m_sweep_path->GetSelection() : wxNOT_FOUND;
        f.sweep_path_ref = (sel != wxNOT_FOUND)
            ? int(reinterpret_cast<intptr_t>(m_sweep_path->GetClientData(sel))) : -1;
        f.mode           = static_cast<BooleanMode>(m_sweep_mode->GetSelection());
        break;
    }
    case Tool::Pattern:
        f.type             = CadFeatureType::Pattern;
        f.pattern_circular = (m_pattern_type->GetSelection() == 1);
        f.pattern_count    = int(m_pattern_count->GetValue());
        f.pattern_spacing  = m_pattern_spacing->GetValue();
        f.pattern_dir      = m_pattern_dir->GetSelection();
        f.pattern_angle    = m_pattern_angle->GetValue();
        if (!editing) {
            f.pattern_inclusive = true;
            f.plane             = SketchPlane::XY();
            f.plane.origin     += m_doc.modeling_origin;
        }
        break;
    case Tool::Plane:
        f.type             = CadFeatureType::Plane;
        f.plane_base       = m_plane_base->GetSelection();
        f.plane_offset     = m_plane_offset->GetValue();
        f.plane_angle_tilt = m_plane_tilt->GetValue();
        f.plane_axis       = m_plane_tilt_axis->GetSelection();
        apply_plane_refs(f);
        break;
    case Tool::Axis:
        f.type = CadFeatureType::Axis;
        apply_axis_refs(f);
        break;
    case Tool::CoordSys:
        f.type = CadFeatureType::CoordSys;
        apply_coordsys_refs(f);
        break;
    case Tool::Loft: {
        f.type      = CadFeatureType::Loft;
        f.loft_ruled = m_loft_ruled->GetValue();
        f.mode      = static_cast<BooleanMode>(m_loft_mode->GetSelection());
        f.loft_profile_refs.clear();
        for (unsigned i = 0; i < m_loft_list->GetCount(); ++i)
            if (m_loft_list->IsChecked(i) && i < m_loft_sketch_idx.size())
                f.loft_profile_refs.push_back(m_loft_sketch_idx[i]);
        break;
    }
    case Tool::Boolean: {
        f.type           = CadFeatureType::Boolean;
        const int sel    = m_bool_op->GetSelection();
        f.mode           = (sel == 1) ? BooleanMode::Cut
                         : (sel == 2) ? BooleanMode::Intersect
                                      : BooleanMode::Add;
        f.target_body    = m_bool_target->GetSelection();
        f.bool_tool_body = m_bool_tool->GetSelection();
        f.bool_keep_tool = m_bool_keep->GetValue();
        f.bool_tolerance = m_bool_tol->GetValue();
        break;
    }
    case Tool::Cut:
        f.type           = CadFeatureType::Cut;
        f.plane          = plane_from_choice(m_cut_plane->GetSelection());
        f.cut_offset     = m_cut_offset->GetValue();
        f.cut_flip       = false;
        f.cut_keep_upper = true;
        f.cut_keep_lower = true;
        f.target_body    = m_cut_target->GetSelection();
        break;
    case Tool::SurfaceExtrude:
        f.type      = CadFeatureType::SurfaceExtrude;
        f.sketch_ref = m_surf_extrude_sketch_ref;
        f.distance   = m_surf_extrude_distance->GetValue();
        break;
    case Tool::SurfaceRevolve:
        f.type         = CadFeatureType::SurfaceRevolve;
        f.sketch_ref   = m_surf_revolve_sketch_ref;
        f.revolve_angle = m_surf_revolve_angle->GetValue();
        read_revolve_axis(m_surf_revolve_axis, m_surf_revolve_axis_ents, f.revolve_axis, f.revolve_axis_entity);
        f.flip         = m_surf_revolve_flip->GetValue();
        break;
    case Tool::SurfaceLoft: {
        f.type      = CadFeatureType::SurfaceLoft;
        f.loft_ruled = m_surf_loft_ruled->GetValue();
        f.loft_profile_refs.clear();
        for (unsigned i = 0; i < m_surf_loft_list->GetCount(); ++i)
            if (m_surf_loft_list->IsChecked(i) && i < m_surf_loft_sketch_idx.size())
                f.loft_profile_refs.push_back(m_surf_loft_sketch_idx[i]);
        break;
    }
    case Tool::SurfaceFill:
        f.type      = CadFeatureType::SurfaceFill;
        f.sketch_ref = m_surf_fill_sketch_ref;
        break;
    case Tool::SurfaceOffset: {
        f.type = CadFeatureType::SurfaceOffset;
        f.target_body = sheet_choice_body(m_surf_offset_body);
        f.plane_offset = m_surf_offset_distance->GetValue();
        break;
    }
    case Tool::ThickenSurface: {
        f.type = CadFeatureType::ThickenSurface;
        f.target_body       = sheet_choice_body(m_surf_thicken_body);
        f.thicken_thickness = m_surf_thicken_thickness->GetValue();
        f.thicken_flip      = m_surf_thicken_flip->GetValue();
        break;
    }
    case Tool::Transform: {
        f.type = CadFeatureType::Transform;
        const int sel = m_xf_body->GetSelection();
        f.target_body   = (sel != wxNOT_FOUND) ? sel : -1;
        f.xf_translate  = Vec3d(m_xf_dx->GetValue(), m_xf_dy->GetValue(), m_xf_dz->GetValue());
        const int ax = m_xf_axis->GetSelection();
        f.xf_axis   = (ax == 0) ? Vec3d(1, 0, 0) : (ax == 1) ? Vec3d(0, 1, 0) : Vec3d(0, 0, 1);
        f.xf_pivot  = Vec3d(m_xf_pivot_x->GetValue(), m_xf_pivot_y->GetValue(), m_xf_pivot_z->GetValue());
        f.xf_angle_deg = m_xf_angle->GetValue();
        f.xf_copy      = m_xf_copy->GetValue();
        break;
    }
    case Tool::Mirror: {
        f.type = CadFeatureType::Mirror;
        const int sel = m_mirror_body->GetSelection();
        f.target_body          = (sel != wxNOT_FOUND) ? sel : -1;
        f.plane                = plane_from_choice(m_mirror_plane->GetSelection());
        f.mirror_keep_original = m_mirror_keep->GetValue();
        f.mode                 = f.mirror_keep_original ? BooleanMode::New : BooleanMode::Add;
        break;
    }
    case Tool::Thicken: {
        f.type = CadFeatureType::Thicken;
        const int sel = m_thicken_body->GetSelection();
        f.target_body       = (sel != wxNOT_FOUND) ? sel : -1;
        f.thicken_face      = (m_sel_solid_face >= 0) ? m_sel_solid_face : -1;
        f.thicken_thickness = m_thicken_thickness->GetValue();
        f.thicken_flip      = m_thicken_flip->GetValue();
        break;
    }
    case Tool::Rib: {
        f.type = CadFeatureType::Rib;
        const int bsel = m_rib_body->GetSelection();
        f.target_body      = (bsel != wxNOT_FOUND) ? bsel : -1;
        const int ssel = m_rib_sketch->GetSelection();
        f.rib_sketch_ref = (ssel != wxNOT_FOUND)
                               ? int(reinterpret_cast<intptr_t>(m_rib_sketch->GetClientData(ssel))) : -1;
        f.rib_entity     = m_rib_entity->GetValue();
        f.rib_thickness  = m_rib_thickness->GetValue();
        f.rib_depth      = m_rib_depth->GetValue();
        break;
    }
    case Tool::Project: {
        f.type = CadFeatureType::Project;
        const int sel = m_proj_source_body->GetSelection();
        f.project_source_body = (sel != wxNOT_FOUND) ? sel : -1;
        f.plane               = plane_from_choice(m_proj_plane->GetSelection());
        if (m_sel_solid_face >= 0) {
            f.project_face = m_sel_solid_face;
        } else {
            f.project_face = -1;
        }
        f.project_edges.clear();
        break;
    }
    case Tool::DeleteFace: {
        f.type = CadFeatureType::DeleteFace;
        const int sel = m_del_face_body->GetSelection();
        f.target_body  = (sel != wxNOT_FOUND) ? sel : -1;
        f.delete_faces = m_del_faces;
        break;
    }
    case Tool::Helix: {
        f.type = CadFeatureType::Helix;
        f.plane              = plane_from_choice(m_helix_plane->GetSelection());
        f.helix_radius       = m_helix_radius->GetValue();
        f.helix_pitch        = m_helix_pitch->GetValue();
        f.helix_height       = m_helix_height->GetValue();
        f.helix_left_handed  = m_helix_left_handed->GetValue();
        f.helix_taper_deg    = m_helix_taper->GetValue();
        break;
    }
    case Tool::Mate: {
        f.type       = CadFeatureType::Mate;
        f.mate_kind  = m_mate_kind->GetSelection();
        f.mate_cs_a  = m_mate_cs_a->GetSelection() != wxNOT_FOUND
                           ? int(reinterpret_cast<intptr_t>(m_mate_cs_a->GetClientData(m_mate_cs_a->GetSelection()))) : -1;
        f.mate_cs_b  = m_mate_cs_b->GetSelection() != wxNOT_FOUND
                           ? int(reinterpret_cast<intptr_t>(m_mate_cs_b->GetClientData(m_mate_cs_b->GetSelection()))) : -1;
        f.mate_offset = m_mate_offset->GetValue();
        f.mate_angle  = m_mate_angle->GetValue();
        f.mate_flip   = m_mate_flip->GetValue();
        break;
    }
    case Tool::Insert:
    case Tool::None:
        break;
    }
    if (!editing && m_active != Tool::Boolean && m_active != Tool::Cut
        && m_active != Tool::Transform && m_active != Tool::Mirror && m_active != Tool::Thicken
        && m_active != Tool::DeleteFace && m_active != Tool::Rib && m_active != Tool::Project
        && m_active != Tool::Helix && m_active != Tool::SurfaceOffset
        && m_active != Tool::ThickenSurface)
        f.target_body = (m_active == Tool::Extrude && m_sel_solid_body < 0) ? m_extrude_auto_body
                                                                           : m_sel_solid_body;
    return f;
}

std::vector<int> DesignPanel::dressup_edges() const
{
    if (m_sel_solid_edge < 0) return {};
    if (!m_sel_solid_edges.empty() && m_sel_solid_edges.back() == m_sel_solid_edge)
        return m_sel_solid_edges;
    return { m_sel_solid_edge };
}

void DesignPanel::sync_dressup_target()
{
    if (m_dressup_edge_label == nullptr) return;
    const size_t n = dressup_edges().size();
    const bool have_edge = n > 0;
    m_dressup_edge_label->SetLabel(n > 1 ? wxString::Format(_L_PLURAL("%zu edge", "%zu edges", n), n)
        : have_edge ? wxString::Format(_L("Edge %d"), m_sel_solid_edge)
        : _L("(no edge picked — group below)"));
    if (m_face_group != nullptr) m_face_group->Enable(!have_edge);
    m_dressup_edge_label->Refresh();
}

void DesignPanel::update_fillet_gizmo()
{
    if (!m_viewport) return;
    const bool ok = (m_active == Tool::Dressup) && m_sel_solid_edge >= 0
                    && m_sel_solid_body >= 0 && m_sel_solid_body < int(m_disp_body_meshes.size());
    if (!ok) { m_viewport->clear_fillet_gizmo(); return; }
    const Vec3d centroid = m_disp_body_meshes[m_sel_solid_body].bounding_box().center();
    m_viewport->begin_fillet_gizmo(centroid, m_dressup_size->GetValue());
}

void DesignPanel::update_body_gates()
{
    const int n = int(m_doc.bodies.size());
    for (const BodyGate& g : m_body_gates) {
        if (g.btn == nullptr) continue;
        const bool live = n >= g.min_bodies;
        g.btn->Enable(live);
        g.btn->SetToolTip(live ? g.tip_live : g.tip_gated);
    }
}

void DesignPanel::update_hole_gizmo()
{
    if (!m_viewport) return;
    if (m_active != Tool::Hole) { m_viewport->clear_hole_gizmo(); return; }
    const SketchPlane plane = hole_plane();
    m_viewport->set_hole_face_bounds(m_hole_has_bounds, m_hole_umin, m_hole_umax,
                                     m_hole_vmin, m_hole_vmax);
    m_viewport->begin_hole_gizmo(plane,
                                 m_hole_x->GetValue(), m_hole_y->GetValue(),
                                 m_hole_diameter->GetValue(), m_hole_depth->GetValue(),
                                 m_hole_through->GetValue());
}

void DesignPanel::update_thread_gizmo()
{
    if (!m_viewport) return;
    if (m_active != Tool::Thread) { m_viewport->clear_thread_gizmo(); return; }
    const SketchPlane plane = thread_plane();
    m_viewport->begin_thread_gizmo(plane,
                                   m_thread_x->GetValue(), m_thread_y->GetValue(),
                                   m_thread_radius->GetValue() * 0.5, m_thread_height->GetValue());
}

void DesignPanel::update_shell_gizmo()
{
    if (!m_viewport) return;
    const int b = m_sel_solid_body;
    const bool ok = (m_active == Tool::Shell) && m_sel_solid_face >= 0
                    && b >= 0 && b < int(m_doc.bodies.size());
    if (!ok) { m_viewport->clear_shell_gizmo(); return; }
    const TopoDS_Face fc = GeometryEngine::face_by_index(m_doc.bodies[b].shape, m_sel_solid_face);
    if (fc.IsNull()) { m_viewport->clear_shell_gizmo(); return; }
    Vec3d c = GeometryEngine::face_centroid_world(fc);
    Vec3d n = GeometryEngine::face_normal_world(fc);
    sync_body_xform();
    if (b < int(m_body_xform.size())) {
        c = m_body_xform[b] * c;
        n = m_body_xform[b].linear() * n;
    }
    if (n.norm() < 1e-9) { m_viewport->clear_shell_gizmo(); return; }
    m_viewport->begin_shell_gizmo(c, (-n).normalized(), m_shell_thickness->GetValue());
}

void DesignPanel::fill_revolve_axes(ComboBox* combo, std::vector<int>& ents, int sketch_ref, int axis, int entity)
{
    combo->Clear();
    ents.clear();
    combo->Append(_L("Plane X"));
    combo->Append(_L("Plane Y"));
    int centerline = -1, centerlines = 0;
    if (sketch_ref >= 0 && sketch_ref < int(m_doc.features.size())) {
        const std::vector<SketchEntity>& es = m_doc.features[sketch_ref].entities;
        for (int i = 0; i < int(es.size()); ++i) {
            if (es[i].type != SketchEntity::Type::Line)
                continue;
            combo->Append(es[i].construction ? wxString::Format(_L("Centerline E%d"), i)
                                             : wxString::Format(_L("Line E%d"), i));
            ents.push_back(i);
            if (es[i].construction) { centerline = i; ++centerlines; }
        }
    }
    if (entity == -2)
        entity = centerlines == 1 ? centerline : -1;
    int sel = axis == 1 ? 1 : 0;
    for (int k = 0; k < int(ents.size()); ++k)
        if (ents[k] == entity) sel = 2 + k;
    combo->SetSelection(sel);
}

void DesignPanel::read_revolve_axis(ComboBox* combo, const std::vector<int>& ents, int& axis, int& entity)
{
    const int sel = combo->GetSelection();
    axis   = sel == 1 ? 1 : 0;
    entity = sel >= 2 && sel - 2 < int(ents.size()) ? ents[sel - 2] : -1;
}

void DesignPanel::update_revolve_gizmo()
{
    if (!m_viewport) return;
    if (m_active != Tool::Revolve
        || m_revolve_sketch_ref < 0 || m_revolve_sketch_ref >= int(m_doc.features.size())) {
        m_viewport->clear_revolve_gizmo();
        return;
    }
    const CadFeature& sk = m_doc.features[m_revolve_sketch_ref];
    Vec2d centroid(0, 0);
    if (!sk.entities.empty()) {
        Vec2d acc(0, 0); int n = 0;
        for (const SketchEntity& e : sk.entities) {
            if (e.construction) continue;
            switch (e.type) {
            case SketchEntity::Type::Line:    acc += 0.5 * (e.p0 + e.p1); ++n; break;
            case SketchEntity::Type::Arc:
            case SketchEntity::Type::EllipseArc:
            case SketchEntity::Type::Circle:
            case SketchEntity::Type::Ellipse: acc += e.center; ++n; break;
            case SketchEntity::Type::Point:   acc += e.p0; ++n; break;
            case SketchEntity::Type::BSpline:
                if (!e.ctrl.empty()) {
                    Vec2d s(0, 0); for (const Vec2d& q : e.ctrl) s += q;
                    acc += s / double(e.ctrl.size()); ++n;
                }
                break;
            }
        }
        if (n > 0) centroid = acc / double(n);
    } else if (!sk.profile.points.empty()) {
        for (const Vec2d& p : sk.profile.points) centroid += p;
        centroid /= double(sk.profile.points.size());
    }
    int axis = 0, axis_entity = -1;
    read_revolve_axis(m_revolve_axis, m_revolve_axis_ents, axis, axis_entity);
    Vec3d ax_o = sk.plane.origin, ax_d = axis == 1 ? sk.plane.y_axis : sk.plane.x_axis;
    if (axis_entity >= 0 && axis_entity < int(sk.entities.size())
        && (sk.entities[axis_entity].p1 - sk.entities[axis_entity].p0).norm() > 1e-9) {
        ax_o = sk.plane.to_world(sk.entities[axis_entity].p0);
        ax_d = sk.plane.to_world(sk.entities[axis_entity].p1) - ax_o;
    }
    m_viewport->begin_revolve_gizmo(sk.plane, centroid, ax_o, ax_d.normalized(),
                                    m_revolve_angle->GetValue(), m_revolve_flip->GetValue());
}

void DesignPanel::update_draft_gizmo()
{
    if (!m_viewport) return;
    if (m_active != Tool::Draft || m_sel_solid_face < 0 || m_sel_solid_body < 0
        || m_sel_solid_body >= int(m_doc.bodies.size())) {
    m_viewport->clear_draft_gizmo();
        return;
    }
    const TopoDS_Face f = GeometryEngine::face_by_index(m_doc.bodies[m_sel_solid_body].shape, m_sel_solid_face);
    const Vec3d c = GeometryEngine::face_centroid_world(f);
    const Vec3d n = GeometryEngine::face_normal_world(f);
    m_viewport->set_draft_gizmo(c, n, m_draft_angle->GetValue());
}

void DesignPanel::update_cut_gizmo()
{
    if (!m_viewport) return;
    if (m_active != Tool::Cut) { m_viewport->clear_cut_gizmo(); return; }
    const int bi = m_cut_target ? m_cut_target->GetSelection() : -1;
    if (bi < 0 || bi >= int(m_doc.bodies.size()) || bi >= int(m_doc.display_body_meshes.size())) {
        m_viewport->clear_cut_gizmo();
        return;
    }
    const SketchPlane plane = plane_from_choice(m_cut_plane->GetSelection());
    const BoundingBoxf3 bb = m_doc.display_body_meshes[bi].bounding_box();
    const Vec3d center = bb.center();
    const double half = std::max(0.5 * (bb.max - bb.min).norm(), 10.0);
    m_viewport->set_cut_gizmo(plane, m_cut_offset->GetValue(), center, half);
}

void DesignPanel::update_operand_highlight()
{
    if (!m_viewport) return;
    int bt = -1, bl = -1;
    std::vector<std::pair<int, ColorRGBA>> sk;
    if (m_active == Tool::Boolean) {
        if (m_bool_target) bt = m_bool_target->GetSelection();
        if (m_bool_tool)   bl = m_bool_tool->GetSelection();
    } else if (m_active == Tool::Sweep) {
        if (m_sweep_profile_ref >= 0) sk.emplace_back(m_sweep_profile_ref, ColorRGBA(0.30f, 0.85f, 1.0f, 1.0f));
        if (m_sweep_path_ref    >= 0) sk.emplace_back(m_sweep_path_ref,    ColorRGBA(1.00f, 0.40f, 0.90f, 1.0f));
    } else if (m_active == Tool::Loft) {
        if (m_loft_list)
            for (unsigned i = 0; i < m_loft_list->GetCount(); ++i)
                if (m_loft_list->IsChecked(i) && i < m_loft_sketch_idx.size())
                    sk.emplace_back(m_loft_sketch_idx[i], ColorRGBA(0.40f, 0.90f, 0.50f, 1.0f));
    }
    m_viewport->set_operand_bodies(bt, bl);
    m_viewport->set_highlight_sketches(std::move(sk));
}

void DesignPanel::update_pattern_gizmo()
{
    if (!m_viewport) return;
    if (m_active != Tool::Pattern || m_doc.display_body_meshes.empty()) {
        m_viewport->clear_pattern_gizmo();
        return;
    }
    const int b = (m_sel_solid_body >= 0 && m_sel_solid_body < int(m_doc.display_body_meshes.size()))
                  ? m_sel_solid_body : int(m_doc.display_body_meshes.size()) - 1;
    SketchPlane plane;
    Vec3d base = m_doc.display_body_meshes[b].bounding_box().center();
    if (b < int(m_body_xform.size())) {
        plane.origin = m_body_xform[b].translation();
        base = m_body_xform[b] * base;
    }
    m_viewport->begin_pattern_gizmo(plane, base, m_pattern_type->GetSelection() == 1,
                                    int(m_pattern_count->GetValue()), m_pattern_dir->GetSelection(),
                                    m_pattern_spacing->GetValue(), m_pattern_angle->GetValue());
}

void DesignPanel::update_extrude_gizmo()
{
    if (!m_viewport) return;
    if (m_active != Tool::Extrude && m_active != Tool::SurfaceExtrude
        && m_active != Tool::Thicken && m_active != Tool::Rib
        && m_active != Tool::SurfaceOffset && m_active != Tool::ThickenSurface) {
        m_viewport->clear_extrude_gizmo(); return;
    }

    if (m_active == Tool::SurfaceOffset || m_active == Tool::ThickenSurface) {
        const bool off = (m_active == Tool::SurfaceOffset);
        const int sheet = sheet_choice_body(off ? m_surf_offset_body : m_surf_thicken_body);
        if (sheet < 0 || sheet >= int(m_doc.bodies.size())
            || m_sel_solid_body != sheet || m_sel_solid_face < 0) {
            m_viewport->clear_extrude_gizmo(); return;
        }
        TopoDS_Face f = GeometryEngine::face_by_index(m_doc.bodies[sheet].shape, m_sel_solid_face);
        if (f.IsNull()) { m_viewport->clear_extrude_gizmo(); return; }
        SketchPlane plane = SketchPlane::from_face(f);
        Vec3d c = GeometryEngine::face_centroid_world(f);
        Vec3d n = GeometryEngine::face_normal_world(f);
        if (f.Orientation() == TopAbs_REVERSED) n = -n;
        sync_body_xform();
        if (sheet < int(m_body_xform.size())) {
            c = m_body_xform[sheet] * c;
            n = m_body_xform[sheet].linear() * n;
        }
        plane.origin = c;
        if (n.norm() > 1e-9) plane.normal = n.normalized();
        wxSpinCtrlDouble* spin = off ? m_surf_offset_distance : m_surf_thicken_thickness;
        m_viewport->set_extrude_gizmo(plane, Vec2d(0, 0), spin ? spin->GetValue() : 0.0,
                                      0.0, false,
                                      !off && m_surf_thicken_flip && m_surf_thicken_flip->GetValue());
        return;
    }

    if (m_active == Tool::Rib) {
        const int ssel = m_rib_sketch ? m_rib_sketch->GetSelection() : wxNOT_FOUND;
        const int ref  = (ssel != wxNOT_FOUND)
                             ? int(reinterpret_cast<intptr_t>(m_rib_sketch->GetClientData(ssel))) : -1;
        if (ref < 0 || ref >= int(m_doc.features.size())) { m_viewport->clear_extrude_gizmo(); return; }
        const CadFeature& sk = m_doc.features[ref];
        const int ei = m_rib_entity ? m_rib_entity->GetValue() : 0;
        if (ei < 0 || ei >= int(sk.entities.size())) { m_viewport->clear_extrude_gizmo(); return; }
        const SketchEntity& e = sk.entities[ei];
        const Vec2d mid = (e.type == SketchEntity::Type::Line) ? Vec2d(0.5 * (e.p0 + e.p1))
                                                               : Vec2d(e.center);
        m_viewport->set_extrude_gizmo(sk.plane, mid,
                                      m_rib_depth ? m_rib_depth->GetValue() : 0.0,
                                      0.0, false, false);
        return;
    }

    if (m_active == Tool::SurfaceExtrude) {
        const int ref = m_surf_extrude_sketch_ref;
        if (ref < 0 || ref >= int(m_doc.features.size())) { m_viewport->clear_extrude_gizmo(); return; }
        const CadFeature& sk = m_doc.features[ref];
        Vec2d c(0, 0);
        if (!sk.profile.points.empty()) {
            for (const Vec2d& p : sk.profile.points) c += p;
            c /= double(sk.profile.points.size());
        }
        m_viewport->set_extrude_gizmo(sk.plane, c,
                                      m_surf_extrude_distance ? m_surf_extrude_distance->GetValue() : 0.0,
                                      0.0, false, false);
        return;
    }

    if (m_active == Tool::Thicken) {
        const int b = m_sel_solid_body;
        if (b < 0 || b >= int(m_doc.bodies.size()) || m_sel_solid_face < 0) {
            m_viewport->clear_extrude_gizmo(); return;
        }
        TopoDS_Face f = GeometryEngine::face_by_index(m_doc.bodies[b].shape, m_sel_solid_face);
        if (f.IsNull()) { m_viewport->clear_extrude_gizmo(); return; }
        SketchPlane plane = SketchPlane::from_face(f);
        Vec3d c = GeometryEngine::face_centroid_world(f);
        Vec3d n = GeometryEngine::face_normal_world(f);
        if (f.Orientation() == TopAbs_REVERSED) n = -n;
        sync_body_xform();
        if (b < int(m_body_xform.size())) { c = m_body_xform[b] * c; n = m_body_xform[b].linear() * n; }
        plane.origin = c;
        if (n.norm() > 1e-9) plane.normal = n.normalized();
        m_viewport->set_extrude_gizmo(plane, Vec2d(0, 0),
                                      m_thicken_thickness ? m_thicken_thickness->GetValue() : 0.0,
                                      0.0, false,
                                      m_thicken_flip && m_thicken_flip->GetValue());
        return;
    }

    SketchPlane plane;
    std::vector<SketchEntity> ents;
    Vec2d centroid(0, 0);
    bool  have = false, have_centroid = false;
    if (extrude_uses_loop()) {
        plane = m_doc.features[m_extrude_sketch_ref].plane;
        ents  = m_viewport->selected_loop_entities();
        have  = true;
    } else if (m_extrude_sketch_ref >= 0 && m_extrude_sketch_ref < int(m_doc.features.size())) {
        const CadFeature& sk = m_doc.features[m_extrude_sketch_ref];
        plane = sk.plane;
        ents  = sk.entities;
        have  = true;
        if (ents.empty() && !sk.profile.points.empty()) {
            for (const Vec2d& p : sk.profile.points) centroid += p;
            centroid /= double(sk.profile.points.size());
            have_centroid = true;
        }
    } else if (m_extrude_face_src >= 0 && !m_doc.bodies.empty()) {
        const int b = int(m_doc.bodies.size()) - 1;
        TopoDS_Face srcf = GeometryEngine::face_by_index(m_doc.bodies[b].shape, m_extrude_face_src);
        if (!srcf.IsNull()) {
            plane = SketchPlane::from_face(srcf);
            Vec3d c = GeometryEngine::face_centroid_world(srcf);
            Vec3d n = GeometryEngine::face_normal_world(srcf);
            if (srcf.Orientation() == TopAbs_REVERSED) n = -n;
            sync_body_xform();
            if (b < int(m_body_xform.size())) { c = m_body_xform[b] * c; n = m_body_xform[b].linear() * n; }
            plane.origin = c;
            if (n.norm() > 1e-9) plane.normal = n.normalized();
            have = true;
        }
    }
    if (!have) { m_viewport->clear_extrude_gizmo(); return; }

    if (!ents.empty()) {
        Vec2d acc(0, 0); int n = 0;
        for (const SketchEntity& e : ents) {
            switch (e.type) {
            case SketchEntity::Type::Line:    acc += 0.5 * (e.p0 + e.p1); ++n; break;
            case SketchEntity::Type::Arc:
            case SketchEntity::Type::EllipseArc:
            case SketchEntity::Type::Circle:
            case SketchEntity::Type::Ellipse: acc += e.center; ++n; break;
            case SketchEntity::Type::Point:   acc += e.p0; ++n; break;
            case SketchEntity::Type::BSpline:
                if (!e.ctrl.empty()) {
                    Vec2d s(0, 0); for (const Vec2d& q : e.ctrl) s += q;
                    acc += s / double(e.ctrl.size()); ++n;
                }
                break;
            }
        }
        if (n > 0) { centroid = acc / double(n); have_centroid = true; }
    }
    (void)have_centroid;

    const ExtrudeEnd end = static_cast<ExtrudeEnd>(m_extrude_end->GetSelection());
    m_viewport->set_extrude_gizmo(plane, centroid,
                                  m_distance->GetValue(), m_distance2->GetValue(),
                                  end == ExtrudeEnd::TwoSided, m_flip->GetValue());
}

void DesignPanel::refresh_datum_planes()
{
    if (!m_viewport) return;
    std::vector<SketchPlane> dplanes;
    for (const auto& dp : m_doc.resolve_datum_planes()) dplanes.push_back(dp.second);
    std::vector<Vec2d> dsizes;
    for (const auto& f : m_doc.features)
        if (f.type == CadFeatureType::Plane && f.enabled)
            dsizes.emplace_back(f.plane_u_size, f.plane_v_size);
    m_viewport->set_datum_planes(std::move(dplanes), std::move(dsizes));
    refresh_mate_connectors();
}

void DesignPanel::refresh_mate_connectors()
{
    if (!m_viewport) return;
    std::map<int, int> role_by_feature;
    for (const CadFeature& mf : m_doc.features) {
        if (mf.type != CadFeatureType::Mate || !mf.enabled) continue;
        if (mf.mate_cs_a >= 0) role_by_feature[mf.mate_cs_a] = 1;
        if (mf.mate_cs_b >= 0) role_by_feature[mf.mate_cs_b] = 2;
    }
    const int cs_a = (m_active == Tool::Mate && m_mate_cs_a) ? m_mate_cs_a->GetSelection() : -1;
    const int cs_b = (m_active == Tool::Mate && m_mate_cs_b) ? m_mate_cs_b->GetSelection() : -1;

    std::vector<DesignSketchTool::MateConnectorGlyph> out;
    std::map<int, Vec3d> origin_by_feature, origin_by_ordinal;
    const auto frames = m_doc.resolve_datum_coordsys();
    size_t k = 0;
    for (size_t i = 0; i < m_doc.features.size() && k < frames.size(); ++i) {
        const CadFeature& f = m_doc.features[i];
        if (f.type != CadFeatureType::CoordSys || !f.enabled) continue;
        const auto& fr = frames[k];
        const int ordinal = int(k);
        ++k;
        if (!fr.error.empty()) continue;
        DesignSketchTool::MateConnectorGlyph g;
        g.origin = fr.origin;
        g.x      = fr.x;
        g.y      = fr.y;
        const auto it = role_by_feature.find(int(i));
        g.role   = (ordinal == cs_b) ? 2 : (ordinal == cs_a ? 1
                 : (it != role_by_feature.end() ? it->second : 0));
        g.roll_undefined = (f.coordsys_type == CoordSysType::FaceAndDirection
                            && f.coordsys_edge < 0);
        origin_by_feature[int(i)] = g.origin;
        origin_by_ordinal[ordinal] = g.origin;
        out.push_back(g);
    }

    std::vector<std::pair<Vec3d, Vec3d>> links;
    auto add_link = [&links](const std::map<int, Vec3d>& m, int a, int b) {
        const auto ia = m.find(a), ib = m.find(b);
        if (ia != m.end() && ib != m.end()) links.emplace_back(ia->second, ib->second);
    };
    for (const CadFeature& mf : m_doc.features)
        if (mf.type == CadFeatureType::Mate && mf.enabled)
            add_link(origin_by_feature, mf.mate_cs_a, mf.mate_cs_b);
    if (cs_a >= 0 && cs_b >= 0 && cs_a != cs_b)
        add_link(origin_by_ordinal, cs_a, cs_b);

    m_viewport->set_mate_connectors(std::move(out));
    m_viewport->set_mate_links(std::move(links));
}

void DesignPanel::update_datum_gizmo()
{
    if (!m_viewport) return;
    if (m_active != Tool::Plane) { m_viewport->clear_datum_gizmo(); update_reference_planes(); return; }

    CadFeature f = build_candidate(Tool::Plane);
    const bool editing = (m_edit_index >= 0 && m_edit_index < int(m_doc.features.size()) &&
                          m_doc.features[m_edit_index].type == CadFeatureType::Plane);
    CadFeature saved;
    int slot;
    if (editing) { saved = m_doc.features[m_edit_index]; m_doc.features[m_edit_index] = f; slot = m_edit_index; }
    else         { m_doc.features.push_back(f); slot = int(m_doc.features.size()) - 1; }

    auto datums = m_doc.resolve_datum_planes();
    int ord = -1;
    for (int i = 0; i <= slot; ++i)
        if (m_doc.features[i].type == CadFeatureType::Plane && m_doc.features[i].enabled) ++ord;
    const bool ok = (ord >= 0 && ord < int(datums.size()));
    SketchPlane frame; if (ok) frame = datums[ord].second;

    if (editing) m_doc.features[m_edit_index] = saved; else m_doc.features.pop_back();

    if (!ok) { m_viewport->clear_datum_gizmo(); update_reference_planes(); return; }

    const Vec3d anchor   = frame.origin - frame.normal * f.plane_offset;
    const bool  offset_on = (f.plane_type == PlaneType::Offset);
    m_viewport->set_datum_gizmo(frame, f.plane_u_size, f.plane_v_size,
                                anchor, frame.normal, f.plane_offset, offset_on);
    update_reference_planes();
}

void DesignPanel::update_helix_gizmo()
{
    if (!m_viewport) return;
    if (m_active != Tool::Helix) { m_viewport->clear_helix_gizmo(); return; }
    m_viewport->set_helix_gizmo(plane_from_choice(m_helix_plane->GetSelection()),
                                m_helix_radius ? m_helix_radius->GetValue() : 0.0,
                                m_helix_pitch  ? m_helix_pitch->GetValue()  : 1.0,
                                m_helix_height ? m_helix_height->GetValue() : 0.0,
                                m_helix_taper  ? m_helix_taper->GetValue()  : 0.0,
                                m_helix_left_handed && m_helix_left_handed->GetValue());
}

void DesignPanel::update_rib_gizmo()
{
    if (!m_viewport) return;
    if (m_active != Tool::Rib) { m_viewport->clear_rib_gizmo(); return; }
    const int ssel = m_rib_sketch ? m_rib_sketch->GetSelection() : wxNOT_FOUND;
    const int ref  = (ssel != wxNOT_FOUND)
                         ? int(reinterpret_cast<intptr_t>(m_rib_sketch->GetClientData(ssel))) : -1;
    if (ref < 0 || ref >= int(m_doc.features.size())) { m_viewport->clear_rib_gizmo(); return; }
    const CadFeature& sk = m_doc.features[ref];
    const int ei = m_rib_entity ? m_rib_entity->GetValue() : 0;
    if (ei < 0 || ei >= int(sk.entities.size())) { m_viewport->clear_rib_gizmo(); return; }
    const SketchEntity& e = sk.entities[ei];
    if (e.type != SketchEntity::Type::Line) { m_viewport->clear_rib_gizmo(); return; }
    m_viewport->set_rib_gizmo(sk.plane, e.p0, e.p1,
                              m_rib_thickness ? m_rib_thickness->GetValue() : 0.0);
}

void DesignPanel::update_reference_planes()
{
    if (!m_viewport) return;
    const Vec3d o = m_doc.modeling_origin;
    SketchPlane xy = SketchPlane::XY(); xy.origin += o;
    SketchPlane xz = SketchPlane::XZ(); xz.origin += o;
    SketchPlane yz = SketchPlane::YZ(); yz.origin += o;
    std::vector<SketchPlane> bp = { xy, xz, yz };
    std::vector<int>         bi = { 0, 1, 2 };
    std::vector<std::string> bl = { "XY", "XZ", "YZ" };

    if (m_active == Tool::Plane) {
        if (m_plane_type && (PlaneType)m_plane_type->GetSelection() == PlaneType::Offset) {
            auto datums = m_doc.resolve_datum_planes();
            for (int i = 0; i < int(datums.size()); ++i) {
                bp.push_back(datums[i].second); bi.push_back(3 + i); bl.push_back(datums[i].first);
            }
            m_viewport->set_base_pick(std::move(bp), std::move(bi), std::move(bl));
        } else {
            m_viewport->clear_base_pick();
        }
        return;
    }
    if (m_show_origin || m_choosing_sketch_plane)
        m_viewport->set_base_pick(std::move(bp), std::move(bi), std::move(bl));
    else
        m_viewport->clear_base_pick();
}

void DesignPanel::toggle_origin()
{
    m_show_origin = !m_show_origin;
    wxGetApp().app_config->set_bool("design_show_origin", m_show_origin);
    if (!m_show_origin) drop_plane_pick();
    update_reference_planes();
    refresh_pinned();
}

void DesignPanel::toggle_bed()
{
    m_show_bed = !m_show_bed;
    wxGetApp().app_config->set_bool("design_show_bed", m_show_bed);
    if (m_viewport) m_viewport->set_show_bed(m_show_bed);
    refresh_pinned();
}

void DesignPanel::refresh_pinned()
{
    std::vector<DesignRowList::Row> rows;
    auto add = [&rows](const char* icon, const wxString& label, bool shown, const wxString& tip) {
        DesignRowList::Action eye = eye_action(shown);
        eye.tip = tip;
        rows.push_back({ icon, label, {}, shown ? dp_item_text() : dp_item_dim(), { eye, {} } });
    };
    add("design_plane", _L("Origin"), m_show_origin, origin_toggle_text(m_show_origin));
    add("design_bed",   _L("Bed"),    m_show_bed,    bed_toggle_text(m_show_bed));
    m_pinned->set_rows(std::move(rows));
}

void DesignPanel::start_sketch()
{
    if (m_ui_mode != UiMode::Feature) return;
    if (m_active != Tool::None || m_text_dlg != nullptr || body_move_pending()) {
        set_status(StatusKind::Info, _L("Finish or cancel what is open first"));
        return;
    }
    wxString where;
    if (sketch_plane_target(where)) {
        start_sketch_on_target(true);
        return;
    }
    m_choosing_sketch_plane = true;
    update_reference_planes();
    update_action_bar();
    set_status(StatusKind::Info, sketch_plane_prompt());
}

void DesignPanel::start_sketch_on_target(bool offer_tools)
{
    if (!m_viewport) return;
    wxString on;
    const SketchPlane plane = sketch_plane_from_selection(on);
    m_viewport->begin_sketch(plane, DesignSketchTool::Mode::Select);
    drop_solid_pick();
    m_plane_picked = false;
    m_construction->SetValue(false);
    set_ui_mode(UiMode::Sketch);
    m_sketch_on = on;
    set_status(StatusKind::Info, wxString::Format(_L("Sketch plane: %s — pick a tool"), on));
    if (m_sketch_hint) {
        m_sketch_hint->SetLabel(wxString::Format(_L("Sketch plane: %s.\nPick a tool, or press Menu for the list."), on));
        m_sketch_hint->Refresh();
        m_cards->Layout();
    }
    if (offer_tools)
        CallAfter([this] { show_offer_menu(offer_anchor()); });
}

void DesignPanel::end_sketch_plane_choice()
{
    if (!m_choosing_sketch_plane) return;
    m_choosing_sketch_plane = false;
    update_reference_planes();
    update_action_bar();
}

bool DesignPanel::sketch_plane_pick_live() const
{
    return m_choosing_sketch_plane && m_ui_mode == UiMode::Feature && m_active == Tool::None
        && m_text_dlg == nullptr && !body_move_pending();
}

TriangleMesh DesignPanel::ghost_from_bodies(const std::vector<TriangleMesh>& per_body) const
{
    TriangleMesh out;
    for (size_t b = 0; b < per_body.size(); ++b) {
        TriangleMesh m = per_body[b];
        if (b < m_body_xform.size()) m.transform(m_body_xform[b]);
        out.merge(m);
    }
    return out;
}

bool DesignPanel::show_mate_ghost(int kind, int cs_a, int cs_b,
                                  double offset, double angle_deg, bool flip, std::string& err)
{
    CadFeature cand;
    cand.type        = CadFeatureType::Mate;
    cand.mate_kind   = kind;
    cand.mate_cs_a   = cs_a;
    cand.mate_cs_b   = cs_b;
    cand.mate_offset = offset;
    cand.mate_angle  = angle_deg;
    cand.mate_flip   = flip;

    TriangleMesh              mesh;
    std::vector<TriangleMesh> pbm;
    if (!m_doc.preview(cand, mesh, pbm, err)) {
        m_viewport->clear_preview();
        m_viewport->set_body_hidden(false);
        return false;
    }
    m_viewport->set_preview_mesh(ghost_from_bodies(pbm));
    m_viewport->set_body_hidden(true);
    return true;
}

void DesignPanel::refresh_preview()
{
    if (m_active == Tool::None) { m_viewport->clear_preview(); return; }

    if (m_active == Tool::Transform) { xf_live_preview(); return; }

    if (m_active == Tool::Sketch  || m_active == Tool::Plane   || m_active == Tool::Axis ||
        m_active == Tool::CoordSys || m_active == Tool::Helix   || m_active == Tool::Project) {
        m_viewport->clear_preview();
        wxString ready;
        switch (m_active) {
        case Tool::Plane:    ready = _L("Plane ready");     break;
        case Tool::Axis:     ready = _L("Axis ready");      break;
        case Tool::CoordSys: ready = _L("Coordinate system ready"); break;
        case Tool::Helix:    ready = _L("Helix ready");     break;
        case Tool::Project:  ready = _L("Project ready");   break;
        default:             ready = _L("Sketch ready");    break;
        }
        set_status(StatusKind::Ok, ready);
        m_candidate_ok = true;
        update_confirm_button();
        m_status->Refresh();
        update_datum_gizmo();
        update_helix_gizmo();
        return;
    }

    if (m_active == Tool::Mate) {
        const bool has_two = m_mate_cs_a && m_mate_cs_a->GetCount() >= 2;
        const int sel_a = has_two ? m_mate_cs_a->GetSelection() : wxNOT_FOUND;
        const int sel_b = has_two ? m_mate_cs_b->GetSelection() : wxNOT_FOUND;
        const bool same = has_two && sel_a != wxNOT_FOUND && sel_b != wxNOT_FOUND && sel_a == sel_b;
        bool ok = has_two && !same;
        if (!has_two) {
            m_viewport->clear_preview();
            m_viewport->set_body_hidden(false);
            set_status(StatusKind::Warning, _L("Mate needs two coordinate systems — create them first"));
        } else if (same) {
            m_viewport->clear_preview();
            m_viewport->set_body_hidden(false);
            set_status(StatusKind::Error, _L("Mate: A and B must be different coordinate systems"));
        } else {
            sync_body_xform();
            const int cs_a = int(reinterpret_cast<intptr_t>(m_mate_cs_a->GetClientData(sel_a)));
            const int cs_b = int(reinterpret_cast<intptr_t>(m_mate_cs_b->GetClientData(sel_b)));
            std::string err;
            ok = show_mate_ghost(m_mate_kind ? m_mate_kind->GetSelection() : 0, cs_a, cs_b,
                                 m_mate_offset ? m_mate_offset->GetValue() : 0.0,
                                 m_mate_angle  ? m_mate_angle->GetValue()  : 0.0,
                                 m_mate_flip   && m_mate_flip->GetValue(), err);
            if (ok) {
                set_status(StatusKind::Ok, _L("Mate ready"));
            } else {
                set_status(StatusKind::Error, wxString::Format(_L("Invalid: %s"), kernel_error_text(err)));
            }
        }
        m_candidate_ok = ok;
        update_confirm_button();
        m_status->Refresh();
        return;
    }

    sync_body_xform();

    CadFeature   cand = build_candidate(m_active);
    TriangleMesh mesh;
    std::string  err;
    bool         ok = false;

    auto ghost_from = [this](const std::vector<TriangleMesh>& pbm) { return ghost_from_bodies(pbm); };

    const bool editing_single = (m_edit_index >= 0);
    if (editing_single) {
        CadDocument tmp = m_doc;
        ok = tmp.replace_feature(m_edit_index, cand);
        if (ok) mesh = ghost_from(tmp.display_body_meshes); else err = tmp.error;
    } else {
        std::vector<TriangleMesh> pbm;
        ok = m_doc.preview(cand, mesh, pbm, err);
        if (ok) mesh = ghost_from(pbm);
    }

    if (ok) {
        m_viewport->set_preview_mesh(mesh);
        set_status(StatusKind::Ok, wxString::Format(_L("Preview — %zu triangles"), mesh.its.indices.size()));
    } else {
        m_viewport->clear_preview();
        set_status(StatusKind::Error, wxString::Format(_L("Invalid: %s"), kernel_error_text(err)));
    }
    m_candidate_ok = ok;
    update_confirm_button();
    m_viewport->set_body_hidden((m_active == Tool::Dressup || m_active == Tool::Draft ||
                                 m_active == Tool::Hole) && ok);
    m_status->Refresh();

    update_extrude_gizmo();
    update_fillet_gizmo();
    update_hole_gizmo();
    update_thread_gizmo();
    update_shell_gizmo();
    update_revolve_gizmo();
    update_draft_gizmo();
    update_cut_gizmo();
    update_pattern_gizmo();
    update_datum_gizmo();
    update_helix_gizmo();
    update_rib_gizmo();
    update_operand_highlight();
}

void DesignPanel::update_cards_frame()
{
    if (m_cards == nullptr || m_cards->GetSizer() == nullptr) return;
    wxSizer* root = m_form ? m_form->GetSizer() : nullptr;
    if (root == nullptr) return;
    bool any = false;
    for (const wxSizerItem* it : m_cards->GetSizer()->GetChildren())
        if (it->IsShown()) { any = true; break; }
    root->Show(m_cards, any, false);
}

void DesignPanel::apply_move_card()
{
    const int b = m_move_body;
    if (b < 0 || b >= int(m_body_xform.size()) || b >= int(m_doc.display_body_meshes.size())) return;

    const double ang = m_move_angle ? m_move_angle->GetValue() * M_PI / 180.0 : 0.0;
    const int    ax  = m_move_axis ? m_move_axis->GetSelection() : 2;
    const Vec3d  axis = (ax == 0) ? Vec3d::UnitX() : (ax == 1) ? Vec3d::UnitY() : Vec3d::UnitZ();
    const Vec3d  d(m_move_dx ? m_move_dx->GetValue() : 0.0,
                   m_move_dy ? m_move_dy->GetValue() : 0.0,
                   m_move_dz ? m_move_dz->GetValue() : 0.0);

    const Vec3d pivot = m_move_prev * m_doc.display_body_meshes[b].bounding_box().center();
    Transform3d x = Transform3d::Identity();
    x.translate(d + pivot);
    x.rotate(Eigen::AngleAxisd(ang, axis));
    x.translate(-pivot);

    m_body_xform[b] = x * m_move_prev;
    feed_bodies();
    if (m_viewport) m_viewport->request_repaint();
    set_status(StatusKind::Info, wxString::Format(_L("Body %d — moved (%.1f, %.1f, %.1f) mm, rotated %.1f°"),
                                        b + 1, d.x(), d.y(), d.z(),
                                        m_move_angle ? m_move_angle->GetValue() : 0.0));
}

void DesignPanel::show_move_card(bool show)
{
    if (m_box_move == nullptr || m_cards == nullptr || m_cards->GetSizer() == nullptr) return;
    m_cards->GetSizer()->Show(m_box_move, show, true);
    update_cards_frame();
    if (m_form) { m_form->Layout(); m_form->FitInside(); }
}

void DesignPanel::push_polygon_params()
{
    if (m_viewport == nullptr) return;
    m_viewport->set_sketch_polygon_sides(m_poly_sides);
    m_viewport->set_sketch_polygon_circumscribed(m_poly_circumscribed);
}

void DesignPanel::open_tool(Tool t)
{
    end_body_move(true);
    m_active = t;
    end_sketch_plane_choice();
    m_candidate_ok = true;
    if (m_viewport) { m_viewport->set_body_translucent(false); m_viewport->set_body_hidden(false);
                      m_viewport->set_xray_focus(t == Tool::CoordSys && m_cs_body ? m_cs_body->GetSelection() - 1 : -1); }
    wxSizer* s = m_cards->GetSizer();
    s->Show(m_box_sketch,  t == Tool::Sketch,  true);
    s->Show(m_box_extrude, t == Tool::Extrude, true);
    s->Show(m_box_dressup, t == Tool::Dressup, true);
    s->Show(m_box_hole,    t == Tool::Hole,    true);
    s->Show(m_box_thread,  t == Tool::Thread,  true);
    s->Show(m_box_shell,   t == Tool::Shell,   true);
    s->Show(m_box_revolve, t == Tool::Revolve, true);
    s->Show(m_box_sweep,   t == Tool::Sweep,   true);
    s->Show(m_box_pattern, t == Tool::Pattern, true);
    s->Show(m_box_plane,   t == Tool::Plane,   true);
    s->Show(m_box_axis,   t == Tool::Axis,   true);
    s->Show(m_box_coordsys, t == Tool::CoordSys, true);
    s->Show(m_box_loft,    t == Tool::Loft,    true);
    s->Show(m_box_boolean, t == Tool::Boolean, true);
    s->Show(m_box_cut,     t == Tool::Cut,     true);
    s->Show(m_box_draft,   t == Tool::Draft,   true);
    s->Show(m_box_surf_extrude, t == Tool::SurfaceExtrude, true);
    s->Show(m_box_surf_revolve, t == Tool::SurfaceRevolve, true);
    s->Show(m_box_surf_loft,    t == Tool::SurfaceLoft,    true);
    s->Show(m_box_surf_fill,    t == Tool::SurfaceFill,    true);
    s->Show(m_box_surf_offset,  t == Tool::SurfaceOffset,  true);
    s->Show(m_box_surf_thicken, t == Tool::ThickenSurface,  true);
    s->Show(m_box_transform,    t == Tool::Transform,      true);
    s->Show(m_box_mirror,       t == Tool::Mirror,         true);
    s->Show(m_box_thicken,      t == Tool::Thicken,        true);
    s->Show(m_box_rib,          t == Tool::Rib,            true);
    s->Show(m_box_project,      t == Tool::Project,        true);
    s->Show(m_box_delete_face,  t == Tool::DeleteFace,     true);
    s->Show(m_box_helix,        t == Tool::Helix,          true);
    s->Show(m_box_mate,         t == Tool::Mate,           true);
    s->Show(m_box_insert,  t == Tool::Insert,  true);
    s->Show(m_box_expr,   m_edit_index >= 0,  true);

    if (t == Tool::Mate) {
        int keep_a = -1, keep_b = -1;
        if (m_mate_cs_a->GetCount() > 0 && m_mate_cs_a->GetSelection() != wxNOT_FOUND) {
            const auto* cl = m_mate_cs_a->GetClientData(m_mate_cs_a->GetSelection());
            if (cl) keep_a = int(reinterpret_cast<intptr_t>(cl));
        }
        if (m_mate_cs_b->GetCount() > 0 && m_mate_cs_b->GetSelection() != wxNOT_FOUND) {
            const auto* cl = m_mate_cs_b->GetClientData(m_mate_cs_b->GetSelection());
            if (cl) keep_b = int(reinterpret_cast<intptr_t>(cl));
        }
        m_mate_cs_a->Clear();
        m_mate_cs_b->Clear();
        for (int i = 0; i < int(m_doc.features.size()); ++i) {
            if (m_doc.features[i].type != CadFeatureType::CoordSys) continue;
            const wxString nm = wxString::FromUTF8(m_doc.features[i].name);
            const int pos_a = combo_append_index(m_mate_cs_a, nm, i);
            const int pos_b = combo_append_index(m_mate_cs_b, nm, i);
            if (i == keep_a) m_mate_cs_a->SetSelection(pos_a);
            if (i == keep_b) m_mate_cs_b->SetSelection(pos_b);
        }
        if (keep_a < 0 && m_mate_cs_a->GetCount() > 0) m_mate_cs_a->SetSelection(0);
        if (keep_b < 0 && m_mate_cs_b->GetCount() > 0) m_mate_cs_b->SetSelection(0);
    }

    if (t == Tool::Revolve && m_revolve_sketch_ref >= 0
        && m_revolve_sketch_ref < int(m_doc.features.size()))
        m_revolve_sketch_label->SetLabel(_L("Sketch: ") +
            wxString::FromUTF8(m_doc.features[m_revolve_sketch_ref].name));

    if (t == Tool::Sweep) {
        if (m_sweep_profile_ref >= 0 && m_sweep_profile_ref < int(m_doc.features.size()))
            m_sweep_profile_label->SetLabel(_L("Profile: ") +
                wxString::FromUTF8(m_doc.features[m_sweep_profile_ref].name));
        m_sweep_path->Clear();
        int sel_idx = wxNOT_FOUND;
        for (int i = 0; i < int(m_doc.features.size()); ++i) {
            const CadFeature& sf = m_doc.features[i];
            if (sf.type != CadFeatureType::Sketch || i == m_sweep_profile_ref) continue;
            const int pos = combo_append_index(m_sweep_path, wxString::FromUTF8(sf.name), i);
            if (i == m_sweep_path_ref) sel_idx = pos;
        }
        if (sel_idx != wxNOT_FOUND) m_sweep_path->SetSelection(sel_idx);
        else if (m_sweep_path->GetCount() > 0) m_sweep_path->SetSelection(0);
    }

    if (t == Tool::Loft) {
        m_loft_list->Clear();
        m_loft_sketch_idx.clear();
        for (int i = 0; i < int(m_doc.features.size()); ++i) {
            const CadFeature& sf = m_doc.features[i];
            if (sf.type != CadFeatureType::Sketch) continue;
            const int row = m_loft_list->Append(wxString::FromUTF8(sf.name));
            m_loft_sketch_idx.push_back(i);
            if (std::find(m_loft_refs.begin(), m_loft_refs.end(), i) != m_loft_refs.end())
                m_loft_list->Check(row, true);
        }
    }

    if (t == Tool::SurfaceExtrude) {
        if (m_surf_extrude_sketch_ref >= 0 && m_surf_extrude_sketch_ref < int(m_doc.features.size()))
            m_surf_extrude_sketch_label->SetLabel(_L("Sketch: ") +
                wxString::FromUTF8(m_doc.features[m_surf_extrude_sketch_ref].name));
    }

    if (t == Tool::SurfaceRevolve) {
        if (m_surf_revolve_sketch_ref >= 0 && m_surf_revolve_sketch_ref < int(m_doc.features.size()))
            m_surf_revolve_sketch_label->SetLabel(_L("Sketch: ") +
                wxString::FromUTF8(m_doc.features[m_surf_revolve_sketch_ref].name));
    }

    if (t == Tool::SurfaceLoft) {
        m_surf_loft_list->Clear();
        m_surf_loft_sketch_idx.clear();
        for (int i = 0; i < int(m_doc.features.size()); ++i) {
            const CadFeature& sf = m_doc.features[i];
            if (sf.type != CadFeatureType::Sketch) continue;
            const int row = m_surf_loft_list->Append(wxString::FromUTF8(sf.name));
            m_surf_loft_sketch_idx.push_back(i);
            if (std::find(m_surf_loft_refs.begin(), m_surf_loft_refs.end(), i) != m_surf_loft_refs.end())
                m_surf_loft_list->Check(row, true);
        }
    }

    if (t == Tool::SurfaceFill) {
        if (m_surf_fill_sketch_ref >= 0 && m_surf_fill_sketch_ref < int(m_doc.features.size()))
            m_surf_fill_sketch_label->SetLabel(_L("Sketch: ") +
                wxString::FromUTF8(m_doc.features[m_surf_fill_sketch_ref].name));
    }

    if (t == Tool::Dressup && m_edit_index < 0)
        sync_dressup_target();

    if (t == Tool::Shell && m_edit_index < 0)
        m_shell_face_label->SetLabel(m_sel_solid_face >= 0
            ? wxString::Format(_L("Face %d"), m_sel_solid_face)
            : _L("(all faces — closed hollow)"));

    if (t == Tool::Draft && m_edit_index < 0)
        m_draft_face_label->SetLabel(m_sel_solid_face >= 0
            ? wxString::Format(_L("Face %d"), m_sel_solid_face)
            : _L("(pick a side face)"));

    if (t == Tool::Extrude) {
        if (m_extrude_face_src >= 0)
            m_extrude_sketch_label->SetLabel(
                wxString::Format(_L("Face %d (push/pull)"), m_extrude_face_src));
        else if (m_extrude_sketch_ref >= 0 && m_extrude_sketch_ref < int(m_doc.features.size()))
            m_extrude_sketch_label->SetLabel(_L("Sketch: ") +
                wxString::FromUTF8(m_doc.features[m_extrude_sketch_ref].name));
        m_extrude_auto_body = -1;
        if (m_edit_index < 0) {
            const bool on_face_import =
                m_extrude_sketch_ref >= 0 && m_extrude_sketch_ref < int(m_doc.features.size())
                && m_doc.features[m_extrude_sketch_ref].import_on_face;
            if (on_face_import) {
                m_mode->SetSelection(2);
                m_flip->SetValue(true);
            } else if (m_extrude_face_src >= 0) {
                m_mode->SetSelection(1);
            } else {
                m_extrude_auto_body = m_doc.body_touching_sketch(m_extrude_sketch_ref);
                m_mode->SetSelection(m_extrude_auto_body >= 0 ? 1 : 0);
            }
        }
    }

    const bool editing = (m_edit_index >= 0 && m_edit_index < int(m_doc.features.size()));
    auto title = [&](const wxString& base) -> wxString {
        return editing ? wxString::FromUTF8(m_doc.features[m_edit_index].name)
                       : base + wxString::Format(" %d", m_feature_counter + 1);
    };
    switch (t) {
    case Tool::Sketch:  m_hdr_sketch->SetLabel(title(_L("Sketch")));   break;
    case Tool::Extrude: m_hdr_extrude->SetLabel(title(_L_CONTEXT("Extrude", "Design"))); break;
    case Tool::Dressup: m_hdr_dressup->SetLabel(title(
                            m_dressup_type->GetSelection() == 0 ? _L("Fillet") : _L("Chamfer"))); break;
    case Tool::Hole:    m_hdr_hole->SetLabel(title(_L("Hole")));       break;
    case Tool::Thread:  m_hdr_thread->SetLabel(title(_L("Thread")));   break;
    case Tool::Shell:   m_hdr_shell->SetLabel(title(_L_CONTEXT("Shell", "Design")));     break;
    case Tool::Revolve: m_hdr_revolve->SetLabel(title(_L("Revolve"))); break;
    case Tool::Sweep:   m_hdr_sweep->SetLabel(title(_L("Sweep")));     break;
    case Tool::Pattern: m_hdr_pattern->SetLabel(title(_L_CONTEXT("Pattern", "Design"))); break;
    case Tool::Plane:   m_hdr_plane->SetLabel(title(_L("Plane")));     break;
    case Tool::Loft:    m_hdr_loft->SetLabel(title(_L("Loft")));       break;
    case Tool::Draft:   m_hdr_draft->SetLabel(title(_L("Draft")));     break;
    case Tool::Boolean: m_hdr_boolean->SetLabel(title(_L("Boolean"))); break;
    case Tool::Cut:     m_hdr_cut->SetLabel(title(_L_CONTEXT("Cut", "Design")));         break;
    case Tool::Axis:    m_hdr_axis->SetLabel(title(_L("Axis")));       break;
    case Tool::CoordSys: m_hdr_coordsys->SetLabel(title(_L("Coordinate system"))); break;
    case Tool::SurfaceExtrude:  m_hdr_surf_extrude->SetLabel(title(_L("Surface Extrude")));  break;
    case Tool::SurfaceRevolve:  m_hdr_surf_revolve->SetLabel(title(_L("Surface Revolve")));  break;
    case Tool::SurfaceLoft:     m_hdr_surf_loft->SetLabel(title(_L("Surface Loft")));      break;
    case Tool::SurfaceFill:     m_hdr_surf_fill->SetLabel(title(_L("Surface Fill")));      break;
    case Tool::SurfaceOffset:   m_hdr_surf_offset->SetLabel(title(_L("Surface Offset")));   break;
    case Tool::ThickenSurface:  m_hdr_surf_thicken->SetLabel(title(_L("Thicken Surface")));   break;
    case Tool::Transform:   m_hdr_transform->SetLabel(title(_L("Transform")));   break;
    case Tool::Mirror:      m_hdr_mirror->SetLabel(title(_L("Mirror")));         break;
    case Tool::Thicken:     m_hdr_thicken->SetLabel(title(_L("Thicken")));       break;
    case Tool::Rib:         m_hdr_rib->SetLabel(title(_L_CONTEXT("Rib", "Design")));               break;
    case Tool::Project:     m_hdr_project->SetLabel(title(_L_CONTEXT("Project", "Design")));       break;
    case Tool::DeleteFace:  m_hdr_delete_face->SetLabel(title(_L("Delete Face"))); break;
    case Tool::Helix:       m_hdr_helix->SetLabel(title(_L("Helix")));           break;
    case Tool::Mate:        m_hdr_mate->SetLabel(title(_L("Mate")));             break;
    case Tool::Insert:  break;
    case Tool::None:    break;
    }

    {
        const bool have = m_sel_solid_face >= 0;
        const wxString face_name = have ? wxString::Format(_L("Face %d"), m_sel_solid_face) : wxString();
        if (t == Tool::Thicken && m_thicken_face_label)
            m_thicken_face_label->SetLabel(have ? face_name : _L("(pick a solid face)"));
        if (t == Tool::Shell && m_shell_face_label)
            m_shell_face_label->SetLabel(have ? face_name : _L("(all faces — closed hollow)"));
        if (t == Tool::Draft && m_draft_face_label)
            m_draft_face_label->SetLabel(have ? face_name : _L("(pick a side face)"));
    }

    if (editing) {
        populate_expr_fields(t);
        const CadFeature& ef = m_doc.features[m_edit_index];
        if (ef.expr.empty()) {
            m_expr_status->SetLabel(_L("(no bindings)"));
            m_expr_status->SetForegroundColour(dp_sec_text());
        } else {
            wxString s;
            for (const auto& [field, e] : ef.expr) {
                if (!s.IsEmpty()) s += "; ";
                s += wxString::FromUTF8(field) + " = " + wxString::FromUTF8(e);
            }
            m_expr_status->SetLabel(s);
            m_expr_status->SetForegroundColour(dp_ctl_text());
        }
    }

    update_cards_frame(); m_form->Layout();
    m_form->FitInside();
    update_action_bar();
    refresh_preview();

    if (t == Tool::Transform && m_edit_index < 0)
        arm_transform_gizmo();
    if (t == Tool::Boolean)
        m_bool_next_slot = 0;
    request_feature_highlight();
}

void DesignPanel::close_tool()
{
    m_active = Tool::None;
    set_active_tool_btn(nullptr);
    xf_clear_preview();
    if (m_xf_gizmo_body >= 0) {
        sync_body_xform();
        if (m_xf_gizmo_body < int(m_body_xform.size()))
            m_body_xform[m_xf_gizmo_body] = m_xf_gizmo_base;
        if (m_viewport) m_viewport->clear_move_gizmo();
        feed_bodies();
        m_xf_gizmo_body = -1;
    }
    if (m_viewport) { m_viewport->set_body_translucent(false); m_viewport->set_body_hidden(false); m_viewport->set_xray_focus(-1); }
    wxSizer* s = m_cards->GetSizer();
    s->Show(m_box_sketch,  false, true);
    s->Show(m_box_extrude, false, true);
    s->Show(m_box_dressup, false, true);
    s->Show(m_box_hole,    false, true);
    s->Show(m_box_thread,  false, true);
    s->Show(m_box_shell,   false, true);
    s->Show(m_box_revolve, false, true);
    s->Show(m_box_sweep,   false, true);
    s->Show(m_box_pattern, false, true);
    s->Show(m_box_plane,   false, true);
    s->Show(m_box_axis,   false, true);
    s->Show(m_box_coordsys, false, true);
    s->Show(m_box_loft,    false, true);
    s->Show(m_box_boolean, false, true);
    s->Show(m_box_cut,     false, true);
    s->Show(m_box_draft,   false, true);
    s->Show(m_box_surf_extrude, false, true);
    s->Show(m_box_surf_revolve, false, true);
    s->Show(m_box_surf_loft,    false, true);
    s->Show(m_box_surf_fill,    false, true);
    s->Show(m_box_surf_offset,  false, true);
    s->Show(m_box_surf_thicken, false, true);
    s->Show(m_box_transform,    false, true);
    s->Show(m_box_mirror,       false, true);
    s->Show(m_box_thicken,      false, true);
    s->Show(m_box_rib,          false, true);
    s->Show(m_box_project,      false, true);
    s->Show(m_box_delete_face,  false, true);
    s->Show(m_box_helix,        false, true);
    s->Show(m_box_mate,         false, true);
    s->Show(m_box_insert,  false, true);
    s->Show(m_box_expr,    false, true);
    m_viewport->clear_preview();
    m_viewport->clear_extrude_gizmo();
    m_viewport->clear_fillet_gizmo();
    m_viewport->clear_hole_gizmo();
    m_viewport->clear_thread_gizmo();
    m_viewport->clear_shell_gizmo();
    m_viewport->clear_revolve_gizmo();
    m_viewport->clear_draft_gizmo();
    m_viewport->clear_cut_gizmo();
    m_viewport->clear_pattern_gizmo();
    m_viewport->clear_datum_gizmo();
    m_viewport->set_operand_bodies(-1, -1);
    m_viewport->set_highlight_sketches({});
    update_reference_planes();
    update_cards_frame(); m_form->Layout();
    m_form->FitInside();
    update_action_bar();
    request_feature_highlight();
}

void DesignPanel::confirm_tool()
{
    m_doc.checkpoint();
    const bool editing_single = (m_edit_index >= 0);

    if (editing_single) {
        CadFeature cand = build_candidate(m_active);
        bool ok = m_doc.replace_feature(m_edit_index, cand);
        reset_edit_state();
        close_tool();
        after_tree_edit(ok);
        return;
    }

    switch (m_active) {
    case Tool::Sketch:  on_add_sketch();  break;
    case Tool::Extrude: on_add_extrude(); break;
    case Tool::Dressup: on_add_dressup(); break;
    case Tool::Hole:    on_add_hole();    break;
    case Tool::Thread:  on_add_thread();  break;
    case Tool::Shell:   on_add_shell();   break;
    case Tool::Revolve: on_add_revolve(); break;
    case Tool::Sweep:   on_add_sweep();   break;
    case Tool::Pattern: on_add_pattern(); break;
    case Tool::Plane:   if (!on_add_plane()) return;   break;
    case Tool::Loft:    on_add_loft();    break;
    case Tool::Draft:   on_add_draft();   break;
    case Tool::Boolean: on_add_boolean(); break;
    case Tool::Cut:     on_add_cut();     break;
    case Tool::Axis:    on_add_axis();    break;
    case Tool::CoordSys: on_add_coordsys(); break;
    case Tool::Mate:   on_add_mate();   break;
    case Tool::SurfaceExtrude:  on_add_surface_extrude();  break;
    case Tool::SurfaceRevolve:  on_add_surface_revolve();  break;
    case Tool::SurfaceLoft:     on_add_surface_loft();     break;
    case Tool::SurfaceFill:     on_add_surface_fill();     break;
    case Tool::SurfaceOffset:   on_add_surface_offset();   break;
    case Tool::ThickenSurface:  on_add_thicken_surface();  break;
    case Tool::Transform:    on_add_transform();    break;
    case Tool::Mirror:       on_add_mirror();       break;
    case Tool::Thicken:      on_add_thicken();      break;
    case Tool::Rib:          on_add_rib();          break;
    case Tool::Project:      on_add_project();      break;
    case Tool::DeleteFace:   on_add_delete_face();  break;
    case Tool::Helix:        on_add_helix();        break;
    case Tool::Insert:  return;
    case Tool::None:    return;
    }
    close_tool();
}

void DesignPanel::cancel_tool()
{
    reset_edit_state();
    close_tool();
    set_status(StatusKind::Info, wxString());
}

void DesignPanel::tool_confirm()
{
    if (m_value_cont) { confirm_value(); return; }
    if (body_move_pending()) {
        end_body_move(true);
        set_status_ok();
        return;
    }
    if (m_active == Tool::Insert) { finalize_insert(); return; }
    if (m_active != Tool::None)   { confirm_tool();   return; }
    if (m_ui_mode == UiMode::Sketch) {
        if (m_viewport && m_viewport->is_sketching()) {
            m_viewport->sketch_confirm_pending();
            m_viewport->finish_sketch();
        }
        set_ui_mode(UiMode::Feature);
        return;
    }
    if (m_ui_mode == UiMode::Constrain) {
        cancel_value();
        if (m_viewport) m_viewport->end_constrain();
        m_constrain_feat = -1;
        set_ui_mode(UiMode::Feature);
        set_status(StatusKind::Info, wxString());
    }
}

void DesignPanel::tool_cancel()
{
    if (m_value_cont) { cancel_value(); return; }
    if (body_move_pending()) {
        end_body_move(false);
        set_status(StatusKind::Info, _L("Move canceled"));
        return;
    }
    if (m_active == Tool::Insert) { cancel_insert(); return; }
    if (m_active != Tool::None)   { cancel_tool();   return; }
    if (m_choosing_sketch_plane) {
        end_sketch_plane_choice();
        set_status(StatusKind::Info, wxString());
        return;
    }
    if (m_ui_mode == UiMode::Sketch) {
        if (m_viewport && m_viewport->live_sketch_has_work()) {
            RichMessageDialog dlg(this,
                                _L("Discard this sketch and everything drawn in it?"),
                                _L("Discard sketch"),
                                wxYES_NO | wxNO_DEFAULT | wxICON_EXCLAMATION);
            dlg.SetYesNoLabels(_L("Discard"), _L("Keep drawing"));
            if (dlg.ShowModal() != wxID_YES) return;
        }
        if (m_viewport) m_viewport->cancel_sketch();
        m_edit_index = -1;
        set_ui_mode(UiMode::Feature);
        sync_sketch_display();
        refresh_tree();
        set_status(StatusKind::Info, wxString());
        return;
    }
    if (m_ui_mode == UiMode::Constrain) {
        cancel_value();
        if (m_viewport) m_viewport->end_constrain();
        m_constrain_feat = -1;
        set_ui_mode(UiMode::Feature);
        set_status(StatusKind::Info, wxString());
    }
}

bool DesignPanel::confirm_enabled() const
{
    if (m_value_cont) return true;
    if (m_active == Tool::None)
        return body_move_pending() || m_ui_mode == UiMode::Sketch || m_ui_mode == UiMode::Constrain;
    if (m_active == Tool::Insert) return true;
    return m_candidate_ok;
}

void DesignPanel::update_confirm_button()
{
    const bool ok = confirm_enabled();
    for (::Button* b : m_confirm_btns)
        if (b != nullptr) b->Enable(ok);
}

CadLevel DesignPanel::escape_level() const
{
    CadInteractionState st;
    st.value_field_open = (m_value_cont != nullptr)
                          || (m_viewport && m_viewport->inline_busy());
    st.gesture_active   = body_move_pending()
                          || (m_viewport && m_viewport->drawing_in_progress());
    st.tool_armed       = (m_active != Tool::None)
                          || m_choosing_sketch_plane
                          || m_ui_mode == UiMode::Constrain
                          || (m_ui_mode == UiMode::Sketch && m_viewport && !m_viewport->sketch_is_selecting());
    st.has_selection    = m_viewport && m_viewport->has_any_selection();
    return cad_escape_level(st);
}

void DesignPanel::escape()
{
    switch (escape_level()) {
    case CadLevel::Transient:
        if (m_value_cont)                      { cancel_value(); return; }
        if (m_viewport)                        { m_viewport->inline_cancel(); return; }
        return;

    case CadLevel::Gesture:
        if (m_viewport && m_viewport->drawing_in_progress()) {
            m_viewport->sketch_abort_gesture();
            set_status(StatusKind::Info, wxString());
            return;
        }
        tool_cancel();
        return;

    case CadLevel::Tool:
        if (m_active != Tool::None || m_choosing_sketch_plane || m_ui_mode == UiMode::Constrain) {
            tool_cancel();
            return;
        }
        if (m_viewport && m_viewport->sketch_disarm_tool()) {
            set_status(StatusKind::Info, _L("Select"));
        }
        return;

    case CadLevel::Idle: {
        const bool row = deselect_rows();
        const bool plane = drop_plane_pick();
        if ((m_viewport && m_viewport->clear_any_selection()) || row || plane) {
            set_status(StatusKind::Info, wxString());
            return;
        }
        if (m_ui_mode == UiMode::Sketch && m_viewport && !m_viewport->live_sketch_has_work()) {
            m_viewport->request_sketch_exit();
            return;
        }
        if (m_ui_mode == UiMode::Sketch) {
            set_status(StatusKind::Info, _L("Sketch kept — Confirm to commit it, Cancel to discard"));
        }
        return;
    }
    }
}

bool DesignPanel::menu_can_undo_redo(bool redo) const
{
    if (m_ui_mode == UiMode::Sketch && m_viewport && m_viewport->is_sketching())
        return redo ? m_viewport->can_redo_sketch_entity() : m_viewport->can_undo_sketch_entity();
    if (m_ui_mode != UiMode::Feature || m_active != Tool::None || body_move_pending()) return false;
    return redo ? m_doc.can_redo() : m_doc.can_undo();
}

void DesignPanel::update_undo_redo_buttons()
{
    if (MainFrame* frame = wxGetApp().mainframe; frame != nullptr && IsShownOnScreen())
        frame->set_undo_redo_enabled(menu_can_undo_redo(false), menu_can_undo_redo(true));
}

void DesignPanel::update_action_bar()
{
    update_undo_redo_buttons();
    update_confirm_button();
    if (m_tb_action == nullptr || m_toolbar == nullptr) return;
    wxSizer* s = m_toolbar->GetSizer();
    if (s == nullptr) return;
    const bool active = (m_active != Tool::None)
                     || m_choosing_sketch_plane
                     || m_ui_mode == UiMode::Sketch
                     || m_ui_mode == UiMode::Constrain
                     || body_move_pending();
    s->Show(m_tb_action, active, true);
    if (!active && m_viewport != nullptr) {
        wxWindow* f = wxWindow::FindFocus();
        bool in_bar = (f == nullptr);
        for (wxWindow* w = f; w != nullptr && !in_bar; w = w->GetParent())
            if (w == m_toolbar) in_bar = true;
        if (in_bar) m_viewport->SetFocus();
    }
    m_toolbar->Layout();
    m_toolbar->FitInside();
}

void DesignPanel::do_undo_redo(bool redo)
{
    if (m_ui_mode == UiMode::Sketch && m_viewport && m_viewport->is_sketching()) {
        const bool ok = redo ? m_viewport->redo_last_sketch_entity() : m_viewport->undo_last_sketch_entity();
        if (!ok) set_status(redo ? _L("Nothing to redo") : _L("Nothing to undo"));
        update_undo_redo_buttons();
        return;
    }
    if (m_ui_mode != UiMode::Feature || m_active != Tool::None || body_move_pending()) {
        set_status(StatusKind::Info, _L("Finish or cancel the current tool first (Esc)"));
        return;
    }
    const bool ok = redo ? m_doc.redo() : m_doc.undo();
    if (!ok) {
        set_status(StatusKind::Info, redo ? _L("Nothing to redo") : _L("Nothing to undo"));
        return;
    }
    drop_selection();
    reset_edit_state();
    after_tree_edit(true);
    const size_t depth = redo ? m_doc.redo_depth() : m_doc.undo_depth();
    set_status(StatusKind::Info, wxString::Format(redo ? _L_PLURAL("Redo  (%zu more step)", "Redo  (%zu more steps)", unsigned(depth))
                                                       : _L_PLURAL("Undo  (%zu more step)", "Undo  (%zu more steps)", unsigned(depth)),
                                                  depth));
}

void DesignPanel::refresh_variables()
{
    if (!m_var_list) return;
    m_var_list->DeleteAllItems();
    int row = 0;
    for (const auto& [name, expr] : m_doc.variables) {
        m_var_list->InsertItem(row, wxString::FromUTF8(name));
        m_var_list->SetItem(row, 1, wxString::FromUTF8(expr));
        ++row;
    }
}

class DesignVariableDialog : public DPIDialog
{
public:
    DesignVariableDialog(wxWindow* parent, const wxString& title, const wxString& name,
                         const wxString& expr, bool name_editable)
        : DPIDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    {
        SetBackgroundColour(*wxWHITE);
        SetFont(Label::Body_14);
        auto* grid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(12));
        grid->AddGrowableCol(1);
        auto field = [this, grid](const wxString& label, const wxString& value) {
            grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
            auto* in = new ::TextInput(this, value, "", "", wxDefaultPosition, wxSize(FromDIP(240), -1),
                                       wxTE_PROCESS_ENTER);
            in->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { EndModal(wxID_OK); });
            grid->Add(in, 1, wxEXPAND);
            return in;
        };
        m_name = field(_L("Name"), name);
        m_expr = field(_L("Expression"), expr);
        m_name->Enable(name_editable);

        m_buttons = new DialogButtons(this, {"OK", "Cancel"});
        m_buttons->GetOK()->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_OK); });
        m_buttons->GetCANCEL()->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(grid, 1, wxEXPAND | wxALL, FromDIP(16));
        sizer->Add(m_buttons, 0, wxEXPAND);
        SetSizerAndFit(sizer);
        CenterOnParent();
        (name_editable ? m_name : m_expr)->GetTextCtrl()->SetFocus();
        wxGetApp().UpdateDlgDarkUI(this);
    }

    wxString name() const { return trimmed(m_name); }
    wxString expression() const { return trimmed(m_expr); }

protected:
    void on_dpi_changed(const wxRect&) override
    {
        m_name->Rescale();
        m_expr->Rescale();
        GetSizer()->SetSizeHints(this);
        Refresh();
    }

private:
    static wxString trimmed(const ::TextInput* in)
    {
        wxString v = in->GetTextCtrl()->GetValue();
        return v.Trim(true).Trim(false);
    }
    ::TextInput*   m_name{nullptr};
    ::TextInput*   m_expr{nullptr};
    DialogButtons* m_buttons{nullptr};
};

void DesignPanel::on_add_variable()
{
    DesignVariableDialog dlg(this, _L("Add Variable"), "", "0", true);
    if (dlg.ShowModal() != wxID_OK) return;
    const wxString name = dlg.name();
    const wxString expr = dlg.expression();
    if (name.IsEmpty() || expr.IsEmpty()) return;
    if (name.Contains(' ')) {
        set_status(StatusKind::Error, _L("Variable name must not contain spaces"));
        return;
    }

    const std::string name_str = name.ToUTF8().data();
    const std::string expr_str = expr.ToUTF8().data();

    m_doc.checkpoint();
    m_doc.variables[name_str] = expr_str;
    bool ok = m_doc.recompute();
    if (!ok) { const std::string why = m_doc.error; m_doc.undo(); m_doc.error = why; }
    after_tree_edit(ok);
    refresh_variables();
}

void DesignPanel::on_edit_variable()
{
    if (!m_var_list) return;
    const long sel = m_var_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0) {
        set_status(_L("Select a variable first"));
        m_status->Refresh();
        return;
    }
    const std::string name_str = m_var_list->GetItemText(sel, 0).ToUTF8().data();
    const std::string old_expr  = m_var_list->GetItemText(sel, 1).ToUTF8().data();
    DesignVariableDialog dlg(this, _L("Edit Variable"), m_var_list->GetItemText(sel, 0),
                             wxString::FromUTF8(old_expr), false);
    if (dlg.ShowModal() != wxID_OK) return;
    const wxString expr = dlg.expression();
    if (expr.IsEmpty()) return;

    const std::string expr_str = expr.ToUTF8().data();
    m_doc.checkpoint();
    m_doc.variables[name_str] = expr_str;
    bool ok = m_doc.recompute();
    if (!ok) { const std::string why = m_doc.error; m_doc.undo(); m_doc.error = why; }
    after_tree_edit(ok);
    refresh_variables();
}

void DesignPanel::on_remove_variable()
{
    if (!m_var_list) return;
    const long sel = m_var_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0) {
        set_status(_L("Select a variable first"));
        m_status->Refresh();
        return;
    }
    const std::string name_str = m_var_list->GetItemText(sel, 0).ToUTF8().data();

    m_doc.checkpoint();
    m_doc.variables.erase(name_str);
    bool ok = m_doc.recompute();
    if (!ok) {
        const std::string why = m_doc.error;
        m_doc.undo();
        m_doc.error = wxString::Format(
            _L("Variable '%s' could not be removed (likely still referenced by a feature "
               "expression): %s"),
            m_var_list->GetItemText(sel, 0), wxString::FromUTF8(why)).ToUTF8().data();
    }
    after_tree_edit(ok);
    refresh_variables();
}

std::vector<std::string> DesignPanel::fields_for_tool(Tool t)
{
    using T = DesignPanel::Tool;
    switch (t) {
    case T::Sketch:           return {"width", "height", "radius"};
    case T::Extrude:          return {"distance", "distance2", "taper_deg"};
    case T::Dressup:          return {"dressup_size"};
    case T::Hole:             return {"hole_diameter", "hole_depth", "hole_x", "hole_y"};
    case T::Thread:           return {"thread_diameter", "thread_pitch", "thread_height", "thread_depth", "thread_x", "thread_y"};
    case T::Shell:            return {"shell_thickness"};
    case T::Revolve:          return {"revolve_angle"};
    case T::Sweep:            return {};
    case T::Pattern:          return {"pattern_count", "pattern_spacing", "pattern_angle"};
    case T::Plane:            return {"plane_offset", "plane_angle_tilt"};
    case T::Loft:             return {};
    case T::Draft:            return {"draft_angle"};
    case T::Boolean:          return {};
    case T::Cut:              return {};
    case T::SurfaceExtrude:   return {"distance"};
    case T::SurfaceRevolve:   return {"revolve_angle"};
    case T::SurfaceLoft:      return {};
    case T::SurfaceFill:      return {};
    case T::SurfaceOffset:    return {"plane_offset"};
    case T::ThickenSurface:   return {"thicken_thickness"};
    case T::Transform:        return {};
    case T::Mirror:           return {};
    case T::Thicken:          return {"thicken_thickness"};
    case T::Rib:              return {"rib_thickness", "rib_depth"};
    case T::Project:          return {};
    case T::DeleteFace:       return {};
    case T::Helix:            return {"helix_radius", "helix_pitch", "helix_height", "helix_taper_deg"};
    case T::Axis:             return {};
    case T::CoordSys:         return {};
    case T::Mate:             return {};
    case T::Insert:           return {};
    case T::None:             return {};
    }
    return {};
}

void DesignPanel::populate_expr_fields(Tool t)
{
    if (!m_expr_field) return;
    m_expr_field->Clear();
    for (const std::string& f : fields_for_tool(t))
        m_expr_field->Append(wxString::FromUTF8(f));
    if (m_expr_field->GetCount() > 0)
        m_expr_field->SetSelection(0);
}

void DesignPanel::on_set_expr()
{
    if (m_edit_index < 0 || m_edit_index >= int(m_doc.features.size())) return;
    if (!m_expr_field || !m_expr_text) return;

    const wxString fwx = m_expr_field->GetValue();
    const wxString ewx = m_expr_text->GetValue();
    if (fwx.IsEmpty()) return;

    const std::string field = fwx.ToUTF8().data();
    const std::string expr  = ewx.ToUTF8().data();
    if (!CadDocument::is_bindable_field(field)) {
        set_status(StatusKind::Error, wxString::Format(_L("\"%s\" is not a value an expression can drive"), fwx));
        return;
    }

    m_doc.checkpoint();
    m_doc.features[m_edit_index].expr[field] = expr;
    bool ok = m_doc.recompute();
    if (!ok) { const std::string why = m_doc.error; m_doc.undo(); m_doc.error = why; }
    after_tree_edit(ok);
    if (ok) m_expr_text->Clear();

    if (m_edit_index >= 0 && m_edit_index < int(m_doc.features.size())) {
        const CadFeature& ef = m_doc.features[m_edit_index];
        if (ef.expr.empty()) {
            m_expr_status->SetLabel(_L("(no bindings)"));
            m_expr_status->SetForegroundColour(dp_sec_text());
        } else {
            wxString s;
            for (const auto& [f, e] : ef.expr) {
                if (!s.IsEmpty()) s += "; ";
                s += wxString::FromUTF8(f) + " = " + wxString::FromUTF8(e);
            }
            m_expr_status->SetLabel(s);
            m_expr_status->SetForegroundColour(dp_ctl_text());
        }
    }
}

void DesignPanel::on_clear_expr()
{
    if (m_edit_index < 0 || m_edit_index >= int(m_doc.features.size())) return;
    if (!m_expr_field) return;

    const wxString fwx = m_expr_field->GetValue();
    if (fwx.IsEmpty()) return;

    const std::string field = fwx.ToUTF8().data();
    auto& feat_expr = m_doc.features[m_edit_index].expr;
    if (feat_expr.find(field) == feat_expr.end()) {
        set_status(StatusKind::Error, _L("No binding for that field"));
        return;
    }

    m_doc.checkpoint();
    feat_expr.erase(field);
    bool ok = m_doc.recompute();
    if (!ok) { const std::string why = m_doc.error; m_doc.undo(); m_doc.error = why; }
    after_tree_edit(ok);

    if (m_edit_index >= 0 && m_edit_index < int(m_doc.features.size())) {
        const CadFeature& ef = m_doc.features[m_edit_index];
        if (ef.expr.empty()) {
            m_expr_status->SetLabel(_L("(no bindings)"));
            m_expr_status->SetForegroundColour(dp_sec_text());
        } else {
            wxString s;
            for (const auto& [f, e] : ef.expr) {
                if (!s.IsEmpty()) s += "; ";
                s += wxString::FromUTF8(f) + " = " + wxString::FromUTF8(e);
            }
            m_expr_status->SetLabel(s);
            m_expr_status->SetForegroundColour(dp_ctl_text());
        }
    }
}

}}
