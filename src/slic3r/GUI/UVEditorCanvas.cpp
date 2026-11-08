#include "UVEditorCanvas.hpp"

#include <Eigen/Core>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include "libslic3r/Point.hpp"
#include <cstddef>
#include <limits>
#include <string>
#include <math.h>
#include "slic3r/GUI/GLModel.hpp"
#include <unordered_map>

#include <glad/gl.h>

#include <vector>
#include <utility>
#include <wx/colour.h>
#include <wx/dcbuffer.h>
#include <wx/gdicmn.h>
#include <wx/glcanvas.h>
#include <wx/event.h>
#include <wx/dcclient.h>
#include <wx/image.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbmp.h>
#include <wx/window.h>
#include <wx/utils.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>

#include "3DScene.hpp"
#include "BitmapCache.hpp"
#include "GLShader.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "OpenGLManager.hpp"
#include "Plater.hpp"
#include "wxExtensions.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/SpinInput.hpp"
#include "libslic3r/AppConfig.hpp"

namespace Slic3r::GUI {

namespace {
const ColorRGBA UV_COLOR_BG            = { 0.16f, 0.16f, 0.16f, 1.f };
const ColorRGBA UV_COLOR_OUTSIDE       = { 0.16f, 0.16f, 0.16f, 0.72f };
const ColorRGBA UV_COLOR_GRID          = { 1.f, 1.f, 1.f, 0.08f };
const ColorRGBA UV_COLOR_HALO          = { 0.06f, 0.06f, 0.07f, 1.f };
const ColorRGBA UV_COLOR_TILE_OUTLINE  = { 0.70f, 0.70f, 0.72f, 1.f };
const ColorRGBA UV_COLOR_WIRE          = { 1.f, 1.f, 1.f, 0.30f };
const ColorRGBA UV_COLOR_WIRE_SHADOW   = { 0.f, 0.f, 0.f, 0.35f };
const ColorRGBA UV_COLOR_BOUNDARY      = { 0.93f, 0.93f, 0.94f, 1.f };
const ColorRGBA UV_COLOR_HOVER         = { 0.72f, 0.98f, 0.93f, 1.f };
const ColorRGBA UV_COLOR_HOVER_FILL    = { 1.f, 1.f, 1.f, 0.12f };
const ColorRGBA UV_COLOR_FILL          = { 1.f, 1.f, 1.f, 0.f };
const ColorRGBA UV_COLOR_SEL_FILL      = { 0.15f, 0.85f, 0.75f, 0.24f };
const ColorRGBA UV_COLOR_SEL_WIRE      = { 0.55f, 1.f, 0.92f, 0.55f };
const ColorRGBA UV_COLOR_SEL_BOUNDARY  = { 0.16f, 0.90f, 0.78f, 1.f };
const ColorRGBA UV_COLOR_DIAL          = { 1.f, 0.85f, 0.2f, 0.9f };

constexpr float SNAP_PIXELS = 28.f;
wxSize gl_drawable_size(const wxWindow *win, const wxSize &logical_size)
{
#if defined(__APPLE__) || defined(__WXGTK3__)
    const double scale = (win != nullptr) ? win->GetContentScaleFactor() : 1.0;
    if (scale > 0.0)
        return wxSize(std::max(1, int(std::lround(logical_size.GetWidth() * scale))),
                      std::max(1, int(std::lround(logical_size.GetHeight() * scale))));
#else
    (void) win;
#endif
    return wxSize(std::max(1, logical_size.GetWidth()), std::max(1, logical_size.GetHeight()));
}

std::vector<int> gl_attrib_list()
{
    int antialiasing_samples = 4;
    if (const AppConfig *app_config = wxGetApp().app_config; app_config != nullptr) {
        const std::string value = app_config->get(SETTING_OPENGL_AA_SAMPLES);
        if (value == "0" || value == "2" || value == "4" || value == "8" || value == "16")
            antialiasing_samples = ::atoi(value.c_str());
    }
    if (!OpenGLManager::can_multisample())
        antialiasing_samples = 0;

    return {
        WX_GL_RGBA,
        WX_GL_DOUBLEBUFFER,
        WX_GL_MIN_RED,        8,
        WX_GL_MIN_GREEN,      8,
        WX_GL_MIN_BLUE,       8,
        WX_GL_MIN_ALPHA,      8,
        WX_GL_DEPTH_SIZE,     24,
        WX_GL_STENCIL_SIZE,   8,
        WX_GL_SAMPLE_BUFFERS, antialiasing_samples > 0 ? GL_TRUE : GL_FALSE,
        WX_GL_SAMPLES,        antialiasing_samples,
        0
    };
}

void set_line_width(float width)
{
    ::glLineWidth(width);
    ::glGetError();
}

uint64_t undirected_edge_key(int a, int b)
{
    if (a > b)
        std::swap(a, b);
    return (uint64_t(uint32_t(a)) << 32) | uint32_t(b);
}

bool point_in_triangle(const Vec2f &p, const Vec2f &a, const Vec2f &b, const Vec2f &c)
{
    const auto cross = [](const Vec2f &u, const Vec2f &v) { return u.x() * v.y() - u.y() * v.x(); };
    const float d0 = cross(b - a, p - a);
    const float d1 = cross(c - b, p - b);
    const float d2 = cross(a - c, p - c);
    const bool  has_neg = (d0 < 0.f) || (d1 < 0.f) || (d2 < 0.f);
    const bool  has_pos = (d0 > 0.f) || (d1 > 0.f) || (d2 > 0.f);
    return !(has_neg && has_pos);
}

float angle_delta(float from, float to)
{
    float d = to - from;
    while (d > float(M_PI))
        d -= 2.f * float(M_PI);
    while (d < -float(M_PI))
        d += 2.f * float(M_PI);
    return d;
}

Transform3d to_transform3d(const UVEditorCanvas::IslandTransform &m)
{
    Transform3d t  = Transform3d::Identity();
    t(0, 0) = m(0, 0); t(0, 1) = m(0, 1); t(0, 3) = m(0, 2);
    t(1, 0) = m(1, 0); t(1, 1) = m(1, 1); t(1, 3) = m(1, 2);
    return t;
}
}

UVEditorCanvas::UVEditorCanvas(wxWindow *parent)
    : wxGLCanvas(parent, wxID_ANY, gl_attrib_list().data(), wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    m_context = wxGetApp().init_glcontext(*this);

    Bind(wxEVT_PAINT, &UVEditorCanvas::on_paint, this);
    Bind(wxEVT_SIZE, &UVEditorCanvas::on_size, this);
    Bind(wxEVT_LEFT_DOWN, &UVEditorCanvas::on_mouse, this);
    Bind(wxEVT_LEFT_UP, &UVEditorCanvas::on_mouse, this);
    Bind(wxEVT_RIGHT_DOWN, &UVEditorCanvas::on_mouse, this);
    Bind(wxEVT_RIGHT_UP, &UVEditorCanvas::on_mouse, this);
    Bind(wxEVT_MIDDLE_DOWN, &UVEditorCanvas::on_mouse, this);
    Bind(wxEVT_MIDDLE_UP, &UVEditorCanvas::on_mouse, this);
    Bind(wxEVT_MOTION, &UVEditorCanvas::on_mouse, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &UVEditorCanvas::on_capture_lost, this);
    Bind(wxEVT_MOUSEWHEEL, &UVEditorCanvas::on_mouse, this);
    Bind(wxEVT_LEAVE_WINDOW, &UVEditorCanvas::on_leave, this);
    Bind(wxEVT_KEY_DOWN, &UVEditorCanvas::on_key, this);
    Bind(wxEVT_ERASE_BACKGROUND, &UVEditorCanvas::on_erase_background, this);
}

void UVEditorCanvas::set_islands(Islands islands)
{
    m_needs_fit |= m_islands.indices.empty();
    const bool same_structure = m_islands.island_count == islands.island_count &&
                                m_islands.uvs.size() == islands.uvs.size();
    m_islands    = std::move(islands);
    if (m_selected_island >= m_islands.island_count)
        m_selected_island = -1;
    if (!same_structure) {
        m_selection.clear();
        if (m_selected_island >= 0)
            m_selection.push_back(m_selected_island);
        m_active_vertex = -1;
        m_active_edge   = { -1, -1 };
        m_sel_vertices.clear();
        m_sel_edges.clear();
        m_hover_island = m_hover_vertex = -1;
        m_hover_edge   = { -1, -1 };
    }

    const size_t n_islands = size_t(std::max(m_islands.island_count, 0));
    m_island_boundary_verts.assign(n_islands, {});
    m_island_boundary_edges.assign(n_islands, {});
    std::vector<bool> seen(m_islands.uvs.size(), false);
    for (const auto &[a, b] : m_islands.boundary_edges) {
        if (a < 0 || b < 0 || size_t(a) >= m_islands.uvs.size() || size_t(b) >= m_islands.uvs.size())
            continue;
        const int island = m_islands.vertex_island[size_t(a)];
        if (island < 0 || size_t(island) >= n_islands)
            continue;
        m_island_boundary_edges[size_t(island)].emplace_back(a, b);
        for (const int v : { a, b })
            if (!seen[size_t(v)]) {
                seen[size_t(v)] = true;
                m_island_boundary_verts[size_t(island)].push_back(v);
            }
    }

    m_island_tris.assign(n_islands, {});
    m_island_raw_bounds.assign(n_islands, { Vec2f::Constant(std::numeric_limits<float>::max()),
                                            Vec2f::Constant(std::numeric_limits<float>::lowest()) });
    for (size_t t = 0; t < m_islands.indices.size(); ++t) {
        const Vec3i32 &tri = m_islands.indices[t];
        if (tri.minCoeff() < 0 || size_t(tri.maxCoeff()) >= m_islands.uvs.size())
            continue;
        const int island = m_islands.vertex_island[size_t(tri[0])];
        if (island < 0 || size_t(island) >= n_islands)
            continue;
        m_island_tris[size_t(island)].push_back(int(t));
        auto &[lo, hi] = m_island_raw_bounds[size_t(island)];
        for (int k = 0; k < 3; ++k) {
            lo = lo.cwiseMin(m_islands.uvs[size_t(tri[k])]);
            hi = hi.cwiseMax(m_islands.uvs[size_t(tri[k])]);
        }
    }

    m_mesh_dirty            = true;
    m_background_quad_dirty = true;
    Refresh();
}

void UVEditorCanvas::set_island_transforms(std::vector<IslandTransform> transforms)
{
    m_transforms            = std::move(transforms);
    m_background_quad_dirty = true;
    Refresh();
}

void UVEditorCanvas::set_island_fill_colors(std::vector<ColorRGBA> colors)
{
    m_island_fill_colors = std::move(colors);
    Refresh();
}

void UVEditorCanvas::set_uv_transform(float tiling_scale, float rotation_deg, bool tile_enabled, bool tile_mirrored)
{
    m_tiling_scale          = (std::abs(tiling_scale) > 1e-6f) ? tiling_scale : 1.f;
    m_rotation_deg          = rotation_deg;
    m_tile_enabled          = tile_enabled;
    m_tile_mirrored         = tile_mirrored;
    m_background_quad_dirty = true;
}

void UVEditorCanvas::set_background_texture(const std::vector<unsigned char> &grayscale_pixels, int width, int height)
{
    if (width <= 0 || height <= 0 || grayscale_pixels.size() != size_t(width) * size_t(height)) {
        m_background_width = m_background_height = 0;
        m_background_pixels.clear();
    } else {
        m_background_width  = width;
        m_background_height = height;
        m_background_pixels.assign(size_t(width) * size_t(height) * 4, 255);
        for (size_t i = 0; i < grayscale_pixels.size(); ++i) {
            const unsigned char g          = grayscale_pixels[i];
            m_background_pixels[i * 4 + 0] = g;
            m_background_pixels[i * 4 + 1] = g;
            m_background_pixels[i * 4 + 2] = g;
            m_background_pixels[i * 4 + 3] = 255;
        }
    }
    m_background_dirty = true;
    Refresh();
}

void UVEditorCanvas::reset_view()
{
    m_needs_fit = true;
    Refresh();
}

void UVEditorCanvas::run_command(Command cmd, float value)
{
    switch (cmd) {
    case Command::FrameAll:   reset_view(); return;
    case Command::ToggleSnap: m_snap_enabled = !m_snap_enabled; update_status(); return;
    case Command::SetSelectMode:
        set_select_mode(static_cast<SelectMode>(std::clamp(int(value), 0, 2)));
        break;
    default: break;
    }
    if (m_on_command)
        m_on_command(cmd, value);
}

void UVEditorCanvas::set_pane_state(PaneState state)
{
    m_pane_state = std::move(state);
    update_status();
    if (m_on_pane_state)
        m_on_pane_state(m_pane_state);
}

void UVEditorCanvas::update_status()
{
    if (!m_on_status)
        return;

    wxString msg;
    switch (m_gesture) {
    case Gesture::MoveIsland:        msg = _L("Moving island  |  release to drop, Esc to cancel"); break;
    case Gesture::RotateIsland:
    case Gesture::RotateIslandModal:
        msg = wxString::Format(_L("Rotating island: %d°  |  Shift = snap 15°, click to confirm, Esc to cancel"),
                                int(std::lround(m_rot_display_deg)));
        break;
    case Gesture::ScaleIslandModal:  msg = _L("Scaling island  |  click to confirm, Esc to cancel"); break;
    case Gesture::MoveVertex:        msg = _L("Moving vertex  |  release to drop"); break;
    case Gesture::MoveEdge:          msg = _L("Moving edge  |  release to drop"); break;
    case Gesture::Pan:               msg = _L("Panning"); break;
    case Gesture::None:
    default:
        if (!has_islands())
            msg = m_pane_state.has_layer ? _L("Paint the area on the model, then press Unwrap") :
                                           _L("Pick Unwrap as a texture layer's mapping to edit its UVs here");
        else if (m_select_mode == SelectMode::Vertex)
            msg = m_sel_vertices.size() > 1 ?
                      wxString::Format(_L("%d vertices selected  |  drag = move together, Shift/Ctrl click = add/remove"),
                                       int(m_sel_vertices.size())) :
                      _L("Vertex mode: drag a vertex to reshape  |  Shift/Ctrl click = multi-select, wheel = zoom, Home = frame");
        else if (m_select_mode == SelectMode::Edge)
            msg = m_sel_edges.size() > 1 ?
                      wxString::Format(_L("%d edges selected  |  drag = move together, Shift/Ctrl click = add/remove"),
                                       int(m_sel_edges.size())) :
                      _L("Edge mode: drag an island edge to reshape  |  Shift/Ctrl click = multi-select, wheel = zoom, Home = frame");
        else if (m_selection.size() > 1)
            msg = wxString::Format(_L("%d islands selected  |  drag = move together, Shift/Ctrl click = add/remove, R/S = rotate/scale primary"),
                                    int(m_selection.size()));
        else if (m_selected_island >= 0)
            msg = wxString::Format(_L("Island %d selected  |  drag = move, R = rotate, S = scale, Shift/Ctrl click = multi-select, Home = frame"),
                                    m_selected_island + 1);
        else
            msg = _L("Click an island to select  |  Shift/Ctrl click = multi-select, wheel = zoom, middle-drag = pan, Home = frame all");
        if (m_snap_enabled)
            msg += _L("  |  snap ON");
        break;
    }
    m_on_status(msg);
}

Vec2f UVEditorCanvas::island_uv(size_t vertex) const
{
    const Vec2f &raw = m_islands.uvs[vertex];
    const int    island = m_islands.vertex_island[vertex];
    if (island < 0 || size_t(island) >= m_transforms.size())
        return raw;
    const IslandTransform &m = m_transforms[size_t(island)];
    return m.block<2, 2>(0, 0) * raw + m.col(2);
}

void UVEditorCanvas::content_bounds(Vec2f &min_uv, Vec2f &max_uv) const
{
    min_uv = Vec2f(0.f, 0.f);
    max_uv = Vec2f(1.f, 1.f);
    for (size_t i = 0; i < m_islands.uvs.size(); ++i) {
        const Vec2f uv = island_uv(i);
        min_uv = min_uv.cwiseMin(uv);
        max_uv = max_uv.cwiseMax(uv);
    }
}

void UVEditorCanvas::framed_bounds(Vec2f &min_uv, Vec2f &max_uv) const
{
    content_bounds(min_uv, max_uv);
    if (m_tile_enabled) {
        min_uv = Vec2f(std::floor(min_uv.x()), std::floor(min_uv.y()));
        max_uv = Vec2f(std::ceil(max_uv.x()), std::ceil(max_uv.y()));
    }
}

void UVEditorCanvas::fit_view_to_content()
{
    Vec2f min_uv, max_uv;
    framed_bounds(min_uv, max_uv);

    const wxSize size   = GetSize();
    const float  aspect = float(std::max(1, size.GetWidth())) / float(std::max(1, size.GetHeight()));
    const Vec2f  half   = 0.5f * (max_uv - min_uv);

    m_pan = 0.5f * (min_uv + max_uv);
    m_zoom = std::max(1.1f * std::max(half.x() / std::max(aspect, 1.f), half.y() * std::min(aspect, 1.f)),
                      0.05f);
    m_needs_fit = false;
}

void UVEditorCanvas::view_half_extents(float &half_w, float &half_h) const
{
    const wxSize size = GetSize();
    const float  w    = float(std::max(1, size.GetWidth()));
    const float  h    = float(std::max(1, size.GetHeight()));

    const float aspect = w / h;
    half_w             = m_zoom * std::max(aspect, 1.f);
    half_h             = m_zoom * std::max(1.f / aspect, 1.f);
}

Vec2f UVEditorCanvas::screen_to_uv(const wxPoint &px) const
{
    const wxSize size = GetSize();
    const float  w    = float(std::max(1, size.GetWidth()));
    const float  h    = float(std::max(1, size.GetHeight()));

    float half_w, half_h;
    view_half_extents(half_w, half_h);

    return Vec2f(m_pan.x() + (2.f * float(px.x) / w - 1.f) * half_w,
                 m_pan.y() + (2.f * float(px.y) / h - 1.f) * half_h);
}

int UVEditorCanvas::island_at(const Vec2f &uv) const
{
    int found = -1;
    for (int island = 0; island < int(m_island_tris.size()); ++island) {
        const auto &[lo, hi] = m_island_raw_bounds[size_t(island)];
        if (lo.x() > hi.x())
            continue;
        const IslandTransform m = size_t(island) < m_transforms.size() ? m_transforms[size_t(island)] :
                                                                         IslandTransform(IslandTransform::Identity());
        Vec2f bmin = Vec2f::Constant(std::numeric_limits<float>::max()), bmax = -bmin;
        for (const Vec2f &corner : { lo, hi, Vec2f(lo.x(), hi.y()), Vec2f(hi.x(), lo.y()) }) {
            const Vec2f p = m.block<2, 2>(0, 0) * corner + m.col(2);
            bmin          = bmin.cwiseMin(p);
            bmax          = bmax.cwiseMax(p);
        }
        if (uv.x() < bmin.x() || uv.y() < bmin.y() || uv.x() > bmax.x() || uv.y() > bmax.y())
            continue;

        for (const int t : m_island_tris[size_t(island)]) {
            const Vec3i32 &tri = m_islands.indices[size_t(t)];
            if (!point_in_triangle(uv, island_uv(size_t(tri[0])), island_uv(size_t(tri[1])), island_uv(size_t(tri[2]))))
                continue;
            if (is_selected(island))
                return island;
            found = island;
            break;
        }
    }
    return found;
}

void UVEditorCanvas::update_hover(const wxPoint &pos)
{
    int                 island = -1, vertex = -1;
    std::pair<int, int> edge{ -1, -1 };
    if (has_islands()) {
        const Vec2f uv = screen_to_uv(pos);
        switch (m_select_mode) {
        case SelectMode::Island: island = island_at(uv); break;
        case SelectMode::Vertex: vertex = vertex_at(uv); break;
        case SelectMode::Edge:   edge = edge_at(uv); break;
        }
    }
    if (island != m_hover_island || vertex != m_hover_vertex || edge != m_hover_edge) {
        m_hover_island = island;
        m_hover_vertex = vertex;
        m_hover_edge   = edge;
        Refresh();
    }
}

void UVEditorCanvas::set_select_mode(SelectMode mode)
{
    if (m_select_mode == mode)
        return;
    m_select_mode   = mode;
    m_active_vertex = -1;
    m_active_edge   = { -1, -1 };
    m_hover_island  = m_hover_vertex = -1;
    m_hover_edge    = { -1, -1 };
    m_sel_vertices.clear();
    m_sel_edges.clear();
    m_gesture       = Gesture::None;
    update_status();
    Refresh();
}

int UVEditorCanvas::vertex_at(const Vec2f &uv) const
{
    const wxSize size      = GetSize();
    const float  uv_per_px = 2.f * m_zoom / float(std::max(1, std::min(size.GetWidth(), size.GetHeight())));
    const float  threshold = SNAP_PIXELS * uv_per_px;

    int   best   = -1;
    float best_d = threshold * threshold;
    for (size_t i = 0; i < m_islands.uvs.size(); ++i) {
        const float d = (island_uv(i) - uv).squaredNorm();
        if (d < best_d) {
            best_d = d;
            best   = int(i);
        }
    }
    return best;
}

std::pair<int, int> UVEditorCanvas::edge_at(const Vec2f &uv) const
{
    const wxSize size      = GetSize();
    const float  uv_per_px = 2.f * m_zoom / float(std::max(1, std::min(size.GetWidth(), size.GetHeight())));
    const float  threshold = SNAP_PIXELS * uv_per_px;

    const auto seg_dist_sq = [](const Vec2f &p, const Vec2f &a, const Vec2f &b) {
        const Vec2f ab = b - a;
        const float l2 = ab.squaredNorm();
        const float t  = (l2 > 1e-12f) ? std::clamp((p - a).dot(ab) / l2, 0.f, 1.f) : 0.f;
        return (p - (a + ab * t)).squaredNorm();
    };
    std::pair<int, int> best{ -1, -1 };
    float               best_d = threshold * threshold;
    for (const auto &[a, b] : m_islands.boundary_edges) {
        if (a < 0 || b < 0 || size_t(a) >= m_islands.uvs.size() || size_t(b) >= m_islands.uvs.size())
            continue;
        const float d = seg_dist_sq(uv, island_uv(size_t(a)), island_uv(size_t(b)));
        if (d < best_d) {
            best_d = d;
            best   = { a, b };
        }
    }
    return best;
}

void UVEditorCanvas::move_vertex_raw(int v, const Vec2f &delta_uv)
{
    if (v < 0 || size_t(v) >= m_islands.uvs.size())
        return;
    const int island = m_islands.vertex_island[size_t(v)];
    Eigen::Matrix2f lin = Eigen::Matrix2f::Identity();
    if (island >= 0 && size_t(island) < m_transforms.size())
        lin = m_transforms[size_t(island)].block<2, 2>(0, 0);
    const float det = lin.determinant();
    const Vec2f raw_delta = (std::abs(det) > 1e-12f) ? Vec2f(lin.inverse() * delta_uv) : delta_uv;
    m_islands.uvs[size_t(v)] += raw_delta;
    if (island >= 0 && size_t(island) < m_island_raw_bounds.size()) {
        auto &[lo, hi] = m_island_raw_bounds[size_t(island)];
        lo = lo.cwiseMin(m_islands.uvs[size_t(v)]);
        hi = hi.cwiseMax(m_islands.uvs[size_t(v)]);
    }
    m_mesh_dirty = true;
}

float UVEditorCanvas::island_rotation_deg(int island) const
{
    if (island < 0 || size_t(island) >= m_transforms.size())
        return 0.f;
    const IslandTransform &m = m_transforms[size_t(island)];
    return std::atan2(m(1, 0), m(0, 0)) * 180.f / float(M_PI);
}

Vec2f UVEditorCanvas::island_centroid(int island) const
{
    Vec2f sum   = Vec2f::Zero();
    int   count = 0;
    for (size_t i = 0; i < m_islands.uvs.size(); ++i)
        if (m_islands.vertex_island[i] == island) {
            sum += island_uv(i);
            ++count;
        }
    return (count > 0) ? Vec2f(sum / float(count)) : Vec2f::Zero();
}

Vec2f UVEditorCanvas::uv_delta_to_unwrap(const Vec2f &delta_uv) const
{
    const float rad = m_rotation_deg * float(M_PI) / 180.f;
    const float cs = std::cos(rad), sn = std::sin(rad);
    return Vec2f(delta_uv.x() * cs + delta_uv.y() * sn, -delta_uv.x() * sn + delta_uv.y() * cs) * m_tiling_scale;
}

Vec2f UVEditorCanvas::snap_correction(int island) const
{
    if (!m_snap_enabled || island < 0 || size_t(island) >= m_island_boundary_verts.size())
        return Vec2f::Zero();

    const wxSize size      = GetSize();
    const float  uv_per_px = 2.f * m_zoom / float(std::max(1, std::min(size.GetWidth(), size.GetHeight())));
    const float  threshold = SNAP_PIXELS * uv_per_px;

    float best_dist_sq = threshold * threshold;
    Vec2f best         = Vec2f::Zero();
    for (const int mine : m_island_boundary_verts[size_t(island)]) {
        const Vec2f a = island_uv(size_t(mine));
        for (size_t other = 0; other < m_island_boundary_verts.size(); ++other) {
            if (int(other) == island)
                continue;
            for (const int theirs : m_island_boundary_verts[other]) {
                const Vec2f d       = island_uv(size_t(theirs)) - a;
                const float dist_sq = d.squaredNorm();
                if (dist_sq < best_dist_sq) {
                    best_dist_sq = dist_sq;
                    best         = d;
                }
            }
        }
    }
    return best;
}

std::vector<int> UVEditorCanvas::selected_edge_endpoints() const
{
    std::vector<int> verts;
    for (const auto &[a, b] : m_sel_edges)
        for (const int v : { a, b })
            if (v >= 0 && std::find(verts.begin(), verts.end(), v) == verts.end())
                verts.push_back(v);
    return verts;
}

void UVEditorCanvas::end_gesture()
{
    const bool was_editing = m_gesture == Gesture::MoveIsland || m_gesture == Gesture::RotateIsland ||
                             m_gesture == Gesture::RotateIslandModal || m_gesture == Gesture::ScaleIslandModal;

    if (m_gesture == Gesture::MoveIsland && m_on_island_edit) {
        const Vec2f correction = snap_correction(m_selected_island);
        if (!correction.isZero())
            m_on_island_edit(m_selected_island, uv_delta_to_unwrap(correction), 0.f, 1.f, false);
    }

    if (was_editing && m_on_island_edit)
        m_on_island_edit(m_selected_island, Vec2f::Zero(), 0.f, 1.f,  true);

    if ((m_gesture == Gesture::MoveVertex || m_gesture == Gesture::MoveEdge) && m_vertex_edit_moved &&
        m_on_vertex_edit) {
        std::vector<std::pair<int, Vec2f>> edits;
        const auto add = [&](int v) {
            if (v >= 0 && size_t(v) < m_islands.uvs.size())
                edits.emplace_back(v, m_islands.uvs[size_t(v)]);
        };
        if (m_gesture == Gesture::MoveVertex) {
            if (m_sel_vertices.empty())
                add(m_active_vertex);
            else
                for (const int v : m_sel_vertices)
                    add(v);
        } else {
            const std::vector<int> endpoints = selected_edge_endpoints();
            if (endpoints.empty()) {
                add(m_active_edge.first);
                add(m_active_edge.second);
            } else
                for (const int v : endpoints)
                    add(v);
        }
        if (!edits.empty())
            m_on_vertex_edit(edits);
    }

    m_gesture           = Gesture::None;
    m_rot_raw_deg       = 0.f;
    m_rot_applied_deg   = 0.f;
    m_modal_scale_accum = 1.f;
    drop_mouse();
}

void UVEditorCanvas::cancel_gesture()
{
    if (m_on_island_edit) {
        if (m_gesture == Gesture::RotateIslandModal && m_rot_applied_deg != 0.f)
            m_on_island_edit(m_selected_island, Vec2f::Zero(), -m_rot_applied_deg, 1.f, false);
        if (m_gesture == Gesture::ScaleIslandModal && m_modal_scale_accum != 1.f)
            m_on_island_edit(m_selected_island, Vec2f::Zero(), 0.f, 1.f / m_modal_scale_accum, false);
    }

    m_gesture           = Gesture::None;
    m_rot_raw_deg       = 0.f;
    m_rot_applied_deg   = 0.f;
    m_modal_scale_accum = 1.f;
    m_vertex_edit_moved = false;
}

void UVEditorCanvas::grab_mouse()
{
    if (!HasCapture())
        CaptureMouse();
}

void UVEditorCanvas::drop_mouse()
{
    if (HasCapture())
        ReleaseMouse();
}

void UVEditorCanvas::on_capture_lost(wxMouseCaptureLostEvent &)
{
    cancel_gesture();
    Refresh();
}

void UVEditorCanvas::on_key(wxKeyEvent &evt)
{
    const int key = evt.GetKeyCode();

    if (evt.ControlDown() && (key == 'Z' || key == 'z')) {
        if (evt.ShiftDown()) wxGetApp().plater()->redo();
        else                 wxGetApp().plater()->undo();
        return;
    }
    if (evt.ControlDown() && (key == 'Y' || key == 'y')) {
        wxGetApp().plater()->redo();
        return;
    }

    if (m_gesture == Gesture::RotateIslandModal || m_gesture == Gesture::ScaleIslandModal) {
        if (key == WXK_ESCAPE) {
            if (m_on_island_edit) {
                if (m_gesture == Gesture::RotateIslandModal && m_rot_applied_deg != 0.f)
                    m_on_island_edit(m_selected_island, Vec2f::Zero(), -m_rot_applied_deg, 1.f, false);
                if (m_gesture == Gesture::ScaleIslandModal && m_modal_scale_accum != 1.f)
                    m_on_island_edit(m_selected_island, Vec2f::Zero(), 0.f, 1.f / m_modal_scale_accum, false);
            }
            end_gesture();
            Refresh();
            return;
        }
        if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER) {
            end_gesture();
            Refresh();
            return;
        }
    }

    if (key == WXK_HOME || key == 'F' || key == 'f') {
        reset_view();
        return;
    }

    if (m_selected_island >= 0 && m_select_mode == SelectMode::Island && m_gesture == Gesture::None &&
        (key == 'R' || key == 'r' || key == 'S' || key == 's')) {
        const Vec2f uv     = screen_to_uv(ScreenToClient(wxGetMousePosition()));
        const Vec2f centre = island_centroid(m_selected_island);
        const Vec2f rel    = uv - centre;
        if (key == 'R' || key == 'r') {
            m_gesture            = Gesture::RotateIslandModal;
            m_rot_raw_deg        = 0.f;
            m_rot_applied_deg    = 0.f;
            m_rot_base_deg       = island_rotation_deg(m_selected_island);
            m_rot_display_deg    = m_rot_base_deg;
            m_gesture_last_angle = std::atan2(rel.y(), rel.x());
        } else {
            m_gesture           = Gesture::ScaleIslandModal;
            m_modal_scale_accum = 1.f;
            m_gesture_last_dist = std::max(rel.norm(), 1e-6f);
        }
        return;
    }

    evt.Skip();
}

void UVEditorCanvas::on_mouse(wxMouseEvent &evt)
{
    const wxEventType type = evt.GetEventType();
    const wxPoint     pos  = evt.GetPosition();

    m_cursor_px     = pos;
    m_cursor_inside = true;
    if (type == wxEVT_MOTION && m_gesture == Gesture::None) {
        update_hover(pos);
        if (m_select_mode != SelectMode::Island)
            Refresh();
    }

    if (type == wxEVT_LEFT_DOWN || type == wxEVT_RIGHT_DOWN || type == wxEVT_MIDDLE_DOWN)
        SetFocus();

    if ((m_gesture == Gesture::RotateIslandModal || m_gesture == Gesture::ScaleIslandModal) &&
        (type == wxEVT_LEFT_DOWN || type == wxEVT_RIGHT_DOWN)) {
        end_gesture();
        Refresh();
        return;
    }

    if (type == wxEVT_LEFT_DOWN) {
        const Vec2f uv    = screen_to_uv(pos);
        m_drag_last_px    = pos;
        m_gesture_last_uv = uv;
        if (m_select_mode == SelectMode::Vertex) {
            const int hit       = vertex_at(uv);
            m_active_edge       = { -1, -1 };
            m_vertex_edit_moved = false;
            if (hit >= 0) {
                if (evt.ShiftDown()) {
                    if (!is_vertex_selected(hit))
                        m_sel_vertices.push_back(hit);
                } else if (evt.ControlDown()) {
                    if (auto it = std::find(m_sel_vertices.begin(), m_sel_vertices.end(), hit); it != m_sel_vertices.end())
                        m_sel_vertices.erase(it);
                    else
                        m_sel_vertices.push_back(hit);
                } else if (!is_vertex_selected(hit)) {
                    m_sel_vertices.assign(1, hit);
                }
            } else if (!evt.ShiftDown() && !evt.ControlDown()) {
                m_sel_vertices.clear();
            }
            m_active_vertex = (hit >= 0 && is_vertex_selected(hit)) ? hit : -1;
            m_gesture       = (m_active_vertex >= 0) ? Gesture::MoveVertex : Gesture::Pan;
        } else if (m_select_mode == SelectMode::Edge) {
            const std::pair<int, int> hit = edge_at(uv);
            m_active_vertex     = -1;
            m_vertex_edit_moved = false;
            if (hit.first >= 0) {
                if (evt.ShiftDown()) {
                    if (!is_edge_selected(hit))
                        m_sel_edges.push_back(hit);
                } else if (evt.ControlDown()) {
                    if (auto it = std::find(m_sel_edges.begin(), m_sel_edges.end(), hit); it != m_sel_edges.end())
                        m_sel_edges.erase(it);
                    else
                        m_sel_edges.push_back(hit);
                } else if (!is_edge_selected(hit)) {
                    m_sel_edges.assign(1, hit);
                }
            } else if (!evt.ShiftDown() && !evt.ControlDown()) {
                m_sel_edges.clear();
            }
            m_active_edge = (hit.first >= 0 && is_edge_selected(hit)) ? hit : std::pair<int, int>{ -1, -1 };
            m_gesture     = (m_active_edge.first >= 0) ? Gesture::MoveEdge : Gesture::Pan;
        } else {
            const int hit = island_at(uv);
            if (hit >= 0) {
                if (evt.ShiftDown()) {
                    if (!is_selected(hit))
                        m_selection.push_back(hit);
                    m_selected_island = hit;
                } else if (evt.ControlDown()) {
                    if (auto it = std::find(m_selection.begin(), m_selection.end(), hit); it != m_selection.end()) {
                        m_selection.erase(it);
                        m_selected_island = m_selection.empty() ? -1 : m_selection.back();
                    } else {
                        m_selection.push_back(hit);
                        m_selected_island = hit;
                    }
                } else {
                    if (!is_selected(hit))
                        m_selection.assign(1, hit);
                    m_selected_island = hit;
                }
            } else if (!evt.ShiftDown() && !evt.ControlDown()) {
                m_selection.clear();
                m_selected_island = -1;
            }
            m_gesture = (m_selected_island >= 0) ? Gesture::MoveIsland : Gesture::Pan;
        }
        grab_mouse();
        Refresh();
    } else if (type == wxEVT_RIGHT_DOWN && m_selected_island >= 0 && m_select_mode == SelectMode::Island) {
        const Vec2f rel      = screen_to_uv(pos) - island_centroid(m_selected_island);
        m_gesture            = Gesture::RotateIsland;
        m_rot_raw_deg        = 0.f;
        m_rot_applied_deg    = 0.f;
        m_rot_base_deg       = island_rotation_deg(m_selected_island);
        m_rot_display_deg    = m_rot_base_deg;
        m_gesture_last_angle = std::atan2(rel.y(), rel.x());
        grab_mouse();
    } else if (type == wxEVT_MIDDLE_DOWN) {
        m_gesture      = Gesture::Pan;
        m_drag_last_px = pos;
        grab_mouse();
    } else if (type == wxEVT_LEFT_UP || type == wxEVT_RIGHT_UP || type == wxEVT_MIDDLE_UP) {
        drop_mouse();
        if (m_gesture != Gesture::RotateIslandModal && m_gesture != Gesture::ScaleIslandModal) {
            end_gesture();
            Refresh();
        }
    } else if (type == wxEVT_MOTION) {
        switch (m_gesture) {
        case Gesture::Pan: {
            const wxSize size  = GetSize();
            const float  scale = 2.f * m_zoom / float(std::max(1, std::min(size.GetWidth(), size.GetHeight())));
            m_pan.x() -= float(pos.x - m_drag_last_px.x) * scale;
            m_pan.y() -= float(pos.y - m_drag_last_px.y) * scale;
            m_drag_last_px = pos;
            Refresh();
            break;
        }
        case Gesture::MoveIsland: {
            const Vec2f uv = screen_to_uv(pos);
            if (m_on_island_edit)
                m_on_island_edit(m_selected_island, uv_delta_to_unwrap(uv - m_gesture_last_uv), 0.f, 1.f, false);
            m_gesture_last_uv = uv;
            Refresh();
            break;
        }
        case Gesture::MoveVertex: {
            const Vec2f uv    = screen_to_uv(pos);
            const Vec2f delta = uv - m_gesture_last_uv;
            if (m_sel_vertices.empty())
                move_vertex_raw(m_active_vertex, delta);
            else
                for (const int v : m_sel_vertices)
                    move_vertex_raw(v, delta);
            m_gesture_last_uv   = uv;
            m_vertex_edit_moved = true;
            Refresh();
            break;
        }
        case Gesture::MoveEdge: {
            const Vec2f uv    = screen_to_uv(pos);
            const Vec2f delta = uv - m_gesture_last_uv;
            const std::vector<int> endpoints = selected_edge_endpoints();
            if (endpoints.empty()) {
                move_vertex_raw(m_active_edge.first, delta);
                move_vertex_raw(m_active_edge.second, delta);
            } else
                for (const int v : endpoints)
                    move_vertex_raw(v, delta);
            m_gesture_last_uv   = uv;
            m_vertex_edit_moved = true;
            Refresh();
            break;
        }
        case Gesture::RotateIsland:
        case Gesture::RotateIslandModal: {
            const Vec2f rel   = screen_to_uv(pos) - island_centroid(m_selected_island);
            const float angle = std::atan2(rel.y(), rel.x());
            m_rot_raw_deg += angle_delta(m_gesture_last_angle, angle) * 180.f / float(M_PI);
            m_gesture_last_angle = angle;

            constexpr float STEP       = 15.f;
            const float     absolute   = m_rot_base_deg + m_rot_raw_deg;
            const float     target_abs = evt.ShiftDown() ? std::round(absolute / STEP) * STEP : absolute;
            const float     deg        = target_abs - (m_rot_base_deg + m_rot_applied_deg);
            if (m_on_island_edit && deg != 0.f)
                m_on_island_edit(m_selected_island, Vec2f::Zero(), deg, 1.f, false);
            m_rot_applied_deg = target_abs - m_rot_base_deg;
            m_rot_display_deg = target_abs;
            Refresh();
            break;
        }
        case Gesture::ScaleIslandModal: {
            const Vec2f rel  = screen_to_uv(pos) - island_centroid(m_selected_island);
            const float dist = std::max(rel.norm(), 1e-6f);
            const float factor = dist / m_gesture_last_dist;
            if (m_on_island_edit && factor != 1.f)
                m_on_island_edit(m_selected_island, Vec2f::Zero(), 0.f, factor, false);
            m_modal_scale_accum *= factor;
            m_gesture_last_dist = dist;
            Refresh();
            break;
        }
        default: break;
        }
    } else if (type == wxEVT_MOUSEWHEEL) {
        const Vec2f before = screen_to_uv(pos);
        const float factor = std::pow(0.9f, float(evt.GetWheelRotation()) / float(evt.GetWheelDelta()));
        m_zoom             = std::clamp(m_zoom * factor, 0.001f, 5000.f);
        const Vec2f after  = screen_to_uv(pos);
        m_pan += before - after;
        if (m_gesture == Gesture::None)
            update_hover(pos);
        Refresh();
    }
}

void UVEditorCanvas::rebuild_island_models()
{
    m_mesh_dirty = false;
    m_island_wireframe.clear();
    m_island_fill.clear();

    const int islands = std::max(m_islands.island_count, 0);
    if (islands == 0 || m_islands.uvs.empty() || m_islands.indices.empty())
        return;

    m_island_wireframe.resize(size_t(islands));
    m_island_fill.resize(size_t(islands));

    const int vertex_count = int(m_islands.uvs.size());

    std::unordered_map<uint64_t, bool> is_boundary;
    is_boundary.reserve(m_islands.boundary_edges.size() * 2);
    for (const auto &[a, b] : m_islands.boundary_edges)
        is_boundary[undirected_edge_key(a, b)] = true;

    const size_t n_islands = size_t(islands);

    std::vector<int> local(m_islands.uvs.size(), -1);
    std::vector<std::vector<Vec2f>> island_verts(n_islands);
    for (size_t i = 0; i < m_islands.uvs.size(); ++i) {
        const int island = m_islands.vertex_island[i];
        if (island < 0 || island >= islands)
            continue;
        local[i] = int(island_verts[size_t(island)].size());
        island_verts[size_t(island)].push_back(m_islands.uvs[i]);
    }

    std::vector<GLModel::Geometry> wire(n_islands), fill(n_islands);
    for (int c = 0; c < islands; ++c) {
        wire[size_t(c)].format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
        fill[size_t(c)].format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
        for (GLModel::Geometry *g : { &wire[size_t(c)], &fill[size_t(c)] }) {
            g->reserve_vertices(island_verts[size_t(c)].size());
            for (const Vec2f &uv : island_verts[size_t(c)])
                g->add_vertex(Vec3f(uv.x(), uv.y(), 0.f));
        }
    }

    for (const Vec3i32 &tri : m_islands.indices) {
        if (tri.minCoeff() < 0 || tri.maxCoeff() >= vertex_count)
            continue;
        const int island = m_islands.vertex_island[size_t(tri[0])];
        if (island < 0 || island >= islands)
            continue;

        fill[size_t(island)].add_triangle(unsigned(local[size_t(tri[0])]), unsigned(local[size_t(tri[1])]),
                                          unsigned(local[size_t(tri[2])]));
        for (int i = 0; i < 3; ++i) {
            const int a = tri[i], b = tri[(i + 1) % 3];
            if (!is_boundary.count(undirected_edge_key(a, b)))
                wire[size_t(island)].add_line(unsigned(local[size_t(a)]), unsigned(local[size_t(b)]));
        }
    }

    for (int c = 0; c < islands; ++c) {
        if (!wire[size_t(c)].is_empty())
            m_island_wireframe[size_t(c)].init_from(std::move(wire[size_t(c)]));
        if (!fill[size_t(c)].is_empty())
            m_island_fill[size_t(c)].init_from(std::move(fill[size_t(c)]));
    }
}

void UVEditorCanvas::rebuild_grid()
{
    const float span = 2.f * m_zoom;
    float       step = 1.f;
    while (step > 0.01f && span / step > 40.f)
        step *= 2.f;
    while (span / step < 4.f)
        step *= 0.5f;

    float half_w, half_h;
    view_half_extents(half_w, half_h);
    const Vec2f lo = m_pan - Vec2f(half_w, half_h) - Vec2f(step, step);
    const Vec2f hi = m_pan + Vec2f(half_w, half_h) + Vec2f(step, step);
    m_grid_step    = step;

    const int first_x = int(std::floor(lo.x() / step)), last_x = int(std::ceil(hi.x() / step));
    const int first_y = int(std::floor(lo.y() / step)), last_y = int(std::ceil(hi.y() / step));
    if (last_x - first_x > 4000 || last_y - first_y > 4000)
        return;

    GLModel::Geometry grid;
    grid.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
    unsigned index = 0;
    const auto line = [&](const Vec2f &a, const Vec2f &b) {
        grid.add_vertex(Vec3f(a.x(), a.y(), 0.f));
        grid.add_vertex(Vec3f(b.x(), b.y(), 0.f));
        grid.add_line(index, index + 1);
        index += 2;
    };
    for (int i = first_x; i <= last_x; ++i)
        line(Vec2f(float(i) * step, lo.y()), Vec2f(float(i) * step, hi.y()));
    for (int i = first_y; i <= last_y; ++i)
        line(Vec2f(lo.x(), float(i) * step), Vec2f(hi.x(), float(i) * step));

    m_grid_glmodel.reset();
    if (!grid.is_empty())
        m_grid_glmodel.init_from(std::move(grid));
}

void UVEditorCanvas::rebuild_rotation_dial()
{
    m_dial_glmodel.reset();
    const bool rotating = (m_gesture == Gesture::RotateIsland || m_gesture == Gesture::RotateIslandModal);
    if (!rotating || m_selected_island < 0)
        return;

    const wxSize size      = GetSize();
    const float  uv_per_px = 2.f * m_zoom / float(std::max(1, std::min(size.GetWidth(), size.GetHeight())));
    const float  radius    = 90.f * uv_per_px;
    const Vec2f  centre    = island_centroid(m_selected_island);

    GLModel::Geometry dial;
    dial.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
    unsigned index = 0;
    const auto line = [&](const Vec2f &a, const Vec2f &b) {
        dial.add_vertex(Vec3f(a.x(), a.y(), 0.f));
        dial.add_vertex(Vec3f(b.x(), b.y(), 0.f));
        dial.add_line(index, index + 1);
        index += 2;
    };
    const auto on_ring = [&](float deg, float r) {
        const float a = deg * float(M_PI) / 180.f;
        return centre + r * Vec2f(std::cos(a), std::sin(a));
    };

    constexpr int SEG = 72;
    for (int i = 0; i < SEG; ++i)
        line(on_ring(float(i) * 360.f / SEG, radius), on_ring(float(i + 1) * 360.f / SEG, radius));
    for (int d = 0; d < 360; d += 15) {
        const bool cardinal = (d % 90) == 0;
        line(on_ring(float(d), radius * (cardinal ? 0.80f : 0.90f)), on_ring(float(d), radius * (cardinal ? 1.08f : 1.0f)));
    }
    line(centre, on_ring(m_rot_display_deg, radius * 1.12f));

    if (!dial.is_empty())
        m_dial_glmodel.init_from(std::move(dial));
}

void UVEditorCanvas::rebuild_background_quad()
{
    m_background_glmodel.reset();
    m_background_quad_dirty = false;
    if (m_background_width <= 0 || m_background_height <= 0)
        return;

    Vec2f lo, hi;
    if (m_tile_enabled) {
        framed_bounds(lo, hi);
    } else {
        lo = Vec2f(0.f, 0.f);
        hi = Vec2f(1.f, 1.f);
    }

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3T2 };
    init_data.reserve_vertices(4);
    init_data.reserve_indices(6);
    init_data.add_vertex(Vec3f(lo.x(), lo.y(), 0.f), Vec2f(lo.x(), lo.y()));
    init_data.add_vertex(Vec3f(hi.x(), lo.y(), 0.f), Vec2f(hi.x(), lo.y()));
    init_data.add_vertex(Vec3f(hi.x(), hi.y(), 0.f), Vec2f(hi.x(), hi.y()));
    init_data.add_vertex(Vec3f(lo.x(), hi.y(), 0.f), Vec2f(lo.x(), hi.y()));
    init_data.add_triangle(0, 1, 2);
    init_data.add_triangle(0, 2, 3);
    m_background_glmodel.init_from(std::move(init_data));
}

void UVEditorCanvas::rebuild_background_texture()
{
    m_background_texture.reset();
    m_background_dirty = false;
    if (m_background_width > 0 && m_background_height > 0)
        m_background_texture.load_from_raw_data(m_background_pixels, (unsigned int) m_background_width,
                                                 (unsigned int) m_background_height, false);
    m_background_quad_dirty = true;
}

void UVEditorCanvas::on_paint(wxPaintEvent & )
{
    wxPaintDC dc(this);
    render();
}

void UVEditorCanvas::on_size(wxSizeEvent &evt)
{
    evt.Skip();
    Refresh();
    Update();
}

void UVEditorCanvas::on_leave(wxMouseEvent &evt)
{
    evt.Skip();
    if (m_cursor_inside) {
        m_cursor_inside = false;
        if (m_gesture == Gesture::None) {
            m_hover_island = m_hover_vertex = -1;
            m_hover_edge   = { -1, -1 };
        }
        Refresh();
    }
}

void UVEditorCanvas::render()
{
    if (m_context == nullptr || !IsShownOnScreen())
        return;

    if (!SetCurrent(*m_context))
        return;

    const GLboolean prev_cull    = ::glIsEnabled(GL_CULL_FACE);
    const GLboolean prev_scissor = ::glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean prev_stencil = ::glIsEnabled(GL_STENCIL_TEST);
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glDisable(GL_SCISSOR_TEST));
    glsafe(::glDisable(GL_STENCIL_TEST));
    glsafe(::glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
    glsafe(::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));

    if (m_gl_reset_pending) {
        m_gl_reset_pending = false;
        m_mesh_dirty       = true;
        m_background_dirty = true;
        m_tile_outline_glmodel.reset();
        m_vertex_marker_glmodel.reset();
        m_dim_quad_glmodel.reset();
        m_stroke_glmodel.reset();
        m_stencil_bits = -1;
    }
    if (m_mesh_dirty)
        rebuild_island_models();
    if (m_background_dirty)
        rebuild_background_texture();
    if (m_background_quad_dirty)
        rebuild_background_quad();
    if (m_needs_fit)
        fit_view_to_content();
    rebuild_grid();
    rebuild_rotation_dial();

    const wxSize size     = GetSize();
    const wxSize viewport = gl_drawable_size(this, size);
    glsafe(::glViewport(0, 0, viewport.GetWidth(), viewport.GetHeight()));
    const float px_scale   = float(viewport.GetWidth()) / float(std::max(1, size.GetWidth()));
    const auto  line_width = [px_scale](float w) { set_line_width(w * px_scale); };
    glsafe(::glClearColor(UV_COLOR_BG.r(), UV_COLOR_BG.g(), UV_COLOR_BG.b(), 1.f));
    glsafe(::glClearStencil(0));
    glsafe(::glStencilMask(0xFF));
    glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));
    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    float half_w, half_h;
    view_half_extents(half_w, half_h);

    Transform3d projection_matrix = Transform3d::Identity();
    projection_matrix(0, 0)       =  1.0 / std::max(double(half_w), 1e-6);
    projection_matrix(1, 1)       = -1.0 / std::max(double(half_h), 1e-6);
    Transform3d view_matrix       = Transform3d::Identity();
    view_matrix.translate(Vec3d(-double(m_pan.x()), -double(m_pan.y()), 0.0));

    const auto draw_raw = [&](GLModel &model, const ColorRGBA &color, const Transform3d &view_model, const Transform3d &projection) {
        if (!model.is_initialized())
            return;
        GLShaderProgram *shader = wxGetApp().get_shader("flat");
        if (shader == nullptr)
            return;
        shader->start_using();
        shader->set_uniform("view_model_matrix", view_model);
        shader->set_uniform("projection_matrix", projection);
        model.set_color(color);
        model.render();
        shader->stop_using();
    };
    const auto draw = [&](GLModel &model, const ColorRGBA &color, const Transform3d &model_matrix) {
        draw_raw(model, color, view_matrix * model_matrix, projection_matrix);
    };
    const Transform3d identity = Transform3d::Identity();

    if (m_background_glmodel.is_initialized()) {
        if (GLShaderProgram *shader = wxGetApp().get_shader("flat_texture")) {
            shader->start_using();
            shader->set_uniform("view_model_matrix", view_matrix);
            shader->set_uniform("projection_matrix", projection_matrix);
            glsafe(::glActiveTexture(GL_TEXTURE0));
            glsafe(::glBindTexture(GL_TEXTURE_2D, m_background_texture.get_id()));
            const GLint wrap = m_tile_enabled ? (m_tile_mirrored ? GL_MIRRORED_REPEAT : GL_REPEAT) : GL_CLAMP_TO_BORDER;
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap));
            if (!m_tile_enabled) {
                constexpr GLfloat border[4] = { 0.f, 0.f, 0.f, 1.f };
                glsafe(::glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border));
            }
            m_background_glmodel.render();
            glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
            shader->stop_using();
        }
    }

    const auto island_matrix = [this, &identity](int c) {
        return (size_t(c) < m_transforms.size()) ? to_transform3d(m_transforms[size_t(c)]) : identity;
    };
    const float uv_per_px = 2.f * m_zoom / float(std::max(1, std::min(size.GetWidth(), size.GetHeight())));

    using Segments           = std::vector<std::pair<Vec2f, Vec2f>>;
    const auto draw_segments = [&](const Segments &segments, float width_px, const ColorRGBA &color) {
        if (segments.empty())
            return;
        const float       half = 0.5f * width_px * uv_per_px;
        GLModel::Geometry quads;
        quads.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
        quads.reserve_vertices(segments.size() * 4);
        quads.reserve_indices(segments.size() * 6);
        unsigned base = 0;
        for (const auto &[a, b] : segments) {
            const Vec2f d   = b - a;
            const float len = d.norm();
            if (len <= 1e-12f)
                continue;
            const Vec2f t = d * (half / len);
            const Vec2f n(-t.y(), t.x());
            for (const Vec2f &p : { Vec2f(a - t + n), Vec2f(a - t - n), Vec2f(b + t - n), Vec2f(b + t + n) })
                quads.add_vertex(Vec3f(p.x(), p.y(), 0.f));
            quads.add_triangle(base, base + 1, base + 2);
            quads.add_triangle(base, base + 2, base + 3);
            base += 4;
        }
        m_stroke_glmodel.reset();
        if (quads.is_empty())
            return;
        m_stroke_glmodel.init_from(std::move(quads));
        draw(m_stroke_glmodel, color, identity);
    };
    const auto draw_stroke = [&](const Segments &segments, float width_px, const ColorRGBA &color, float halo_px) {
        draw_segments(segments, width_px + 2.f * halo_px, UV_COLOR_HALO);
        draw_segments(segments, width_px, color);
    };
    const auto edge_segments = [this](const std::vector<std::pair<int, int>> &edges) {
        Segments out;
        out.reserve(edges.size());
        for (const auto &[a, b] : edges)
            if (a >= 0 && b >= 0 && size_t(a) < m_islands.uvs.size() && size_t(b) < m_islands.uvs.size())
                out.emplace_back(island_uv(size_t(a)), island_uv(size_t(b)));
        return out;
    };

    if (m_stencil_bits < 0) {
        GLint bits = 0;
        ::glGetError();
        ::glGetIntegerv(GL_STENCIL_BITS, &bits);
        if (::glGetError() != GL_NO_ERROR) {
            bits = 0;
            ::glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_STENCIL, GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &bits);
            ::glGetError();
        }
        m_stencil_bits = std::max(0, int(bits));
    }
    if (has_islands() && !m_island_fill.empty() && m_stencil_bits > 0) {
        if (!m_dim_quad_glmodel.is_initialized()) {
            GLModel::Geometry q;
            q.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
            q.add_vertex(Vec3f(-1.f, -1.f, 0.f));
            q.add_vertex(Vec3f(1.f, -1.f, 0.f));
            q.add_vertex(Vec3f(1.f, 1.f, 0.f));
            q.add_vertex(Vec3f(-1.f, 1.f, 0.f));
            q.add_triangle(0, 1, 2);
            q.add_triangle(0, 2, 3);
            m_dim_quad_glmodel.init_from(std::move(q));
        }
        glsafe(::glEnable(GL_STENCIL_TEST));
        glsafe(::glStencilFunc(GL_ALWAYS, 1, 0xFF));
        glsafe(::glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE));
        glsafe(::glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE));
        for (int c = 0; c < int(m_island_fill.size()); ++c)
            draw(m_island_fill[size_t(c)], ColorRGBA::WHITE(), island_matrix(c));
        glsafe(::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));
        glsafe(::glStencilFunc(GL_EQUAL, 0, 0xFF));
        glsafe(::glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP));
        draw_raw(m_dim_quad_glmodel, UV_COLOR_OUTSIDE, identity, identity);
        glsafe(::glDisable(GL_STENCIL_TEST));
    }

    line_width(1.f);
    draw(m_grid_glmodel, UV_COLOR_GRID, identity);

    draw_stroke({ { Vec2f(0.f, 0.f), Vec2f(1.f, 0.f) }, { Vec2f(1.f, 0.f), Vec2f(1.f, 1.f) },
                  { Vec2f(1.f, 1.f), Vec2f(0.f, 1.f) }, { Vec2f(0.f, 1.f), Vec2f(0.f, 0.f) } },
                1.5f, UV_COLOR_TILE_OUTLINE, 1.f);

    for (int c = 0; c < int(m_island_fill.size()); ++c) {
        ColorRGBA fill = size_t(c) < m_island_fill_colors.size() ? m_island_fill_colors[size_t(c)] : UV_COLOR_FILL;
        if (is_selected(c))
            fill = UV_COLOR_SEL_FILL;
        else if (c == m_hover_island && m_island_fill_colors.empty())
            fill = UV_COLOR_HOVER_FILL;
        if (fill.a() > 0.f)
            draw(m_island_fill[size_t(c)], fill, island_matrix(c));
    }

    Transform3d shadow_projection = projection_matrix;
    shadow_projection.pretranslate(Vec3d(2.0 / std::max(1, size.GetWidth()), -2.0 / std::max(1, size.GetHeight()), 0.0));
    line_width(1.f);
    for (int c = 0; c < int(m_island_wireframe.size()); ++c) {
        draw_raw(m_island_wireframe[size_t(c)], UV_COLOR_WIRE_SHADOW, view_matrix * island_matrix(c), shadow_projection);
        draw(m_island_wireframe[size_t(c)], is_selected(c) ? UV_COLOR_SEL_WIRE : UV_COLOR_WIRE, island_matrix(c));
    }

    {
        std::vector<std::pair<int, int>> plain, hovered, selected;
        for (int c = 0; c < int(m_island_boundary_edges.size()); ++c) {
            auto &dst = is_selected(c) ? selected : (c == m_hover_island ? hovered : plain);
            dst.insert(dst.end(), m_island_boundary_edges[size_t(c)].begin(), m_island_boundary_edges[size_t(c)].end());
        }
        draw_stroke(edge_segments(plain), 1.5f, UV_COLOR_BOUNDARY, 1.25f);
        draw_stroke(edge_segments(hovered), 2.5f, UV_COLOR_HOVER, 1.5f);
        draw_stroke(edge_segments(selected), 3.f, UV_COLOR_SEL_BOUNDARY, 1.75f);
    }

    if (m_dial_glmodel.is_initialized()) {
        line_width(2.f);
        draw(m_dial_glmodel, UV_COLOR_DIAL, identity);
        line_width(1.f);
    }

    if (!m_vertex_marker_glmodel.is_initialized()) {
        GLModel::Geometry q;
        q.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
        q.reserve_vertices(4);
        q.reserve_indices(6);
        q.add_vertex(Vec3f(-0.5f, -0.5f, 0.f));
        q.add_vertex(Vec3f(0.5f, -0.5f, 0.f));
        q.add_vertex(Vec3f(0.5f, 0.5f, 0.f));
        q.add_vertex(Vec3f(-0.5f, 0.5f, 0.f));
        q.add_triangle(0, 1, 2);
        q.add_triangle(0, 2, 3);
        m_vertex_marker_glmodel.init_from(std::move(q));
    }
    const auto draw_marker = [&](int v, float px, const ColorRGBA &color) {
        if (v < 0 || size_t(v) >= m_islands.uvs.size())
            return;
        const Vec2f p = island_uv(size_t(v));
        Transform3d m = Transform3d::Identity();
        m.translate(Vec3d(double(p.x()), double(p.y()), 0.0));
        m.scale(double(px * uv_per_px));
        draw(m_vertex_marker_glmodel, color, m);
    };
    const auto draw_handle = [&](int v, const ColorRGBA &color, float px) {
        draw_marker(v, px + 4.f, UV_COLOR_HALO);
        draw_marker(v, px, color);
    };
    if (m_select_mode == SelectMode::Vertex) {
        if (m_hover_vertex >= 0 && !is_vertex_selected(m_hover_vertex))
            draw_handle(m_hover_vertex, UV_COLOR_HOVER, 7.f);
        if (m_sel_vertices.empty())
            draw_handle(m_active_vertex, UV_COLOR_SEL_BOUNDARY, 8.f);
        else
            for (const int v : m_sel_vertices)
                draw_handle(v, UV_COLOR_SEL_BOUNDARY, 8.f);
    } else if (m_select_mode == SelectMode::Edge) {
        if (m_hover_edge.first >= 0 && !is_edge_selected(m_hover_edge))
            draw_stroke(edge_segments({ m_hover_edge }), 3.f, UV_COLOR_HOVER, 2.f);
        std::vector<std::pair<int, int>> edges = m_sel_edges;
        if (edges.empty() && m_active_edge.first >= 0)
            edges.push_back(m_active_edge);
        draw_stroke(edge_segments(edges), 4.f, UV_COLOR_SEL_BOUNDARY, 2.f);
        for (const auto &[a, b] : edges) {
            draw_handle(a, UV_COLOR_SEL_BOUNDARY, 6.f);
            draw_handle(b, UV_COLOR_SEL_BOUNDARY, 6.f);
        }
    }

    if (m_select_mode != SelectMode::Island && m_cursor_inside) {
        const Vec2f centre   = screen_to_uv(m_cursor_px) + Vec2f(14.f, -14.f) * uv_per_px;
        const float r        = 6.f * uv_per_px;
        const bool  removing = wxGetKeyState(WXK_CONTROL);
        Segments    sign{ { centre - Vec2f(r, 0.f), centre + Vec2f(r, 0.f) } };
        if (!removing)
            sign.emplace_back(centre - Vec2f(0.f, r), centre + Vec2f(0.f, r));
        draw_stroke(sign, 2.5f, removing ? ColorRGBA(1.f, 0.36f, 0.30f, 1.f) : ColorRGBA(0.40f, 0.92f, 0.50f, 1.f), 1.f);
    }

    glsafe(::glDisable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));
    if (prev_cull)
        glsafe(::glEnable(GL_CULL_FACE));
    if (prev_scissor)
        glsafe(::glEnable(GL_SCISSOR_TEST));
    if (prev_stencil)
        glsafe(::glEnable(GL_STENCIL_TEST));

    SwapBuffers();

    update_status();
}

namespace {
enum : int {
    ID_UV_FRAME = wxID_HIGHEST + 4200,
    ID_UV_SNAP,
    ID_UV_AVG_SCALE,
    ID_UV_CUT,
    ID_UV_JOIN,
    ID_UV_UNJOIN,
    ID_UV_UNWRAP,
    ID_UV_MARK_SEAMS,
    ID_UV_SEAM_PATH,
    ID_UV_CLEAR_SEAMS,
    ID_UV_CLEAR_EDITS,
    ID_UV_SELECT_ISLAND,
    ID_UV_SELECT_VERTEX,
    ID_UV_SELECT_EDGE,
    ID_UV_BG_HEIGHT,
    ID_UV_BG_CHECKER,
    ID_UV_BG_DISTORTION,
    ID_UV_PICK_TEXTURE,
};

wxBitmap pane_icon(wxWindow *win, const std::string &name, int size_dip)
{
#ifdef __WXGTK3__
    if (const double scale = win->GetContentScaleFactor(); scale > 1.0) {
        static BitmapCache cache;
        const unsigned px = unsigned(std::lround(size_dip * scale));
        if (wxBitmap *bmp = cache.load_svg(name, 0, px, false, wxGetApp().dark_mode()); bmp != nullptr && bmp->IsOk())
            return wxBitmap(bmp->ConvertToImage(), -1, scale);
    }
#endif
    return create_scaled_bitmap(name, win, size_dip);
}

wxSize drawn_size(const wxBitmap &bmp)
{
#ifdef __WXGTK3__
    return bmp.GetLogicalSize();
#else
    return ScalableBitmap::GetBmpSize(bmp);
#endif
}

wxBitmap faded(const wxBitmap &bmp, double alpha)
{
    wxImage image = bmp.ConvertToImage();
    if (!image.HasAlpha())
        image.InitAlpha();
    unsigned char *a = image.GetAlpha();
    for (int i = 0, n = image.GetWidth() * image.GetHeight(); i < n; ++i)
        a[i] = (unsigned char) std::lround(a[i] * alpha);
    return wxBitmap(image, -1, bmp.GetScaleFactor());
}

struct PaneColors
{
    wxColour bg, ink, dim, rule, frame, hover;
    static PaneColors current()
    {
        if (wxGetApp().dark_mode())
            return { wxColour(0x2d, 0x2d, 0x31), wxColour(0xef, 0xef, 0xf0), wxColour(0x90, 0x90, 0x96),
                     wxColour(0x3d, 0x3d, 0x45), wxColour(0x36, 0x36, 0x3c), wxColour(0x49, 0x49, 0x50) };
        return { wxColour(0xff, 0xff, 0xff), wxColour(0x32, 0x3a, 0x3d), wxColour(0x7c, 0x82, 0x82),
                 wxColour(0xed, 0xed, 0xed), wxColour(0xce, 0xce, 0xce), wxColour(0xee, 0xee, 0xee) };
    }
};

wxColour mix(const wxColour &a, const wxColour &b, double t)
{
    const auto ch = [t](unsigned char x, unsigned char y) { return (unsigned char) std::lround(x + (y - x) * t); };
    return wxColour(ch(a.Red(), b.Red()), ch(a.Green(), b.Green()), ch(a.Blue(), b.Blue()));
}
}
class UVToolButton : public wxWindow
{
public:
    UVToolButton(wxWindow *parent, wxWindowID id, const std::string &icon, const wxString &label, const wxString &tip,
                 bool toggle, bool accent = false, int size_dip = 26)
        : wxWindow(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE)
        , m_icon_name(icon), m_icon_dip(size_dip >= 26 ? 16 : 14), m_label(label), m_toggle(toggle), m_accent(accent)
        , m_size_dip(size_dip), m_tip(tip)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetToolTip(tip);
        if (accent) {
            wxFont font = GetFont();
            font.MakeBold();
            SetFont(font);
        }
        Bind(wxEVT_PAINT, &UVToolButton::on_paint, this);
        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &) { m_hover = true; Refresh(); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) { m_hover = false; m_pressed = false; Refresh(); });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) {
            if (usable()) {
                m_pressed = true;
                Refresh();
            }
        });
        Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &e) {
            const bool was_pressed = m_pressed;
            m_pressed              = false;
            Refresh();
            if (!was_pressed || !usable() || !GetClientRect().Contains(e.GetPosition()))
                return;
            if (m_toggle)
                m_on = !m_on;
            wxCommandEvent evt(wxEVT_BUTTON, GetId());
            evt.SetEventObject(this);
            evt.SetInt(m_on ? 1 : 0);
            ProcessWindowEvent(evt);
        });
        SetMinSize(DoGetBestSize());
    }

    void SetValue(bool on)
    {
        if (on != m_on) {
            m_on = on;
            Refresh();
        }
    }
    bool GetValue() const { return m_on; }
    void SetBitmap(const wxBitmap &bmp)
    {
        m_bitmap = bmp;
        Refresh();
    }
    void SetBadge(bool badge)
    {
        if (badge != m_badge) {
            m_badge = badge;
            Refresh();
        }
    }
    bool Enable(bool enable = true) override
    {
        const bool changed = wxWindow::Enable(enable);
        if (changed)
            Refresh();
        return changed;
    }
    void SetUnavailable(const wxString &reason)
    {
        if (reason == m_unavailable)
            return;
        m_unavailable = reason;
        SetToolTip(reason.empty() || m_tip.empty() ? m_tip : m_tip + "\n\n" + reason);
        if (!m_unavailable.empty())
            m_pressed = false;
        Refresh();
    }
    void SetTip(const wxString &tip)
    {
        if (tip == m_tip)
            return;
        m_tip = tip;
        SetToolTip(m_unavailable.empty() || m_tip.empty() ? m_tip : m_tip + "\n\n" + m_unavailable);
    }
    bool usable() const { return IsEnabled() && m_unavailable.empty(); }

protected:
    wxSize DoGetBestSize() const override
    {
        const int h = FromDIP(m_size_dip);
        if (m_label.empty())
            return wxSize(h, h);
        const int icon_w = m_icon_name.empty() ? 0 : FromDIP(m_icon_dip) + FromDIP(6);
        return wxSize(FromDIP(9) + icon_w + GetTextExtent(m_label).x + FromDIP(10), h);
    }

private:
    void on_paint(wxPaintEvent &)
    {
        wxAutoBufferedPaintDC dc(this);
        const PaneColors c    = PaneColors::current();
        const wxRect     r    = GetClientRect();
        const wxColour   teal(0x00, 0x96, 0x88);
        const bool       enabled = usable();

        wxColour fill = c.bg, border = c.frame, text = c.ink;
        if (m_accent) {
            fill   = (m_hover && enabled) ? wxColour(0x26, 0xa6, 0x9a) : teal;
            border = fill;
            text   = *wxWHITE;
        } else if (m_on) {
            fill   = mix(c.bg, teal, 0.22);
            border = teal;
        } else if (m_hover && enabled) {
            fill = c.hover;
        }
        if (m_pressed)
            fill = mix(fill, teal, 0.18);

        dc.SetBackground(wxBrush(c.bg));
        dc.Clear();
        dc.SetBrush(wxBrush(fill));
        dc.SetPen(wxPen(border, 1));
        dc.DrawRoundedRectangle(r, FromDIP(2));

        if (!m_icon_name.empty() && (!m_icon.IsOk() || m_icon_scale != GetContentScaleFactor())) {
            m_icon_scale = GetContentScaleFactor();
            m_icon       = pane_icon(this, m_icon_name, m_icon_dip);
        }
        const wxBitmap &bmp = m_bitmap.IsOk() ? m_bitmap : m_icon;
        int             x   = 0;
        if (bmp.IsOk()) {
            const wxSize bs = drawn_size(bmp);
            x               = m_label.empty() ? (r.width - bs.x) / 2 : FromDIP(9);
            dc.DrawBitmap(enabled ? bmp : faded(bmp, 0.35), x, (r.height - bs.y) / 2, true);
            x += bs.x + FromDIP(6);
        } else {
            x = FromDIP(9);
        }
        if (!m_label.empty()) {
            dc.SetFont(GetFont());
            dc.SetTextForeground(enabled ? text : mix(text, c.bg, 0.55));
            dc.DrawText(m_label, x, (r.height - dc.GetTextExtent(m_label).y) / 2);
        }
        if (m_badge) {
            const int d = FromDIP(9);
            dc.SetPen(wxPen(c.bg, FromDIP(2)));
            dc.SetBrush(wxBrush(wxColour(0xe0, 0xa4, 0x4a)));
            dc.DrawCircle(r.width - d / 2 - 1, d / 2 + 1, d / 2);
        }
    }

    std::string    m_icon_name;
    int            m_icon_dip   = 16;
    wxBitmap       m_icon;
    double         m_icon_scale = 0.;
    wxBitmap       m_bitmap;
    wxString       m_label;
    bool           m_toggle   = false;
    bool           m_accent   = false;
    int            m_size_dip = 26;
    wxString       m_tip;
    wxString       m_unavailable;
    bool           m_on       = false;
    bool           m_badge    = false;
    bool           m_hover    = false;
    bool           m_pressed  = false;
};

UVEditorPanel::UVEditorPanel(wxWindow *parent) : wxPanel(parent, wxID_ANY)
{
    const PaneColors c   = PaneColors::current();
    const int        gap = FromDIP(6);
    const int        pad = FromDIP(8);
    SetBackgroundColour(c.bg);

    const auto rule = [&](const wxSize &size) {
        auto *w = new wxWindow(this, wxID_ANY, wxDefaultPosition, size);
        w->SetBackgroundColour(c.rule);
        w->SetMinSize(size);
        return w;
    };
    const auto text = [&](const wxString &label, const wxColour &colour, long style = 0) {
        auto *t = new wxStaticText(this, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, style);
        t->SetForegroundColour(colour);
        t->SetBackgroundColour(c.bg);
        return t;
    };

    auto *header = new wxBoxSizer(wxHORIZONTAL);
    m_thumb      = new UVToolButton(this, ID_UV_PICK_TEXTURE, std::string(), wxEmptyString,
                                    _L("Change texture - choose another one from the texture library"), false, false, 26);
    m_layer_name = text(wxEmptyString, c.ink, wxST_ELLIPSIZE_END);
    {
        wxFont font = m_layer_name->GetFont();
        font.MakeBold();
        m_layer_name->SetFont(font);
    }
    m_layer_name->SetCursor(wxCursor(wxCURSOR_HAND));
    m_layer_name->SetToolTip(m_thumb->GetToolTipText());
    m_layer_name->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) {
        if (m_canvas->pane_state().has_layer)
            m_canvas->run_command(UVEditorCanvas::Command::PickTexture);
    });
    m_layer_name->SetMinSize(wxSize(FromDIP(30), -1));
    m_tile = text(wxEmptyString, c.dim);
    m_tile->SetToolTip(_L("The active layer's tile size: how much of the model one repeat of the texture covers. The "
                          "canvas is measured in tiles, so one grid cell is one repeat. Change it with Tiling in the "
                          "layer's settings."));
    header->Add(m_thumb, 0, wxALIGN_CENTER_VERTICAL);
    header->Add(m_layer_name, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, gap);
    header->Add(m_tile, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, gap);

    const char *const bg_icons[3] = { "texture_displacement_uv_bg_height", "texture_displacement_checker",
                                      "texture_displacement_distortion" };
    const wxString    bg_tips[3]  = { _L("Height map - show the layer's texture under the islands"),
                                      _L("Checker - a test grid; squares stay square where the unwrap does not stretch"),
                                      _L("Distortion - color each island by how much the unwrap stretches it") };
    for (int i = 0; i < 3; ++i) {
        m_background[i] = new UVToolButton(this, ID_UV_BG_HEIGHT + i, bg_icons[i], wxEmptyString, bg_tips[i], true, false, 22);
        header->Add(m_background[i], 0, wxALIGN_CENTER_VERTICAL | wxLEFT, i == 0 ? gap : FromDIP(3));
    }
    header->Add(rule(wxSize(1, FromDIP(18))), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, gap);
    m_unwrap = new UVToolButton(this, ID_UV_UNWRAP, "texture_displacement_map_unwrap", _L("Unwrap"), wxEmptyString, false, true, 24);
    header->Add(m_unwrap, 0, wxALIGN_CENTER_VERTICAL);

    auto *settings = new wxBoxSizer(wxHORIZONTAL);
    auto *seam_label = text(_L("Seam angle"), c.dim);
    m_seam_angle = new ::SpinInput(this, wxEmptyString, wxString::FromUTF8("°"), wxDefaultPosition, wxSize(FromDIP(76), FromDIP(24)), 0,
                                   5, 90, 40);
    const wxString seam_tip = _L("Edges sharper than this are cut, and the pieces either side of them are flattened separately. "
                                 "Lower it to cut more: each piece then lies flat with less stretching, at the cost of the "
                                 "texture not running continuously across the cut. Takes effect at the next Unwrap.");
    seam_label->SetToolTip(seam_tip);
    m_seam_angle->SetToolTip(seam_tip);
    m_connect            = new ::CheckBox(this);
    auto *connect_label  = text(_L("Connect islands"), c.ink);
    const wxString connect_tip = _L("Lay the unwrap out as a connected net: pieces that share an edge are unfolded next to each "
                                    "other (a cube becomes a joined net rather than six loose squares). They stay separate "
                                    "islands, so you can still move any of them by hand afterwards.");
    m_connect->SetToolTip(connect_tip);
    connect_label->SetToolTip(connect_tip);
    settings->Add(seam_label, 0, wxALIGN_CENTER_VERTICAL);
    settings->Add(m_seam_angle, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, gap);
    settings->AddStretchSpacer();
    settings->Add(m_connect, 0, wxALIGN_CENTER_VERTICAL);
    settings->Add(connect_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));

    auto      *strip = new wxBoxSizer(wxVERTICAL);
    const auto tool  = [&](int id, const char *icon, const wxString &tip, bool toggle) {
        auto *b = new UVToolButton(this, id, icon, wxEmptyString, tip, toggle);
        strip->Add(b, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(4));
        return b;
    };
    const auto strip_rule = [&]() {
        auto *r = rule(wxSize(FromDIP(20), 1));
        strip->Add(r, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(11));
        strip->AddSpacer(FromDIP(7));
    };
    m_select[0] = tool(ID_UV_SELECT_ISLAND, "texture_displacement_uv_select_island",
                       _L("Island - work on whole islands. Click one to select it, then drag to move it, right-drag to "
                          "rotate it, or press R to rotate and S to scale with the mouse (click or Enter to confirm, Esc "
                          "to cancel)."),
                       true);
    m_select[1] = tool(ID_UV_SELECT_VERTEX, "texture_displacement_uv_select_vertex",
                       _L("Vertex - drag vertices to reshape an island by hand; Shift adds to the selection, Ctrl "
                          "toggles one in or out of it."),
                       true);
    m_select[2] = tool(ID_UV_SELECT_EDGE, "texture_displacement_uv_select_edge",
                       _L("Edge - drag edges to reshape an island by hand; Shift adds to the selection, Ctrl toggles "
                          "one in or out of it."),
                       true);
    strip_rule();
    m_mark_seams = tool(ID_UV_MARK_SEAMS, "texture_displacement_uv_seam",
                        _L("Mark seams - click edges on the model to cut the unwrap along them. The edge under the cursor is "
                           "highlighted yellow; click to mark it red, click a red edge again to unmark it. Painting is paused "
                           "while this is on."),
                        true);
    m_seam_path = tool(ID_UV_SEAM_PATH, "texture_displacement_uv_path",
                       _L("Path - instead of clicking every edge, click a start point and then an end point: the whole "
                          "shortest path between them is seamed at once. Available while marking seams."),
                       true);
    m_clear_seams = tool(ID_UV_CLEAR_SEAMS, "texture_displacement_cross", _L("Clear seams - remove every seam marked on this layer"), false);
    strip_rule();
    m_avg_scale = tool(ID_UV_AVG_SCALE, "texture_displacement_uv_avg_scale", _L("Average scale - give every island the same texel density"), false);
    m_cut       = tool(ID_UV_CUT, "texture_displacement_uv_cut", _L("Cut - split the selected island across its long axis"), false);
    m_join      = tool(ID_UV_JOIN, "texture_displacement_uv_join", _L("Join - unfold the selected island onto its nearest neighbour along their shared edge"), false);
    m_unjoin    = tool(ID_UV_UNJOIN, "texture_displacement_uv_unjoin", _L("Unjoin - send the selected island back to its own packed position"), false);
    strip->AddStretchSpacer();
    m_clear_edits = tool(ID_UV_CLEAR_EDITS, "texture_displacement_uv_clear_edits",
                         _L("Clear UV edits - discard all manual vertex/edge moves and return the unwrap to its automatic shape"), false);
    m_snap  = tool(ID_UV_SNAP, "texture_displacement_uv_snap", _L("Snap - stick islands together when dragging one against another"), true);
    m_frame = tool(ID_UV_FRAME, "texture_displacement_uv_frame",
                   _L("Frame all islands, fitting every one of them in view (Home or F).\n"
                      "Elsewhere on the canvas: scroll to zoom around the cursor, and middle-drag - or drag empty space - "
                      "to pan."),
                   false);
    strip->AddSpacer(FromDIP(4));

    m_canvas   = new UVEditorCanvas(this);
    auto *body = new wxBoxSizer(wxHORIZONTAL);
    body->Add(strip, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(4));
    body->Add(rule(wxSize(1, -1)), 0, wxEXPAND);
    body->Add(m_canvas, 1, wxEXPAND);

    m_status = text(wxEmptyString, c.dim, wxST_ELLIPSIZE_END);
    m_status->SetToolTip(_L("What is selected, and the exact figures of the move, rotation or scale while you drag one."));
    m_status->SetMinSize(wxSize(FromDIP(40), -1));
    m_stats      = text(wxEmptyString, c.dim);
    auto *status = new wxBoxSizer(wxHORIZONTAL);
    status->Add(m_status, 1, wxALIGN_CENTER_VERTICAL);
    status->Add(m_stats, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, gap);

    auto *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(header, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
    sizer->Add(settings, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);
    sizer->Add(rule(wxSize(-1, 1)), 0, wxEXPAND | wxTOP, gap);
    sizer->Add(body, 1, wxEXPAND);
    sizer->Add(rule(wxSize(-1, 1)), 0, wxEXPAND);
    sizer->Add(status, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(5));
    SetSizer(sizer);

    Bind(wxEVT_BUTTON, &UVEditorPanel::on_tool, this);
    m_seam_angle->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent &) {
        const int value = m_seam_angle->GetValue();
        if (value != int(std::lround(m_canvas->pane_state().seam_angle_deg)))
            m_canvas->run_command(UVEditorCanvas::Command::SetSeamAngle, float(value));
    });
    const auto send_connect = [this]() {
        if (m_connect->GetValue() != m_canvas->pane_state().connect_islands)
            m_canvas->run_command(UVEditorCanvas::Command::SetConnectIslands, m_connect->GetValue() ? 1.f : 0.f);
    };
    m_connect->Bind(wxEVT_TOGGLEBUTTON, [send_connect](wxCommandEvent &e) {
        send_connect();
        e.Skip();
    });
    connect_label->Bind(wxEVT_LEFT_DOWN, [this, send_connect](wxMouseEvent &) {
        if (!m_connect->IsEnabled())
            return;
        m_connect->SetValue(!m_connect->GetValue());
        send_connect();
    });

    m_canvas->set_status_callback([this](const wxString &label) {
        if (m_status != nullptr && m_status->GetLabel() != label) {
            m_status->SetLabel(label);
            m_status->Refresh();
            refresh_selection_tools();
        }
    });
    m_canvas->set_pane_state_callback([this](const UVEditorCanvas::PaneState &state) { apply_state(state); });
    apply_state(m_canvas->pane_state());

    Bind(wxEVT_SHOW, [this](wxShowEvent &e) {
        e.Skip();
        if (e.IsShown())
            m_canvas->invalidate_gl();
    });
}

void UVEditorPanel::apply_state(const UVEditorCanvas::PaneState &s)
{
    const wxString name = s.has_layer ? s.layer_name : _L("No layer mapped with Unwrap");
    const wxString tile = s.has_layer ? wxString::Format(_L("%.1f mm tile"), s.tile_mm) : wxString();
    const bool     relayout = m_layer_name->GetLabel() != name || m_tile->GetLabel() != tile;
    m_layer_name->SetLabel(name);
    m_tile->SetLabel(tile);

    if (s.thumbnail_px > 0 && s.thumbnail_rgb.size() == size_t(s.thumbnail_px) * size_t(s.thumbnail_px) * 3) {
        const double scale = GetContentScaleFactor();
        const int    px    = std::max(1, int(std::lround(FromDIP(20) * scale)));
        wxImage      image(s.thumbnail_px, s.thumbnail_px, false);
        std::copy(s.thumbnail_rgb.begin(), s.thumbnail_rgb.end(), image.GetData());
        m_thumb->SetBitmap(wxBitmap(image.Scale(px, px, wxIMAGE_QUALITY_HIGH), -1, scale));
    } else {
        m_thumb->SetBitmap(wxNullBitmap);
    }
    const wxString no_layer = s.has_layer ? wxString() :
                                            _L("The pane follows the active texture layer, and that layer has to be mapped "
                                               "with Unwrap (LSCM). Add a layer and set its Mapping to Unwrap.");
    m_thumb->SetUnavailable(no_layer);
    m_layer_name->SetToolTip(s.has_layer ? m_thumb->GetToolTipText() : no_layer);

    for (int i = 0; i < 3; ++i) {
        m_background[i]->SetValue(int(s.background) == i);
        m_background[i]->SetUnavailable(no_layer);
    }
    m_unwrap->SetUnavailable(no_layer);
    m_unwrap->SetBadge(s.unwrap_stale);
    m_unwrap->SetTip(s.unwrap_stale ?
                         _L("Out of date - the paint, the seams or the seam angle changed since this unwrap was made. "
                            "Press to unwrap again.") :
                         _L("Flatten the painted area into UV islands. It is computed only when you press this, not on "
                            "every edit - so paint, change the seam angle or mark seams first, then press Unwrap."));

    if (m_seam_angle->GetValue() != int(std::lround(s.seam_angle_deg)))
        m_seam_angle->SetValue(int(std::lround(s.seam_angle_deg)));
    m_seam_angle->Enable(s.has_layer);
    m_connect->SetValue(s.connect_islands);
    m_connect->Enable(s.has_layer);

    m_mark_seams->SetValue(s.mark_seams);
    m_mark_seams->SetUnavailable(no_layer);
    m_seam_path->SetValue(s.seam_path);
    m_seam_path->SetUnavailable(!no_layer.empty() ? no_layer :
                                s.mark_seams     ? wxString() :
                                                   _L("Turn Mark seams on first - Path is a quicker way of marking them."));
    m_clear_seams->SetUnavailable(!no_layer.empty() ? no_layer :
                                  s.has_seams      ? wxString() :
                                                     _L("No seams are marked on this layer."));
    m_clear_edits->SetUnavailable(!no_layer.empty() ? no_layer :
                                  s.has_uv_edits   ? wxString() :
                                                     _L("No islands have been reshaped by hand, so there is nothing to "
                                                        "discard."));

    m_stats->SetLabel(s.unwrapped ? wxString::Format(_L("%d islands, %s faces"), s.island_count,
                                                     wxString(std::to_string(s.face_count))) :
                                    wxString());
    m_stats->SetToolTip(s.unwrapped ? _L("How the painted area came out of the unwrap: the number of separate pieces it "
                                         "was cut into (at the seams and at edges sharper than the seam angle), and how "
                                         "many triangles they hold in total.") :
                                      wxString());
    refresh_selection_tools();
    if (relayout)
        Layout();
}

void UVEditorPanel::refresh_selection_tools()
{
    const bool has_islands = m_canvas->has_islands();
    const int  mode        = int(m_canvas->select_mode());
    const wxString not_unwrapped = has_islands ? wxString() : _L("Press Unwrap first - there are no islands to work on yet.");
    for (int i = 0; i < 3; ++i) {
        m_select[i]->SetValue(i == mode);
        m_select[i]->SetUnavailable(not_unwrapped);
    }
    const bool island_picked = has_islands && m_canvas->select_mode() == UVEditorCanvas::SelectMode::Island &&
                               m_canvas->selected_island() >= 0;
    const wxString no_island = !not_unwrapped.empty() ? not_unwrapped :
                               island_picked          ? wxString() :
                                                        _L("Click an island on the canvas first, in Island mode.");
    m_avg_scale->SetUnavailable(not_unwrapped);
    m_cut->SetUnavailable(no_island);
    m_join->SetUnavailable(no_island);
    m_unjoin->SetUnavailable(no_island);
    m_snap->SetUnavailable(not_unwrapped);
    m_snap->SetValue(m_canvas->snap_enabled());
    m_frame->SetUnavailable(not_unwrapped);
}

void UVEditorPanel::on_tool(wxCommandEvent &evt)
{
    using Command = UVEditorCanvas::Command;
    const int  id = evt.GetId();
    const bool on = evt.GetInt() != 0;
    switch (id) {
    case ID_UV_FRAME:       m_canvas->run_command(Command::FrameAll); break;
    case ID_UV_SNAP:        m_canvas->run_command(Command::ToggleSnap); break;
    case ID_UV_AVG_SCALE:   m_canvas->run_command(Command::AverageScale); break;
    case ID_UV_CUT:         m_canvas->run_command(Command::CutSelectedIsland); break;
    case ID_UV_JOIN:        m_canvas->run_command(Command::JoinSelected); break;
    case ID_UV_UNJOIN:      m_canvas->run_command(Command::UnjoinSelected); break;
    case ID_UV_UNWRAP:      m_canvas->run_command(Command::Unwrap); break;
    case ID_UV_MARK_SEAMS:  m_canvas->run_command(Command::SetMarkSeams, on ? 1.f : 0.f); break;
    case ID_UV_SEAM_PATH:   m_canvas->run_command(Command::SetSeamPath, on ? 1.f : 0.f); break;
    case ID_UV_CLEAR_SEAMS: m_canvas->run_command(Command::ClearSeams); break;
    case ID_UV_CLEAR_EDITS: m_canvas->run_command(Command::ClearUVEdits); break;
    case ID_UV_PICK_TEXTURE: m_canvas->run_command(Command::PickTexture); break;
    case ID_UV_SELECT_ISLAND:
    case ID_UV_SELECT_VERTEX:
    case ID_UV_SELECT_EDGE: m_canvas->run_command(Command::SetSelectMode, float(id - ID_UV_SELECT_ISLAND)); break;
    case ID_UV_BG_HEIGHT:
    case ID_UV_BG_CHECKER:
    case ID_UV_BG_DISTORTION:
        for (int i = 0; i < 3; ++i)
            m_background[i]->SetValue(i == id - ID_UV_BG_HEIGHT);
        m_canvas->run_command(Command::SetBackground, float(id - ID_UV_BG_HEIGHT));
        break;
    default: evt.Skip(); return;
    }
    refresh_selection_tools();
}

}
