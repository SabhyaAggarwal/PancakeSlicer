#include "GLGizmoTextureDisplacement.hpp"

#include <Eigen/Core>
#include <boost/filesystem.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>

#include "ColorSpaceConvert.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/FilamentMixer.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/format.hpp"

#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/CameraUtils.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/GuiColor.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Shortcuts.hpp"
#include "slic3r/GUI/TextureLibrary.hpp"
#include "slic3r/GUI/TextureProjectorFrame.hpp"
#include "slic3r/GUI/UVEditorCanvas.hpp"
#include "slic3r/GUI/Jobs/TextureDisplacementBakeJob.hpp"
#include "slic3r/GUI/Jobs/TextureDisplacementPrepareJob.hpp"
#include "slic3r/GUI/Jobs/TextureDisplacementDebugJob.hpp"
#include "slic3r/GUI/Jobs/TextureDisplacementPreviewJob.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "GLGizmoUtils.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <chrono>
#include <cstdio>
#include <glad/gl.h>
#include <imgui.h>
#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include <memory>
#include "slic3r/GUI/GLTexture.hpp"
#include "libslic3r/TextureDisplacement.hpp"
#include <string>
#include "slic3r/GUI/Gizmos/GLGizmoPainterBase.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GLShader.hpp"
#include <math.h>
#include "slic3r/GUI/Gizmos/GLGizmoBase.hpp"
#include "slic3r/GUI/Jobs/Worker.hpp"
#include "libslic3r/Geometry.hpp"
#include "slic3r/GUI/Event.hpp"
#include "slic3r/GUI/IconManager.hpp"
#include <optional>
#include "libslic3r/Config.hpp"
#include "libslic3r/TextureBake/TextureBakeDebug.hpp"
#include <ratio>
#include <tbb/parallel_for.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <vector>
#include <utility>
#include <wx/gdicmn.h>
#include <wx/busycursor.h>
#include <wx/string.h>
#include <wx/filedlg.h>

namespace Slic3r::GUI {

namespace {
constexpr float ImGuiLogSlider = float(ImGuiSliderFlags_Logarithmic);

constexpr int THUMBNAIL_MAX_PX = 128;

constexpr int HEIGHT_TEX_MAX_PX = 2048;

constexpr int BORDER_BAND_RINGS = 2;

bool point_in_triangle_coplanar(const Vec3f &p, const std::array<Vec3f, 3> &t)
{
    const Vec3f n = (t[1] - t[0]).cross(t[2] - t[0]);
    const float n2 = n.squaredNorm();
    if (n2 < 1e-20f)
        return false;
    const float eps = -1e-4f * n2;
    return (t[1] - t[0]).cross(p - t[0]).dot(n) >= eps &&
           (t[2] - t[1]).cross(p - t[1]).dot(n) >= eps &&
           (t[0] - t[2]).cross(p - t[2]).dot(n) >= eps;
}

TriangleSelector::TriangleSplittingData remap_texture_paint_spatial(
    const TriangleMesh &src_mesh, const TriangleSelector::TriangleSplittingData &src_data,
    const AABBTreeIndirect::Tree3f &src_tree, const TriangleMesh &dst_mesh, const Vec3f &dst_to_src)
{
    const indexed_triangle_set &src  = src_mesh.its;
    const indexed_triangle_set &dst  = dst_mesh.its;
    const size_t                ntri = src.indices.size();
    if (src_data.bitstream.empty() || ntri == 0 || dst.indices.empty() || src_tree.empty())
        return {};

    TriangleSelector src_sel(src_mesh);
    src_sel.deserialize(src_data, false);
    std::vector<int>           piece_src;
    const indexed_triangle_set patch = src_sel.get_facets_strict(EnforcerBlockerType::ENFORCER, &piece_src);
    if (patch.indices.empty())
        return {};

    const auto tri_area2 = [](const Vec3f &a, const Vec3f &b, const Vec3f &c) {
        return (b - a).cross(c - a).norm();
    };
    std::vector<float> src_area2(ntri, 0.f), covered2(ntri, 0.f);
    for (size_t i = 0; i < ntri; ++i)
        src_area2[i] = tri_area2(src.vertices[size_t(src.indices[i][0])], src.vertices[size_t(src.indices[i][1])],
                                 src.vertices[size_t(src.indices[i][2])]);
    for (size_t j = 0; j < patch.indices.size() && j < piece_src.size(); ++j) {
        if (size_t(piece_src[j]) >= ntri)
            continue;
        const stl_triangle_vertex_indices &t = patch.indices[j];
        covered2[size_t(piece_src[j])] += tri_area2(patch.vertices[size_t(t[0])], patch.vertices[size_t(t[1])],
                                                    patch.vertices[size_t(t[2])]);
    }
    std::vector<uint8_t> full(ntri, 0);
    for (size_t i = 0; i < ntri; ++i)
        full[i] = (src_area2[i] > 0.f && covered2[i] >= 0.999f * src_area2[i]) ? 1 : 0;

    std::vector<int> part_start(ntri + 1, 0);
    for (size_t j = 0; j < patch.indices.size() && j < piece_src.size(); ++j)
        if (size_t(piece_src[j]) < ntri && !full[size_t(piece_src[j])])
            ++part_start[size_t(piece_src[j]) + 1];
    for (size_t i = 0; i < ntri; ++i)
        part_start[i + 1] += part_start[i];
    std::vector<std::array<Vec3f, 3>> part;
    part.resize(size_t(part_start[ntri]));
    {
        std::vector<int> fill(part_start.begin(), part_start.begin() + ntri);
        for (size_t j = 0; j < patch.indices.size() && j < piece_src.size(); ++j) {
            const size_t S = size_t(piece_src[j]);
            if (S >= ntri || full[S])
                continue;
            const stl_triangle_vertex_indices &t = patch.indices[j];
            part[size_t(fill[S]++)] = { patch.vertices[size_t(t[0])], patch.vertices[size_t(t[1])],
                                        patch.vertices[size_t(t[2])] };
        }
    }

    std::vector<uint8_t> painted(dst.indices.size(), 0);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, dst.indices.size()),
                      [&](const tbb::blocked_range<size_t> &range) {
        for (size_t i = range.begin(); i < range.end(); ++i) {
            const stl_triangle_vertex_indices &t = dst.indices[i];
            const Vec3f centroid = (dst.vertices[size_t(t[0])] + dst.vertices[size_t(t[1])] +
                                    dst.vertices[size_t(t[2])]) / 3.f + dst_to_src;
            size_t hit     = 0;
            Vec3f  hit_pos = Vec3f::Zero();
            if (AABBTreeIndirect::squared_distance_to_indexed_triangle_set(src.vertices, src.indices, src_tree,
                                                                          centroid, hit, hit_pos) < 0.f ||
                hit >= ntri)
                continue;
            if (full[hit]) {
                painted[i] = 1;
                continue;
            }
            for (int k = part_start[hit]; k < part_start[hit + 1]; ++k)
                if (point_in_triangle_coplanar(hit_pos, part[size_t(k)])) {
                    painted[i] = 1;
                    break;
                }
        }
    });

    TriangleSelector dst_sel(dst_mesh);
    for (size_t i = 0; i < painted.size(); ++i)
        if (painted[i])
            dst_sel.set_facet(int(i), EnforcerBlockerType::ENFORCER);
    return dst_sel.serialize();
}

constexpr int PALETTE_LUT_EDGE = 24;

constexpr int PALETTE_MAX_ENTRIES = 64;

constexpr float PREFER_PURE_DE = 2.f;

constexpr size_t MIX_TARGET_SAMPLES = size_t(1) << 20;
constexpr size_t MIX_TARGET_BINS    = 256;
constexpr float MIN_MIX_GAIN = 0.05f;

std::vector<std::string> mixed_slot_signature(const DynamicPrintConfig &project_config)
{
    std::vector<std::string> out;
    const auto *is_mixed = project_config.option<ConfigOptionBools>("filament_is_mixed");
    const auto *comps    = project_config.option<ConfigOptionStrings>("filament_mixed_components");
    const auto *ratios   = project_config.option<ConfigOptionStrings>("filament_mixed_sublayer_ratios");
    const auto *gradient = project_config.option<ConfigOptionBools>("filament_mixed_gradient");
    if (is_mixed == nullptr || comps == nullptr || ratios == nullptr)
        return out;
    for (size_t i = 0; i < is_mixed->values.size(); ++i)
        if (is_mixed->values[i])
            out.push_back(std::to_string(i) + ':' + (i < comps->values.size() ? comps->values[i] : std::string()) + '|' +
                          (i < ratios->values.size() ? ratios->values[i] : std::string()) + '|' +
                          (gradient != nullptr && i < gradient->values.size() && gradient->values[i] ? "g" : ""));
    return out;
}

bool same_palette(const std::vector<PrintableColor> &l, const std::vector<PrintableColor> &r)
{
    return std::equal(l.begin(), l.end(), r.begin(), r.end(), [](const PrintableColor &x, const PrintableColor &y) {
        return x.a == y.a && x.b == y.b && x.num == y.num && x.den == y.den && x.rgb == y.rgb;
    });
}

static Vec3f srgb_to_lab(const Vec3f &c)
{
    const auto lin = [](float v) { return v > 0.04045f ? std::pow((v + 0.055f) / 1.055f, 2.4f) : v / 12.92f; };
    const float r = lin(c.x()), g = lin(c.y()), b = lin(c.z());
    const float x = (0.4124f * r + 0.3576f * g + 0.1805f * b) / 0.95047f;
    const float y = (0.2126f * r + 0.7152f * g + 0.0722f * b);
    const float z = (0.0193f * r + 0.1192f * g + 0.9505f * b) / 1.08883f;
    const auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.f / 116.f; };
    const float fx = f(x), fy = f(y), fz = f(z);
    return Vec3f(116.f * fy - 16.f, 500.f * (fx - fy), 200.f * (fy - fz));
}

static Vec3f lab_to_srgb(const Vec3f &lab)
{
    const float fy = (lab.x() + 16.f) / 116.f, fx = lab.y() / 500.f + fy, fz = fy - lab.z() / 200.f;
    const auto  finv = [](float t) { return t > 0.206893f ? t * t * t : (t - 16.f / 116.f) / 7.787f; };
    const float x = finv(fx) * 0.95047f, y = finv(fy), z = finv(fz) * 1.08883f;
    const float r = 3.2406f * x - 1.5372f * y - 0.4986f * z;
    const float g = -0.9689f * x + 1.8758f * y + 0.0415f * z;
    const float b = 0.0557f * x - 0.2040f * y + 1.0570f * z;
    const auto gam = [](float v) {
        v = std::clamp(v, 0.f, 1.f);
        return v > 0.0031308f ? 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f : 12.92f * v;
    };
    return Vec3f(gam(r), gam(g), gam(b));
}

std::unique_ptr<GLTexture> upload_height_thumbnail(const DecodedHeightTexture &decoded, int max_px = THUMBNAIL_MAX_PX)
{
    if (decoded.empty())
        return nullptr;

    const int scale = std::max(1, (std::max(decoded.width, decoded.height) + max_px - 1) / max_px);
    const int w     = std::max(1, decoded.width / scale);
    const int h     = std::max(1, decoded.height / scale);

    const bool                 color = decoded.has_color();
    std::vector<unsigned char> rgba(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int x0 = x * decoded.width / w, x1 = std::max(x0 + 1, (x + 1) * decoded.width / w);
            const int y0 = y * decoded.height / h, y1 = std::max(y0 + 1, (y + 1) * decoded.height / h);
            unsigned int sum[3] = { 0, 0, 0 };
            unsigned int n      = 0;
            for (int sy = y0; sy < y1 && sy < decoded.height; ++sy)
                for (int sx = x0; sx < x1 && sx < decoded.width; ++sx, ++n) {
                    const size_t si = size_t(sy) * size_t(decoded.width) + size_t(sx);
                    for (int c = 0; c < 3; ++c)
                        sum[c] += color ? decoded.rgb[si * 3 + size_t(c)] : decoded.pixels[si];
                }

            const size_t di = (size_t(y) * size_t(w) + size_t(x)) * 4;
            for (int c = 0; c < 3; ++c)
                rgba[di + size_t(c)] = (n > 0) ? static_cast<unsigned char>(sum[c] / n) : 0;
            rgba[di + 3] = 255;
        }

    auto texture = std::make_unique<GLTexture>();
    if (!texture->load_from_raw_data(std::move(rgba), (unsigned int) w, (unsigned int) h, false,  false))
        return nullptr;
    return texture;
}

std::unique_ptr<GLTexture> upload_color_texture(const DecodedHeightTexture &decoded, int max_px)
{
    if (!decoded.has_color())
        return nullptr;

    const int scale = std::max(1, (std::max(decoded.width, decoded.height) + max_px - 1) / max_px);
    const int w     = std::max(1, decoded.width / scale);
    const int h     = std::max(1, decoded.height / scale);

    std::vector<unsigned char> rgba(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int x0 = x * decoded.width / w, x1 = std::max(x0 + 1, (x + 1) * decoded.width / w);
            const int y0 = y * decoded.height / h, y1 = std::max(y0 + 1, (y + 1) * decoded.height / h);
            unsigned int sum[3] = { 0, 0, 0 };
            unsigned int n      = 0;
            for (int sy = y0; sy < y1 && sy < decoded.height; ++sy)
                for (int sx = x0; sx < x1 && sx < decoded.width; ++sx, ++n) {
                    const size_t si = (size_t(sy) * size_t(decoded.width) + size_t(sx)) * 3;
                    for (int c = 0; c < 3; ++c)
                        sum[c] += decoded.rgb[si + size_t(c)];
                }
            const size_t di = (size_t(y) * size_t(w) + size_t(x)) * 4;
            for (int c = 0; c < 3; ++c)
                rgba[di + size_t(c)] = (n > 0) ? static_cast<unsigned char>(sum[size_t(c)] / n) : 0;
            rgba[di + 3] = 255;
        }

    auto texture = std::make_unique<GLTexture>();
    if (!texture->load_from_raw_data(std::move(rgba), (unsigned int) w, (unsigned int) h, false, false))
        return nullptr;
    return texture;
}

bool ray_plane_hit(const Camera &camera, const Vec2d &mouse_pos, const Transform3d &trafo,
                    const Vec3f &plane_point_local, const Vec3f &plane_normal_local, Vec3d &out_world_hit)
{
    Vec3d ray_origin, ray_dir;
    CameraUtils::ray_from_screen_pos(camera, mouse_pos, ray_origin, ray_dir);

    const Vec3d plane_point_world  = trafo * plane_point_local.cast<double>();
    const Vec3d plane_normal_world = (trafo.matrix().block(0, 0, 3, 3).inverse().transpose() * plane_normal_local.cast<double>()).normalized();

    const double denom = ray_dir.dot(plane_normal_world);
    if (std::abs(denom) < 1e-8)
        return false;

    const double t = (plane_point_world - ray_origin).dot(plane_normal_world) / denom;
    if (t < 0.0)
        return false;

    out_world_hit = ray_origin + ray_dir * t;
    return true;
}
}

GLGizmoTextureDisplacement::GLGizmoTextureDisplacement(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoPainterBase(parent, icon_filename, sprite_id)
{
}

bool GLGizmoTextureDisplacement::on_init()
{
    m_shortcut = Shortcut::GizmoDisplacement;
    const wxString ctrl  = GUI::shortkey_ctrl_prefix();
    const wxString alt   = GUI::shortkey_alt_prefix();
    const wxString shift = GUI::shortkey_shift_prefix();


    m_desc["cursor_size"]   = _L("Brush size");
    m_desc["circle"]        = _L("Circle");
    m_desc["sphere"]        = _L("Sphere");
    m_desc["remove_layer"]  = _L("Remove");
    m_desc["bake"]          = _L_CONTEXT("Bake", "Texture Displacement");


    m_desc["paint"]            = _L("Paint");
    m_desc["erase"]            = _L("Erase");
    m_desc["gap_area"]         = _L("Gap area");
    m_desc["smart_fill_angle"] = _L("Smart fill angle");
    m_desc["toggle_wireframe"] = _L("Toggle Wireframe");

    std::pair<wxString, wxString> paint_shortcut            = {_L("Left mouse button"),         m_desc["paint"]};
    std::pair<wxString, wxString> erase_shortcut            = {shift + _L("Left mouse button"), m_desc["erase"]};
    std::pair<wxString, wxString> toggle_wireframe_shortcut = {alt + shift + _L_CONTEXT("Enter", "Keyboard Shortcut"), m_desc["toggle_wireframe"]};

    m_shortcuts_brush = {
        paint_shortcut,
        erase_shortcut,
        {ctrl + _L("Mouse wheel"), m_desc["cursor_size"]},
        toggle_wireframe_shortcut
    };

    m_shortcuts_bucket_fill = {
        paint_shortcut,
        erase_shortcut,
        {ctrl + _L("Mouse wheel"), m_desc["smart_fill_angle"]},
        toggle_wireframe_shortcut
    };

    m_shortcuts_gap_fill = {
        {ctrl + _L("Mouse wheel"), m_desc["gap_area"]},
        toggle_wireframe_shortcut
    };
 
    return true;
}

std::string GLGizmoTextureDisplacement::on_get_name() const
{
    return _u8L("Texture displacement");
}

bool GLGizmoTextureDisplacement::on_is_activable() const
{
    return m_parent.get_canvas_type() != GLCanvas3D::CanvasAssembleView && GLGizmoPainterBase::on_is_activable();
}

void GLGizmoTextureDisplacement::on_shutdown()
{
    m_parent.toggle_model_objects_visibility(true);
    m_preview_glmodel.reset();
    m_shaded_preview_glmodel.reset();
    m_paint_overlay_glmodel.reset();
    m_paint_overlay_dirty = false;
    m_painted_colors = PaintedColors{};
    m_painted_colors_key.clear();
    m_painted_colors_glmodel.reset();
    m_painted_colors_drawn_key.clear();
    m_painted_colors_runs.clear();
    m_seed_fill_last_mesh_id = -1;
    m_preview_generation->fetch_add(1);
    m_preview_job_pending = false;
    m_palette_cache.clear();
    m_palette_quantizer      = nullptr;
    m_palette_pure_quantizer = nullptr;
    m_palette_filaments.clear();
    m_palette_images.clear();
    m_mix_ranking.reset();
    m_palette_slots.clear();
    m_palette_changed = false;
    m_uvcheck_glmodel.reset();
    m_wireframe_overlay_glmodel.reset();
    m_wireframe_overlay_vcount = 0;
    m_seam_glmodel.reset();
    m_seam_hover_glmodel.reset();
    m_seam_hover_edge     = { -1, -1 };
    m_seam_hover_vertex   = -1;
    m_seam_anchor_glmodel.reset();
    m_seam_path_mode      = false;
    m_seam_path_anchor    = -1;
    m_subdivide_editing       = false;
    m_subdivide_preview_tris = -1;
    m_subdivide_preview_glmodel.reset();
    m_shaded_active_chart  = -1;
    m_shaded_active_face.clear();
    m_shaded_island_delta  = Eigen::Matrix<float, 2, 3>::Identity();
    m_island_drag_active = false;
    m_island_move_set.clear();
    m_adjust_texture_mode = false;
    m_seam_edit_mode      = false;
    m_adjust_drag_handle  = AdjustHandle::None;
    m_adjust_anchor_valid = false;
    m_show_uv_editor = false;
    wxGetApp().plater()->show_uv_editor(false);

    if (m_projector_frame != nullptr) {
        m_projector_frame->Destroy();
        m_projector_frame = nullptr;
    }
    m_projector_tex_source    = nullptr;
    m_projector_tex_smoothing = -1.f;
}

PainterGizmoType GLGizmoTextureDisplacement::get_painter_type() const
{
    return PainterGizmoType::TEXTURE_DISPLACEMENT;
}

wxString GLGizmoTextureDisplacement::handle_snapshot_action_name(bool shift_down, GLGizmoPainterBase::Button button_down) const
{
    const bool erasing = shift_down || (m_erase_mode != (button_down == GLGizmoPainterBase::Button::Right));
    return erasing ? _L("Erase texture displacement paint") : _L("Paint texture displacement");
}

void GLGizmoTextureDisplacement::render_painter_gizmo()
{
    const Selection &selection = m_parent.get_selection();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    if (m_use_shaded_preview && m_shaded_preview_dirty) {
        rebuild_shaded_preview_mesh();
        m_shaded_preview_dirty = false;
    }
    if (m_paint_overlay_dirty) {
        rebuild_paint_overlay();
        m_paint_overlay_dirty = false;
    }
    rebuild_other_paint_overlay();
    const bool use_shaded = m_use_shaded_preview && m_shaded_preview_glmodel.is_initialized() && shaded_preview_ready();
    const bool use_true_preview = !use_shaded && m_uv_check_mode == UVCheckMode::None &&
                                  m_preview_glmodel.is_initialized();
    const bool show_paint_overlay = m_uv_check_mode == UVCheckMode::None;

    ModelVolume *mv = texture_volume();
    m_parent.toggle_model_objects_visibility(true);
    if ((use_shaded || use_true_preview) && mv != nullptr)
        m_parent.toggle_model_objects_visibility(false, m_c->selection_info()->model_object(),
                                                  m_c->selection_info()->get_active_instance(), mv);

    const bool paint_feedback = is_painting() || m_debug_stage >= 0;

    bool textured_selector_drawn = true;
    if (use_shaded) {
        render_shaded_preview_mesh();
        render_triangles(selection, mv);
        textured_selector_drawn = false;
    } else if (use_true_preview) {
        render_preview_mesh();
        glsafe(::glEnable(GL_POLYGON_OFFSET_FILL));
        glsafe(::glPolygonOffset(-1.0f, -1.0f));
        render_triangles(selection, paint_feedback ? nullptr : mv);
        glsafe(::glDisable(GL_POLYGON_OFFSET_FILL));
        textured_selector_drawn = paint_feedback;
    } else {
        render_triangles(selection);
    }

    const bool painted_colors_drawn = show_paint_overlay && m_debug_stage < 0 && render_painted_colors(use_true_preview);

    if (show_paint_overlay)
        render_paint_overlay(m_other_paint_glmodel);

    const bool preview_drawn = use_shaded || use_true_preview;
    if (show_paint_overlay && (preview_drawn ? paint_feedback : painted_colors_drawn && is_painting()))
        render_paint_overlay(m_paint_overlay_glmodel);

    const bool fill_tool = m_tool_type == ToolType::SMART_FILL || m_tool_type == ToolType::BUCKET_FILL ||
                           (m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::POINTER);
    const int  textured_mesh_id = texture_volume_raycaster_index();
    if (painted_colors_drawn && textured_selector_drawn && fill_tool && textured_mesh_id >= 0 &&
        textured_mesh_id == m_seed_fill_last_mesh_id && size_t(textured_mesh_id) < m_triangle_selectors.size()) {
        const ModelObject *mo = m_c->selection_info()->model_object();
        GLint depth_func = GL_LESS;
        glsafe(::glGetIntegerv(GL_DEPTH_FUNC, &depth_func));
        glsafe(::glDepthFunc(GL_LEQUAL));
        m_triangle_selectors[size_t(textured_mesh_id)]->render_paint_contour(
            mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix());
        glsafe(::glDepthFunc(GLenum(depth_func)));
    }

    {
        const TextureDisplacementLayer *al = active_layer();
        const UVEditorCanvas           *uv_canvas = wxGetApp().plater()->get_uv_editor_canvas();
        if (m_show_uv_editor && al != nullptr && al->projection_method == TextureProjectionMethod::LSCM &&
            uv_canvas != nullptr && !m_uv_editor_unwrap.empty()) {
            if (uv_canvas->selected_islands() != m_island_overlay_selection)
                rebuild_island_overlay(uv_canvas->selected_islands());
            render_island_overlay();
        } else if (m_island_overlay_glmodel.is_initialized()) {
            m_island_overlay_glmodel.reset();
            m_island_overlay_selection.clear();
        }
    }

    if (m_uv_check_mode != UVCheckMode::None)
        render_uvcheck_mesh();
    if (m_subdivide_editing)
        render_subdivide_preview();
    else if (m_wireframe_overlay)
        render_wireframe_overlay();
    render_seam_overlay();

    m_c->object_clipper()->render_cut();
    m_c->instances_hider()->render_cut();
    if (m_adjust_texture_mode)
        render_adjust_texture_gizmo();
    else if (!m_seam_edit_mode)
        render_cursor();

    glsafe(::glDisable(GL_BLEND));
}

bool GLGizmoTextureDisplacement::on_mouse(const wxMouseEvent &mouse_event)
{
    if (m_seam_edit_mode)
        return on_mouse_seam(mouse_event);
    if (m_adjust_texture_mode)
        return on_mouse_adjust_texture(mouse_event);

    const bool handled = GLGizmoPainterBase::on_mouse(mouse_event);
    if (handled && (mouse_event.Dragging() || mouse_event.LeftDown() || mouse_event.RightDown() ||
                    mouse_event.LeftUp() || mouse_event.RightUp()))
        m_paint_overlay_dirty = true;
    return handled;
}

bool GLGizmoTextureDisplacement::on_mouse_seam(const wxMouseEvent &mouse_event)
{
    if (mouse_event.CmdDown())
        return false;

    if (mouse_event.LeftDown()) {
        const Vec2d pos(mouse_event.GetX(), mouse_event.GetY());
        if (m_seam_path_mode) {
            const int v = seam_vertex_at(pos);
            if (v >= 0) {
                if (m_seam_path_anchor < 0)
                    m_seam_path_anchor = v;
                else {
                    mark_seam_path(m_seam_path_anchor, v);
                    m_seam_path_anchor = v;
                }
                rebuild_seam_anchor_overlay();
                m_parent.set_as_dirty();
            }
        } else {
            toggle_seam_at(pos);
        }
        return true;
    }
    if (mouse_event.Moving()) {
        const Vec2d pos(mouse_event.GetX(), mouse_event.GetY());
        if (m_seam_path_mode) {
            const int v = seam_vertex_at(pos);
            if (v != m_seam_hover_vertex) {
                m_seam_hover_vertex = v;
                m_seam_hover_edge   = { -1, -1 };
                rebuild_seam_hover_overlay();
                m_parent.set_as_dirty();
            }
        } else {
            const std::pair<int, int> edge = seam_edge_at(pos);
            if (edge != m_seam_hover_edge) {
                m_seam_hover_edge   = edge;
                m_seam_hover_vertex = -1;
                rebuild_seam_hover_overlay();
                m_parent.set_as_dirty();
            }
        }
    }
    if (mouse_event.LeftUp() || (mouse_event.Dragging() && mouse_event.LeftIsDown()))
        return true;
    return false;
}

int GLGizmoTextureDisplacement::texture_volume_raycaster_index() const
{
    const ModelVolume *mv = texture_volume();
    const ModelObject *mo = m_c->selection_info()->model_object();
    if (mv == nullptr || mo == nullptr)
        return -1;

    int idx = -1, count = 0;
    for (const ModelVolume *v : mo->volumes) {
        if (!v->is_model_part())
            continue;
        if (v == mv) { idx = count; break; }
        ++count;
    }
    return (idx >= 0 && idx < int(m_c->raycaster()->raycasters().size())) ? idx : -1;
}

std::pair<int, int> GLGizmoTextureDisplacement::seam_edge_at(const Vec2d &mouse_pos) const
{
    const ModelVolume *mv = texture_volume();
    const ModelObject *mo = m_c->selection_info()->model_object();
    if (mv == nullptr || mo == nullptr)
        return { -1, -1 };

    const int idx = texture_volume_raycaster_index();
    if (idx < 0)
        return { -1, -1 };
    const auto &raycasters = m_c->raycaster()->raycasters();

    const Selection  &selection = m_parent.get_selection();
    const Transform3d trafo     = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();
    const Camera     &camera    = wxGetApp().plater()->get_camera();

    Vec3f  hit = Vec3f::Zero(), normal = Vec3f::Zero();
    size_t facet = 0;
    if (!raycasters[size_t(idx)]->unproject_on_mesh(mouse_pos, trafo, camera, hit, normal,
                                                     m_c->object_clipper()->get_clipping_plane(), &facet))
        return { -1, -1 };

    const indexed_triangle_set &its = mv->mesh().its;
    if (facet >= its.indices.size())
        return { -1, -1 };
    const stl_triangle_vertex_indices &tri = its.indices[facet];

    const auto seg_dist = [](const Vec3f &p, const Vec3f &a, const Vec3f &b) {
        const Vec3f  ab = b - a;
        const float  l2 = ab.squaredNorm();
        const float  t  = (l2 > 1e-12f) ? std::clamp((p - a).dot(ab) / l2, 0.f, 1.f) : 0.f;
        return (p - (a + ab * t)).norm();
    };
    int   best_i = 0;
    float best_d = std::numeric_limits<float>::max();
    for (int i = 0; i < 3; ++i) {
        const float d = seg_dist(hit, its.vertices[tri[i]], its.vertices[tri[(i + 1) % 3]]);
        if (d < best_d) { best_d = d; best_i = i; }
    }
    const int a = tri[best_i], b = tri[(best_i + 1) % 3];
    return { std::min(a, b), std::max(a, b) };
}

void GLGizmoTextureDisplacement::toggle_seam_at(const Vec2d &mouse_pos)
{
    TextureDisplacementLayer *layer = active_layer();
    if (layer == nullptr)
        return;
    const std::pair<int, int> edge = seam_edge_at(mouse_pos);
    if (edge.first < 0)
        return;

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Mark texture seam"), UndoRedo::SnapshotType::GizmoAction);
    auto &seams = layer->lscm_seam_edges;
    if (const auto it = std::find(seams.begin(), seams.end(), edge); it != seams.end())
        seams.erase(it);
    else
        seams.push_back(edge);
    rebuild_preview();
}

int GLGizmoTextureDisplacement::seam_vertex_at(const Vec2d &mouse_pos) const
{
    const ModelVolume *mv = texture_volume();
    const ModelObject *mo = m_c->selection_info()->model_object();
    if (mv == nullptr || mo == nullptr)
        return -1;
    const int idx = texture_volume_raycaster_index();
    if (idx < 0)
        return -1;
    const auto &raycasters = m_c->raycaster()->raycasters();

    const Selection  &selection = m_parent.get_selection();
    const Transform3d trafo     = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();
    const Camera     &camera    = wxGetApp().plater()->get_camera();

    Vec3f  hit = Vec3f::Zero(), normal = Vec3f::Zero();
    size_t facet = 0;
    if (!raycasters[size_t(idx)]->unproject_on_mesh(mouse_pos, trafo, camera, hit, normal,
                                                     m_c->object_clipper()->get_clipping_plane(), &facet))
        return -1;
    const indexed_triangle_set &its = mv->mesh().its;
    if (facet >= its.indices.size())
        return -1;
    const stl_triangle_vertex_indices &tri = its.indices[facet];
    int   best = tri[0];
    float best_d = std::numeric_limits<float>::max();
    for (int i = 0; i < 3; ++i) {
        const float d = (hit - its.vertices[tri[i]]).squaredNorm();
        if (d < best_d) { best_d = d; best = tri[i]; }
    }
    return best;
}

void GLGizmoTextureDisplacement::mark_seam_path(int v_from, int v_to)
{
    TextureDisplacementLayer *layer = active_layer();
    const ModelVolume        *mv    = texture_volume();
    if (layer == nullptr || mv == nullptr || v_from < 0 || v_to < 0 || v_from == v_to)
        return;
    const indexed_triangle_set &its = mv->mesh().its;
    const size_t                n   = its.vertices.size();
    if (size_t(v_from) >= n || size_t(v_to) >= n)
        return;

    std::vector<std::vector<std::pair<int, float>>> adj(n);
    for (const stl_triangle_vertex_indices &tri : its.indices)
        for (int i = 0; i < 3; ++i) {
            const int a = tri[i], b = tri[(i + 1) % 3];
            const float w = (its.vertices[size_t(a)] - its.vertices[size_t(b)]).norm();
            adj[size_t(a)].push_back({ b, w });
            adj[size_t(b)].push_back({ a, w });
        }

    std::vector<float> dist(n, std::numeric_limits<float>::infinity());
    std::vector<int>   prev(n, -1);
    using QN = std::pair<float, int>;
    std::priority_queue<QN, std::vector<QN>, std::greater<QN>> pq;
    dist[size_t(v_from)] = 0.f;
    pq.push({ 0.f, v_from });
    while (!pq.empty()) {
        const auto [d, u] = pq.top();
        pq.pop();
        if (d > dist[size_t(u)])
            continue;
        if (u == v_to)
            break;
        for (const auto &[w, ew] : adj[size_t(u)]) {
            const float nd = d + ew;
            if (nd < dist[size_t(w)]) {
                dist[size_t(w)] = nd;
                prev[size_t(w)] = u;
                pq.push({ nd, w });
            }
        }
    }
    if (prev[size_t(v_to)] < 0)
        return;

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Mark texture seam path"), UndoRedo::SnapshotType::GizmoAction);
    auto &seams = layer->lscm_seam_edges;
    for (int v = v_to; v != v_from && v >= 0; v = prev[size_t(v)]) {
        const int p = prev[size_t(v)];
        if (p < 0)
            break;
        const std::pair<int, int> e{ std::min(v, p), std::max(v, p) };
        if (std::find(seams.begin(), seams.end(), e) == seams.end())
            seams.push_back(e);
    }
    rebuild_preview();
}

void GLGizmoTextureDisplacement::rebuild_seam_anchor_overlay()
{
    m_seam_anchor_glmodel.reset();
    const ModelVolume *mv = texture_volume();
    if (!m_seam_edit_mode || !m_seam_path_mode || mv == nullptr || m_seam_path_anchor < 0)
        return;
    const indexed_triangle_set &its = mv->mesh().its;
    if (size_t(m_seam_path_anchor) >= its.vertices.size())
        return;

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
    unsigned nn = 0;
    for (const stl_triangle_vertex_indices &tri : its.indices)
        for (int i = 0; i < 3; ++i) {
            const int a = tri[i], b = tri[(i + 1) % 3];
            if (a == m_seam_path_anchor || b == m_seam_path_anchor) {
                init_data.add_vertex(its.vertices[size_t(a)]);
                init_data.add_vertex(its.vertices[size_t(b)]);
                init_data.add_line(nn, nn + 1);
                nn += 2;
            }
        }
    if (!init_data.is_empty())
        m_seam_anchor_glmodel.init_from(std::move(init_data));
}

void GLGizmoTextureDisplacement::rebuild_seam_hover_overlay()
{
    m_seam_hover_glmodel.reset();
    const ModelVolume *mv = texture_volume();
    if (!m_seam_edit_mode || mv == nullptr)
        return;
    const indexed_triangle_set &its = mv->mesh().its;

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };

    if (m_seam_path_mode) {
        if (m_seam_hover_vertex < 0 || size_t(m_seam_hover_vertex) >= its.vertices.size())
            return;
        unsigned nn = 0;
        for (const stl_triangle_vertex_indices &tri : its.indices)
            for (int i = 0; i < 3; ++i) {
                const int a = tri[i], b = tri[(i + 1) % 3];
                if (a == m_seam_hover_vertex || b == m_seam_hover_vertex) {
                    init_data.add_vertex(its.vertices[size_t(a)]);
                    init_data.add_vertex(its.vertices[size_t(b)]);
                    init_data.add_line(nn, nn + 1);
                    nn += 2;
                }
            }
        if (!init_data.is_empty())
            m_seam_hover_glmodel.init_from(std::move(init_data));
        return;
    }

    if (m_seam_hover_edge.first < 0 || size_t(m_seam_hover_edge.first) >= its.vertices.size() ||
        size_t(m_seam_hover_edge.second) >= its.vertices.size())
        return;
    init_data.reserve_vertices(2);
    init_data.reserve_indices(2);
    init_data.add_vertex(its.vertices[size_t(m_seam_hover_edge.first)]);
    init_data.add_vertex(its.vertices[size_t(m_seam_hover_edge.second)]);
    init_data.add_line(0, 1);
    m_seam_hover_glmodel.init_from(std::move(init_data));
}

void GLGizmoTextureDisplacement::rebuild_seam_overlay()
{
    m_seam_glmodel.reset();
    const ModelVolume              *mv    = texture_volume();
    const TextureDisplacementLayer *layer = active_layer();
    if (mv == nullptr || layer == nullptr || layer->lscm_seam_edges.empty())
        return;
    const indexed_triangle_set &its = mv->mesh().its;

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
    init_data.reserve_vertices(layer->lscm_seam_edges.size() * 2);
    init_data.reserve_indices(layer->lscm_seam_edges.size() * 2);
    unsigned n = 0;
    for (const auto &[a, b] : layer->lscm_seam_edges) {
        if (a < 0 || b < 0 || size_t(a) >= its.vertices.size() || size_t(b) >= its.vertices.size())
            continue;
        init_data.add_vertex(its.vertices[size_t(a)]);
        init_data.add_vertex(its.vertices[size_t(b)]);
        init_data.add_line(n, n + 1);
        n += 2;
    }
    if (!init_data.is_empty())
        m_seam_glmodel.init_from(std::move(init_data));
}

void GLGizmoTextureDisplacement::render_seam_overlay()
{
    const ModelObject *mo = m_c->selection_info()->model_object();
    const ModelVolume *mv = texture_volume();
    const bool         have_marked = m_seam_glmodel.is_initialized();
    const bool         have_hover  = m_seam_edit_mode && m_seam_hover_glmodel.is_initialized();
    const bool         have_anchor = m_seam_edit_mode && m_seam_anchor_glmodel.is_initialized();
    if (mo == nullptr || mv == nullptr || (!have_marked && !have_hover && !have_anchor))
        return;
    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    const Selection  &selection    = m_parent.get_selection();
    const Transform3d trafo_matrix = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();
    const Camera     &camera       = wxGetApp().plater()->get_camera();

    shader->start_using();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix() * trafo_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    glsafe(::glEnable(GL_POLYGON_OFFSET_LINE));
    glsafe(::glPolygonOffset(-2.0f, -2.0f));
    const bool seams_on_top = m_wireframe_overlay || m_seam_edit_mode;
    if (seams_on_top)
        glsafe(::glDisable(GL_DEPTH_TEST));
#if !SLIC3R_OPENGL_ES
    const bool wide = !OpenGLManager::get_gl_info().is_core_profile();
    if (wide)
        glsafe(::glLineWidth(4.0f));
#endif
    if (have_marked) {
        m_seam_glmodel.set_color(ColorRGBA(1.0f, 0.15f, 0.15f, 1.0f));
        m_seam_glmodel.render();
    }
    if (have_hover) {
        glsafe(::glPolygonOffset(-3.0f, -3.0f));
        m_seam_hover_glmodel.set_color(ColorRGBA(1.0f, 0.9f, 0.15f, 1.0f));
        m_seam_hover_glmodel.render();
    }
    if (have_anchor) {
        glsafe(::glPolygonOffset(-3.0f, -3.0f));
        m_seam_anchor_glmodel.set_color(ColorRGBA(0.2f, 1.0f, 0.4f, 1.0f));
        m_seam_anchor_glmodel.render();
    }
#if !SLIC3R_OPENGL_ES
    if (wide)
        glsafe(::glLineWidth(1.0f));
#endif
    glsafe(::glDisable(GL_POLYGON_OFFSET_LINE));
    if (seams_on_top)
        glsafe(::glEnable(GL_DEPTH_TEST));
    shader->stop_using();
}

void GLGizmoTextureDisplacement::render_preview_mesh()
{
    const ModelObject *mo = m_c->selection_info()->model_object();
    const ModelVolume  *mv = texture_volume();
    if (mo == nullptr || mv == nullptr)
        return;

    const Selection  &selection    = m_parent.get_selection();
    const Transform3d trafo_matrix = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();

    auto *shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;
    shader->start_using();
    const Camera      &camera      = wxGetApp().plater()->get_camera();
    const Transform3d &view_matrix = camera.get_view_matrix();
    shader->set_uniform("view_model_matrix", view_matrix * trafo_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * trafo_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
    shader->set_uniform("view_normal_matrix", view_normal_matrix);
    if (m_preview_color_runs.empty()) {
        m_preview_glmodel.render();
    } else {
        for (const PreviewColorRun &run : m_preview_color_runs) {
            m_preview_glmodel.set_color(run.color);
            m_preview_glmodel.render(run.range, shader);
        }
    }
    shader->stop_using();
}

float GLGizmoTextureDisplacement::layer_texture_aspect(const TextureDisplacementLayer &layer)
{
    const DecodedHeightTexture tex = decode_height_texture(layer);
    return (tex.width > 0 && tex.height > 0) ? float(tex.width) / float(tex.height) : 1.f;
}

indexed_triangle_set GLGizmoTextureDisplacement::patch_in_world(const indexed_triangle_set &patch) const
{
    const ModelVolume *mv = texture_volume();
    if (mv == nullptr)
        return patch;
    const Transform3d to_world = texture_displacement_bake_frame(texture_displacement_volume_to_world(*mv));
    if (to_world.matrix().isApprox(Transform3d::Identity().matrix()))
        return patch;

    indexed_triangle_set world = patch;
    for (Vec3f &v : world.vertices)
        v = (to_world * v.cast<double>()).cast<float>();
    return world;
}

std::vector<Vec2f> GLGizmoTextureDisplacement::compute_layer_vertex_uvs(const indexed_triangle_set     &local_patch,
                                                                       const TextureDisplacementLayer &layer) const
{
    const indexed_triangle_set patch  = patch_in_world(local_patch);
    const float                aspect = layer_texture_aspect(layer);
    if (layer.projection_method == TextureProjectionMethod::LSCM) {
        std::vector<Vec2f> uv = compute_lscm_uvs(patch, layer);
        for (Vec2f &p : uv)
            p = apply_uv_transform(p, layer, aspect);
        return uv;
    }
    if (layer.projection_method == TextureProjectionMethod::ViewProjected) {
        std::vector<Vec2f> uv(patch.vertices.size());
        for (size_t vi = 0; vi < patch.vertices.size(); ++vi) {
            if (layer.view_project_projective) {
                if (!project_uv_projective(layer.view_project_matrix, patch.vertices[vi], uv[vi]))
                    uv[vi] = Vec2f(-1e6f, -1e6f);
                continue;
            }
            const Vec2f planar(patch.vertices[vi].dot(layer.view_project_right),
                               patch.vertices[vi].dot(layer.view_project_up));
            uv[vi] = apply_uv_transform(planar, layer, aspect);
        }
        return uv;
    }
    return {};
}

int GLGizmoTextureDisplacement::layer_projection_frame(const indexed_triangle_set     &local_patch,
                                                       const TextureDisplacementLayer &layer,
                                                       Vec3f &center, Vec3f &axis) const
{
    center = Vec3f::Zero();
    axis   = Vec3f::UnitZ();
    const ModelVolume *mv = texture_volume();
    if (mv == nullptr || (layer.projection_method != TextureProjectionMethod::Cylindrical &&
                          layer.projection_method != TextureProjectionMethod::Spherical))
        return 0;
    Vec3f average_normal;
    texture_displacement_patch_frame(patch_in_world(local_patch),
                                     texture_displacement_vertex_normals(patch_in_world(mv->mesh().its)),
                                     center, axis, average_normal);
    return layer.projection_method == TextureProjectionMethod::Cylindrical ? 1 : 2;
}

std::vector<Vec2f> GLGizmoTextureDisplacement::compute_layer_corner_uvs(const indexed_triangle_set     &local_patch,
                                                                        const TextureDisplacementLayer &layer) const
{
    if (layer.projection_method == TextureProjectionMethod::LSCM) {
        const indexed_triangle_set patch  = patch_in_world(local_patch);
        const float                aspect = layer_texture_aspect(layer);
        std::vector<Vec2f>         uv     = compute_lscm_corner_uvs(patch, layer);
        for (Vec2f &p : uv)
            p = apply_uv_transform(p, layer, aspect);
        return uv;
    }
    const std::vector<Vec2f> per_vertex = compute_layer_vertex_uvs(local_patch, layer);
    if (per_vertex.size() != local_patch.vertices.size())
        return {};
    std::vector<Vec2f> corner(local_patch.indices.size() * 3);
    for (size_t f = 0; f < local_patch.indices.size(); ++f)
        for (int k = 0; k < 3; ++k)
            corner[f * 3 + size_t(k)] = per_vertex[size_t(local_patch.indices[f][k])];
    return corner;
}

void GLGizmoTextureDisplacement::rebuild_shaded_preview_mesh()
{
    m_shaded_preview_glmodel.reset();

    const ModelVolume *mv = texture_volume();
    if (mv == nullptr || m_triangle_selectors.empty())
        return;

    const indexed_triangle_set patch = m_triangle_selectors[0]->get_facets_strict(EnforcerBlockerType::ENFORCER);
    if (patch.indices.empty())
        return;

    const indexed_triangle_set rest = m_triangle_selectors[0]->get_facets_strict(EnforcerBlockerType::NONE);

    const TextureDisplacementLayer *active = active_layer();
    std::vector<Vec2f> corner_uv = active != nullptr ? compute_layer_corner_uvs(patch, *active) : std::vector<Vec2f>{};
    m_shaded_preview_uses_vertex_uv = corner_uv.size() == patch.indices.size() * 3;
    if (!m_shaded_preview_uses_vertex_uv)
        corner_uv.clear();
    m_shaded_projection_mode = (active != nullptr && !m_shaded_preview_uses_vertex_uv) ?
                                 layer_projection_frame(patch, *active, m_shaded_patch_center, m_shaded_patch_axis) : 0;

    compute_shaded_active_faces(m_shaded_active_chart >= 0 ? m_island_move_set : std::vector<int>{}, patch.indices.size());

    m_shaded_preview_palette = (active != nullptr && active->color_enabled) ? cached_palette()
                                                                         : std::vector<PaletteEntry>{};

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3T2 };

    const size_t   tri_total = patch.indices.size() + rest.indices.size();
    init_data.reserve_vertices(tri_total * 3);
    init_data.reserve_indices(tri_total * 3);
    unsigned vcount = 0;
    const auto emit_triangles = [&](const indexed_triangle_set &its, float weight, bool painted) {
        for (size_t f = 0; f < its.indices.size(); ++f) {
            const stl_triangle_vertex_indices &tri = its.indices[f];
            const float act = (painted && f < m_shaded_active_face.size() && m_shaded_active_face[f]) ? 1.f : 0.f;
            for (int i = 0; i < 3; ++i) {
                const int   idx = tri[i];
                const Vec2f uv  = (painted && m_shaded_preview_uses_vertex_uv) ? corner_uv[f * 3 + size_t(i)]
                                                                             : Vec2f::Zero();
                init_data.add_vertex(its.vertices[size_t(idx)], Vec3f(weight, act, 0.f), uv);
            }
            init_data.add_triangle(vcount, vcount + 1, vcount + 2);
            vcount += 3;
        }
    };
    emit_triangles(patch, 1.f, true);
    emit_triangles(rest, 0.f, false);

    m_shaded_preview_glmodel.init_from(std::move(init_data));
    m_shaded_preview_glmodel.set_color(GLVolume::NEUTRAL_COLOR);

    m_shaded_island_delta = Eigen::Matrix<float, 2, 3>::Identity();
    const TextureDisplacementLayer *al = active_layer();
    if (m_shaded_active_chart >= 0 && al != nullptr) {
        const std::vector<Eigen::Matrix<float, 2, 3>> xf = uv_editor_island_transforms(*al);
        m_shaded_baked_active_xf = (size_t(m_shaded_active_chart) < xf.size()) ? xf[size_t(m_shaded_active_chart)]
                                                                           : Eigen::Matrix<float, 2, 3>::Identity();
    } else {
        m_shaded_baked_active_xf = Eigen::Matrix<float, 2, 3>::Identity();
    }
}

void GLGizmoTextureDisplacement::compute_shaded_active_faces(const std::vector<int> &charts, size_t patch_face_count)
{
    m_shaded_active_face.clear();
    if (charts.empty() || patch_face_count == 0)
        return;
    const PatchUnwrap &u = m_uv_editor_unwrap;
    if (u.source_face.size() != u.indices.size())
        return;
    m_shaded_active_face.assign(patch_face_count, 0);
    for (size_t t = 0; t < u.indices.size(); ++t) {
        const int f  = u.source_face[t];
        const int v0 = u.indices[t][0];
        if (f < 0 || size_t(f) >= m_shaded_active_face.size() || v0 < 0 || size_t(v0) >= u.vertex_chart.size())
            continue;
        if (std::find(charts.begin(), charts.end(), u.vertex_chart[size_t(v0)]) != charts.end())
            m_shaded_active_face[size_t(f)] = 1;
    }
}

int GLGizmoTextureDisplacement::island_group_of(const std::vector<int> &groups, int c)
{
    return (c >= 0 && size_t(c) < groups.size() && groups[size_t(c)] >= 0) ? groups[size_t(c)] : c;
}

void GLGizmoTextureDisplacement::join_island_groups(std::vector<int> &groups, int a, int b, int chart_count)
{
    if (a < 0 || b < 0 || chart_count <= 0)
        return;
    if (int(groups.size()) < chart_count) {
        const size_t old = groups.size();
        groups.resize(size_t(chart_count));
        for (size_t i = old; i < groups.size(); ++i)
            groups[i] = int(i);
    }
    for (size_t i = 0; i < groups.size(); ++i)
        if (groups[i] < 0)
            groups[i] = int(i);
    const int ga = groups[size_t(a)], gb = groups[size_t(b)];
    if (ga == gb)
        return;
    const int g = std::min(ga, gb);
    for (int &x : groups)
        if (x == ga || x == gb)
            x = g;
}

std::vector<int> GLGizmoTextureDisplacement::build_island_move_set(const TextureDisplacementLayer &layer, int primary) const
{
    std::vector<int> set;
    const int        chart_count = std::max(m_uv_editor_unwrap.chart_count, 0);

    std::vector<int> seeds;
    if (const UVEditorCanvas *canvas = wxGetApp().plater()->get_uv_editor_canvas())
        seeds = canvas->selected_islands();
    if (seeds.empty() && primary >= 0)
        seeds.push_back(primary);

    const auto add = [&set](int c) {
        if (c >= 0 && std::find(set.begin(), set.end(), c) == set.end())
            set.push_back(c);
    };
    for (int s : seeds) {
        add(s);
        const int gs = island_group_of(layer.island_groups, s);
        for (int c = 0; c < chart_count; ++c)
            if (island_group_of(layer.island_groups, c) == gs)
                add(c);
    }
    add(primary);
    return set;
}

void GLGizmoTextureDisplacement::render_shaded_preview_mesh()
{
    const ModelObject *mo = m_c->selection_info()->model_object();
    const ModelVolume  *mv = texture_volume();
    if (mo == nullptr || mv == nullptr || !m_shaded_preview_glmodel.is_initialized())
        return;

    const TextureDisplacementLayer *layer = active_layer();
    if (layer == nullptr || layer->empty())
        return;

    GLTexture *tex = get_layer_height_texture(*layer);
    if (tex == nullptr || tex->get_width() <= 0 || tex->get_height() <= 0)
        return;

    GLShaderProgram *shader = wxGetApp().get_shader("texture_displacement_shaded");
    if (shader == nullptr)
        return;

    const Selection  &selection    = m_parent.get_selection();
    const Transform3d trafo_matrix = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();
    const Camera      &camera      = wxGetApp().plater()->get_camera();

    shader->start_using();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix() * trafo_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    shader->set_uniform("volume_world_matrix", trafo_matrix);
    const ClippingPlaneDataWrapper clp_data = this->get_clipping_plane_data();
    shader->set_uniform("clipping_plane", clp_data.clp_dataf);
    shader->set_uniform("z_range", clp_data.z_range);
    const Matrix3d view_normal_matrix = camera.get_view_matrix().matrix().block(0, 0, 3, 3);
    shader->set_uniform("view_normal_matrix", view_normal_matrix);
    shader->set_uniform("volume_mirrored", trafo_matrix.matrix().determinant() < 0.0);
    glsafe(::glActiveTexture(GL_TEXTURE0));
    glsafe(::glBindTexture(GL_TEXTURE_2D, tex->get_id()));
#if SLIC3R_OPENGL_ES
    const GLint no_tile_wrap = GL_CLAMP_TO_EDGE;
#else
    const GLint no_tile_wrap = GL_CLAMP_TO_BORDER;
#endif
    const GLint wrap = !layer->tile_enabled                                      ? no_tile_wrap :
                       (layer->tile_method == TextureTileMethod::MirroredRepeat) ? GL_MIRRORED_REPEAT :
                                                                                   GL_REPEAT;
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap));
#if !SLIC3R_OPENGL_ES
    if (wrap == GL_CLAMP_TO_BORDER) {
        static const GLfloat border[4] = { 0.f, 0.f, 0.f, 0.f };
        glsafe(::glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border));
    }
#endif
    shader->set_uniform("height_tex", 0);
    shader->set_uniform("height_tex_texel", Vec2f(1.f / float(tex->get_width()), 1.f / float(tex->get_height())));
    shader->set_uniform("depth_mm", layer->depth_mm);
    shader->set_uniform("tiling_scale", layer->tiling_scale);
    shader->set_uniform("tex_aspect", float(tex->get_width()) / float(tex->get_height()));
    shader->set_uniform("rotation_rad", layer->rotation_deg * float(M_PI) / 180.f);
    shader->set_uniform("uv_offset", layer->offset);
    shader->set_uniform("invert", layer->invert);
    shader->set_uniform("midlevel", layer->midlevel);
    const Vec3d tex_anchor = trafo_matrix.translation();
    shader->set_uniform("tex_anchor", Vec3f(tex_anchor.cast<float>()));
    shader->set_uniform("eye_model_pos", Vec3f((camera.get_position() - tex_anchor).cast<float>()));
    shader->set_uniform("use_vertex_uv", m_shaded_preview_uses_vertex_uv);
    shader->set_uniform("projection_mode", m_shaded_projection_mode);
    shader->set_uniform("patch_center", m_shaded_patch_center);
    shader->set_uniform("patch_axis", m_shaded_patch_axis);

    const GLTexture *color_tex = get_layer_color_texture(*layer);
    const int        palette_count =
        (color_tex != nullptr) ? int(std::min(m_shaded_preview_palette.size(), size_t(PALETTE_MAX_ENTRIES))) : 0;
    shader->set_uniform("palette_count", palette_count);
    shader->set_uniform("has_color_tex", color_tex != nullptr);
    shader->set_uniform("pure_only", color_tex != nullptr && analyze_texture_detail(*layer).flat_colors);
    shader->set_uniform("prefer_pure_de", PREFER_PURE_DE);
    for (int i = 0; i < palette_count; ++i) {
        const PaletteEntry &e   = m_shaded_preview_palette[size_t(i)];
        const std::string   idx = "[" + std::to_string(i) + "]";
        shader->set_uniform(("palette_rgb" + idx).c_str(), e.rgb);
        shader->set_uniform(("palette_lab" + idx).c_str(), srgb_to_lab(e.rgb));
        shader->set_uniform(("palette_a" + idx).c_str(), e.a);
        shader->set_uniform(("palette_b" + idx).c_str(), e.b);
    }
    if (color_tex != nullptr) {
        shader->set_uniform("color_tex", 1);
        glsafe(::glActiveTexture(GL_TEXTURE1));
        glsafe(::glBindTexture(GL_TEXTURE_2D, (GLuint) color_tex->get_id()));
        glsafe(::glActiveTexture(GL_TEXTURE0));
    }
    const Eigen::Matrix<float, 2, 3> &d = m_shaded_island_delta;
    shader->set_uniform("island_delta_lin", std::array<float, 4>{ d(0, 0), d(0, 1), d(1, 0), d(1, 1) });
    shader->set_uniform("island_delta_tr", Vec2f(d(0, 2), d(1, 2)));
    m_shaded_preview_glmodel.render();
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
    shader->stop_using();
}

bool GLGizmoTextureDisplacement::shaded_preview_ready() const
{
    if (m_c->selection_info() == nullptr || m_c->selection_info()->model_object() == nullptr)
        return false;
    if (texture_volume() == nullptr)
        return false;
    const TextureDisplacementLayer *layer = active_layer();
    if (layer == nullptr || layer->empty())
        return false;
    const GLTexture *tex = const_cast<GLGizmoTextureDisplacement *>(this)->get_layer_height_texture(*layer);
    if (tex == nullptr || tex->get_width() <= 0 || tex->get_height() <= 0)
        return false;
    return wxGetApp().get_shader("texture_displacement_shaded") != nullptr;
}

static void append_paint_patch(GLModel::Geometry &out, const indexed_triangle_set &patch, const std::vector<Vec3f> *displaced)
{
    unsigned n = unsigned(out.vertices_count());
    for (const stl_triangle_vertex_indices &tri : patch.indices) {
        for (int i = 0; i < 3; ++i) {
            const size_t idx = size_t(tri[i]);
            out.add_vertex((displaced != nullptr && idx < displaced->size()) ? (*displaced)[idx] : patch.vertices[idx]);
        }
        out.add_triangle(n, n + 1, n + 2);
        n += 3;
    }
}

void GLGizmoTextureDisplacement::rebuild_other_paint_overlay()
{
    const ModelVolume *mv = texture_volume();
    std::string key;
    if (mv != nullptr) {
        key = std::to_string(mv->id().id) + ":" + std::to_string(m_active_layer_slot) + (m_use_shaded_preview ? ":b:" : ":t:") +
              std::to_string(reinterpret_cast<uintptr_t>(m_preview_its.vertices.data())) + ":" +
              std::to_string(m_preview_its.vertices.size());
        for (const TextureDisplacementLayer &l : mv->texture_displacement_layers)
            if (l.slot != m_active_layer_slot && l.slot >= 0 && l.slot < int(TEXTURE_DISPLACEMENT_MAX_LAYERS))
                key += "|" + std::to_string(l.slot) + "@" + std::to_string(mv->texture_displacement_facet(l.slot).timestamp());
    }
    if (key == m_other_paint_key)
        return;
    m_other_paint_key = std::move(key);
    m_other_paint_glmodel.reset();
    if (mv == nullptr)
        return;

    const std::vector<Vec3f> *displaced = nullptr;
    if (!m_use_shaded_preview && m_preview_its.vertices.size() == mv->mesh().its.vertices.size() &&
        !m_preview_its.vertices.empty())
        displaced = &m_preview_its.vertices;

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
    for (const TextureDisplacementLayer &l : mv->texture_displacement_layers) {
        if (l.slot == m_active_layer_slot || l.slot < 0 || l.slot >= int(TEXTURE_DISPLACEMENT_MAX_LAYERS) ||
            mv->texture_displacement_facet(l.slot).empty())
            continue;
        TriangleSelector selector(mv->mesh());
        selector.deserialize(mv->texture_displacement_facet(l.slot).get_data(), false);
        append_paint_patch(init_data, selector.get_facets_strict(EnforcerBlockerType::ENFORCER), displaced);
    }
    if (init_data.is_empty())
        return;
    m_other_paint_glmodel.init_from(std::move(init_data));
    m_other_paint_glmodel.set_color(ColorRGBA(0.55f, 0.58f, 0.60f, 0.35f));
}

std::vector<size_t> GLGizmoTextureDisplacement::PaintedColors::outside(const std::vector<bool> &excluded) const
{
    std::vector<size_t> out;
    out.reserve(source.size());
    for (size_t i = 0; i < source.size(); ++i)
        if (size_t(source[i]) >= excluded.size() || !excluded[size_t(source[i])])
            out.push_back(i);
    return out;
}

GLGizmoTextureDisplacement::PaintedColors GLGizmoTextureDisplacement::painted_colors(const TriangleMesh &mesh,
                                                                                     const TriangleSelector::TriangleSplittingData &paint)
{
    PaintedColors out;
    TriangleSelector selector(mesh);
    selector.deserialize(paint, false);
    for (const EnforcerBlockerType state : TriangleSelector::extract_used_facet_states(paint)) {
        if (state == EnforcerBlockerType::NONE)
            continue;
        std::vector<int>           source;
        const indexed_triangle_set part = selector.get_facets_strict(state, &source);
        if (out.facets.vertices.empty())
            out.facets.vertices = part.vertices;
        out.facets.indices.insert(out.facets.indices.end(), part.indices.begin(), part.indices.end());
        out.source.insert(out.source.end(), source.begin(), source.end());
        out.state.resize(out.facets.indices.size(), int(state));
    }
    return out;
}

void GLGizmoTextureDisplacement::rebuild_painted_colors(bool whole_stack)
{
    const ModelVolume *mv    = texture_volume();
    const bool         shown = mv != nullptr && any_layer_colors(*mv) && !mv->mmu_segmentation_facets.empty();

    std::string key;
    if (shown)
        key = std::to_string(mv->id().id) + ":" + std::to_string(reinterpret_cast<uintptr_t>(mv->mesh_ptr().get())) + ":" +
              std::to_string(mv->mmu_segmentation_facets.timestamp());
    if (key != m_painted_colors_key) {
        m_painted_colors_key = std::move(key);
        m_painted_colors     = shown ? painted_colors(mv->mesh(), mv->mmu_segmentation_facets.get_data()) : PaintedColors{};
        m_painted_colors_drawn_key.clear();
        m_painted_colors_glmodel.reset();
        m_painted_colors_runs.clear();
    }
    if (m_painted_colors.state.empty())
        return;

    std::string drawn_key = m_painted_colors_key + (whole_stack ? std::string(":all") : ":" + std::to_string(m_active_layer_slot));
    for (const TextureDisplacementLayer &l : mv->texture_displacement_layers)
        if (l.slot >= 0 && l.slot < int(TEXTURE_DISPLACEMENT_MAX_LAYERS))
            drawn_key += "|" + std::to_string(l.slot) + "@" + std::to_string(mv->texture_displacement_facet(l.slot).timestamp());
    if (drawn_key == m_painted_colors_drawn_key)
        return;
    m_painted_colors_drawn_key = std::move(drawn_key);
    m_painted_colors_glmodel.reset();
    m_painted_colors_runs.clear();

    std::vector<bool> excluded(mv->mesh().its.indices.size(), false);
    for (const TextureDisplacementLayer &l : mv->texture_displacement_layers) {
        if (l.slot < 0 || l.slot >= int(TEXTURE_DISPLACEMENT_MAX_LAYERS) || (!whole_stack && l.slot != m_active_layer_slot))
            continue;
        for (const TriangleSelector::TriangleBitStreamMapping &m : mv->texture_displacement_facet(l.slot).get_data().triangles_to_split)
            if (m.triangle_idx >= 0 && size_t(m.triangle_idx) < excluded.size())
                excluded[size_t(m.triangle_idx)] = true;
    }

    const std::vector<size_t> kept = m_painted_colors.outside(excluded);
    GLModel::Geometry         init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
    init_data.reserve_vertices(kept.size() * 3);
    init_data.reserve_indices(kept.size() * 3);
    unsigned n = 0;
    for (const size_t t : kept) {
        const int state = m_painted_colors.state[t];
        if (m_painted_colors_runs.empty() || m_painted_colors_runs.back().first != state)
            m_painted_colors_runs.push_back({ state, { size_t(n), size_t(n) } });
        for (int i = 0; i < 3; ++i)
            init_data.add_vertex(m_painted_colors.facets.vertices[size_t(m_painted_colors.facets.indices[t][i])]);
        init_data.add_triangle(n, n + 1, n + 2);
        n += 3;
        m_painted_colors_runs.back().second.second = size_t(n);
    }
    if (!init_data.is_empty())
        m_painted_colors_glmodel.init_from(std::move(init_data));
}

bool GLGizmoTextureDisplacement::render_painted_colors(bool whole_stack)
{
    rebuild_painted_colors(whole_stack);
    const ModelObject *mo     = m_c->selection_info()->model_object();
    const ModelVolume *mv     = texture_volume();
    GLShaderProgram   *shader = wxGetApp().get_shader("mm_gouraud");
    if (mo == nullptr || mv == nullptr || shader == nullptr || !m_painted_colors_glmodel.is_initialized())
        return false;

    const Selection             &selection    = m_parent.get_selection();
    const Transform3d            trafo_matrix = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();
    const Camera                &camera       = wxGetApp().plater()->get_camera();
    const Transform3d           &view_matrix  = camera.get_view_matrix();
    const Matrix3d               normal_matrix = trafo_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
    const std::vector<ColorRGBA> colors        = wxGetApp().plater()->get_extruders_colors();

    shader->start_using();
    const ClippingPlaneDataWrapper clp_data = get_clipping_plane_data();
    shader->set_uniform("clipping_plane", clp_data.clp_dataf);
    shader->set_uniform("z_range", clp_data.z_range);
    shader->set_uniform("view_model_matrix", view_matrix * trafo_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    shader->set_uniform("view_normal_matrix", Matrix3d(view_matrix.matrix().block(0, 0, 3, 3) * normal_matrix));
    shader->set_uniform("volume_world_matrix", trafo_matrix);
    shader->set_uniform("volume_mirrored", trafo_matrix.matrix().determinant() < 0.);
    shader->set_uniform("slope.actived", m_parent.is_using_slope());
    shader->set_uniform("slope.volume_world_normal_matrix", Matrix3f(normal_matrix.cast<float>()));
    shader->set_uniform("slope.normal_z", float(-std::cos(Geometry::deg2rad(m_highlight_by_angle_threshold_deg))));
    shader->set_uniform("slope.up_direction", get_tilt_up_direction());
    shader->set_uniform("show_wireframe", false);
    glsafe(::glEnable(GL_POLYGON_OFFSET_FILL));
    glsafe(::glPolygonOffset(-2.f, -2.f));
    glsafe(::glDepthMask(GL_FALSE));
    bool drawn = false;
    for (const auto &[state, range] : m_painted_colors_runs)
        if (state >= 1 && size_t(state) <= colors.size()) {
            m_painted_colors_glmodel.set_color(adjust_color_for_rendering(colors[size_t(state - 1)]));
            m_painted_colors_glmodel.render(range, shader);
            drawn = true;
        }
    glsafe(::glDepthMask(GL_TRUE));
    glsafe(::glDisable(GL_POLYGON_OFFSET_FILL));
    shader->stop_using();
    return drawn;
}

void GLGizmoTextureDisplacement::rebuild_paint_overlay()
{
    m_paint_overlay_glmodel.reset();
    const ModelVolume *mv = texture_volume();
    if (mv == nullptr || m_triangle_selectors.empty())
        return;

    const indexed_triangle_set patch = m_triangle_selectors[0]->get_facets_strict(EnforcerBlockerType::ENFORCER);
    if (patch.indices.empty())
        return;

    const std::vector<Vec3f> *displaced = nullptr;
    if (!m_use_shaded_preview && m_preview_its.vertices.size() == mv->mesh().its.vertices.size() &&
        !m_preview_its.vertices.empty())
        displaced = &m_preview_its.vertices;

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
    init_data.reserve_vertices(patch.indices.size() * 3);
    init_data.reserve_indices(patch.indices.size() * 3);
    append_paint_patch(init_data, patch, displaced);
    m_paint_overlay_glmodel.init_from(std::move(init_data));
    m_paint_overlay_glmodel.set_color(ColorRGBA(0.16f, 0.79f, 0.35f, 0.38f));
}

void GLGizmoTextureDisplacement::render_paint_overlay(GLModel &overlay)
{
    const ModelObject *mo = m_c->selection_info()->model_object();
    const ModelVolume *mv = texture_volume();
    if (mo == nullptr || mv == nullptr || !overlay.is_initialized())
        return;
    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    const Selection  &selection    = m_parent.get_selection();
    const Transform3d trafo_matrix = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();
    const Camera     &camera       = wxGetApp().plater()->get_camera();

    shader->start_using();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix() * trafo_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    glsafe(::glEnable(GL_POLYGON_OFFSET_FILL));
    glsafe(::glPolygonOffset(-2.f, -2.f));
    glsafe(::glDepthMask(GL_FALSE));
    overlay.render();
    glsafe(::glDepthMask(GL_TRUE));
    glsafe(::glDisable(GL_POLYGON_OFFSET_FILL));
    shader->stop_using();
}

void GLGizmoTextureDisplacement::rebuild_island_overlay(const std::vector<int> &selection)
{
    m_island_overlay_glmodel.reset();
    m_island_overlay_selection = selection;
    const ModelVolume *mv = texture_volume();
    if (mv == nullptr || selection.empty() || m_uv_editor_unwrap.empty())
        return;
    const indexed_triangle_set patch = extract_painted_patch(mv->mesh().its, m_uv_editor_state.facets);
    const PatchUnwrap         &uw    = m_uv_editor_unwrap;
    std::vector<uint8_t>       chosen(size_t(std::max(uw.chart_count, 0)), 0);
    for (const int c : selection)
        if (c >= 0 && size_t(c) < chosen.size())
            chosen[size_t(c)] = 1;

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
    unsigned n = 0;
    for (const stl_triangle_vertex_indices &tri : uw.indices) {
        const int v0 = tri[0];
        if (v0 < 0 || size_t(v0) >= uw.vertex_chart.size())
            continue;
        const int c = uw.vertex_chart[size_t(v0)];
        if (c < 0 || size_t(c) >= chosen.size() || !chosen[size_t(c)])
            continue;
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            const int u = tri[k];
            ok = u >= 0 && size_t(u) < uw.source_vertex.size() && uw.source_vertex[size_t(u)] >= 0 &&
                 size_t(uw.source_vertex[size_t(u)]) < patch.vertices.size();
        }
        if (!ok)
            continue;
        for (int k = 0; k < 3; ++k)
            init_data.add_vertex(patch.vertices[size_t(uw.source_vertex[size_t(tri[k])])]);
        init_data.add_triangle(n, n + 1, n + 2);
        n += 3;
    }
    if (n == 0)
        return;
    m_island_overlay_glmodel.init_from(std::move(init_data));
    m_island_overlay_glmodel.set_color(ColorRGBA(0.10f, 0.55f, 0.95f, 0.45f));
}

void GLGizmoTextureDisplacement::render_island_overlay()
{
    const ModelObject *mo = m_c->selection_info()->model_object();
    const ModelVolume *mv = texture_volume();
    if (mo == nullptr || mv == nullptr || !m_island_overlay_glmodel.is_initialized())
        return;
    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;
    const Selection  &selection    = m_parent.get_selection();
    const Transform3d trafo_matrix = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();
    const Camera     &camera       = wxGetApp().plater()->get_camera();
    shader->start_using();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix() * trafo_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    glsafe(::glEnable(GL_POLYGON_OFFSET_FILL));
    glsafe(::glPolygonOffset(-2.0f, -2.0f));
    glsafe(::glDepthMask(GL_FALSE));
    m_island_overlay_glmodel.render();
    glsafe(::glDepthMask(GL_TRUE));
    glsafe(::glDisable(GL_POLYGON_OFFSET_FILL));
    shader->stop_using();
}

void GLGizmoTextureDisplacement::rebuild_uvcheck_mesh()
{
    m_uvcheck_glmodel.reset();
    if (m_uv_check_mode == UVCheckMode::None)
        return;

    const ModelVolume *mv = texture_volume();
    if (mv == nullptr || m_triangle_selectors.empty())
        return;
    const indexed_triangle_set patch = m_triangle_selectors[0]->get_facets_strict(EnforcerBlockerType::ENFORCER);
    if (patch.indices.empty())
        return;
    const TextureDisplacementLayer *layer = active_layer();
    if (layer == nullptr)
        return;

    std::vector<Vec2f> uv       = compute_layer_vertex_uvs(patch, *layer);
    const bool         have_uvs = uv.size() == patch.vertices.size();
    m_uvcheck_uses_vertex_uv    = have_uvs;
    m_uvcheck_projection_mode   = have_uvs ? 0 :
                                      layer_projection_frame(patch, *layer, m_uvcheck_patch_center, m_uvcheck_patch_axis);
    const std::vector<Vec2f> corner_uv = compute_layer_corner_uvs(patch, *layer);
    const bool               have_corner_uvs = have_uvs && corner_uv.size() == patch.indices.size() * 3;

    std::vector<float> distortion(patch.vertices.size(), 0.5f);
    if (m_uv_check_mode == UVCheckMode::Distortion && have_uvs) {
        std::vector<float> tri_log(patch.indices.size(), 0.f);
        for (size_t f = 0; f < patch.indices.size(); ++f) {
            const stl_triangle_vertex_indices &t = patch.indices[f];
            const float a3 = 0.5f * (patch.vertices[t[1]] - patch.vertices[t[0]]).cross(patch.vertices[t[2]] - patch.vertices[t[0]]).norm();
            const Vec2f e0 = uv[t[1]] - uv[t[0]];
            const Vec2f e1 = uv[t[2]] - uv[t[0]];
            const float a2 = 0.5f * std::abs(e0.x() * e1.y() - e0.y() * e1.x());
            tri_log[f] = (a3 > 1e-12f && a2 > 1e-12f) ? std::log2(a2 / a3) : 0.f;
        }
        std::vector<float> sorted = tri_log;
        float              median = 0.f;
        if (!sorted.empty()) {
            std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
            median = sorted[sorted.size() / 2];
        }
        std::vector<float> sum(patch.vertices.size(), 0.f);
        std::vector<int>   cnt(patch.vertices.size(), 0);
        for (size_t f = 0; f < patch.indices.size(); ++f) {
            const float d = std::clamp(0.5f + (tri_log[f] - median) / 4.f, 0.f, 1.f);
            for (int k = 0; k < 3; ++k) {
                sum[patch.indices[f][k]] += d;
                ++cnt[patch.indices[f][k]];
            }
        }
        for (size_t v = 0; v < distortion.size(); ++v)
            if (cnt[v] > 0)
                distortion[v] = sum[v] / float(cnt[v]);
    }

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3T2 };
    init_data.reserve_vertices(patch.indices.size() * 3);
    init_data.reserve_indices(patch.indices.size() * 3);
    unsigned vcount = 0;
    for (size_t f = 0; f < patch.indices.size(); ++f) {
        const stl_triangle_vertex_indices &tri = patch.indices[f];
        for (int k = 0; k < 3; ++k) {
            const size_t vi = size_t(tri[k]);
            init_data.add_vertex(patch.vertices[vi], Vec3f(distortion[vi], 0.f, 0.f),
                                 have_corner_uvs ? corner_uv[f * 3 + size_t(k)] :
                                                   (have_uvs ? uv[vi] : Vec2f::Zero()));
        }
        init_data.add_triangle(vcount, vcount + 1, vcount + 2);
        vcount += 3;
    }

    m_uvcheck_glmodel.init_from(std::move(init_data));
}

void GLGizmoTextureDisplacement::render_uvcheck_mesh()
{
    const ModelObject *mo = m_c->selection_info()->model_object();
    const ModelVolume *mv = texture_volume();
    if (mo == nullptr || mv == nullptr || !m_uvcheck_glmodel.is_initialized())
        return;
    const TextureDisplacementLayer *layer = active_layer();
    if (layer == nullptr)
        return;
    GLShaderProgram *shader = wxGetApp().get_shader("texture_displacement_uvcheck");
    if (shader == nullptr)
        return;

    const Selection  &selection    = m_parent.get_selection();
    const Transform3d trafo_matrix = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();
    const Camera     &camera       = wxGetApp().plater()->get_camera();

    shader->start_using();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix() * trafo_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    shader->set_uniform("volume_world_matrix", trafo_matrix);
    const ClippingPlaneDataWrapper clp_data = this->get_clipping_plane_data();
    shader->set_uniform("clipping_plane", clp_data.clp_dataf);
    shader->set_uniform("z_range", clp_data.z_range);
    const Matrix3d view_normal_matrix = camera.get_view_matrix().matrix().block(0, 0, 3, 3);
    shader->set_uniform("view_normal_matrix", view_normal_matrix);
    shader->set_uniform("volume_mirrored", trafo_matrix.matrix().determinant() < 0.0);
    shader->set_uniform("tex_anchor", Vec3f(trafo_matrix.translation().cast<float>()));
    shader->set_uniform("mode", m_uv_check_mode == UVCheckMode::Distortion ? 1 : 0);
    shader->set_uniform("checker_freq", 4.f);
    shader->set_uniform("tiling_scale", layer->tiling_scale);
    shader->set_uniform("rotation_rad", layer->rotation_deg * float(M_PI) / 180.f);
    shader->set_uniform("uv_offset", layer->offset);
    shader->set_uniform("use_vertex_uv", m_uvcheck_uses_vertex_uv);
    shader->set_uniform("projection_mode", m_uvcheck_projection_mode);
    shader->set_uniform("patch_center", m_uvcheck_patch_center);
    shader->set_uniform("patch_axis", m_uvcheck_patch_axis);
    shader->set_uniform("tex_aspect", layer_texture_aspect(*layer));

    glsafe(::glEnable(GL_POLYGON_OFFSET_FILL));
    glsafe(::glPolygonOffset(-1.0f, -1.0f));
    m_uvcheck_glmodel.render();
    glsafe(::glDisable(GL_POLYGON_OFFSET_FILL));
    shader->stop_using();
}

void GLGizmoTextureDisplacement::build_wireframe_from_its(const indexed_triangle_set &its)
{
    m_wireframe_overlay_glmodel.reset();
    m_wireframe_overlay_vcount = its.vertices.size();
    if (its.indices.empty())
        return;

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
    init_data.reserve_vertices(its.vertices.size());
    init_data.reserve_indices(its.indices.size() * 6);
    for (const Vec3f &v : its.vertices)
        init_data.add_vertex(v);
    for (const stl_triangle_vertex_indices &tri : its.indices)
        for (int i = 0; i < 3; ++i)
            init_data.add_line(unsigned(tri[i]), unsigned(tri[(i + 1) % 3]));

    if (!init_data.is_empty())
        m_wireframe_overlay_glmodel.init_from(std::move(init_data));
}

void GLGizmoTextureDisplacement::rebuild_wireframe_overlay()
{
    if (!m_wireframe_overlay) {
        m_wireframe_overlay_glmodel.reset();
        m_wireframe_overlay_vcount = 0;
        return;
    }
    const ModelVolume *mv = texture_volume();
    if (mv == nullptr)
        return;
    const indexed_triangle_set &its = mv->mesh().its;
    if (its.indices.empty())
        return;

    if (m_wireframe_overlay_glmodel.is_initialized() && m_wireframe_overlay_vcount == its.vertices.size())
        return;
    build_wireframe_from_its(its);
}

void GLGizmoTextureDisplacement::refresh_wireframe()
{
    if (!m_wireframe_overlay) {
        m_wireframe_overlay_glmodel.reset();
        m_wireframe_overlay_vcount = 0;
        return;
    }
    if (!m_use_shaded_preview && !m_preview_its.indices.empty())
        build_wireframe_from_its(m_preview_its);
    else
        rebuild_wireframe_overlay();
}

void GLGizmoTextureDisplacement::render_wireframe_overlay()
{
    const ModelObject *mo = m_c->selection_info()->model_object();
    const ModelVolume *mv = texture_volume();
    if (mo == nullptr || mv == nullptr || !m_wireframe_overlay_glmodel.is_initialized())
        return;
    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    const Selection  &selection    = m_parent.get_selection();
    const Transform3d trafo_matrix = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();
    const Camera     &camera       = wxGetApp().plater()->get_camera();

    shader->start_using();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix() * trafo_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    glsafe(::glEnable(GL_POLYGON_OFFSET_LINE));
    glsafe(::glPolygonOffset(-1.0f, -1.0f));
    m_wireframe_overlay_glmodel.set_color(ColorRGBA(1.0f, 1.0f, 1.0f, 0.6f));
    m_wireframe_overlay_glmodel.render();
    glsafe(::glDisable(GL_POLYGON_OFFSET_LINE));
    shader->stop_using();
}

void GLGizmoTextureDisplacement::rebuild_preview()
{
    m_preview_generation->fetch_add(1);
    m_palette_changed = false;
    update_uv_editor();
    rebuild_shaded_preview_mesh();
    rebuild_paint_overlay();
    rebuild_uvcheck_mesh();
    rebuild_seam_overlay();
    if (m_subdivide_editing && m_subdivide_adaptive)
        rebuild_subdivide_preview();

    if (m_debug_stage >= 0)
        return;

    const ModelVolume *mv = texture_volume();
    if (mv == nullptr || !mv->is_texture_displacement_painted()) {
        m_preview_glmodel.reset();
        m_preview_its = indexed_triangle_set{};
        m_preview_job_pending = false;
        refresh_wireframe();
        return;
    }
    if (m_use_shaded_preview) {
        refresh_wireframe();
        m_preview_job_pending = false;
        return;
    }

    queue_preview_job();
}

void GLGizmoTextureDisplacement::queue_preview_job()
{
    if (m_state != On)
        return;
    const ModelVolume *mv = texture_volume();
    if (mv == nullptr || !mv->is_texture_displacement_painted())
        return;

    if (m_preview_job_running) {
        m_preview_job_pending = true;
        return;
    }

    const uint64_t generation = m_preview_generation->load();

    TextureDisplacementPreviewInput input;
    input.base_mesh = mv->mesh().its;
    input.layers    = mv->texture_displacement_layers;
    input.options   = mv->texture_displacement_options;
    if (input.options.pipeline_v2)
        input.options.v2_max_triangles_k = 0;
    input.volume_to_world = texture_displacement_volume_to_world(*mv);
    for (int i = 0; i < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++i)
        input.facets_data[size_t(i)] = mv->texture_displacement_facet(i).get_data();
    input.color = color_settings_for(*mv);
    std::vector<ColorRGBA> entry_colors;
    entry_colors.reserve(input.color.palette.size());
    for (const PrintableColor &e : input.color.palette)
        entry_colors.emplace_back(e.rgb.x(), e.rgb.y(), e.rgb.z(), 1.f);

    m_preview_job_running = true;
    auto &worker = wxGetApp().plater()->get_ui_job_worker();
    queue_job(worker, std::make_unique<TextureDisplacementPreviewJob>(std::move(input), generation, m_preview_generation,
        [this, entry_colors](TextureDisplacementPreviewResult result, uint64_t result_generation) {
            indexed_triangle_set its = std::move(result.mesh);
            m_preview_job_running = false;
            if (result_generation != m_preview_generation->load()) {
                m_preview_job_pending = true;
            } else if (its.indices.empty()) {
            } else {
                m_preview_glmodel.reset();
                m_preview_color_runs.clear();
                if (result.triangle_color.size() == its.indices.size() && !entry_colors.empty()) {
                    indexed_triangle_set sorted;
                    sorted.vertices = its.vertices;
                    sorted.indices.reserve(its.indices.size());
                    for (int want = 0; want <= int(entry_colors.size()); ++want) {
                        const size_t first = sorted.indices.size();
                        for (size_t i = 0; i < its.indices.size(); ++i)
                            if (int(result.triangle_color[i]) == want)
                                sorted.indices.push_back(its.indices[i]);
                        if (sorted.indices.size() == first)
                            continue;
                        m_preview_color_runs.push_back(
                            { { first * 3, sorted.indices.size() * 3 },
                              want == 0 ? GLVolume::NEUTRAL_COLOR : entry_colors[size_t(want - 1)] });
                    }
                    m_preview_glmodel.init_from(sorted);
                } else {
                    m_preview_glmodel.init_from(its);
                }
                m_preview_glmodel.set_color(GLVolume::NEUTRAL_COLOR);
                m_preview_its = std::move(its);
                refresh_wireframe();
                m_paint_overlay_dirty = true;
            }
            if (m_preview_job_pending) {
                m_preview_job_pending = false;
                if (!m_use_shaded_preview)
                    queue_preview_job();
            }
            m_parent.set_as_dirty();
        }));
}

void GLGizmoTextureDisplacement::update_uv_editor()
{
    Plater         *plater    = wxGetApp().plater();
    UVEditorCanvas *uv_canvas = plater->get_uv_editor_canvas();
    if (uv_canvas == nullptr)
        return;
    uv_canvas->set_command_callback([this](UVEditorCanvas::Command cmd, float value) { on_uv_command(int(cmd), value); });

    const ModelVolume        *mv    = texture_volume();
    TextureDisplacementLayer *layer = active_layer();
    if (!m_show_uv_editor || mv == nullptr || layer == nullptr ||
        layer->projection_method != TextureProjectionMethod::LSCM) {
        m_uv_editor_bg = UVBackground::None;
        push_uv_pane_state();
        plater->show_uv_editor(false);
        return;
    }

    {
        size_t sig = 1469598103934665603ull;
        const auto mix = [&sig](uint64_t x) { sig = (sig ^ x) * 1099511628211ull; };
        mix(layer->lscm_uv_overrides.size());
        for (const auto &[v, uv] : layer->lscm_uv_overrides) {
            mix(uint64_t(uint32_t(v)));
            mix(uint64_t(uint32_t(int32_t(std::llround(uv.x() * 1024.f)))));
            mix(uint64_t(uint32_t(int32_t(std::llround(uv.y() * 1024.f)))));
        }
        if (sig != m_uv_overrides_sig) {
            m_uv_overrides_sig  = sig;
            m_uv_unwrap_pending = true;
        }
    }

    UVEditorState state;
    state.slot       = m_active_layer_slot;
    state.image_data = layer->image_data.get();
    state.seam_angle = layer->lscm_seam_angle_deg;
    state.padding    = layer->island_padding_mm;
    state.facets     = mv->texture_displacement_facet(m_active_layer_slot).get_data();
    state.seam_edges = layer->lscm_seam_edges;

    bool unwrap_changed = false;
    if (m_uv_unwrap_pending) {
        m_uv_unwrap_pending = false;
        const indexed_triangle_set patch = patch_in_world(extract_painted_patch(mv->mesh().its, state.facets));
        if (patch.indices.empty()) {
            m_uv_editor_state  = UVEditorState{};
            m_uv_editor_unwrap = PatchUnwrap{};
            m_uv_editor_distortion_colors.clear();
            uv_canvas->set_islands({});
            push_uv_pane_state();
            plater->show_uv_editor(true);
            return;
        }
        m_uv_editor_unwrap = compute_patch_unwrap(patch, layer->lscm_seam_angle_deg, 0.f, layer->lscm_seam_edges);
        m_island_overlay_glmodel.reset();
        m_island_overlay_selection.clear();
        apply_lscm_uv_overrides(m_uv_editor_unwrap, layer->lscm_uv_overrides);
        m_uv_editor_state  = std::move(state);
        unwrap_changed     = true;
        compute_uv_editor_distortion_colors(patch);
    }

    const UVBackground desired_bg = (m_uv_check_mode == UVCheckMode::Checker) ? UVBackground::Checker : UVBackground::Height;
    const bool bg_smoothing_changed = (desired_bg == UVBackground::Height) && (m_uv_editor_bg_smoothing != layer->smoothing);
    if (desired_bg != m_uv_editor_bg || layer->image_data.get() != m_uv_editor_bg_image || bg_smoothing_changed) {
        m_uv_editor_bg_smoothing = layer->smoothing;
        m_uv_editor_bg_image     = layer->image_data.get();
        m_uv_editor_bg           = desired_bg;
        if (desired_bg == UVBackground::Checker) {
            constexpr int              tex = 512, squares = 8, cell = tex / squares;
            std::vector<unsigned char> checker(size_t(tex) * size_t(tex));
            for (int y = 0; y < tex; ++y)
                for (int x = 0; x < tex; ++x)
                    checker[size_t(y) * tex + x] = ((x / cell + y / cell) & 1) ? 205 : 70;
            uv_canvas->set_background_texture(checker, tex, tex);
        } else {
            const DecodedHeightTexture height = decode_height_texture(*layer);
            if (!height.empty()) {
                uv_canvas->set_background_texture(height.pixels, height.width, height.height);
            } else {
                uv_canvas->set_background_texture({}, 0, 0);
                m_uv_editor_bg = UVBackground::None;
            }
        }
    }
    uv_canvas->set_uv_transform(layer->tiling_scale, layer->rotation_deg, layer->tile_enabled,
                                layer->tile_method == TextureTileMethod::MirroredRepeat);

    if (m_uv_editor_unwrap.empty()) {
        push_uv_pane_state();
        plater->show_uv_editor(true);
        return;
    }

    if (layer->islands.size() < size_t(m_uv_editor_unwrap.chart_count))
        layer->islands.resize(size_t(m_uv_editor_unwrap.chart_count));

    if (unwrap_changed && m_uv_apply_connected_net && layer->auto_connect_islands) {
        std::vector<TextureIsland> net = compute_connected_net(m_uv_editor_unwrap);
        if (net.size() == size_t(m_uv_editor_unwrap.chart_count)) {
            if (layer->islands.size() < net.size())
                layer->islands.resize(net.size());
            for (size_t i = 0; i < net.size(); ++i)
                layer->islands[i] = net[i];
        }
    }
    if (unwrap_changed)
        m_uv_apply_connected_net = false;
    if (unwrap_changed) {
        UVEditorCanvas::Islands view;
        view.uvs            = m_uv_editor_unwrap.uvs;
        view.indices        = m_uv_editor_unwrap.indices;
        view.vertex_island  = m_uv_editor_unwrap.vertex_chart;
        view.boundary_edges = m_uv_editor_unwrap.boundary_edges;
        view.island_count   = m_uv_editor_unwrap.chart_count;

        uv_canvas->set_island_edit_callback(
            [this](int island, const Vec2f &offset_delta, float rotation_delta, float scale_factor, bool finished) {
                on_island_edited(island, offset_delta, rotation_delta, scale_factor, finished);
            });
        uv_canvas->set_vertex_edit_callback(
            [this](const std::vector<std::pair<int, Vec2f>> &edits) { on_uv_vertex_edited(edits); });
        uv_canvas->set_islands(std::move(view));
    }

    uv_canvas->set_select_mode(static_cast<UVEditorCanvas::SelectMode>(m_uv_select_mode));
    uv_canvas->set_island_transforms(uv_editor_island_transforms(*layer));
    if (m_uv_check_mode == UVCheckMode::Distortion)
        uv_canvas->set_island_fill_colors(m_uv_editor_distortion_colors);
    else
        uv_canvas->set_island_fill_colors({});

    push_uv_pane_state();
    plater->show_uv_editor(true);
}

void GLGizmoTextureDisplacement::compute_uv_editor_distortion_colors(const indexed_triangle_set &patch)
{
    m_uv_editor_distortion_colors.clear();
    const PatchUnwrap &u = m_uv_editor_unwrap;
    if (u.empty() || u.chart_count <= 0)
        return;

    std::vector<float> tri_log(u.indices.size(), 0.f);
    std::vector<int>   tri_chart(u.indices.size(), -1);
    for (size_t f = 0; f < u.indices.size(); ++f) {
        const stl_triangle_vertex_indices &t = u.indices[f];
        if (t[0] < 0 || size_t(t[0]) >= u.vertex_chart.size())
            continue;
        tri_chart[f] = u.vertex_chart[size_t(t[0])];
        const auto p3 = [&](int uv_idx) -> Vec3f {
            const int sv = (size_t(uv_idx) < u.source_vertex.size()) ? u.source_vertex[size_t(uv_idx)] : -1;
            return (sv >= 0 && size_t(sv) < patch.vertices.size()) ? patch.vertices[size_t(sv)] : Vec3f::Zero();
        };
        const float a3 = 0.5f * (p3(t[1]) - p3(t[0])).cross(p3(t[2]) - p3(t[0])).norm();
        const Vec2f e0 = u.uvs[size_t(t[1])] - u.uvs[size_t(t[0])];
        const Vec2f e1 = u.uvs[size_t(t[2])] - u.uvs[size_t(t[0])];
        const float a2 = 0.5f * std::abs(e0.x() * e1.y() - e0.y() * e1.x());
        tri_log[f] = (a3 > 1e-12f && a2 > 1e-12f) ? std::log2(a2 / a3) : 0.f;
    }

    std::vector<float> sorted = tri_log;
    float              median = 0.f;
    if (!sorted.empty()) {
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        median = sorted[sorted.size() / 2];
    }

    std::vector<double> chart_sum(size_t(u.chart_count), 0.0);
    std::vector<int>    chart_cnt(size_t(u.chart_count), 0);
    for (size_t f = 0; f < tri_log.size(); ++f) {
        const int c = tri_chart[f];
        if (c >= 0 && c < u.chart_count) {
            chart_sum[size_t(c)] += tri_log[f];
            ++chart_cnt[size_t(c)];
        }
    }

    const auto heat = [](float t) -> ColorRGBA {
        t = std::clamp(t, 0.f, 1.f);
        const ColorRGBA blue{ 0.15f, 0.35f, 1.0f, 0.5f }, green{ 0.2f, 0.9f, 0.3f, 0.5f }, red{ 1.0f, 0.2f, 0.15f, 0.5f };
        if (t < 0.5f) { const float s = t * 2.f;         return blue * (1.f - s) + green * s; }
        const float s = (t - 0.5f) * 2.f;                return green * (1.f - s) + red * s;
    };

    m_uv_editor_distortion_colors.resize(size_t(u.chart_count), heat(0.5f));
    for (int c = 0; c < u.chart_count; ++c)
        if (chart_cnt[size_t(c)] > 0) {
            const float avg = float(chart_sum[size_t(c)] / chart_cnt[size_t(c)]);
            m_uv_editor_distortion_colors[size_t(c)] = heat(std::clamp(0.5f + (avg - median) / 4.f, 0.f, 1.f));
        }
}

void GLGizmoTextureDisplacement::on_uv_command(int cmd, float value)
{
    m_uv_command_queue.emplace_back(cmd, value);
    m_parent.set_as_dirty();
}

void GLGizmoTextureDisplacement::process_uv_commands()
{
    std::vector<std::pair<int, float>> queue;
    queue.swap(m_uv_command_queue);
    for (const auto &[cmd, value] : queue)
        run_uv_command(cmd, value);
}

void GLGizmoTextureDisplacement::apply_view_mode(int mode)
{
    m_use_shaded_preview = (mode == 1);
    m_uv_check_mode    = (mode == 2) ? UVCheckMode::Checker :
                         (mode == 3) ? UVCheckMode::Distortion : UVCheckMode::None;
    rebuild_uvcheck_mesh();
    if (m_use_shaded_preview)
        rebuild_shaded_preview_mesh();
    else
        queue_preview_job();
    m_paint_overlay_dirty = true;
    refresh_wireframe();
    update_uv_editor();
    m_parent.set_as_dirty();
}

void GLGizmoTextureDisplacement::push_uv_pane_state()
{
    UVEditorCanvas *canvas = wxGetApp().plater()->get_uv_editor_canvas();
    if (canvas == nullptr)
        return;

    UVEditorCanvas::PaneState       st;
    const ModelVolume              *mv    = texture_volume();
    const TextureDisplacementLayer *layer = active_layer();
    if (mv != nullptr && layer != nullptr && layer->projection_method == TextureProjectionMethod::LSCM) {
        st.has_layer       = true;
        st.layer_name      = from_u8(layer->name.empty() ? Slic3r::format(_u8L("Layer %1%"), layer->slot + 1) : layer->name);
        st.tile_mm         = layer->tiling_scale;
        st.seam_angle_deg  = layer->lscm_seam_angle_deg;
        st.connect_islands = layer->auto_connect_islands;
        st.has_seams       = !layer->lscm_seam_edges.empty();
        st.has_uv_edits    = !layer->lscm_uv_overrides.empty();

        if (layer->image_data.get() != m_uv_thumb_source || m_uv_thumb_rgb.empty()) {
            m_uv_thumb_source = nullptr;
            m_uv_thumb_rgb.clear();
            const DecodedHeightTexture tex = decode_height_texture(*layer);
            if (!tex.empty()) {
                m_uv_thumb_source = layer->image_data.get();
                const int  n     = UV_THUMB_PX;
                const bool color = tex.has_color();
                m_uv_thumb_rgb.resize(size_t(n) * n * 3);
                for (int y = 0; y < n; ++y) {
                    const int y0 = y * tex.height / n, y1 = std::max(y0 + 1, (y + 1) * tex.height / n);
                    for (int x = 0; x < n; ++x) {
                        const int x0 = x * tex.width / n, x1 = std::max(x0 + 1, (x + 1) * tex.width / n);
                        uint64_t  sum[3]{};
                        for (int sy = y0; sy < y1; ++sy)
                            for (int sx = x0; sx < x1; ++sx) {
                                const size_t src = size_t(sy) * size_t(tex.width) + size_t(sx);
                                for (int ch = 0; ch < 3; ++ch)
                                    sum[ch] += color ? tex.rgb[src * 3 + ch] : tex.pixels[src];
                            }
                        const uint64_t count = uint64_t(y1 - y0) * uint64_t(x1 - x0);
                        for (int ch = 0; ch < 3; ++ch)
                            m_uv_thumb_rgb[size_t(y * n + x) * 3 + ch] = (unsigned char) (sum[ch] / count);
                    }
                }
            }
        }
        if (!m_uv_thumb_rgb.empty()) {
            st.thumbnail_px  = UV_THUMB_PX;
            st.thumbnail_rgb = m_uv_thumb_rgb;
        }

        if (!m_uv_editor_unwrap.empty()) {
            UVEditorState now;
            now.slot        = m_active_layer_slot;
            now.image_data  = layer->image_data.get();
            now.seam_angle  = layer->lscm_seam_angle_deg;
            now.padding     = layer->island_padding_mm;
            now.facets      = mv->texture_displacement_facet(m_active_layer_slot).get_data();
            now.seam_edges  = layer->lscm_seam_edges;
            st.unwrapped    = true;
            st.unwrap_stale = !(now == m_uv_editor_state);
            st.island_count = m_uv_editor_unwrap.chart_count;
            st.face_count   = m_uv_editor_unwrap.indices.size();
        }
    }
    st.mark_seams = m_seam_edit_mode;
    st.seam_path  = m_seam_path_mode;
    st.background = m_uv_check_mode == UVCheckMode::Checker    ? UVEditorCanvas::Background::Checker :
                    m_uv_check_mode == UVCheckMode::Distortion ? UVEditorCanvas::Background::Distortion :
                                                                 UVEditorCanvas::Background::Height;
    canvas->set_pane_state(std::move(st));
}

void GLGizmoTextureDisplacement::run_uv_command(int cmd, float value)
{
    using Command = UVEditorCanvas::Command;
    if (cmd == int(Command::PaneClosed)) {
        m_show_uv_editor = false;
        m_uv_editor_bg   = UVBackground::None;
        if (m_seam_edit_mode) {
            m_seam_edit_mode    = false;
            m_seam_hover_edge   = { -1, -1 };
            m_seam_hover_vertex = -1;
            m_seam_hover_glmodel.reset();
            m_seam_path_anchor  = -1;
            m_seam_anchor_glmodel.reset();
            push_uv_pane_state();
        }
        return;
    }
    if (cmd == int(Command::SetBackground)) {
        const int background = std::clamp(int(std::lround(value)), 0, 2);
        apply_view_mode(background == 1 ? 2 : background == 2 ? 3 : (m_use_shaded_preview ? 1 : 0));
        return;
    }

    TextureDisplacementLayer *layer = active_layer();
    if (layer == nullptr)
        return;

    if (cmd == int(UVEditorCanvas::Command::AverageScale)) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Average island scale"), UndoRedo::SnapshotType::GizmoAction);
        average_island_scales(layer->islands);
        rebuild_preview();
    } else if (cmd == int(UVEditorCanvas::Command::CutSelectedIsland)) {
        UVEditorCanvas *canvas = wxGetApp().plater()->get_uv_editor_canvas();
        const int       chart  = canvas != nullptr ? canvas->selected_island() : -1;
        if (chart < 0) {
            show_error(nullptr, _u8L("Select an island in the UV editor first."));
            return;
        }
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Cut texture island"), UndoRedo::SnapshotType::GizmoAction);
        cut_island(*layer, chart);
        rebuild_preview();
    } else if (cmd == int(UVEditorCanvas::Command::JoinSelected)) {
        UVEditorCanvas *canvas = wxGetApp().plater()->get_uv_editor_canvas();
        const int       chart  = canvas != nullptr ? canvas->selected_island() : -1;
        if (chart < 0) {
            show_error(nullptr, _u8L("Select an island in the UV editor first."));
            return;
        }
        const Eigen::Matrix<float, 2, 3> sel_m = island_transform_matrix(chart, m_uv_editor_unwrap, layer->islands);
        const Vec2f sel_c = sel_m.block<2, 2>(0, 0) * m_uv_editor_unwrap.chart_centroid[size_t(chart)] + sel_m.col(2);
        int   best_parent = -1;
        float best_d2     = std::numeric_limits<float>::max();
        TextureIsland best_place, cand;
        for (int p = 0; p < m_uv_editor_unwrap.chart_count; ++p) {
            if (p == chart)
                continue;
            if (!join_chart_placement(m_uv_editor_unwrap, layer->islands, chart, p, cand))
                continue;
            const Eigen::Matrix<float, 2, 3> pm = island_transform_matrix(p, m_uv_editor_unwrap, layer->islands);
            const Vec2f pc = pm.block<2, 2>(0, 0) * m_uv_editor_unwrap.chart_centroid[size_t(p)] + pm.col(2);
            const float d2 = (pc - sel_c).squaredNorm();
            if (d2 < best_d2) { best_d2 = d2; best_parent = p; best_place = cand; }
        }
        if (best_parent < 0) {
            show_error(nullptr, _u8L("This island has no neighbour it shares an edge with."));
            return;
        }
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Join texture island"), UndoRedo::SnapshotType::GizmoAction);
        if (layer->islands.size() <= size_t(chart))
            layer->islands.resize(size_t(chart) + 1);
        layer->islands[size_t(chart)] = best_place;
        join_island_groups(layer->island_groups, chart, best_parent, m_uv_editor_unwrap.chart_count);
        rebuild_preview();
    } else if (cmd == int(UVEditorCanvas::Command::UnjoinSelected)) {
        UVEditorCanvas *canvas = wxGetApp().plater()->get_uv_editor_canvas();
        const int       chart  = canvas != nullptr ? canvas->selected_island() : -1;
        if (chart < 0 || size_t(chart) >= layer->islands.size()) {
            show_error(nullptr, _u8L("Select an island in the UV editor first."));
            return;
        }
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Unjoin texture island"), UndoRedo::SnapshotType::GizmoAction);
        layer->islands[size_t(chart)] = TextureIsland{};
        if (size_t(chart) < layer->island_groups.size())
            layer->island_groups[size_t(chart)] = chart;
        rebuild_preview();
    }
    else if (cmd == int(Command::PickTexture)) {
        m_picker_slot         = m_active_layer_slot;
        m_picker_open_request = true;
    } else if (cmd == int(Command::Unwrap)) {
        m_uv_unwrap_pending      = true;
        m_uv_apply_connected_net = true;
        m_show_uv_editor         = true;
        update_uv_editor();
    } else if (cmd == int(Command::SetSeamAngle)) {
        const float angle = std::clamp(value, 5.f, 90.f);
        if (angle != layer->lscm_seam_angle_deg) {
            layer->lscm_seam_angle_deg = angle;
            rebuild_preview();
        }
    } else if (cmd == int(Command::SetConnectIslands)) {
        const bool connect = value != 0.f;
        if (connect != layer->auto_connect_islands) {
            layer->auto_connect_islands = connect;
            if (connect && !m_uv_editor_unwrap.empty()) {
                std::vector<TextureIsland> net = compute_connected_net(m_uv_editor_unwrap);
                if (net.size() == size_t(m_uv_editor_unwrap.chart_count)) {
                    if (layer->islands.size() < net.size())
                        layer->islands.resize(net.size());
                    for (size_t i = 0; i < net.size(); ++i)
                        layer->islands[i] = net[i];
                }
            }
            rebuild_preview();
        }
    } else if (cmd == int(Command::SetSelectMode)) {
        m_uv_select_mode = std::clamp(int(std::lround(value)), 0, 2);
    } else if (cmd == int(Command::SetMarkSeams)) {
        m_seam_edit_mode = value != 0.f;
        if (m_seam_edit_mode)
            m_adjust_texture_mode = false;
        else {
            m_seam_hover_edge   = { -1, -1 };
            m_seam_hover_vertex = -1;
            m_seam_hover_glmodel.reset();
            m_seam_path_anchor = -1;
            m_seam_anchor_glmodel.reset();
        }
        push_uv_pane_state();
    } else if (cmd == int(Command::SetSeamPath)) {
        m_seam_path_mode    = value != 0.f;
        m_seam_path_anchor  = -1;
        m_seam_hover_edge   = { -1, -1 };
        m_seam_hover_vertex = -1;
        m_seam_hover_glmodel.reset();
        m_seam_anchor_glmodel.reset();
        push_uv_pane_state();
    } else if (cmd == int(Command::ClearSeams)) {
        if (!layer->lscm_seam_edges.empty()) {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Clear texture seams"), UndoRedo::SnapshotType::GizmoAction);
            layer->lscm_seam_edges.clear();
            rebuild_preview();
        }
    } else if (cmd == int(Command::ClearUVEdits)) {
        if (!layer->lscm_uv_overrides.empty()) {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Clear texture UV edits"), UndoRedo::SnapshotType::GizmoAction);
            layer->lscm_uv_overrides.clear();
            m_uv_unwrap_pending = true;
            update_uv_editor();
            rebuild_preview();
        }
    }
    m_parent.set_as_dirty();
}

void GLGizmoTextureDisplacement::capture_view_projection(TextureDisplacementLayer &layer)
{
    const ModelVolume *mv = texture_volume();
    const ModelObject *mo = m_c->selection_info()->model_object();
    if (mv == nullptr || mo == nullptr)
        return;

    const Camera     &camera    = wxGetApp().plater()->get_camera();
    const Selection  &selection = m_parent.get_selection();
    const Transform3d trafo     = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();

    const Matrix3d view_rot   = camera.get_view_matrix().matrix().block<3, 3>(0, 0);
    const Matrix3d trafo_rot  = trafo.matrix().block<3, 3>(0, 0);
    const Matrix3d world_to_local = trafo_rot.inverse();
    const Vec3d    local_right = world_to_local * Vec3d(view_rot.row(0).transpose());
    const Vec3d    local_up    = world_to_local * Vec3d(view_rot.row(1).transpose());

    layer.view_project_right = local_right.norm() > 1e-9 ? Vec3f(local_right.normalized().cast<float>()) : Vec3f::UnitX();
    layer.view_project_up    = local_up.norm() > 1e-9 ? Vec3f(local_up.normalized().cast<float>()) : Vec3f::UnitY();
}

void GLGizmoTextureDisplacement::show_projector(bool show)
{
    if (!show) {
        if (m_projector_frame != nullptr)
            m_projector_frame->Hide();
        return;
    }

    if (m_projector_frame == nullptr) {
        m_projector_frame         = new TextureProjectorFrame(wxGetApp().mainframe);
        m_projector_tex_source    = nullptr;
        m_projector_tex_smoothing = -1.f;
        m_projector_frame->set_opacity(m_projector_opacity);

        if (const wxGLCanvas *cnv = m_parent.get_wxglcanvas(); cnv != nullptr) {
            const wxRect  area = cnv->GetScreenRect();
            const wxSize  size(std::max(160, area.width / 2), std::max(160, area.height / 2));
            m_projector_frame->SetSize(wxRect(area.GetTopLeft() + wxPoint((area.width - size.x) / 2,
                                                                         (area.height - size.y) / 2),
                                              size));
        }
    }

    m_projector_frame->Show();
    m_projector_frame->Raise();
    update_projector();
}

void GLGizmoTextureDisplacement::update_projector()
{
    if (m_projector_frame == nullptr || !m_projector_frame->IsShown())
        return;

    const TextureDisplacementLayer *layer = active_layer();
    if (layer == nullptr || layer->projection_method != TextureProjectionMethod::ViewProjected) {
        m_projector_frame->set_texture({}, 0, 0);
        m_projector_tex_source    = nullptr;
        m_projector_tex_smoothing = -1.f;
        return;
    }

    if (m_projector_tex_source != layer->image_data.get() || m_projector_tex_smoothing != layer->smoothing) {
        const DecodedHeightTexture height = decode_height_texture(*layer);
        if (height.empty())
            m_projector_frame->set_texture({}, 0, 0);
        else
            m_projector_frame->set_texture(height.pixels, height.width, height.height);
        m_projector_tex_source    = layer->image_data.get();
        m_projector_tex_smoothing = layer->smoothing;
    }
}

int GLGizmoTextureDisplacement::apply_projection_frame()
{
    TextureDisplacementLayer *layer = active_layer();
    const ModelVolume        *mv    = texture_volume();
    const ModelObject        *mo    = m_c->selection_info()->model_object();
    wxGLCanvas               *cnv   = m_parent.get_wxglcanvas();
    if (layer == nullptr || mv == nullptr || mo == nullptr || cnv == nullptr || m_projector_frame == nullptr ||
        !m_projector_frame->IsShown())
        return -1;

    const wxRect  gate   = m_projector_frame->client_rect_on_screen();
    const wxPoint tl     = cnv->ScreenToClient(gate.GetTopLeft());
    const double  scale  = double(m_parent.get_scale());
    const double  rx = double(tl.x) * scale, ry = double(tl.y) * scale;
    const double  rw = double(gate.width) * scale, rh = double(gate.height) * scale;
    if (rw < 1.0 || rh < 1.0)
        return -1;

    const Camera             &camera = wxGetApp().plater()->get_camera();
    const std::array<int, 4> &vp     = camera.get_viewport();
    const Selection          &sel    = m_parent.get_selection();
    const Transform3d trafo = mo->instances[sel.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();

    const Eigen::Matrix4d K = camera.get_projection_matrix().matrix() * camera.get_view_matrix().matrix() * trafo.matrix();

    const double A = double(vp[2]) / (2.0 * rw);
    const double B = (double(vp[0]) + double(vp[2]) * 0.5 - rx) / rw;
    const double C = -double(vp[3]) / (2.0 * rh);
    const double D = (double(vp[3]) * 0.5 - double(vp[1]) - ry) / rh;

    const Eigen::Vector4d row_u = A * K.row(0).transpose() + B * K.row(3).transpose();
    const Eigen::Vector4d row_v = C * K.row(1).transpose() + D * K.row(3).transpose();
    const Eigen::Vector4d row_w = K.row(3).transpose();

    std::array<float, 12> m{};
    for (int i = 0; i < 4; ++i) {
        m[size_t(i)]     = float(row_u[i]);
        m[size_t(4 + i)] = float(row_v[i]);
        m[size_t(8 + i)] = float(row_w[i]);
    }

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Apply texture projection frame"),
                                  UndoRedo::SnapshotType::GizmoAction);

    layer->projection_method       = TextureProjectionMethod::ViewProjected;
    layer->view_project_projective = true;
    layer->view_project_matrix     = m;
    layer->tile_enabled = false;
    capture_view_projection(*layer);

    const int painted = select_visible_faces(&m);
    m_preview_params_dirty = true;
    rebuild_preview();
    m_parent.set_as_dirty();
    return painted;
}

void GLGizmoTextureDisplacement::cut_island(TextureDisplacementLayer &layer, int chart)
{
    const ModelVolume *mv = texture_volume();
    if (mv == nullptr)
        return;
    const std::vector<Vec3f> &verts = mv->mesh().its.vertices;
    const PatchUnwrap        &u     = m_uv_editor_unwrap;

    std::vector<std::array<int, 3>> tris;
    Vec3f lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    Vec3f hi(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
    for (const stl_triangle_vertex_indices &t : u.indices) {
        if (t[0] < 0 || size_t(t[0]) >= u.vertex_chart.size() || u.vertex_chart[size_t(t[0])] != chart)
            continue;
        std::array<int, 3> bt{};
        bool               ok = true;
        for (int k = 0; k < 3; ++k) {
            const int uvv = t[k];
            if (uvv < 0 || size_t(uvv) >= u.source_vertex.size()) { ok = false; break; }
            const int base = u.source_vertex[size_t(uvv)];
            if (base < 0 || size_t(base) >= verts.size()) { ok = false; break; }
            bt[k] = base;
            lo    = lo.cwiseMin(verts[size_t(base)]);
            hi    = hi.cwiseMax(verts[size_t(base)]);
        }
        if (ok)
            tris.push_back(bt);
    }
    if (tris.empty())
        return;

    const Vec3f  ext  = hi - lo;
    const int    axis = (ext.x() >= ext.y() && ext.x() >= ext.z()) ? 0 : (ext.y() >= ext.z() ? 1 : 2);
    const float  mid  = 0.5f * (lo[axis] + hi[axis]);

    std::set<std::pair<int, int>> seams(layer.lscm_seam_edges.begin(), layer.lscm_seam_edges.end());
    for (const std::array<int, 3> &bt : tris)
        for (int i = 0; i < 3; ++i) {
            const int a = bt[i], b = bt[(i + 1) % 3];
            if ((verts[size_t(a)][axis] < mid) != (verts[size_t(b)][axis] < mid))
                seams.insert({ std::min(a, b), std::max(a, b) });
        }
    layer.lscm_seam_edges.assign(seams.begin(), seams.end());
}

std::vector<Eigen::Matrix<float, 2, 3>>
GLGizmoTextureDisplacement::uv_editor_island_transforms(const TextureDisplacementLayer &layer)
{
    const float uv_scale = (layer.tiling_scale > 1e-6f) ? (1.f / layer.tiling_scale) : 1.f;
    const float rad      = layer.rotation_deg * float(M_PI) / 180.f;
    const float cs       = std::cos(rad) * uv_scale;
    const float sn       = std::sin(rad) * uv_scale;

    Eigen::Matrix2f uv_linear;
    uv_linear << cs, -sn,
                 sn,  cs;

    m_uv_editor_bbox_min = Vec2f(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    m_uv_editor_bbox_max = Vec2f(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

    std::vector<Eigen::Matrix<float, 2, 3>> transforms(size_t(std::max(m_uv_editor_unwrap.chart_count, 0)));
    for (int c = 0; c < m_uv_editor_unwrap.chart_count; ++c) {
        const Eigen::Matrix<float, 2, 3> island = island_transform_matrix(c, m_uv_editor_unwrap, layer.islands);
        Eigen::Matrix<float, 2, 3>      &m      = transforms[size_t(c)];
        m.block<2, 2>(0, 0) = uv_linear * island.block<2, 2>(0, 0);
        m.col(2)            = uv_linear * island.col(2) + layer.offset;
    }

    for (size_t i = 0; i < m_uv_editor_unwrap.uvs.size(); ++i) {
        const int c = m_uv_editor_unwrap.vertex_chart[i];
        if (c < 0 || size_t(c) >= transforms.size())
            continue;
        const Vec2f uv = transforms[size_t(c)].block<2, 2>(0, 0) * m_uv_editor_unwrap.uvs[i] + transforms[size_t(c)].col(2);
        m_uv_editor_bbox_min = m_uv_editor_bbox_min.cwiseMin(uv);
        m_uv_editor_bbox_max = m_uv_editor_bbox_max.cwiseMax(uv);
    }
    return transforms;
}

void GLGizmoTextureDisplacement::on_island_edited(int island, const Vec2f &offset_delta, float rotation_delta,
                                                  float scale_factor, bool finished)
{
    TextureDisplacementLayer *layer = active_layer();
    if (layer == nullptr || island < 0)
        return;
    if (size_t(island) >= layer->islands.size())
        layer->islands.resize(size_t(island) + 1);

    const bool is_move = rotation_delta == 0.f && scale_factor == 1.f;

    if (!m_island_drag_active) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Move texture island"), UndoRedo::SnapshotType::GizmoAction);
        m_island_drag_active = true;
        m_island_move_set = is_move ? build_island_move_set(*layer, island) : std::vector<int>{ island };
        m_shaded_active_chart = island;
        m_shaded_island_delta  = Eigen::Matrix<float, 2, 3>::Identity();
        m_shaded_preview_dirty = true;
    }

    const std::vector<int>  single{ island };
    const std::vector<int> &targets = (is_move && !m_island_move_set.empty()) ? m_island_move_set : single;
    for (int c : targets) {
        if (c < 0)
            continue;
        if (size_t(c) >= layer->islands.size())
            layer->islands.resize(size_t(c) + 1);
        TextureIsland &target = layer->islands[size_t(c)];
        target.offset += offset_delta;
        if (c == island) {
            target.rotation_deg += rotation_delta;
            target.scale = std::clamp(target.scale * scale_factor, 0.001f, 1000.f);
        }
    }

    if (finished) {
        m_island_drag_active = false;
        m_shaded_active_chart  = -1;
        m_shaded_active_face.clear();
        m_island_move_set.clear();
        m_shaded_island_delta  = Eigen::Matrix<float, 2, 3>::Identity();
        rebuild_preview();
    } else {
        const std::vector<Eigen::Matrix<float, 2, 3>> xf = uv_editor_island_transforms(*layer);
        if (UVEditorCanvas *uv_canvas = wxGetApp().plater()->get_uv_editor_canvas()) {
            uv_canvas->set_island_transforms(xf);
        }
        if (m_use_shaded_preview && m_shaded_active_chart == island && size_t(island) < xf.size()) {
            Eigen::Matrix3f cur = Eigen::Matrix3f::Identity();
            cur.topRows<2>()    = xf[size_t(island)];
            Eigen::Matrix3f bak = Eigen::Matrix3f::Identity();
            bak.topRows<2>()    = m_shaded_baked_active_xf;
            m_shaded_island_delta = (cur * bak.inverse()).topRows<2>();
            m_parent.set_as_dirty();
        }
    }
}

void GLGizmoTextureDisplacement::on_uv_vertex_edited(const std::vector<std::pair<int, Vec2f>> &edits)
{
    TextureDisplacementLayer *layer = active_layer();
    if (layer == nullptr || edits.empty() || m_uv_editor_unwrap.empty())
        return;

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Edit texture UV"), UndoRedo::SnapshotType::GizmoAction);

    for (const auto &[unwrapped, raw_uv] : edits) {
        if (unwrapped < 0 || size_t(unwrapped) >= m_uv_editor_unwrap.source_vertex.size())
            continue;
        if (size_t(unwrapped) < m_uv_editor_unwrap.uvs.size())
            m_uv_editor_unwrap.uvs[size_t(unwrapped)] = raw_uv;

        const int key = lscm_uv_override_key(unwrapped);
        auto it = std::find_if(layer->lscm_uv_overrides.begin(), layer->lscm_uv_overrides.end(),
                               [key](const std::pair<int, Vec2f> &p) { return p.first == key; });
        if (it != layer->lscm_uv_overrides.end())
            it->second = raw_uv;
        else
            layer->lscm_uv_overrides.emplace_back(key, raw_uv);
    }

    rebuild_preview();
}

bool GLGizmoTextureDisplacement::update_adjust_anchor()
{
    const ModelVolume *mv = texture_volume();
    m_adjust_anchor_valid = mv != nullptr &&
        compute_layer_paint_anchor(mv->mesh().its, mv->texture_displacement_facet(m_active_layer_slot).get_data(),
                                    m_adjust_anchor_pos, m_adjust_anchor_normal);
    return m_adjust_anchor_valid;
}

TextureDisplacementLayer *GLGizmoTextureDisplacement::active_layer()
{
    ModelVolume *mv = texture_volume();
    if (mv == nullptr)
        return nullptr;
    for (TextureDisplacementLayer &l : mv->texture_displacement_layers)
        if (l.slot == m_active_layer_slot)
            return &l;
    return nullptr;
}

const TextureDisplacementLayer *GLGizmoTextureDisplacement::active_layer() const
{
    return const_cast<GLGizmoTextureDisplacement *>(this)->active_layer();
}

Vec3f GLGizmoTextureDisplacement::adjust_plane_point() const
{
    const ModelVolume *mv        = texture_volume();
    const float        bbox_size = (mv != nullptr) ? float(mv->mesh().bounding_box().size().norm()) : 1.f;
    const float        lift      = bbox_size * 0.01f + 0.2f;
    return m_adjust_anchor_pos + m_adjust_anchor_normal * lift;
}

Vec3f GLGizmoTextureDisplacement::adjust_handle_center(const TextureDisplacementLayer &layer) const
{
    const float rad = layer.rotation_deg * float(M_PI) / 180.f;
    const float cs = std::cos(rad), sn = std::sin(rad);
    const float aspect = layer_texture_aspect(layer);
    const Vec2f o(layer.offset.x(), (aspect > 0.f) ? layer.offset.y() / aspect : layer.offset.y());
    const Vec2f unrotated(o.x() * cs + o.y() * sn, -o.x() * sn + o.y() * cs);
    const Vec2f planar = -unrotated * layer.tiling_scale;

    Vec3f u_axis, v_axis;
    adjust_tangent_basis(u_axis, v_axis);
    return adjust_plane_point() + u_axis * planar.x() + v_axis * planar.y();
}

void GLGizmoTextureDisplacement::adjust_tangent_basis(Vec3f &u_axis, Vec3f &v_axis) const
{
    const Vec3f n = m_adjust_anchor_normal.cwiseAbs();
    if (n.x() >= n.y() && n.x() >= n.z()) {
        u_axis = Vec3f::UnitY();
        v_axis = Vec3f::UnitZ();
    } else if (n.y() >= n.x() && n.y() >= n.z()) {
        u_axis = Vec3f::UnitX();
        v_axis = Vec3f::UnitZ();
    } else {
        u_axis = Vec3f::UnitX();
        v_axis = Vec3f::UnitY();
    }
}

void GLGizmoTextureDisplacement::render_adjust_texture_gizmo()
{
    if (!m_adjust_anchor_valid)
        return;

    const ModelObject              *mo    = m_c->selection_info()->model_object();
    const ModelVolume              *mv    = texture_volume();
    const TextureDisplacementLayer *layer = active_layer();
    if (mo == nullptr || mv == nullptr || layer == nullptr)
        return;

    const Selection  &selection    = m_parent.get_selection();
    const Transform3d trafo_matrix = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();

    const float bbox_size    = float(mv->mesh().bounding_box().size().norm());
    const float panel_half   = bbox_size * 0.04f + 1.f;
    const float arrow_length = panel_half * 2.2f;

    const Vec3f handle_center_local = adjust_handle_center(*layer);

    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glEnable(GL_BLEND));
    shader->start_using();

    const Camera &camera = wxGetApp().plater()->get_camera();

    Vec3f u_axis, v_axis;
    adjust_tangent_basis(u_axis, v_axis);
    Transform3d plane_transform     = Transform3d::Identity();
    plane_transform.linear().col(0) = u_axis.cast<double>();
    plane_transform.linear().col(1) = v_axis.cast<double>();
    plane_transform.linear().col(2) = m_adjust_anchor_normal.cast<double>();
    plane_transform.translation()   = handle_center_local.cast<double>();

    if (!m_adjust_panel_glmodel.is_initialized()) {
        GLModel::Geometry init_data;
        init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
        init_data.reserve_vertices(4);
        init_data.reserve_indices(6);
        init_data.add_vertex(Vec3f(-1.f, -1.f, 0.f));
        init_data.add_vertex(Vec3f(1.f, -1.f, 0.f));
        init_data.add_vertex(Vec3f(1.f, 1.f, 0.f));
        init_data.add_vertex(Vec3f(-1.f, 1.f, 0.f));
        init_data.add_triangle(0, 1, 2);
        init_data.add_triangle(0, 2, 3);
        m_adjust_panel_glmodel.init_from(std::move(init_data));
    }
    Transform3d view_model_matrix = camera.get_view_matrix() * trafo_matrix * plane_transform *
        Geometry::assemble_transform(Vec3d::Zero(), Vec3d::Zero(), Vec3d(panel_half, panel_half, panel_half));
    shader->set_uniform("view_model_matrix", view_model_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    ColorRGBA panel_color = m_adjust_drag_handle == AdjustHandle::Pan ? ColorRGBA::YELLOW() : ColorRGBA::ORANGE();
    panel_color.a(0.45f);
    m_adjust_panel_glmodel.set_color(panel_color);
    m_adjust_panel_glmodel.render();

    if (!m_adjust_arrow_glmodel.is_initialized()) {
        GLModel::Geometry init_data;
        init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
        init_data.reserve_vertices(4);
        init_data.reserve_indices(6);
        init_data.add_vertex(Vec3f(0.f, 0.f, 0.f));
        init_data.add_vertex(Vec3f(1.f, 0.f, 0.f));
        init_data.add_vertex(Vec3f(0.82f, 0.08f, 0.f));
        init_data.add_vertex(Vec3f(0.82f, -0.08f, 0.f));
        init_data.add_line(0, 1);
        init_data.add_line(1, 2);
        init_data.add_line(1, 3);
        m_adjust_arrow_glmodel.init_from(std::move(init_data));
    }
#if !SLIC3R_OPENGL_ES
    if (!OpenGLManager::get_gl_info().is_core_profile())
        glsafe(::glLineWidth(2.0f));
#endif

    auto render_arrow = [&](const Vec3f &axis, const Vec3f &other_axis, bool is_active) {
        Transform3d arrow_transform     = Transform3d::Identity();
        arrow_transform.linear().col(0) = axis.cast<double>();
        arrow_transform.linear().col(1) = other_axis.cast<double>();
        arrow_transform.linear().col(2) = m_adjust_anchor_normal.cast<double>();
        arrow_transform.translation()   = handle_center_local.cast<double>();

        const Transform3d vmm = camera.get_view_matrix() * trafo_matrix * arrow_transform *
            Geometry::assemble_transform(Vec3d::Zero(), Vec3d::Zero(), Vec3d(arrow_length, arrow_length, arrow_length));
        shader->set_uniform("view_model_matrix", vmm);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        m_adjust_arrow_glmodel.set_color(is_active ? ColorRGBA::YELLOW() : ColorRGBA::ORANGE());
        m_adjust_arrow_glmodel.render();
    };
    render_arrow(u_axis, v_axis, m_adjust_drag_handle == AdjustHandle::AxisU);
    render_arrow(v_axis, u_axis, m_adjust_drag_handle == AdjustHandle::AxisV);

    shader->stop_using();
    glsafe(::glDisable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));
}

bool GLGizmoTextureDisplacement::on_mouse_adjust_texture(const wxMouseEvent &mouse_event)
{
    if (!m_adjust_anchor_valid)
        return false;

    ModelVolume              *mv    = texture_volume();
    ModelObject              *mo    = m_c->selection_info()->model_object();
    TextureDisplacementLayer *layer = active_layer();
    if (mv == nullptr || mo == nullptr || layer == nullptr)
        return false;

    const Selection   &selection    = m_parent.get_selection();
    const Transform3d  trafo_matrix = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();
    const Camera      &camera       = wxGetApp().plater()->get_camera();

    const float bbox_size    = float(mv->mesh().bounding_box().size().norm());
    const float panel_half   = bbox_size * 0.04f + 1.f;
    const float arrow_length = panel_half * 2.2f;
    const Vec3f handle_center_local = adjust_handle_center(*layer);
    const Vec3f drag_plane_local    = adjust_plane_point();
    const Vec3d handle_center_world = trafo_matrix * handle_center_local.cast<double>();
    const Vec2d mouse_pos(mouse_event.GetX(), mouse_event.GetY());

    Vec3f u_axis, v_axis;
    adjust_tangent_basis(u_axis, v_axis);

    const Point handle_screen = CameraUtils::project(camera, handle_center_world);
    const Vec2d handle_screen_d(double(handle_screen.x()), double(handle_screen.y()));

    auto dist_to_segment_px = [](const Vec2d &p, const Vec2d &a, const Vec2d &b) {
        const Vec2d ab = b - a;
        const double len2 = ab.squaredNorm();
        const double t = (len2 > 1e-9) ? std::clamp((p - a).dot(ab) / len2, 0.0, 1.0) : 0.0;
        return (p - (a + ab * t)).norm();
    };

    if (mouse_event.LeftDown()) {
        const Vec3d u_tip_world = trafo_matrix * (handle_center_local + u_axis * arrow_length).cast<double>();
        const Vec3d v_tip_world = trafo_matrix * (handle_center_local + v_axis * arrow_length).cast<double>();
        const Point u_tip_screen = CameraUtils::project(camera, u_tip_world);
        const Point v_tip_screen = CameraUtils::project(camera, v_tip_world);
        const Vec2d u_tip_screen_d(double(u_tip_screen.x()), double(u_tip_screen.y()));
        const Vec2d v_tip_screen_d(double(v_tip_screen.x()), double(v_tip_screen.y()));

        const Vec3d panel_corner_world = trafo_matrix * (handle_center_local + (u_axis + v_axis) * panel_half).cast<double>();
        const Point panel_corner_screen = CameraUtils::project(camera, panel_corner_world);
        const double panel_screen_radius = (Vec2d(double(panel_corner_screen.x()), double(panel_corner_screen.y())) - handle_screen_d).norm();

        constexpr double pick_tolerance_px = 8.0;
        const double dist_to_u = dist_to_segment_px(mouse_pos, handle_screen_d, u_tip_screen_d);
        const double dist_to_v = dist_to_segment_px(mouse_pos, handle_screen_d, v_tip_screen_d);
        const double dist_to_panel = (handle_screen_d - mouse_pos).norm();

        if (dist_to_u <= pick_tolerance_px && dist_to_u <= dist_to_v)
            m_adjust_drag_handle = AdjustHandle::AxisU;
        else if (dist_to_v <= pick_tolerance_px)
            m_adjust_drag_handle = AdjustHandle::AxisV;
        else if (dist_to_panel <= panel_screen_radius)
            m_adjust_drag_handle = AdjustHandle::Pan;
        else {
            m_adjust_drag_handle = AdjustHandle::None;
            return false;
        }

        m_adjust_drag_start_offset = layer->offset;

        Vec3d world_hit;
        if (ray_plane_hit(camera, mouse_pos, trafo_matrix, drag_plane_local, m_adjust_anchor_normal, world_hit)) {
            const Vec3f local_hit = (trafo_matrix.inverse() * world_hit).cast<float>();
            m_adjust_drag_start_planar = project_planar(local_hit, m_adjust_anchor_normal);
        }
        return true;
    }

    if (mouse_event.Dragging() && m_adjust_drag_handle != AdjustHandle::None) {
        Vec3d world_hit;
        if (!ray_plane_hit(camera, mouse_pos, trafo_matrix, drag_plane_local, m_adjust_anchor_normal, world_hit))
            return true;
        const Vec3f local_hit      = (trafo_matrix.inverse() * world_hit).cast<float>();
        const Vec2f current_planar = project_planar(local_hit, m_adjust_anchor_normal);

        Vec2f delta_planar = current_planar - m_adjust_drag_start_planar;
        if (m_adjust_drag_handle == AdjustHandle::AxisU)
            delta_planar.y() = 0.f;
        else if (m_adjust_drag_handle == AdjustHandle::AxisV)
            delta_planar.x() = 0.f;

        const float scale        = (layer->tiling_scale > 1e-6f) ? (1.f / layer->tiling_scale) : 1.f;
        const Vec2f delta_scaled = delta_planar * scale;
        const float rad          = layer->rotation_deg * float(M_PI) / 180.f;
        const float cs = std::cos(rad), sn = std::sin(rad);
        Vec2f delta_rotated(delta_scaled.x() * cs - delta_scaled.y() * sn, delta_scaled.x() * sn + delta_scaled.y() * cs);
        delta_rotated.y() *= layer_texture_aspect(*layer);
        layer->offset = m_adjust_drag_start_offset - delta_rotated;

        m_preview_params_dirty = true;
        m_parent.set_as_dirty();
        return true;
    }

    if (mouse_event.LeftUp() && m_adjust_drag_handle != AdjustHandle::None) {
        m_adjust_drag_handle = AdjustHandle::None;
        rebuild_preview();
        m_preview_params_dirty = false;
        return true;
    }

    return false;
}

ModelVolume* GLGizmoTextureDisplacement::texture_volume()
{
    ModelObject *mo = m_c->selection_info()->model_object();
    if (!mo)
        return nullptr;
    for (ModelVolume *mv : mo->volumes)
        if (mv->is_model_part())
            return mv;
    return nullptr;
}

const ModelVolume* GLGizmoTextureDisplacement::texture_volume() const
{
    return const_cast<GLGizmoTextureDisplacement *>(this)->texture_volume();
}

void GLGizmoTextureDisplacement::update_model_object()
{
    bool         updated = false;
    ModelObject *mo      = m_c->selection_info()->model_object();
    int          idx     = -1;
    for (ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;
        ++idx;
        FacetsAnnotation &facet = mv->texture_displacement_facet(m_active_layer_slot);
        if (m_selectors_stale && !facet.empty() && m_triangle_selectors[idx]->serialize().triangles_to_split.empty())
            continue;
        updated |= facet.set(*m_triangle_selectors[idx]);
    }

    m_shaded_preview_dirty = true;

    if (updated) {
        const ModelObjectPtrs &mos = wxGetApp().model().objects;
        wxGetApp().obj_list()->update_info_items(std::find(mos.begin(), mos.end(), mo) - mos.begin());
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
        rebuild_preview();
    }
}

void GLGizmoTextureDisplacement::update_from_model_object(bool first_update)
{
    wxBusyCursor wait;

    const ModelObject *mo = m_c->selection_info()->model_object();
    m_triangle_selectors.clear();
    m_seed_fill_last_mesh_id = -1;

    std::vector<ColorRGBA> ebt_colors;
    ebt_colors.push_back(GLVolume::NEUTRAL_COLOR);
    ebt_colors.push_back(TriangleSelectorGUI::enforcers_color);
    ebt_colors.push_back(TriangleSelectorGUI::blockers_color);
    m_selectors_stale = false;
    for (const ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;

        const TriangleMesh                            *mesh = &mv->mesh();
        const TriangleSelector::TriangleSplittingData &data =
            mv->texture_displacement_facet(m_active_layer_slot).get_data();
        const size_t facet_count = mesh->its.indices.size();
        for (const TriangleSelector::TriangleBitStreamMapping &m : data.triangles_to_split)
            if (m.triangle_idx < 0 || size_t(m.triangle_idx) >= facet_count) {
                m_selectors_stale = true;
                break;
            }
        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorPatch>(*mesh, ebt_colors));
        m_triangle_selectors.back()->deserialize(data, false);
        m_triangle_selectors.back()->request_update_render_data();
    }

    if (first_update)
        if (ModelVolume *tv = texture_volume(); tv != nullptr && tv->texture_displacement_layers.empty()) {
            add_texture_layer();
            return;
        }

    rebuild_preview();
}

void GLGizmoTextureDisplacement::set_active_layer(int slot)
{
    if (slot == m_active_layer_slot)
        return;
    update_model_object();
    m_active_layer_slot = slot;
    update_from_model_object(false);
    if (m_adjust_texture_mode)
        update_adjust_anchor();
    rebuild_preview();
}

void GLGizmoTextureDisplacement::ensure_panel_icons()
{
    if (m_panel_icons_tried)
        return;
    m_panel_icons_tried = true;

    static const std::vector<std::string> names = {
        "texture_displacement_brush.svg", "texture_displacement_face.svg", "texture_displacement_connected_area.svg",
        "texture_displacement_real_preview.svg", "texture_displacement_fast_preview.svg",
        "texture_displacement_checker.svg", "texture_displacement_distortion.svg",
        "texture_displacement_wireframe.svg", "texture_displacement_cross.svg",
        "texture_displacement_uv_select_island.svg", "texture_displacement_uv_select_edge.svg",
        "texture_displacement_uv_select_vertex.svg",
        "texture_displacement_add.svg", "texture_displacement_add_negative.svg", "circle_paint.svg",
        "menu_obj_sphere.svg", "menu_obj_cube.svg", "menu_obj_cylinder.svg", "texture_displacement_map_unwrap.svg",
        "texture_displacement_map_view.svg", "texture_displacement_tile_repeat.svg", "menu_mirror_x.svg",
        "texture_displacement_adjust.svg", "canvas_drag.svg", "texture_displacement_move_up.svg",
        "texture_displacement_move_down.svg", "texture_displacement_drag.svg",
        "texture_displacement_select_all.svg", "texture_displacement_erase_all.svg",
    };
    std::vector<std::string> paths;
    paths.reserve(names.size());
    for (const std::string &n : names)
        paths.push_back(resources_dir() + "/images/" + n);

    const std::vector<IconManager::Icons> icons =
        m_panel_icons.init(paths, ImVec2(2 * GLToolbar::Default_Icons_Size, 2 * GLToolbar::Default_Icons_Size),
                           IconManager::RasterType::color_wite_gray);
    for (size_t i = 0; i < names.size() && i < icons.size(); ++i)
        m_panel_icon_map[names[i]] = icons[i];
}

void GLGizmoTextureDisplacement::add_texture_layer()
{
    ModelVolume *mv = texture_volume();
    if (!mv)
        return;

    std::array<bool, TEXTURE_DISPLACEMENT_MAX_LAYERS> used{};
    for (const TextureDisplacementLayer &l : mv->texture_displacement_layers)
        if (l.slot >= 0 && size_t(l.slot) < used.size())
            used[size_t(l.slot)] = true;
    int free_slot = -1;
    for (size_t i = 0; i < used.size(); ++i)
        if (!used[i]) { free_slot = int(i); break; }
    if (free_slot < 0) {
        show_error(nullptr, _u8L("Maximum number of texture displacement layers reached."));
        return;
    }

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Add texture displacement layer"), UndoRedo::SnapshotType::GizmoAction);

    TextureDisplacementLayer layer;
    layer.slot = free_slot;
    const std::vector<TextureLibraryEntry> &library = texture_library();
    if (!library.empty())
        if (const LibraryTexture *tex = get_library_texture(library.front().path)) {
            layer.name       = library.front().name;
            layer.path       = library.front().path;
            layer.image_data = tex->image_data;
        }
    mv->texture_displacement_layers.push_back(std::move(layer));
    m_layer_expanded[size_t(free_slot)] = true;

    set_active_layer(free_slot);
    rebuild_preview();
    m_parent.set_as_dirty();
}

const GLGizmoTextureDisplacement::LibraryTexture *GLGizmoTextureDisplacement::get_library_texture(const std::string &path)
{
    if (auto it = m_library_textures.find(path); it != m_library_textures.end())
        return it->second.image_data ? &it->second : nullptr;

    LibraryTexture entry;
    std::string    error;
    entry.image_data = load_texture_image_data(path, error);
    if (entry.image_data) {
        TextureDisplacementLayer probe;
        probe.image_data = entry.image_data;
        entry.thumbnail  = upload_height_thumbnail(decode_height_texture(probe));
        if (!entry.thumbnail)
            entry.image_data.reset();
    } else {
        BOOST_LOG_TRIVIAL(error) << "Texture displacement: could not load texture " << path << ": " << error;
    }

    const auto [pos, inserted] = m_library_textures.emplace(path, std::move(entry));
    return pos->second.image_data ? &pos->second : nullptr;
}

void GLGizmoTextureDisplacement::set_layer_texture(TextureDisplacementLayer &layer, const TextureLibraryEntry &entry)
{
    const LibraryTexture *tex = get_library_texture(entry.path);
    if (tex == nullptr) {
        show_error(nullptr, _u8L("Could not load the selected texture."));
        return;
    }

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Change texture displacement texture"), UndoRedo::SnapshotType::GizmoAction);
    layer.name = entry.name;
    layer.path = entry.path;
    layer.image_data = tex->image_data;
    if (entry.is_user)
        touch_user_texture(entry.path);

    rebuild_preview();
    m_parent.set_as_dirty();
}

void GLGizmoTextureDisplacement::import_custom_texture(TextureDisplacementLayer &layer)
{
    const wxString wildcard = _L("Images (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp");
    wxFileDialog   dialog(nullptr, _L("Choose a texture image (height map)"), wxEmptyString, wxEmptyString, wildcard,
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
        return;

    std::string                              error;
    const std::optional<TextureLibraryEntry> entry = import_texture_to_library(into_u8(dialog.GetPath()), error);
    if (!entry) {
        show_error(nullptr, error);
        return;
    }
    set_layer_texture(layer, *entry);
}

void GLGizmoTextureDisplacement::render_texture_library_popup(const ImVec2 &panel_min, const ImVec2 &panel_max)
{
    ModelVolume              *mv    = texture_volume();
    TextureDisplacementLayer *layer = nullptr;
    if (mv != nullptr)
        for (TextureDisplacementLayer &l : mv->texture_displacement_layers)
            if (l.slot == m_picker_slot)
                layer = &l;

    static const char *const popup_id = "##texture_library";
    if (m_picker_open_request) {
        m_picker_open_request = false;
        if (layer != nullptr)
            ImGui::OpenPopup(popup_id);
    }

    const float tile    = std::round(m_imgui->scaled(3.2f));
    const float spacing = std::round(m_imgui->scaled(0.4f));
    const float grid_w  = 4.f * tile + 3.f * spacing;
    const float gap     = m_imgui->scaled(0.5f);
    const float popup_w = grid_w + 2.f * ImGui::GetStyle().WindowPadding.x;
    const bool  left    = panel_min.x - gap - popup_w >= 0.f;
    ImGui::SetNextWindowPos(ImVec2(left ? panel_min.x - gap : panel_max.x + gap, panel_min.y + m_imgui->scaled(4.f)),
                            ImGuiCond_Appearing, ImVec2(left ? 1.f : 0.f, 0.f));
    if (!ImGui::BeginPopup(popup_id))
        return;
    if (layer == nullptr) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    m_imgui->push_bold_font();
    m_imgui->text(_L("Choose a texture"));
    m_imgui->pop_bold_font();

    const std::vector<TextureLibraryEntry> &library      = texture_library();
    ImDrawList                             *dl           = ImGui::GetWindowDrawList();
    const ImU32                             selected_col = ImGui::GetColorU32(ImGuiWrapper::COL_ORCA);
    std::optional<TextureLibraryEntry>      chosen;
    std::optional<TextureLibraryEntry>      to_remove;
    bool                                    import_clicked = false;

    const auto tile_pos = [&](const ImVec2 &origin, int i) {
        return ImVec2(origin.x + float(i % 4) * (tile + spacing), origin.y + float(i / 4) * (tile + spacing));
    };
    const auto end_grid = [&](const ImVec2 &origin, int count) {
        ImGui::SetCursorScreenPos(origin);
        if (count > 0) {
            const int rows = (count + 3) / 4;
            ImGui::Dummy(ImVec2(grid_w, float(rows) * tile + float(rows - 1) * spacing));
        }
    };
    const auto grid = [&](const ImVec2 &origin, bool user) {
        int n = 0;
        for (const TextureLibraryEntry &entry : library) {
            if (entry.is_user != user)
                continue;
            ImGui::SetCursorScreenPos(tile_pos(origin, n++));
            ImGui::PushID(entry.path.c_str());
            const LibraryTexture *tex     = get_library_texture(entry.path);
            GLTexture            *thumb   = tex != nullptr ? tex->thumbnail.get() : nullptr;
            bool                  clicked = false;
            if (thumb != nullptr) {
                const float aspect = thumb->get_height() > 0 ? float(thumb->get_width()) / float(thumb->get_height()) : 1.f;
                ImVec2      uv0(0.f, 0.f), uv1(1.f, 1.f);
                if (aspect > 1.f) {
                    uv0.x = 0.5f * (1.f - 1.f / aspect);
                    uv1.x = 1.f - uv0.x;
                } else if (aspect < 1.f) {
                    uv0.y = 0.5f * (1.f - aspect);
                    uv1.y = 1.f - uv0.y;
                }
                clicked = ImGui::ImageButton((ImTextureID) (intptr_t) thumb->get_id(), ImVec2(tile, tile), uv0, uv1, 0);
            } else {
                clicked = ImGui::Button("?", ImVec2(tile, tile));
            }
            if (user)
                ImGui::SetItemAllowOverlap();
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            if (entry.path == layer->path)
                dl->AddRect(ImVec2(a.x - 1.f, a.y - 1.f), ImVec2(b.x + 1.f, b.y + 1.f), selected_col, 0.f, 0, 2.f);
            if (ImGui::IsItemHovered())
                m_imgui->tooltip(entry.name, m_imgui->scaled(18.f));

            if (user && ImGui::IsMouseHoveringRect(a, b)) {
                const float  r = std::round(tile * 0.17f);
                const ImVec2 c(b.x - r - 2.f, a.y + r + 2.f);
                ImGui::SetCursorScreenPos(ImVec2(c.x - r, c.y - r));
                const bool remove_clicked = ImGui::InvisibleButton("##remove", ImVec2(2.f * r, 2.f * r));
                const bool hot            = ImGui::IsItemHovered();
                dl->AddCircleFilled(c, r, hot ? IM_COL32(214, 72, 64, 235) : IM_COL32(0, 0, 0, 170));
                const float k = 0.42f * r;
                dl->AddLine(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), IM_COL32_WHITE, 1.5f);
                dl->AddLine(ImVec2(c.x - k, c.y + k), ImVec2(c.x + k, c.y - k), IM_COL32_WHITE, 1.5f);
                if (hot)
                    m_imgui->tooltip(_u8L("Remove from My textures"), m_imgui->scaled(18.f));
                if (remove_clicked)
                    to_remove = entry;
            }
            ImGui::PopID();
            if (clicked)
                chosen = entry;
        }
        return n;
    };

    ImGui::TextDisabled("%s", _u8L("Built-in").c_str());
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const int    count  = grid(origin, false);
        end_grid(origin, count);
        if (count == 0)
            ImGui::TextDisabled("%s", _u8L("No textures found.").c_str());
    }
    ImGui::TextDisabled("%s", _u8L("My textures").c_str());
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const int    count  = grid(origin, true);
        ImGui::SetCursorScreenPos(tile_pos(origin, count));
        if (ImGui::Button("+##import_texture", ImVec2(tile, tile)))
            import_clicked = true;
        if (ImGui::IsItemHovered())
            m_imgui->tooltip(_u8L("Import your own image as a height map. It is converted to the format the slicer "
                                  "bakes from and saved to your personal texture folder, kept separate from the "
                                  "textures shipped with OrcaSlicer."),
                             m_imgui->scaled(20.f));
        end_grid(origin, count + 1);
    }

    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + grid_w);
    ImGui::TextDisabled("%s", _u8L("PNG, JPG or BMP - converted to a height map on import. Hover one of yours to remove it.").c_str());
    ImGui::PopTextWrapPos();

    if (chosen || import_clicked)
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();

    if (chosen)
        set_layer_texture(*layer, *chosen);
    else if (import_clicked)
        import_custom_texture(*layer);
    else if (to_remove) {
        MessageDialog dlg(nullptr,
                          wxString::Format(_L("Remove \"%s\" from My textures?\n\nThe image file is deleted from your "
                                              "texture folder. Layers already using it keep it."),
                                           from_u8(to_remove->name)),
                          _L("Remove texture"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
        if (dlg.ShowModal() == wxID_YES) {
            std::string error;
            if (remove_user_texture(to_remove->path, error))
                m_library_textures.erase(to_remove->path);
            else
                show_error(nullptr, error);
        }
        m_picker_open_request = true;
    }
}

void GLGizmoTextureDisplacement::remove_texture_layer(int slot)
{
    ModelVolume *mv = texture_volume();
    if (!mv)
        return;

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Remove texture displacement layer"), UndoRedo::SnapshotType::GizmoAction);

    auto &layers = mv->texture_displacement_layers;
    layers.erase(std::remove_if(layers.begin(), layers.end(),
                                 [slot](const TextureDisplacementLayer &l) { return l.slot == slot; }),
                 layers.end());
    if (slot >= 0 && slot < int(TEXTURE_DISPLACEMENT_MAX_LAYERS)) {
        mv->texture_displacement_facet(slot).reset();
        m_thumbnails[size_t(slot)].reset();
        m_thumbnail_source[size_t(slot)] = nullptr;
        m_layer_expanded[size_t(slot)]   = false;
    }
    if (m_picker_slot == slot)
        m_picker_slot = -1;

    if (m_active_layer_slot == slot)
        update_from_model_object(false);
    else
        rebuild_preview();

    m_parent.set_as_dirty();
}

void GLGizmoTextureDisplacement::swap_layer_slots(int slot_a, int slot_b)
{
    ModelObject *mo = m_c->selection_info()->model_object();
    ModelVolume *mv = texture_volume();
    const int    n  = int(TEXTURE_DISPLACEMENT_MAX_LAYERS);
    if (mo == nullptr || mv == nullptr || slot_a == slot_b || slot_a < 0 || slot_b < 0 || slot_a >= n || slot_b >= n)
        return;

    for (ModelVolume *v : mo->volumes) {
        if (!v->is_model_part())
            continue;
        TriangleSelector::TriangleSplittingData data_a = v->texture_displacement_facet(slot_a).get_data();
        TriangleSelector::TriangleSplittingData data_b = v->texture_displacement_facet(slot_b).get_data();
        v->texture_displacement_facet(slot_a).set_data(std::move(data_b));
        v->texture_displacement_facet(slot_b).set_data(std::move(data_a));
    }
    for (TextureDisplacementLayer &l : mv->texture_displacement_layers) {
        if (l.slot == slot_a)
            l.slot = slot_b;
        else if (l.slot == slot_b)
            l.slot = slot_a;
    }

    const size_t a = size_t(slot_a), b = size_t(slot_b);
    std::swap(m_thumbnails[a], m_thumbnails[b]);
    std::swap(m_thumbnail_source[a], m_thumbnail_source[b]);
    std::swap(m_thumbnail_smoothing[a], m_thumbnail_smoothing[b]);
    std::swap(m_layer_expanded[a], m_layer_expanded[b]);
    if (m_active_layer_slot == slot_a)
        m_active_layer_slot = slot_b;
    else if (m_active_layer_slot == slot_b)
        m_active_layer_slot = slot_a;
    if (m_picker_slot == slot_a)
        m_picker_slot = slot_b;
    else if (m_picker_slot == slot_b)
        m_picker_slot = slot_a;
}

void GLGizmoTextureDisplacement::move_texture_layer(int slot, int to_index)
{
    ModelVolume *mv = texture_volume();
    if (mv == nullptr)
        return;

    std::vector<int> slots;
    for (const TextureDisplacementLayer &l : mv->texture_displacement_layers)
        slots.push_back(l.slot);
    std::sort(slots.begin(), slots.end());
    const auto it = std::find(slots.begin(), slots.end(), slot);
    if (it == slots.end())
        return;
    const int from = int(it - slots.begin());
    const int to = std::clamp(to_index > from ? to_index - 1 : to_index, 0, int(slots.size()) - 1);
    if (to == from)
        return;

    update_model_object();
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Reorder texture displacement layers"),
                                  UndoRedo::SnapshotType::GizmoAction);
    const int step = to > from ? 1 : -1;
    for (int i = from; i != to; i += step)
        swap_layer_slots(slots[size_t(i)], slots[size_t(i + step)]);

    update_from_model_object(false);
    if (m_adjust_texture_mode)
        update_adjust_anchor();
    m_parent.set_as_dirty();
}

int GLGizmoTextureDisplacement::select_visible_faces(const std::array<float, 12> *uv_clip)
{
    ModelVolume *mv = texture_volume();
    ModelObject *mo = m_c->selection_info()->model_object();
    const int    idx = texture_volume_raycaster_index();
    if (mv == nullptr || mo == nullptr || idx < 0 || idx >= int(m_triangle_selectors.size()))
        return 0;

    const indexed_triangle_set &its = mv->mesh().its;
    if (its.indices.empty())
        return 0;

    const Selection             &selection = m_parent.get_selection();
    const Geometry::Transformation trafo(mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() *
                                         mv->get_matrix());
    const Transform3d            &to_world = trafo.get_matrix();
    const Camera                 &camera   = wxGetApp().plater()->get_camera();

    const Matrix3d normal_matrix = to_world.matrix().block<3, 3>(0, 0).inverse().transpose();

    const Vec3d eye = camera.get_position();
    const bool  ortho = camera.get_type() == Camera::EType::Ortho;
    const Vec3d fwd   = camera.get_dir_forward();

    std::vector<Vec3f>    centroids;
    std::vector<unsigned> front_facing;
    centroids.reserve(its.indices.size() / 2);
    front_facing.reserve(its.indices.size() / 2);

    for (size_t f = 0; f < its.indices.size(); ++f) {
        const stl_triangle_vertex_indices &tri = its.indices[f];
        const Vec3d a = its.vertices[tri[0]].cast<double>();
        const Vec3d b = its.vertices[tri[1]].cast<double>();
        const Vec3d c = its.vertices[tri[2]].cast<double>();
        const Vec3d n_world = normal_matrix * (b - a).cross(c - a);
        if (n_world.squaredNorm() < 1e-20)
            continue;
        const Vec3d centroid_local = (a + b + c) / 3.0;
        const Vec3d centroid_world = to_world * centroid_local;
        const Vec3d view_dir       = ortho ? fwd : Vec3d(centroid_world - eye);
        if (n_world.dot(view_dir) >= 0.0)
            continue;
        if (uv_clip != nullptr) {
            Vec2f uv;
            if (!project_uv_projective(*uv_clip, centroid_local.cast<float>(), uv))
                continue;
            if (uv.x() < 0.f || uv.x() > 1.f || uv.y() < 0.f || uv.y() > 1.f)
                continue;
        }
        centroids.emplace_back(centroid_world.cast<float>());
        front_facing.push_back(unsigned(f));
    }
    if (centroids.empty())
        return 0;

    std::vector<unsigned> unobscured;
    {
        wxBusyCursor wait;
        unobscured = m_c->raycaster()->raycasters()[size_t(idx)]->get_unobscured_idxs(
            trafo, camera, centroids, m_c->object_clipper()->get_clipping_plane());
    }
    if (unobscured.empty())
        return 0;

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Select visible faces for texture displacement"),
                                  UndoRedo::SnapshotType::GizmoAction);

    TriangleSelectorGUI &selector = *m_triangle_selectors[size_t(idx)];
    selector.reset();
    for (unsigned i : unobscured)
        selector.set_facet(int(front_facing[i]), EnforcerBlockerType::ENFORCER);
    selector.request_update_render_data();

    update_model_object();
    m_parent.set_as_dirty();
    return int(unobscured.size());
}

void GLGizmoTextureDisplacement::select_whole_model()
{
    ModelObject *mo = m_c->selection_info()->model_object();
    if (!mo)
        return;

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Select whole model for texture displacement"), UndoRedo::SnapshotType::GizmoAction);

    int idx = -1;
    for (const ModelVolume *v : mo->volumes) {
        if (!v->is_model_part())
            continue;
        ++idx;
        const size_t facet_count = v->mesh().its.indices.size();
        for (size_t i = 0; i < facet_count; ++i)
            m_triangle_selectors[idx]->set_facet(int(i), EnforcerBlockerType::ENFORCER);
        m_triangle_selectors[idx]->request_update_render_data();
    }
    update_model_object();
    m_parent.set_as_dirty();
}

void GLGizmoTextureDisplacement::subdivide_model()
{
    ModelVolume *mv = texture_volume();
    ModelObject *mo = m_c->selection_info()->model_object();
    if (mv == nullptr || mo == nullptr || m_subdivide_count < 1)
        return;

    Plater *plater = wxGetApp().plater();
    Plater::TakeSnapshot snapshot(plater, _u8L("Subdivide model for texture displacement"), UndoRedo::SnapshotType::GizmoAction);

    std::optional<TriangleSelector::SavedPainting> saved_painting = mv->save_painting();

    TriangleMesh new_mesh(subdivide_mesh_uniform(mv->mesh().its, 0.f, m_subdivide_count));
    mv->set_mesh(std::move(new_mesh));
    mv->set_new_unique_id();
    mv->calculate_convex_hull();
    mv->restore_painting(saved_painting);

    if (ObjectList *obj_list = wxGetApp().obj_list()) {
        const ModelObjectPtrs &objs = plater->model().objects;
        auto it = std::find(objs.begin(), objs.end(), mo);
        if (it != objs.end())
            obj_list->update_info_items(size_t(it - objs.begin()));
    }

    plater->changed_object(*mo);
    update_from_model_object(false);
    m_parent.set_as_dirty();
}

bool GLGizmoTextureDisplacement::collect_paint_region(
    const TriangleMesh &mesh, const TextureDisplacementFacetsData &facets,
    std::vector<uint8_t> &region,
    std::array<LayerPaintMap, TEXTURE_DISPLACEMENT_MAX_LAYERS> *paint)
{
    const indexed_triangle_set &its  = mesh.its;
    const size_t                ntri = its.indices.size();
    const size_t                nvert = its.vertices.size();

    region.assign(ntri, 0);
    if (paint)
        for (LayerPaintMap &pm : *paint)
            pm = LayerPaintMap{};

    const auto tri_area2 = [](const Vec3f &a, const Vec3f &b, const Vec3f &c) {
        return (b - a).cross(c - a).norm();
    };
    std::vector<float> source_area2;
    if (paint) {
        source_area2.resize(ntri);
        for (size_t i = 0; i < ntri; ++i)
            source_area2[i] = tri_area2(its.vertices[size_t(its.indices[i][0])],
                                        its.vertices[size_t(its.indices[i][1])],
                                        its.vertices[size_t(its.indices[i][2])]);
    }

    bool any_paint = false;
    for (int slot = 0; slot < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++slot) {
        const TriangleSelector::TriangleSplittingData &data = facets[size_t(slot)];
        if (!TriangleSelector::has_facets(data, EnforcerBlockerType::ENFORCER))
            continue;

        for (const TriangleSelector::TriangleBitStreamMapping &m : data.triangles_to_split)
            if (size_t(m.triangle_idx) < ntri)
                region[m.triangle_idx] |= REFINE_PAINTED;

        if (paint) {
            TriangleSelector sel(mesh);
            sel.deserialize(data, false);
            std::vector<int>           src;
            const indexed_triangle_set patch = sel.get_facets_strict(EnforcerBlockerType::ENFORCER, &src);

            LayerPaintMap &pm = (*paint)[slot];
            pm.full.assign(ntri, 0);
            pm.part_start.assign(ntri + 1, 0);

            std::vector<float> covered2(ntri, 0.f);
            for (size_t j = 0; j < patch.indices.size() && j < src.size(); ++j) {
                if (size_t(src[j]) >= ntri)
                    continue;
                const stl_triangle_vertex_indices &t = patch.indices[j];
                covered2[size_t(src[j])] += tri_area2(patch.vertices[size_t(t[0])], patch.vertices[size_t(t[1])],
                                                      patch.vertices[size_t(t[2])]);
            }
            for (size_t i = 0; i < ntri; ++i)
                pm.full[i] = (source_area2[i] > 0.f && covered2[i] >= 0.999f * source_area2[i]) ? 1 : 0;

            for (size_t j = 0; j < patch.indices.size() && j < src.size(); ++j)
                if (size_t(src[j]) < ntri && !pm.full[size_t(src[j])])
                    ++pm.part_start[size_t(src[j]) + 1];
            for (size_t i = 0; i < ntri; ++i)
                pm.part_start[i + 1] += pm.part_start[i];
            pm.part.resize(size_t(pm.part_start[ntri]));
            {
                std::vector<int> fill(pm.part_start.begin(), pm.part_start.begin() + ntri);
                for (size_t j = 0; j < patch.indices.size() && j < src.size(); ++j) {
                    const size_t S = size_t(src[j]);
                    if (S >= ntri || pm.full[S])
                        continue;
                    const stl_triangle_vertex_indices &t = patch.indices[j];
                    pm.part[size_t(fill[S]++)] = { patch.vertices[size_t(t[0])], patch.vertices[size_t(t[1])],
                                                   patch.vertices[size_t(t[2])] };
                }
            }
        }
        any_paint = true;
    }
    if (!any_paint)
        return false;

    if (BORDER_BAND_RINGS > 0) {
        std::vector<int> vstart(nvert + 1, 0);
        for (size_t i = 0; i < ntri; ++i)
            for (int k = 0; k < 3; ++k)
                if (size_t(its.indices[i][k]) < nvert)
                    ++vstart[size_t(its.indices[i][k]) + 1];
        for (size_t v = 0; v < nvert; ++v)
            vstart[v + 1] += vstart[v];
        std::vector<int> vtri(static_cast<size_t>(vstart[nvert]), 0);
        {
            std::vector<int> fill(vstart.begin(), vstart.begin() + nvert);
            for (size_t i = 0; i < ntri; ++i)
                for (int k = 0; k < 3; ++k)
                    if (size_t(its.indices[i][k]) < nvert)
                        vtri[size_t(fill[size_t(its.indices[i][k])]++)] = int(i);
        }

        std::vector<uint8_t> ring_vertex(nvert, 0);
        for (size_t v = 0; v < nvert; ++v) {
            bool painted = false, unpainted = false;
            for (int k = vstart[v]; k < vstart[v + 1]; ++k)
                ((region[size_t(vtri[size_t(k)])] & REFINE_PAINTED) ? painted : unpainted) = true;
            ring_vertex[v] = (painted && unpainted) ? 1 : 0;
        }

        for (int ring = 0; ring < BORDER_BAND_RINGS; ++ring) {
            std::vector<uint8_t> next = ring_vertex;
            for (size_t v = 0; v < nvert; ++v) {
                if (!ring_vertex[v])
                    continue;
                for (int k = vstart[v]; k < vstart[v + 1]; ++k) {
                    const size_t t = size_t(vtri[size_t(k)]);
                    region[t] |= REFINE_BORDER;
                    for (int c = 0; c < 3; ++c)
                        if (size_t(its.indices[t][c]) < nvert)
                            next[size_t(its.indices[t][c])] = 1;
                }
            }
            ring_vertex.swap(next);
        }
    }
    return true;
}

TextureDisplacementFacetsData GLGizmoTextureDisplacement::masks_after_subdivision(
    const TriangleMesh &new_mesh, const std::vector<int> &source,
    const std::array<LayerPaintMap, TEXTURE_DISPLACEMENT_MAX_LAYERS> &paint)
{
    TextureDisplacementFacetsData       out{};
    const indexed_triangle_set         &its = new_mesh.its;
    for (int slot = 0; slot < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++slot) {
        const LayerPaintMap &pm = paint[size_t(slot)];
        if (pm.empty())
            continue;
        TriangleSelector sel(new_mesh);
        for (size_t i = 0; i < source.size() && i < its.indices.size(); ++i) {
            const size_t S = size_t(source[i]);
            if (S >= pm.full.size())
                continue;
            bool painted = pm.full[S] != 0;
            if (!painted && pm.part_start[S] != pm.part_start[S + 1]) {
                const stl_triangle_vertex_indices &t = its.indices[i];
                const Vec3f centroid = (its.vertices[size_t(t[0])] + its.vertices[size_t(t[1])] +
                                        its.vertices[size_t(t[2])]) / 3.f;
                for (int k = pm.part_start[S]; k < pm.part_start[S + 1] && !painted; ++k)
                    painted = point_in_triangle_coplanar(centroid, pm.part[size_t(k)]);
            }
            if (painted)
                sel.set_facet(int(i), EnforcerBlockerType::ENFORCER);
        }
        out[size_t(slot)] = sel.serialize();
    }
    return out;
}

double GLGizmoTextureDisplacement::painted_area_mm2(const ModelVolume &mv)
{
    const std::string key = std::to_string(mv.id().id) + ":" + std::to_string(mv.mesh().facets_count()) + ":" +
                            std::to_string(m_preview_generation->load());
    if (key == m_painted_area_key)
        return m_painted_area_mm2;

    const TriangleMesh &mesh = mv.mesh();
    std::vector<uint8_t> region;
    double               area = 0.;
    if (collect_paint_region(mesh, facets_data_of(mv), region, nullptr)) {
        const Transform3d   frame = texture_displacement_bake_frame(texture_displacement_volume_to_world(mv));
        const indexed_triangle_set &its = mesh.its;
        for (size_t t = 0; t < its.indices.size() && t < region.size(); ++t) {
            if ((region[t] & REFINE_PAINTED) == 0)
                continue;
            const stl_triangle_vertex_indices &tri = its.indices[t];
            const Vec3d a = frame * its.vertices[size_t(tri[0])].cast<double>();
            const Vec3d b = frame * its.vertices[size_t(tri[1])].cast<double>();
            const Vec3d c = frame * its.vertices[size_t(tri[2])].cast<double>();
            area += 0.5 * (b - a).cross(c - a).norm();
        }
    }
    m_painted_area_mm2 = area;
    m_painted_area_key = key;
    return area;
}

size_t GLGizmoTextureDisplacement::estimated_refined_triangles(const ModelVolume &mv, float edge_mm)
{
    if (edge_mm <= 0.f)
        return 0;
    return size_t(4.0 * painted_area_mm2(mv) / (double(edge_mm) * double(edge_mm)));
}

const V2Resolution &GLGizmoTextureDisplacement::v2_recommendation(const ModelVolume &mv)
{
    std::string key = std::to_string(mv.id().id) + ":" + std::to_string(mv.mesh().facets_count());
    for (const TextureDisplacementLayer &l : mv.texture_displacement_layers)
        key += Slic3r::format("|%1%:%2%:%3%:%4%", l.slot, reinterpret_cast<uintptr_t>(l.image_data.get()), l.tiling_scale, l.depth_mm);
    const Transform3d trafo = texture_displacement_volume_to_world(mv);
    for (int i = 0; i < 12; ++i)
        key += Slic3r::format(",%1%", trafo.matrix()(i / 4, i % 4));
    if (key != m_v2_rec_key) {
        m_v2_rec     = recommend_v2_resolution(mv.mesh().its, mv.texture_displacement_layers, trafo);
        m_v2_rec_key = std::move(key);
    }
    return m_v2_rec;
}

TextureDisplacementFacetsData GLGizmoTextureDisplacement::facets_data_of(const ModelVolume &mv)
{
    TextureDisplacementFacetsData out{};
    for (int i = 0; i < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++i)
        out[size_t(i)] = mv.texture_displacement_facet(i).get_data();
    return out;
}

bool GLGizmoTextureDisplacement::any_layer_colors(const ModelVolume &mv)
{
    for (const TextureDisplacementLayer &layer : mv.texture_displacement_layers)
        if (layer.color_enabled && !layer.empty() && height_texture_has_color(layer))
            return true;
    return false;
}

TextureColorSettings GLGizmoTextureDisplacement::color_settings_for(const ModelVolume &mv)
{
    TextureColorSettings out;
    if (!any_layer_colors(mv))
        return out;
    out.palette          = cached_palette();
    out.palette_pure     = make_palette(m_palette_filaments, {});
    out.despeckle_passes = mv.texture_displacement_options.color_despeckle;
    return out;
}

const std::vector<GLGizmoTextureDisplacement::PaletteEntry> &GLGizmoTextureDisplacement::cached_palette()
{
    const ModelVolume     *mv        = texture_volume();
    const bool             mixing    = mv != nullptr && mv->texture_displacement_options.color_mix_enabled;
    const int              mix_count = mv != nullptr ? std::max(0, mv->texture_displacement_options.color_mix_count) : 0;
    std::vector<ColorRGBA> filaments = filament_palette();
    std::vector<std::shared_ptr<std::vector<unsigned char>>> images;
    if (mv != nullptr)
        for (const TextureDisplacementLayer &layer : mv->texture_displacement_layers)
            if (layer.color_enabled && !layer.empty())
                images.push_back(layer.image_data);

    const bool ranking_stale = filaments != m_palette_filaments || images != m_palette_images;
    if (ranking_stale) {
        m_palette_filaments = std::move(filaments);
        m_palette_images    = std::move(images);
        m_mix_ranking.reset();
    }
    if (mixing && !m_mix_ranking)
        m_mix_ranking = rank_mixes(m_palette_filaments, mix_targets(mv->texture_displacement_layers),
                                   int(EnforcerBlockerType::ExtruderMax) - int(m_palette_filaments.size()));

    const PresetBundle            &bundle     = *wxGetApp().preset_bundle;
    const int                      free_slots = std::max(0, int(EnforcerBlockerType::ExtruderMax) - int(bundle.filament_presets.size()));
    const std::vector<std::string> slots      = mixed_slot_signature(bundle.project_config);
    if (ranking_stale || m_palette_cache.empty() || mixing != m_palette_mixing || mix_count != m_palette_mix_count ||
        free_slots != m_palette_free_slots || slots != m_palette_slots) {
        m_palette_mixing     = mixing;
        m_palette_mix_count  = mix_count;
        m_palette_free_slots = free_slots;
        m_palette_slots      = slots;
        std::vector<PaletteEntry> mixes;
        if (mixing && m_mix_ranking)
            mixes = pick_mixes(*m_mix_ranking, mix_count, free_slots, [&bundle](const PaletteEntry &e) {
                return find_fixed_mixed_filament(bundle.project_config, {unsigned(e.a + 1), unsigned(e.b + 1)},
                                                 {e.a_percent(), 100 - e.a_percent()}) >= 0;
            });
        std::vector<PaletteEntry> palette = make_palette(m_palette_filaments, mixes);
        m_palette_changed        = m_palette_changed || (!m_palette_cache.empty() && !same_palette(palette, m_palette_cache));
        m_palette_cache          = std::move(palette);
        m_palette_quantizer      = nullptr;
        m_palette_pure_quantizer = nullptr;
    }
    return m_palette_cache;
}

std::pair<ColorQuantizeFn, ColorQuantizeFn> GLGizmoTextureDisplacement::palette_quantizers()
{
    cached_palette();
    if (!m_palette_quantizer) {
        m_palette_quantizer      = make_palette_quantizer(m_palette_cache);
        const bool has_mixes     = m_palette_cache.size() > m_palette_filaments.size();
        m_palette_pure_quantizer = has_mixes ? make_palette_quantizer(make_palette(m_palette_filaments, {})) : m_palette_quantizer;
    }
    return { m_palette_quantizer, m_palette_pure_quantizer };
}

std::vector<ColorRGBA> GLGizmoTextureDisplacement::filament_palette()
{
    std::vector<ColorRGBA> all = wxGetApp().plater()->get_extruders_colors();

    const auto *is_mixed = wxGetApp().preset_bundle->project_config.option<ConfigOptionBools>("filament_is_mixed");
    std::vector<ColorRGBA> palette;
    palette.reserve(all.size());
    for (size_t i = 0; i < all.size(); ++i)
        if (is_mixed == nullptr || i >= is_mixed->values.size() || !is_mixed->values[i])
            palette.push_back(all[i]);

    if (palette.size() > size_t(EnforcerBlockerType::ExtruderMax))
        palette.resize(size_t(EnforcerBlockerType::ExtruderMax));
    return palette;
}

std::vector<GLGizmoTextureDisplacement::MixTarget> GLGizmoTextureDisplacement::mix_targets(
    const std::vector<TextureDisplacementLayer> &layers)
{
    std::vector<std::vector<MixTarget>> per_layer;
    for (const TextureDisplacementLayer &layer : layers) {
        if (!layer.color_enabled || layer.empty() || analyze_texture_detail(layer).flat_colors)
            continue;
        TextureDisplacementLayer raw = layer;
        raw.smoothing                = 0.f;
        const DecodedHeightTexture tex = decode_height_texture(raw);
        if (!tex.has_color())
            continue;

        struct Bin
        {
            double   r = 0., g = 0., b = 0.;
            uint32_t n = 0;
        };
        std::vector<Bin> bins(size_t(16 * 16 * 16));
        const size_t npx     = size_t(tex.width) * size_t(tex.height);
        const size_t stride  = std::max<size_t>(1, npx / MIX_TARGET_SAMPLES);
        uint64_t     state   = 0x9E3779B97F4A7C15ull;
        size_t       sampled = 0;
        for (size_t start = 0; start < npx; start += stride) {
            state          = state * 6364136223846793005ull + 1442695040888963407ull;
            const size_t i = start + size_t((uint64_t(uint32_t(state >> 32)) * uint64_t(stride)) >> 32);
            if (i >= npx)
                break;
            const uint8_t *px  = &tex.rgb[i * 3];
            Bin           &bin = bins[size_t(px[0] >> 4) * 256 + size_t(px[1] >> 4) * 16 + size_t(px[2] >> 4)];
            bin.r += px[0];
            bin.g += px[1];
            bin.b += px[2];
            ++bin.n;
            ++sampled;
        }
        std::vector<MixTarget> targets;
        for (const Bin &bin : bins)
            if (bin.n > 0) {
                const double inv = 1. / (255. * double(bin.n));
                targets.push_back({ srgb_to_lab(Vec3f(float(bin.r * inv), float(bin.g * inv), float(bin.b * inv))),
                                    float(double(bin.n) / double(sampled)) });
            }
        per_layer.push_back(std::move(targets));
    }

    std::vector<MixTarget> out;
    for (std::vector<MixTarget> &targets : per_layer)
        for (MixTarget &t : targets) {
            t.weight /= float(per_layer.size());
            out.push_back(t);
        }
    if (out.size() > MIX_TARGET_BINS) {
        std::partial_sort(out.begin(), out.begin() + MIX_TARGET_BINS, out.end(),
                          [](const MixTarget &l, const MixTarget &r) { return l.weight > r.weight; });
        out.resize(MIX_TARGET_BINS);
    }
    return out;
}

std::vector<GLGizmoTextureDisplacement::PaletteEntry> GLGizmoTextureDisplacement::rank_mixes(
    const std::vector<ColorRGBA> &filaments, const std::vector<MixTarget> &targets, int limit)
{
    const int n = int(filaments.size());
    if (n < 2 || targets.empty() || limit <= 0)
        return {};

    std::vector<std::string> hex(filaments.size());
    for (int i = 0; i < n; ++i)
        hex[size_t(i)] = encode_color(filaments[size_t(i)]);
    std::vector<PaletteEntry> candidates;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            for (int den = 2; den <= 6; ++den)
                for (int num = 1; num < den; ++num) {
                    if (std::gcd(num, den) != 1)
                        continue;
                    PaletteEntry e{ Vec3f::Zero(), i, j, num, den };
                    ColorRGB     blended;
                    if (!decode_color(blend_color_multi({ hex[size_t(i)], hex[size_t(j)] },
                                                        { e.a_percent(), 100 - e.a_percent() }),
                                      blended))
                        continue;
                    e.rgb = Vec3f(blended.r(), blended.g(), blended.b());
                    candidates.push_back(e);
                }

    const size_t       nt = targets.size(), nc = candidates.size();
    std::vector<Vec3f> filament_lab(size_t(n), Vec3f::Zero());
    for (int i = 0; i < n; ++i)
        filament_lab[size_t(i)] = srgb_to_lab(Vec3f(filaments[size_t(i)].r(), filaments[size_t(i)].g(), filaments[size_t(i)].b()));
    const auto de = [](const Vec3f &l, const Vec3f &r) { return DeltaE00(l.x(), l.y(), l.z(), r.x(), r.y(), r.z()); };
    std::vector<float> pure_d(nt, std::numeric_limits<float>::max());
    for (size_t t = 0; t < nt; ++t)
        for (const Vec3f &lab : filament_lab)
            pure_d[t] = std::min(pure_d[t], de(targets[t].lab, lab));
    std::vector<float> dist(nc * nt);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, nc), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t c = range.begin(); c < range.end(); ++c) {
            const Vec3f lab = srgb_to_lab(candidates[c].rgb);
            for (size_t t = 0; t < nt; ++t)
                dist[c * nt + t] = de(targets[t].lab, lab);
        }
    });

    std::vector<float>        current = pure_d;
    std::vector<char>         taken(nc, 0);
    std::vector<PaletteEntry> out;
    const auto                counts = [&](size_t c, size_t t) {
        const float d = dist[c * nt + t];
        return d < current[t] && d <= pure_d[t] - PREFER_PURE_DE;
    };
    while (int(out.size()) < limit) {
        size_t best      = nc;
        double best_gain = 0.;
        for (size_t c = 0; c < nc; ++c) {
            if (taken[c])
                continue;
            double gain = 0.;
            for (size_t t = 0; t < nt; ++t)
                if (counts(c, t))
                    gain += double(targets[t].weight) * double(current[t] - dist[c * nt + t]);
            if (gain > best_gain) {
                best_gain = gain;
                best      = c;
            }
        }
        if (best == nc || best_gain < double(MIN_MIX_GAIN))
            break;
        taken[best] = 1;
        out.push_back(candidates[best]);
        for (size_t t = 0; t < nt; ++t)
            if (counts(best, t))
                current[t] = dist[best * nt + t];
    }
    return out;
}

std::vector<GLGizmoTextureDisplacement::PaletteEntry> GLGizmoTextureDisplacement::pick_mixes(
    const std::vector<PaletteEntry> &ranking, int count, int free_slots, const std::function<bool(const PaletteEntry &)> &reusable)
{
    std::vector<PaletteEntry> out;
    for (const PaletteEntry &e : ranking) {
        if (int(out.size()) >= count)
            break;
        if (reusable && reusable(e)) {
            out.push_back(e);
        } else if (free_slots > 0) {
            out.push_back(e);
            --free_slots;
        }
    }
    return out;
}

std::vector<GLGizmoTextureDisplacement::PaletteEntry> GLGizmoTextureDisplacement::make_palette(
    const std::vector<ColorRGBA> &filaments, const std::vector<PaletteEntry> &mixes)
{
    std::vector<PaletteEntry> out;
    out.reserve(filaments.size() + mixes.size());
    for (int i = 0; i < int(filaments.size()); ++i)
        out.push_back({ Vec3f(filaments[size_t(i)].r(), filaments[size_t(i)].g(), filaments[size_t(i)].b()), i, i, 1, 1 });
    out.insert(out.end(), mixes.begin(), mixes.end());
    return out;
}

std::vector<int> GLGizmoTextureDisplacement::palette_filaments(const std::vector<PaletteEntry>                &palette,
                                                               const std::vector<uint8_t>                     &triangle_color,
                                                               const std::function<int(const PaletteEntry &)> &slot_for_mix)
{
    std::vector<char> used(palette.size(), 0);
    for (const uint8_t v : triangle_color)
        if (v > 0 && size_t(v) <= palette.size())
            used[size_t(v) - 1] = 1;

    std::vector<int> out(palette.size(), -1);
    for (size_t i = 0; i < palette.size(); ++i) {
        const PaletteEntry &e = palette[i];
        if (!e.is_mix()) {
            out[i] = e.a;
        } else if (used[i]) {
            const int slot = slot_for_mix ? slot_for_mix(e) : -1;
            out[i] = slot >= 0 ? slot : (e.num * 2 >= e.den ? e.a : e.b);
        }
    }
    return out;
}

ColorQuantizeFn GLGizmoTextureDisplacement::make_palette_quantizer(const std::vector<PaletteEntry> &palette)
{
    if (palette.empty())
        return nullptr;

    struct Lab { float l, a, b; };
    std::vector<Lab> palette_lab(palette.size());
    for (size_t i = 0; i < palette.size(); ++i) {
        const Vec3f lab = srgb_to_lab(palette[i].rgb);
        palette_lab[i]  = { lab.x(), lab.y(), lab.z() };
    }

    constexpr int E   = PALETTE_LUT_EDGE;
    auto          lut = std::make_shared<std::vector<uint8_t>>(size_t(E) * E * E, 0);
    tbb::parallel_for(tbb::blocked_range<int>(0, E), [&](const tbb::blocked_range<int> &range) {
        for (int r = range.begin(); r < range.end(); ++r)
            for (int g = 0; g < E; ++g)
                for (int b = 0; b < E; ++b) {
                    float l0, a0, b0;
                    const Vec3f lab0 = srgb_to_lab(Vec3f((r + 0.5f) / E, (g + 0.5f) / E, (b + 0.5f) / E));
                    l0 = lab0.x(); a0 = lab0.y(); b0 = lab0.z();
                    int   best  = 0, best_pure = -1;
                    float best_d = std::numeric_limits<float>::max(), best_pure_d = best_d;
                    for (size_t i = 0; i < palette_lab.size(); ++i) {
                        const float d = DeltaE00(l0, a0, b0, palette_lab[i].l, palette_lab[i].a, palette_lab[i].b);
                        if (d < best_d) {
                            best_d = d;
                            best   = int(i);
                        }
                        if (!palette[i].is_mix() && d < best_pure_d) {
                            best_pure_d = d;
                            best_pure   = int(i);
                        }
                    }
                    if (best_pure >= 0 && palette[size_t(best)].is_mix() && best_pure_d - best_d < PREFER_PURE_DE)
                        best = best_pure;
                    (*lut)[(size_t(r) * E + size_t(g)) * E + size_t(b)] = uint8_t(best);
                }
    });

    return [lut](const Vec3f &rgb) -> int {
        constexpr int E = PALETTE_LUT_EDGE;
        const int r = std::clamp(int(rgb.x() * E), 0, E - 1);
        const int g = std::clamp(int(rgb.y() * E), 0, E - 1);
        const int b = std::clamp(int(rgb.z() * E), 0, E - 1);
        return int((*lut)[(size_t(r) * E + size_t(g)) * E + size_t(b)]);
    };
}

TextureDisplacementPrepareResult GLGizmoTextureDisplacement::prepare_mesh(
    const indexed_triangle_set &base, const TextureDisplacementFacetsData &masks,
    const std::vector<TextureDisplacementLayer> &layers, const TextureDisplacementPrepareParams &params,
    const TextureColorSettings &color_settings, const DisplacementProgressFn &progress,
    BakeStageRecorder *debug)
{
    TextureDisplacementPrepareResult out;
    const auto                       report = [&progress](int pct) { return !progress || progress(pct); };

    auto       stage_clock = std::chrono::steady_clock::now();
    const auto capture     = [&](const char *name, const indexed_triangle_set &m,
                             const std::string &detail = {}) {
        if (debug == nullptr)
            return;
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stage_clock).count();
        debug->capture(name, m.vertices, m.indices, ms, detail);
        stage_clock = std::chrono::steady_clock::now();
    };
    capture("input", base, "as handed to prepare");

    TriangleMesh                  mesh(base);
    TextureDisplacementFacetsData current = masks;
    bool                          changed = false;
    bool                          had_paint = false;
    for (const TriangleSelector::TriangleSplittingData &m : masks)
        had_paint = had_paint || TriangleSelector::has_facets(m, EnforcerBlockerType::ENFORCER);
    if (!report(1))
        return out;

    if (params.remesh_edge_mm > 0.f) {
        indexed_triangle_set remeshed;
        if (plan_remesh(mesh.its, params.remesh_edge_mm, params.remesh_sharp_deg, remeshed)) {
            TriangleMesh                   new_mesh(std::move(remeshed));
            const AABBTreeIndirect::Tree3f old_tree =
                AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(mesh.its.vertices, mesh.its.indices);
            TextureDisplacementFacetsData carried{};
            bool                          any = false;
            for (int i = 0; i < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++i) {
                carried[size_t(i)] =
                    remap_texture_paint_spatial(mesh, current[size_t(i)], old_tree, new_mesh, Vec3f::Zero());
                any = any || !carried[size_t(i)].bitstream.empty();
            }
            mesh    = std::move(new_mesh);
            current = std::move(carried);
            changed = true;
            capture("remesh", mesh.its,
                    "target " + std::to_string(params.remesh_edge_mm) + " mm" +
                        (had_paint && !any ? ", paint lost" : ""));
            if (had_paint && !any) {
                out.paint_lost = true;
                return out;
            }
        } else if (debug != nullptr) {
            capture("remesh", mesh.its, "skipped: remeshing changed nothing");
        }
    }
    if (!report(50))
        return {};

    if (params.subdiv_target_mm > 0.f) {
        std::vector<uint8_t>                                       region;
        std::array<LayerPaintMap, TEXTURE_DISPLACEMENT_MAX_LAYERS> paint;
        if (collect_paint_region(mesh, current, region, &paint)) {
            HeightFieldSampler sampler;
            if (params.subdiv_feature)
                sampler = make_combined_displacement_sampler(mesh.its, layers, current);
            ColorFieldSampler color;
            if (params.subdiv_color_edge_mm > 0.f && !color_settings.empty())
                color = make_combined_color_sampler(mesh.its, layers, current, make_palette_quantizer(color_settings.palette),
                                                    make_palette_quantizer(color_settings.palette_pure));
            const float tol   = params.subdiv_feature ? params.subdiv_detail_mm : 0.f;
            const float floor = params.subdiv_feature ? params.subdiv_min_edge_mm : 0.f;
            float texel_mm = std::numeric_limits<float>::max();
            if (params.cut_steps && sampler)
                for (const TextureDisplacementLayer &layer : layers) {
                    if (layer.empty())
                        continue;
                    const DecodedHeightTexture &tex = decode_height_texture(layer);
                    if (tex.width > 0 && layer.tiling_scale > 0.f)
                        texel_mm = std::min(texel_mm, layer.tiling_scale / float(tex.width));
                }
            const bool cut_steps = texel_mm < std::numeric_limits<float>::max() &&
                                   texture_has_steps_to_cut(mesh.its, region, sampler, 2.f * texel_mm, 2.f * texel_mm);
            bool        aborted = false;
            std::vector<int> source;
            indexed_triangle_set refined =
                subdivide_mesh_adaptive(mesh.its, region, params.subdiv_target_mm,
                                        int(mesh.its.indices.size()) + params.subdiv_added_triangles, &source,
                                        sampler, tol, floor, params.subdiv_border_mm, [&](int pct) {
                                            aborted = !report(50 + pct / 2);
                                            return !aborted;
                                        },
                                        color, params.subdiv_color_edge_mm, cut_steps);
            if (aborted)
                return {};
            if (refined.indices.size() != mesh.its.indices.size()) {
                mesh    = TriangleMesh(std::move(refined));
                current = masks_after_subdivision(mesh, source, paint);
                changed = true;
                capture("adaptive subdivide", mesh.its,
                        params.subdiv_feature ? "feature adaptive" : "length only");
            } else if (debug != nullptr) {
                capture("adaptive subdivide", mesh.its, "skipped: already meets the criteria");
            }

            if (cut_steps) {
                std::vector<uint8_t> cut_region(mesh.its.indices.size(), 0);
                for (size_t i = 0; i < cut_region.size() && i < source.size(); ++i)
                    cut_region[i] = (region[size_t(source[i])] & REFINE_PAINTED) ? 1 : 0;
                const HeightFieldSampler refined_sampler =
                    make_combined_displacement_sampler(mesh.its, layers, current);
                std::vector<int>     cut_source;
                size_t               cuts = 0;
                indexed_triangle_set cut = cut_mesh_at_steps(mesh.its, cut_region, refined_sampler,
                                                             2.f * texel_mm, 2.f * texel_mm, 0.f,
                                                             &cut_source, &cuts);
                if (cuts > 0) {
                    for (int &s : cut_source)
                        s = source[size_t(s)];
                    mesh    = TriangleMesh(std::move(cut));
                    current = masks_after_subdivision(mesh, cut_source, paint);
                    changed = true;
                    capture("step cut", mesh.its, std::to_string(cuts) + " triangles cut");
                } else if (debug != nullptr) {
                    capture("step cut", mesh.its, "skipped: the refined mesh has no steps left to cut");
                }
            } else if (debug != nullptr && params.cut_steps) {
                capture("step cut", mesh.its, "skipped: not a step texture");
            }
        }
    }
    if (!report(100) || !changed)
        return out;

    out.mesh  = std::move(mesh.its);
    out.masks = std::move(current);
    return out;
}

void GLGizmoTextureDisplacement::subdivide_model_adaptive()
{
    ModelVolume *mv = texture_volume();
    if (mv == nullptr || m_subdivide_target_mm <= 0.f)
        return;

    update_model_object();

    if (!mv->is_texture_displacement_painted()) {
        show_error(nullptr, _u8L("Paint the area you want to subdivide first - adaptive subdivision only "
                                 "refines where you have painted."));
        return;
    }

    TextureDisplacementPrepareParams params;
    params.subdiv_target_mm       = m_subdivide_target_mm;
    params.subdiv_detail_mm       = m_subdivide_detail_mm;
    params.subdiv_min_edge_mm     = m_subdivide_min_edge_mm;
    params.subdiv_border_mm       = m_subdivide_border_mm;
    params.subdiv_color_edge_mm   = m_subdivide_color_mm;
    params.subdiv_feature         = m_subdivide_feature;
    params.subdiv_added_triangles = m_subdivide_budget_k * 1000;
    queue_prepare(params, _u8L("Adaptive subdivide for texture displacement"),  false,
                  _u8L("Nothing to subdivide - the painted area already meets the target edge length and "
                       "detail tolerance, or the triangle budget is already used up."));
}

void GLGizmoTextureDisplacement::smooth_model()
{
    ModelVolume *mv = texture_volume();
    ModelObject *mo = m_c->selection_info()->model_object();
    if (mv == nullptr || mo == nullptr)
        return;
    const TextureDisplacementOptions &opts = mv->texture_displacement_options;
    if (opts.smooth_strength <= 0.f || opts.smooth_iterations <= 0)
        return;

    update_model_object();
    const indexed_triangle_set &its = mv->mesh().its;
    std::vector<uint8_t>        painted_face(its.indices.size(), 0);
    bool                        any = false;
    for (int slot = 0; slot < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++slot)
        for (const TriangleSelector::TriangleBitStreamMapping &m : mv->texture_displacement_facet(slot).get_data().triangles_to_split)
            if (size_t(m.triangle_idx) < its.indices.size()) {
                painted_face[size_t(m.triangle_idx)] = 1;
                any                                  = true;
            }
    if (!any) {
        show_error(nullptr, _u8L("Paint the area you want to smooth first - smoothing only touches the "
                                 "painted part of the model."));
        return;
    }
    std::vector<uint8_t> movable(its.vertices.size(), 0);
    for (size_t i = 0; i < its.indices.size(); ++i)
        if (painted_face[i])
            for (int k = 0; k < 3; ++k)
                movable[size_t(its.indices[i][k])] = 1;
    if (opts.smooth_skip_border)
        for (size_t i = 0; i < its.indices.size(); ++i)
            if (!painted_face[i])
                for (int k = 0; k < 3; ++k)
                    movable[size_t(its.indices[i][k])] = 0;
    if (std::none_of(movable.begin(), movable.end(), [](uint8_t m) { return m != 0; })) {
        show_error(nullptr, _u8L("Nothing to smooth - the painted area is only one triangle deep, so with "
                                 "\"Ignore outer ring\" on there are no interior vertices to relax."));
        return;
    }

    indexed_triangle_set smoothed = its;
    {
        wxBusyCursor wait;
        smooth_mesh_vertices(smoothed, movable, opts.smooth_strength, opts.smooth_iterations);
    }

    Plater *plater = wxGetApp().plater();
    Plater::TakeSnapshot snapshot(plater, _u8L("Smooth texture displacement"), UndoRedo::SnapshotType::GizmoAction);

    std::optional<TriangleSelector::SavedPainting>                    saved_painting = mv->save_painting();
    std::array<TriangleSelector::TriangleSplittingData, TEXTURE_DISPLACEMENT_MAX_LAYERS> saved_texture;
    for (int i = 0; i < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++i)
        saved_texture[size_t(i)] = mv->texture_displacement_facet(i).get_data();

    mv->set_mesh(TriangleMesh(std::move(smoothed)));
    mv->set_new_unique_id();
    mv->calculate_convex_hull();
    mv->restore_painting(saved_painting);
    for (int i = 0; i < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++i)
        mv->texture_displacement_facet(i).set_data(std::move(saved_texture[size_t(i)]));

    if (ObjectList *obj_list = wxGetApp().obj_list()) {
        const ModelObjectPtrs &objs = plater->model().objects;
        auto it = std::find(objs.begin(), objs.end(), mo);
        if (it != objs.end())
            obj_list->update_info_items(size_t(it - objs.begin()));
    }
    plater->changed_object(*mo);
    update_from_model_object(false);
    m_parent.set_as_dirty();
}

bool GLGizmoTextureDisplacement::plan_remesh(const indexed_triangle_set &src, float target_edge_mm,
                                             float sharp_angle_deg, indexed_triangle_set &out)
{
    if (target_edge_mm <= 0.f)
        return false;

    static constexpr double REMESH_MAX_TRIANGLES = 150000.0;
    double area = 0.0;
    for (const stl_triangle_vertex_indices &tri : src.indices)
        area += 0.5 * double((src.vertices[size_t(tri[1])] - src.vertices[size_t(tri[0])])
                                 .cross(src.vertices[size_t(tri[2])] - src.vertices[size_t(tri[0])])
                                 .norm());
    if (const double budget_edge = std::sqrt(area / (0.4330127 * REMESH_MAX_TRIANGLES));
        budget_edge > double(target_edge_mm)) {
        BOOST_LOG_TRIVIAL(info) << "Texture displacement: remesh target raised from " << target_edge_mm
                                << " mm to " << budget_edge << " mm to stay within "
                                << int(REMESH_MAX_TRIANGLES) << " triangles.";
        target_edge_mm = float(budget_edge);
    }

    indexed_triangle_set remeshed = MeshBoolean::cgal::remesh_isotropic(
        src, double(target_edge_mm),  5, double(sharp_angle_deg),
         3);
    if (remeshed.indices.empty() ||
        (remeshed.vertices.size() == src.vertices.size() && remeshed.indices.size() == src.indices.size() &&
         remeshed.indices == src.indices))
        return false;

    out = std::move(remeshed);
    return true;
}

void GLGizmoTextureDisplacement::remesh_model()
{
    if (texture_volume() == nullptr || m_remesh_target_edge_mm <= 0.f)
        return;

    update_model_object();

    TextureDisplacementPrepareParams params;
    params.remesh_edge_mm   = m_remesh_target_edge_mm;
    params.remesh_sharp_deg = m_remesh_keep_sharp_edges ? m_remesh_sharp_angle_deg : 0.f;
    queue_prepare(params, _u8L("Remesh model for texture displacement"),  false,
                  _u8L("Remeshing did not change the model. It may be non-manifold (open edges or edges "
                       "shared by more than two triangles), or the target edge length may already be met."));
}

void GLGizmoTextureDisplacement::rebuild_subdivide_preview()
{
    m_subdivide_preview_glmodel.reset();
    m_subdivide_preview_tris = -1;
    const ModelVolume *mv = texture_volume();
    if (mv == nullptr)
        return;

    indexed_triangle_set its;
    if (m_subdivide_adaptive) {
        if (m_subdivide_target_mm <= 0.f)
            return;
        const TextureDisplacementFacetsData facets = facets_data_of(*mv);
        std::vector<uint8_t>                region;
        if (!collect_paint_region(mv->mesh(), facets, region, nullptr))
            return;
        HeightFieldSampler sampler;
        if (m_subdivide_feature)
            sampler = make_combined_displacement_sampler(mv->mesh().its, mv->texture_displacement_layers, facets);
        const float tol   = m_subdivide_feature ? m_subdivide_detail_mm : 0.f;
        const float floor = m_subdivide_feature ? m_subdivide_min_edge_mm : 0.f;
        ColorFieldSampler color;
        if (m_subdivide_color_mm > 0.f && any_layer_colors(*mv)) {
            const auto [quantize, quantize_pure] = palette_quantizers();
            color = make_combined_color_sampler(mv->mesh().its, mv->texture_displacement_layers, facets, quantize,
                                                quantize_pure);
        }
        its = subdivide_mesh_adaptive(mv->mesh().its, region, m_subdivide_target_mm,
                                      int(mv->mesh().its.indices.size()) + m_subdivide_budget_k * 1000,
                                      nullptr, sampler, tol, floor, m_subdivide_border_mm, nullptr, color,
                                      m_subdivide_color_mm);
    } else {
        if (m_subdivide_count < 1)
            return;
        its = subdivide_mesh_uniform(mv->mesh().its, 0.f, m_subdivide_count);
    }
    if (its.indices.empty())
        return;
    m_subdivide_preview_tris = int(its.indices.size());

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
    init_data.reserve_vertices(its.vertices.size());
    init_data.reserve_indices(its.indices.size() * 6);
    for (const Vec3f &v : its.vertices)
        init_data.add_vertex(v);
    for (const stl_triangle_vertex_indices &tri : its.indices)
        for (int i = 0; i < 3; ++i)
            init_data.add_line(unsigned(tri[i]), unsigned(tri[(i + 1) % 3]));
    if (!init_data.is_empty())
        m_subdivide_preview_glmodel.init_from(std::move(init_data));
}

void GLGizmoTextureDisplacement::render_subdivide_preview()
{
    const ModelObject *mo = m_c->selection_info()->model_object();
    const ModelVolume *mv = texture_volume();
    if (mo == nullptr || mv == nullptr || !m_subdivide_preview_glmodel.is_initialized())
        return;
    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    const Selection  &selection    = m_parent.get_selection();
    const Transform3d trafo_matrix = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();
    const Camera     &camera       = wxGetApp().plater()->get_camera();

    shader->start_using();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix() * trafo_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    glsafe(::glEnable(GL_POLYGON_OFFSET_LINE));
    glsafe(::glPolygonOffset(-1.0f, -1.0f));
    m_subdivide_preview_glmodel.set_color(ColorRGBA(0.2f, 0.9f, 1.0f, 0.7f));
    m_subdivide_preview_glmodel.render();
    glsafe(::glDisable(GL_POLYGON_OFFSET_LINE));
    shader->stop_using();
}

GLTexture *GLGizmoTextureDisplacement::get_layer_thumbnail(const TextureDisplacementLayer &layer)
{
    if (layer.empty() || layer.slot < 0 || size_t(layer.slot) >= TEXTURE_DISPLACEMENT_MAX_LAYERS)
        return nullptr;

    const size_t slot = size_t(layer.slot);
    if (m_thumbnails[slot] && m_thumbnail_source[slot] == layer.image_data.get() &&
        m_thumbnail_smoothing[slot] == layer.smoothing)
        return m_thumbnails[slot].get();

    std::unique_ptr<GLTexture> texture = upload_height_thumbnail(decode_height_texture(layer));
    if (!texture)
        return nullptr;

    m_thumbnails[slot]          = std::move(texture);
    m_thumbnail_source[slot]    = layer.image_data.get();
    m_thumbnail_smoothing[slot] = layer.smoothing;
    return m_thumbnails[slot].get();
}

GLTexture *GLGizmoTextureDisplacement::get_layer_color_texture(const TextureDisplacementLayer &layer)
{
    if (layer.empty() || !layer.color_enabled)
        return nullptr;
    if (m_color_tex && m_color_tex_source == layer.image_data.get() && m_color_tex_smoothing == layer.smoothing)
        return m_color_tex.get();

    std::unique_ptr<GLTexture> texture = upload_color_texture(decode_height_texture(layer), HEIGHT_TEX_MAX_PX);
    if (!texture) {
        m_color_tex.reset();
        m_color_tex_source = nullptr;
        return nullptr;
    }
    m_color_tex           = std::move(texture);
    m_color_tex_source    = layer.image_data.get();
    m_color_tex_smoothing = layer.smoothing;
    return m_color_tex.get();
}

GLTexture *GLGizmoTextureDisplacement::get_layer_height_texture(const TextureDisplacementLayer &layer)
{
    if (layer.empty())
        return nullptr;

    if (m_height_tex && m_height_tex_source == layer.image_data.get() && m_height_tex_smoothing == layer.smoothing)
        return m_height_tex.get();

    std::unique_ptr<GLTexture> texture = upload_height_thumbnail(decode_height_texture(layer), HEIGHT_TEX_MAX_PX);
    if (!texture)
        return nullptr;

    m_height_tex           = std::move(texture);
    m_height_tex_source    = layer.image_data.get();
    m_height_tex_smoothing = layer.smoothing;
    return m_height_tex.get();
}

void GLGizmoTextureDisplacement::bake(bool own_snapshot)
{
    ModelVolume *mv = texture_volume();
    if (!mv || m_bake_in_progress)
        return;

    update_model_object();

    if (!mv->is_texture_displacement_painted()) {
        show_error(nullptr, _u8L("Nothing is painted, there is nothing to bake."));
        return;
    }

    m_bake_in_progress = true;
    queue_texture_displacement_bake(*mv, color_settings_for(*mv), [this]() {
        m_bake_in_progress = false;
        if (m_state == On && m_c->selection_info() && m_c->selection_info()->model_object())
            update_from_model_object(false);
        m_parent.set_as_dirty();
    }, own_snapshot);
}

static constexpr float STD_REMESH_EDGE_MM     = 1.0f;
static constexpr float STD_REMESH_SHARP_DEG   = 40.f;
static constexpr float STD_SUBDIV_MAX_EDGE_MM = 20.f;
static constexpr float STD_SUBDIV_DETAIL_MM   = 0.02f;
static constexpr float STD_SUBDIV_MIN_EDGE_MM = 0.1f;
static constexpr float STD_SUBDIV_BORDER_MM   = 0.4f;
static constexpr float STD_SUBDIV_COLOR_MM    = 0.25f;

bool GLGizmoTextureDisplacement::apply_standard_mode_presets(ModelVolume *mv)
{
    bool       changed = false;
    const auto pin     = [&changed](auto &field, auto value) {
        if (field != value) {
            field   = value;
            changed = true;
        }
    };

    if (mv != nullptr) {
        pin(mv->texture_displacement_options.displace_border, true);
        pin(mv->texture_displacement_options.smooth_enabled, false);
    }
    pin(m_subdivide_adaptive, true);
    pin(m_subdivide_feature, true);
    pin(m_subdivide_target_mm, STD_SUBDIV_MAX_EDGE_MM);
    pin(m_subdivide_detail_mm, STD_SUBDIV_DETAIL_MM);
    pin(m_subdivide_min_edge_mm, STD_SUBDIV_MIN_EDGE_MM);
    pin(m_subdivide_border_mm, STD_SUBDIV_BORDER_MM);
    pin(m_subdivide_color_mm, STD_SUBDIV_COLOR_MM);
    pin(m_remesh_target_edge_mm, STD_REMESH_EDGE_MM);
    pin(m_remesh_keep_sharp_edges, true);
    pin(m_remesh_sharp_angle_deg, STD_REMESH_SHARP_DEG);
    return changed;
}

void GLGizmoTextureDisplacement::bake_standard()
{
    ModelVolume *mv = texture_volume();
    if (mv == nullptr || m_bake_in_progress || m_prepare_in_progress)
        return;

    update_model_object();
    if (!mv->is_texture_displacement_painted()) {
        show_error(nullptr, _u8L("Nothing is painted, there is nothing to bake."));
        return;
    }
    apply_standard_mode_presets(mv);
    if (mv->texture_displacement_options.pipeline_v2) {
        bake();
        return;
    }

    TextureDisplacementPrepareParams params;
    params.remesh_edge_mm         = STD_REMESH_EDGE_MM;
    params.remesh_sharp_deg       = STD_REMESH_SHARP_DEG;
    params.subdiv_target_mm       = STD_SUBDIV_MAX_EDGE_MM;
    params.subdiv_detail_mm       = STD_SUBDIV_DETAIL_MM;
    params.subdiv_min_edge_mm     = STD_SUBDIV_MIN_EDGE_MM;
    params.subdiv_border_mm       = STD_SUBDIV_BORDER_MM;
    params.subdiv_color_edge_mm   = STD_SUBDIV_COLOR_MM;
    params.subdiv_feature         = true;
    params.subdiv_added_triangles = m_subdivide_budget_k * 1000;
    params.cut_steps              = true;
    queue_prepare(params, _u8L("Bake texture displacement"),  true, {});
}

void GLGizmoTextureDisplacement::run_stage_debug()
{
    ModelVolume *mv = texture_volume();
    if (mv == nullptr || m_debug_in_progress || m_bake_in_progress || m_prepare_in_progress)
        return;

    update_model_object();
    if (!mv->is_texture_displacement_painted()) {
        show_error(nullptr, _u8L("Nothing is painted, so there are no bake stages to capture."));
        return;
    }

    const bool run_prepare = !pro_mode() && !mv->texture_displacement_options.pipeline_v2;

    TextureDisplacementPrepareParams params;
    if (run_prepare) {
        apply_standard_mode_presets(mv);
        params.remesh_edge_mm         = STD_REMESH_EDGE_MM;
        params.remesh_sharp_deg       = STD_REMESH_SHARP_DEG;
        params.subdiv_target_mm       = STD_SUBDIV_MAX_EDGE_MM;
        params.subdiv_detail_mm       = STD_SUBDIV_DETAIL_MM;
        params.subdiv_min_edge_mm     = STD_SUBDIV_MIN_EDGE_MM;
        params.subdiv_border_mm       = STD_SUBDIV_BORDER_MM;
        params.subdiv_color_edge_mm   = STD_SUBDIV_COLOR_MM;
        params.subdiv_feature         = true;
        params.subdiv_added_triangles = m_subdivide_budget_k * 1000;
        params.cut_steps              = true;
    }

    exit_debug_view();

    m_debug_in_progress = true;
    queue_texture_displacement_debug(
        *mv, color_settings_for(*mv), params, run_prepare, m_debug_check_topology,
        [this](std::vector<BakeStageSnapshot> stages) {
            m_debug_in_progress = false;
            if (!stages.empty()) {
                m_debug_stages = std::move(stages);
                show_debug_stage(int(m_debug_stages.size()) - 1);
            }
            m_parent.set_as_dirty();
        });
}

void GLGizmoTextureDisplacement::show_debug_stage(int index)
{
    if (index < 0 || size_t(index) >= m_debug_stages.size())
        return;
    const BakeStageSnapshot &stage = m_debug_stages[size_t(index)];
    if (stage.mesh.empty())
        return;

    m_debug_stage = index;
    m_use_shaded_preview = false;
    m_preview_color_runs.clear();
    m_preview_glmodel.reset();

    indexed_triangle_set its;
    its.vertices = stage.mesh.vertices;
    its.indices  = stage.mesh.indices;
    m_preview_glmodel.init_from(its);
    m_preview_glmodel.set_color(GLVolume::NEUTRAL_COLOR);
    m_preview_its = std::move(its);
    refresh_wireframe();
    m_parent.set_as_dirty();
}

void GLGizmoTextureDisplacement::exit_debug_view()
{
    const bool was_showing = m_debug_stage >= 0;
    m_debug_stage          = -1;
    m_debug_stages.clear();
    m_debug_stages.shrink_to_fit();
    if (was_showing)
        rebuild_preview();
}

void GLGizmoTextureDisplacement::render_debug_stage_panel(ModelVolume *mv)
{
    if (mv == nullptr)
        return;

    ImGui::Separator();
    if (!ImGui::CollapsingHeader(_u8L("Bake stages (debug)").c_str()))
        return;

    m_imgui->disabled_begin(m_debug_in_progress || m_bake_in_progress || m_prepare_in_progress ||
                            !mv->is_texture_displacement_painted());
    if (m_imgui->button(m_debug_in_progress ? _L("Capturing...") : _L("Capture stages")))
        run_stage_debug();
    m_imgui->disabled_end();
    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_u8L("Run the same bake the Bake button runs, keeping the mesh after every "
                              "stage, and step through them here. Nothing is committed to the model - "
                              "this is for finding which stage a bad result came from."),
                          m_imgui->scaled(20.f));

    ImGui::SameLine();
    m_imgui->bbl_checkbox(_L("Check topology"), m_debug_check_topology);
    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_u8L("Count open and non-manifold edges after each stage, which is how a stage "
                              "that tore the mesh is spotted. It scans every edge, so it adds noticeably "
                              "to the capture on a fine bake."),
                          m_imgui->scaled(20.f));

    if (m_debug_stages.empty())
        return;

    ImGui::BeginChild("##bake_stages", ImVec2(0.f, m_imgui->scaled(9.f)), true,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    for (size_t i = 0; i < m_debug_stages.size(); ++i) {
        const BakeStageSnapshot &st = m_debug_stages[i];
        char                     label[256];
        std::snprintf(label, sizeof(label), "%zu. %-18s %8zu tris %8.1f ms##stage%zu", i,
                      st.name.c_str(), st.triangles, st.ms, i);
        if (ImGui::Selectable(label, int(i) == m_debug_stage) && !st.mesh.empty())
            show_debug_stage(int(i));
    }
    ImGui::EndChild();

    m_imgui->disabled_begin(m_debug_stage <= 0);
    if (m_imgui->button(_L("< Previous")))
        show_debug_stage(m_debug_stage - 1);
    m_imgui->disabled_end();
    ImGui::SameLine();
    m_imgui->disabled_begin(m_debug_stage < 0 || size_t(m_debug_stage + 1) >= m_debug_stages.size());
    if (m_imgui->button(_L("Next >")))
        show_debug_stage(m_debug_stage + 1);
    m_imgui->disabled_end();
    ImGui::SameLine();
    if (m_imgui->button(_L("Close")))
        exit_debug_view();

    if (m_debug_stage >= 0 && size_t(m_debug_stage) < m_debug_stages.size()) {
        const BakeStageSnapshot &st = m_debug_stages[size_t(m_debug_stage)];
        m_imgui->text(from_u8(st.name) + ": " + std::to_string(st.triangles) + " " +
                      _u8L("triangles") + ", " + std::to_string(st.vertices) + " " + _u8L("vertices"));
        if (!st.detail.empty())
            m_imgui->text(from_u8(st.detail));
        if (st.topology_checked) {
            const std::string topo = _u8L("Open edges") + ": " + std::to_string(st.open_edges) + "   " +
                                     _u8L("Non-manifold") + ": " + std::to_string(st.non_manifold_edges) +
                                     "   " + _u8L("Degenerate") + ": " + std::to_string(st.degenerate);
            if (st.open_edges > 0 || st.non_manifold_edges > 0)
                m_imgui->warning_text(from_u8(topo));
            else
                m_imgui->text(topo);
        }
    }

    double total_ms = 0.0;
    for (const BakeStageSnapshot &st : m_debug_stages)
        total_ms += st.ms;
    char total[128];
    std::snprintf(total, sizeof(total), "%s: %.1f ms", _u8L("Total").c_str(), total_ms);
    m_imgui->text(total);

    if (m_imgui->button(_L("Write stage meshes"))) {
        const std::string dir =
            (boost::filesystem::temp_directory_path() / "orca-bake-stages").string();
        const size_t written = dump_bake_stages(m_debug_stages, dir);
        if (written > 0)
            show_info(nullptr, from_u8(_u8L("Wrote the stage meshes as OBJ files to:") + "\n" + dir));
        else
            show_error(nullptr, _u8L("Could not write the stage meshes."));
    }
    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_u8L("Write every captured stage out as an OBJ file, so they can be opened "
                              "side by side in a mesh viewer."),
                          m_imgui->scaled(20.f));
}

void GLGizmoTextureDisplacement::queue_prepare(const TextureDisplacementPrepareParams &params,
                                               const std::string &snapshot_name, bool then_bake,
                                               const std::string &unchanged_msg)
{
    ModelVolume *mv = texture_volume();
    if (mv == nullptr || m_prepare_in_progress || m_bake_in_progress)
        return;

    TextureDisplacementPrepareInput input;
    input.volume_id     = mv->id();
    input.base_mesh     = mv->mesh().its;
    input.masks         = facets_data_of(*mv);
    input.layers        = mv->texture_displacement_layers;
    input.params        = params;
    input.snapshot_name = snapshot_name;
    if (params.subdiv_color_edge_mm > 0.f)
        input.color = color_settings_for(*mv);

    m_prepare_in_progress = true;
    queue_texture_displacement_prepare(std::move(input), [this, then_bake, unchanged_msg](
                                                             TextureDisplacementPrepareOutcome outcome) {
        m_prepare_in_progress = false;
        if (m_state == On && m_c->selection_info() && m_c->selection_info()->model_object())
            update_from_model_object(false);
        m_parent.set_as_dirty();

        switch (outcome) {
        case TextureDisplacementPrepareOutcome::Failed:
            return;
        case TextureDisplacementPrepareOutcome::PaintLost:
            show_error(nullptr, _u8L("The painted area could not be carried onto the remeshed model, so "
                                     "nothing was changed. Switch to Pro mode and remesh before painting."));
            return;
        case TextureDisplacementPrepareOutcome::Unchanged:
            if (!unchanged_msg.empty())
                show_error(nullptr, unchanged_msg);
            break;
        case TextureDisplacementPrepareOutcome::Committed:
            break;
        }
        if (then_bake)
            bake( outcome == TextureDisplacementPrepareOutcome::Unchanged);
    });
}

void GLGizmoTextureDisplacement::render_paint_cursor_hint()
{
    if (m_seam_edit_mode || m_adjust_texture_mode)
        return;
    const ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse || !ImGui::IsMousePosValid())
        return;

    const bool  removing = io.KeyShift || m_erase_mode;
    const ImU32 color    = removing ? IM_COL32(235, 70, 60, 255) : IM_COL32(90, 210, 110, 255);
    const char *glyph    = removing ? "-" : "+";

    ImDrawList  *dl = ImGui::GetForegroundDrawList();
    const float  fs = ImGui::GetFontSize() * 1.5f;
    const ImVec2 at(io.MousePos.x + 15.f, io.MousePos.y - fs - 6.f);
    dl->AddCircleFilled(ImVec2(at.x + fs * 0.28f, at.y + fs * 0.5f), fs * 0.62f, IM_COL32(0, 0, 0, 150));
    dl->AddText(ImGui::GetFont(), fs, at, color, glyph);
}

void GLGizmoTextureDisplacement::on_render_input_window(float x, float y, float bottom_limit)
{
    ModelObject *mo = m_c->selection_info()->model_object();
    if (!mo)
        return;
    ModelVolume *mv = texture_volume();

    cached_palette();
    if (m_palette_changed)
        m_preview_params_dirty = true;

    float  scale = m_parent.get_scale();
    #ifdef WIN32
        int dpi = get_dpi_for_window(wxGetApp().GetTopWindow());
        scale *= (float) dpi / (float) DPI_DEFAULT;
    #endif

    const float approx_height = m_imgui->scaled(24.f);
    y = std::min(y, bottom_limit - approx_height);

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;
    if (!m_undocked) {
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;
        float right = float(m_parent.get_canvas_size().get_width()) - m_imgui->scaled(0.5f);
        GizmoImguiSetNextWIndowPos(right, y, 0.f, 0.f, ImGuiCond_Always, 1.0f, 0.0f);
    }

    ImGuiWrapper::push_toolbar_style(m_parent.get_scale());
    GizmoImguiBegin(get_name(), flags);
    ensure_panel_icons();
    process_uv_commands();
    const float previous_body_h   = m_panel_body_h;
    const float previous_footer_h = m_panel_footer_h;

    if (!pro_mode() && apply_standard_mode_presets(mv))
        m_preview_params_dirty = true;

    const bool        dark     = wxGetApp().dark_mode();
    const bool        busy     = m_bake_in_progress || m_prepare_in_progress;
    const ImGuiStyle &style    = ImGui::GetStyle();
    const float       panel_w  = m_imgui->scaled(21.5f);
    const float       frame_h  = ImGui::GetFrameHeight();
    const float       icon_md  = std::round(frame_h * 1.25f);
    const float       icon_sm  = std::round(frame_h * 1.05f);
    const float       gap_s    = std::round(m_imgui->scaled(0.4f));
    const float       label_w  = m_imgui->scaled(4.8f);
    const float       card_pad = std::round(m_imgui->scaled(0.55f));
    const float       wrap_w   = m_imgui->scaled(20.f);
    const ImVec4      orca     = ImGuiWrapper::COL_ORCA;
    const ImVec4      col_link = ImGuiWrapper::COL_ORCA;
    const ImVec4      col_frame = ImGui::GetStyleColorVec4(ImGuiCol_Separator);
    const ImU32       col_card = ImGui::GetColorU32(ImGuiCol_Text, dark ? 0.045f : 0.047f);
    const ImU32       col_line = ImGui::GetColorU32(ImGuiCol_Text, dark ? 0.08f : 0.09f);
    const ImU32       col_sep  = ImGui::GetColorU32(ImGuiCol_Separator);

    const auto   scoped_combo   = [&](const char *id, int *v, const char *const items[], int n) {
        ImGuiWrapper::push_combo_style(m_parent.get_scale());
        const bool changed = ImGui::Combo(id, v, items, n);
        ImGuiWrapper::pop_combo_style();
        return changed;
    };
    const auto hover_tip = [&](const auto &text) {
        if (ImGui::IsItemHovered())
            m_imgui->tooltip(text, wrap_w);
    };

    const auto icon_toggle = [&](int uid, const std::string &iconfile, bool active, float sz, const wxString &label,
                                 const wxString &tip_text, const wxString &unavailable = wxString()) -> bool {
        const bool  na      = !unavailable.empty();
        const auto  it      = m_panel_icon_map.find(iconfile);
        const float pad     = std::max(1.f, std::round(sz * 0.14f));
        const int   variant = active ? 1 : 0;
        bool        clicked = false;
        ImGui::PushID(uid);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, na ? style.Alpha * 0.32f : style.Alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(orca.x, orca.y, orca.z, 0.22f) : ImVec4(0.f, 0.f, 0.f, 0.f));
        if (na) {
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.f, 0.f, 0.f, 0.f));
        }
        if (it != m_panel_icon_map.end() && int(it->second.size()) > variant && it->second[size_t(variant)]->is_valid()) {
            const IconManager::Icon &ic = *it->second[size_t(variant)];
            clicked = m_imgui->image_button((ImTextureID) (intptr_t) ic.tex_id, ImVec2(sz - 2.f * pad, sz - 2.f * pad), ic.tl, ic.br,
                                            int(pad));
        } else {
            clicked = ImGui::Button(label.ToUTF8().data(), ImVec2(0.f, sz));
        }
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                            ImGui::GetColorU32(active ? orca : col_frame), style.FrameRounding);
        ImGui::PopStyleColor(na ? 3 : 1);
        ImGui::PopStyleVar(2);
        ImGui::PopID();
        if (ImGui::IsItemHovered())
            m_imgui->tooltip(na ? tip_text + "\n\n" + unavailable : tip_text, wrap_w);
        return clicked && !na;
    };
    const auto icon_button = [&](int uid, const std::string &iconfile, float sz, const wxString &label,
                                 const wxString &tip_text) -> bool {
        const auto  it      = m_panel_icon_map.find(iconfile);
        const float pad     = std::max(1.f, std::round(sz * 0.14f));
        bool        clicked = false;
        ImGui::PushID(uid);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
        if (it != m_panel_icon_map.end() && !it->second.empty() && it->second[0]->is_valid()) {
            const IconManager::Icon &ic = *it->second[0];
            clicked = m_imgui->image_button((ImTextureID) (intptr_t) ic.tex_id, ImVec2(sz - 2.f * pad, sz - 2.f * pad), ic.tl, ic.br,
                                            int(pad));
        } else {
            clicked = ImGui::Button(label.ToUTF8().data(), ImVec2(0.f, sz));
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::PopID();
        if (!tip_text.empty() && ImGui::IsItemHovered())
            m_imgui->tooltip(tip_text, wrap_w);
        return clicked;
    };
    const auto vsep = [&](float h) {
        ImGui::SameLine(0.f, gap_s);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + h * 0.2f), ImVec2(p.x, p.y + h * 0.8f), col_sep);
        ImGui::Dummy(ImVec2(1.f, h));
        ImGui::SameLine(0.f, gap_s);
    };
    const auto heading = [&](const wxString &text) {
        m_imgui->push_bold_font();
        m_imgui->text(text);
        m_imgui->pop_bold_font();
    };
    const auto ellipsize = [](std::string s, float max_w) {
        if (ImGui::CalcTextSize(s.c_str()).x <= max_w)
            return s;
        while (!s.empty() && ImGui::CalcTextSize((s + "...").c_str()).x > max_w) {
            unsigned char c;
            do {
                c = static_cast<unsigned char>(s.back());
                s.pop_back();
            } while (!s.empty() && (c & 0xC0) == 0x80);
        }
        return s + "...";
    };
    const auto slider_label = [&](const wxString &label) {
        const float x0 = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        m_imgui->text(label);
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + label_w));
    };
    const auto float_row = [&](const char *id, const wxString &label, float *v, float v_min, float v_max, const char *format,
                               bool log, float right_inset) -> bool {
        slider_label(label);
        ImGui::SetNextItemWidth(-std::max(right_inset, 1.f));
        return ImGui::SliderFloat(id, v, v_min, v_max, format, ImGuiSliderFlags_AlwaysClamp | (log ? ImGuiSliderFlags_Logarithmic : 0));
    };
    const auto int_row = [&](const char *id, const wxString &label, int *v, int v_min, int v_max, const char *format,
                             float right_inset) -> bool {
        slider_label(label);
        ImGui::SetNextItemWidth(-std::max(right_inset, 1.f));
        return ImGui::SliderInt(id, v, v_min, v_max, format, ImGuiSliderFlags_AlwaysClamp);
    };
    const auto layer_name = [](const TextureDisplacementLayer &l) {
        return l.name.empty() ? Slic3r::format(_u8L("Layer %1%"), l.slot + 1) : l.name;
    };
    const auto slot_painted = [mo](int slot) {
        for (const ModelVolume *v : mo->volumes)
            if (v->is_model_part() && !v->texture_displacement_facet(slot).empty())
                return true;
        return false;
    };

    {
        const float x0 = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        heading(_L("Texture displacement"));

        const std::string labels[2] = { _u8L("Standard"), _u8L("Pro") };
        const float       seg_pad   = m_imgui->scaled(0.5f);
        const float seg_w[2] = { ImGui::CalcTextSize(labels[0].c_str()).x + 2.f * seg_pad, ImGui::CalcTextSize(labels[1].c_str()).x + 2.f * seg_pad };
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + panel_w - (seg_w[0] + seg_w[1] + gap_s + icon_sm)));

        const ImVec2 seg_min = ImGui::GetCursorScreenPos();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, style.ItemSpacing.y));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        m_imgui->disabled_begin(busy);
        for (int m = 0; m < 2; ++m) {
            if (m > 0)
                ImGui::SameLine();
            const bool on = m_panel_mode == m;
            ImGui::PushStyleColor(ImGuiCol_Button, on ? orca : ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? orca : ImVec4(orca.x, orca.y, orca.z, 0.25f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, orca);
            ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(1.f, 1.f, 1.f, 1.f) : style.Colors[ImGuiCol_TextDisabled]);
            const bool clicked = ImGui::Button((labels[m] + "##panel_mode").c_str(), ImVec2(seg_w[m], frame_h));
            ImGui::PopStyleColor(4);
            hover_tip(m == 0 ? _u8L("Standard - pick a texture, paint where it goes, press Bake. Everything the "
                                   "mesh needs is done for you in that one step.") :
                               _u8L("Pro - the mesh preparation is yours to run: Remesh, Subdivide and Bake as "
                                   "separate steps, with every setting on show. For when Standard's result is "
                                   "not what you wanted and you know why."));
            if (clicked && !on) {
                m_panel_mode = m;
                if (!pro_mode()) {
                    m_subdivide_editing      = false;
                    m_subdivide_preview_tris = -1;
                    m_subdivide_preview_glmodel.reset();
                    if (apply_standard_mode_presets(mv))
                        m_preview_params_dirty = true;
                }
                m_parent.set_as_dirty();
            }
        }
        m_imgui->disabled_end();
        ImGui::PopStyleVar(2);
        ImGui::GetWindowDrawList()->AddRect(seg_min, ImVec2(ImGui::GetItemRectMax().x, seg_min.y + frame_h),
                                            ImGui::GetColorU32(col_frame), style.FrameRounding);

        ImGui::SameLine(0.f, gap_s);
        if (icon_button(806, "canvas_drag.svg", icon_sm, m_undocked ? _L("Dock panel") : _L("Undock panel"),
                        _L("Detach this panel so it can be dragged anywhere over the 3D view, or dock it back beside "
                           "the toolbar.")))
            m_undocked = !m_undocked;
    }
    ImGui::Separator();

    const float bottom     = m_undocked ? ImGui::GetIO().DisplaySize.y : bottom_limit;
    const float max_body_h = std::max(m_imgui->scaled(8.f), bottom - ImGui::GetCursorScreenPos().y - m_panel_footer_h -
                                                                 style.WindowPadding.y - style.ItemSpacing.y);
    const float body_h     = m_panel_body_h > 0.f ? std::min(m_panel_body_h + 1.f, max_body_h) : max_body_h;

    const float  scrollbar_w = m_imgui->scaled(0.5f);
    const ImVec4 grab        = dark ? ImVec4(1.f, 1.f, 1.f, 0.26f) : ImVec4(0.f, 0.f, 0.f, 0.26f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, scrollbar_w);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.5f * scrollbar_w);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, grab);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(grab.x, grab.y, grab.z, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(grab.x, grab.y, grab.z, 0.65f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::BeginChild("##td_body", ImVec2(panel_w, body_h), false, ImGuiWindowFlags_NoScrollWithMouse);
    {
        ImGuiIO    &io         = ImGui::GetIO();
        const float scroll_now = ImGui::GetScrollY();
        const float scroll_max = ImGui::GetScrollMaxY();

        if (m_panel_scroll_applied < 0.f || std::abs(scroll_now - m_panel_scroll_applied) > 0.5f)
            m_panel_scroll_target = scroll_now;

        if (io.MouseWheel != 0.f && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
            m_panel_scroll_target -= io.MouseWheel * ImGui::GetFontSize() * 4.f;
        m_panel_scroll_target = std::floor(std::clamp(m_panel_scroll_target, 0.f, scroll_max));

        const float delta = m_panel_scroll_target - scroll_now;
        if (std::abs(delta) >= 1.f) {
            const float t    = 1.f - std::exp(-20.f * std::clamp(io.DeltaTime, 1.f / 240.f, 1.f / 15.f));
            const float step = (std::abs(delta * t) < 1.f) ? delta : delta * t;
            const float next = std::floor(scroll_now + step);
            ImGui::SetScrollY(next);
            m_panel_scroll_applied = next;
            m_parent.set_as_dirty();
        } else {
            m_panel_scroll_applied = scroll_now;
        }
    }

    int slot_to_remove = -1;
    int move_slot      = -1;
    int move_to        = -1;
    int activate_slot  = -1;

    TextureDisplacementLayer *active = mv != nullptr ? active_layer() : nullptr;

    {
        ImGui::AlignTextToFramePadding();
        heading(_L("Paint"));
        const std::string into      = active != nullptr ? Slic3r::format(_u8L("into %1%"), layer_name(*active)) :
                                                          _u8L("add a layer to paint");
        const float       toggles_x = ImGui::GetWindowContentRegionMax().x - (2.f * icon_sm + gap_s);
        const float       text_end  = toggles_x - (2.f * gap_s + 1.f);
        ImGui::SameLine();
        const std::string shown = ellipsize(into, std::max(0.f, text_end - ImGui::GetCursorPosX()));
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), text_end - ImGui::CalcTextSize(shown.c_str()).x));
        ImGui::TextDisabled("%s", shown.c_str());
        vsep(icon_sm);
        ImGui::SetCursorPosX(toggles_x);
        if (icon_toggle(820, "texture_displacement_add.svg", !m_erase_mode, icon_sm, _L("Paint"),
                        _L("Paint - add the active layer where you click or drag. The right button erases, and so does "
                           "holding Shift.")))
            m_erase_mode = false;
        ImGui::SameLine(0.f, gap_s);
        if (icon_toggle(821, "texture_displacement_add_negative.svg", m_erase_mode, icon_sm, _L("Erase"),
                        _L("Erase - remove the active layer where you click or drag. The right button paints.")))
            m_erase_mode = true;
    }

    {
        const bool is_brush_mode = m_tool_type == ToolType::BRUSH && m_cursor_type != TriangleSelector::CursorType::POINTER;
        const bool is_face_mode  = m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::POINTER;
        const bool is_area_mode  = m_tool_type == ToolType::SMART_FILL;
        const float row_y        = ImGui::GetCursorPosY();
        if (icon_toggle(801, "texture_displacement_brush.svg", is_brush_mode, icon_md, _L("Brush"),
                        _L("Brush - paint over the surface by dragging"))) {
            m_tool_type = ToolType::BRUSH;
            if (m_cursor_type == TriangleSelector::CursorType::POINTER)
                m_cursor_type = TriangleSelector::CursorType::CIRCLE;
        }
        ImGui::SameLine(0.f, gap_s);
        if (icon_toggle(802, "texture_displacement_face.svg", is_face_mode, icon_md, _L("Face"), _L("Face - click individual triangles"))) {
            m_tool_type   = ToolType::BRUSH;
            m_cursor_type = TriangleSelector::CursorType::POINTER;
        }
        ImGui::SameLine(0.f, gap_s);
        if (icon_toggle(803, "texture_displacement_connected_area.svg", is_area_mode, icon_md, _L("Connected area"),
                        _L("Connected area - flood-fill the region reachable without crossing an edge sharper than the "
                           "angle threshold"))) {
            m_tool_type   = ToolType::SMART_FILL;
            m_cursor_type = TriangleSelector::CursorType::POINTER;
        }

        const wxString whole_na = busy               ? _L("Wait for the bake to finish.") :
                                  active == nullptr  ? _L("Add a layer first.") :
                                                       wxString();
        const wxString erase_na = !whole_na.empty()                     ? whole_na :
                                  !slot_painted(m_active_layer_slot) ? _L("The active layer has no paint yet.") :
                                                                          wxString();
        ImGui::SameLine(0.f, gap_s);
        if (icon_toggle(806, "texture_displacement_select_all.svg", false, icon_md, _L("Select whole model"),
                        _L("Select whole model - paint every face of the model with the active layer"), whole_na))
            select_whole_model();
        ImGui::SameLine(0.f, gap_s);
        if (icon_toggle(807, "texture_displacement_erase_all.svg", false, icon_md, _L("Erase whole model"),
                        _L("Erase whole model - clear the active layer's paint from every face"), erase_na)) {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Reset texture displacement selection"),
                                          UndoRedo::SnapshotType::GizmoAction);
            int idx = -1;
            for (ModelVolume *v : mo->volumes)
                if (v->is_model_part()) {
                    ++idx;
                    m_triangle_selectors[idx]->reset();
                    m_triangle_selectors[idx]->request_update_render_data();
                }
            update_model_object();
            m_parent.set_as_dirty();
        }

        const float row_end       = ImGui::GetWindowContentRegionMax().x;
        const auto  centre_on_row = [&](float h) { ImGui::SetCursorPosY(row_y + std::round((icon_md - h) * 0.5f)); };
        vsep(icon_md);
        if (is_brush_mode) {
            ImGui::SetNextItemWidth(std::max(1.f, row_end - ImGui::GetCursorPosX() - (3.f * gap_s + 1.f + 2.f * icon_sm)));
            centre_on_row(frame_h);
            ImGui::SliderFloat("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f mm",
                               ImGuiSliderFlags_AlwaysClamp);
            hover_tip(m_desc.at("cursor_size"));
            vsep(icon_sm);
            const bool is_circle = m_cursor_type == TriangleSelector::CursorType::CIRCLE;
            centre_on_row(icon_sm);
            if (icon_toggle(804, "circle_paint.svg", is_circle, icon_sm, m_desc.at("circle"),
                            _L("Circle - paints everything under the brush as seen from the camera")))
                m_cursor_type = TriangleSelector::CursorType::CIRCLE;
            ImGui::SameLine(0.f, gap_s);
            centre_on_row(icon_sm);
            if (icon_toggle(805, "menu_obj_sphere.svg", !is_circle, icon_sm, m_desc.at("sphere"),
                            _L("Sphere - paints only within a ball around the point under the cursor")))
                m_cursor_type = TriangleSelector::CursorType::SPHERE;
        } else if (is_area_mode) {
            ImGui::SetNextItemWidth(std::max(1.f, row_end - ImGui::GetCursorPosX()));
            centre_on_row(frame_h);
            ImGui::SliderFloat("##smart_fill_angle", &m_smart_fill_angle, SmartFillAngleMin, SmartFillAngleMax, "%.0f°",
                               ImGuiSliderFlags_AlwaysClamp);
            hover_tip(_u8L("Angle threshold - the fill stops at edges sharper than this"));
        } else {
            centre_on_row(frame_h);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", ellipsize(_u8L("Click a triangle to paint it"),
                                                std::max(0.f, row_end - ImGui::GetCursorPosX())).c_str());
        }
    }

    {
        const int cur_mode = m_use_shaded_preview                               ? 1 :
                             m_uv_check_mode == UVCheckMode::Checker    ? 2 :
                             m_uv_check_mode == UVCheckMode::Distortion ? 3 : 0;
        int  new_mode       = cur_mode;
        bool wf_toggle      = false;
        bool open_uv_editor = false;
        const wxString uv_view_na = active == nullptr ? _L("Add a layer first.") :
                                    active->projection_method != TextureProjectionMethod::LSCM ?
                                                        _L("Only for a layer mapped with Unwrap (LSCM) - set the "
                                                           "active layer's Mapping to Unwrap to use this view.") :
                                                        wxString();
        if ((cur_mode == 2 || cur_mode == 3) && !uv_view_na.empty())
            new_mode = 0;

        const float x0 = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", _u8L("View").c_str());
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + m_imgui->scaled(2.6f)));
        if (icon_toggle(701, "texture_displacement_real_preview.svg", cur_mode == 0, icon_md, _L("Normal"),
                        _L("Normal - the real geometry, exactly what Bake will produce. Slower to update")))
            new_mode = 0;
        ImGui::SameLine(0.f, gap_s);
        if (icon_toggle(702, "texture_displacement_fast_preview.svg", cur_mode == 1, icon_md, _L("Fast"),
                        _L("Fast - the relief is only shaded on, not built, and only for the layer you are "
                           "editing. Updates instantly while you paint")))
            new_mode = 1;
        ImGui::SameLine(0.f, gap_s);
        if (icon_toggle(703, "texture_displacement_checker.svg", cur_mode == 2, icon_md, _L("Checker"),
                        _L("Checker - a test grid instead of the texture. Where the squares stay square the "
                           "texture is undistorted; where they stretch, it will too. Opens the UV editor if "
                           "it is closed"),
                        uv_view_na)) {
            new_mode       = 2;
            open_uv_editor = true;
        }
        ImGui::SameLine(0.f, gap_s);
        if (icon_toggle(704, "texture_displacement_distortion.svg", cur_mode == 3, icon_md, _L("Distortion"),
                        _L("Distortion - blue-to-red stretch heatmap over the unwrap. Opens the UV editor if "
                           "it is closed"),
                        uv_view_na)) {
            new_mode       = 3;
            open_uv_editor = true;
        }
        vsep(icon_md);
        if (icon_toggle(705, "texture_displacement_wireframe.svg", m_wireframe_overlay, icon_md, _L("Wireframe"),
                        _L("Wireframe - overlay the mesh edges; independent of the view above")))
            wf_toggle = true;

        const std::string auto_label = _u8L("Auto");
        const float       auto_w     = frame_h * 0.78f  + style.ItemInnerSpacing.x + ImGui::CalcTextSize(auto_label.c_str()).x;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - auto_w));
        if (m_imgui->bbl_checkbox(wxString::FromUTF8(auto_label) + "##auto_update", m_auto_update) && m_auto_update)
            rebuild_preview();
        hover_tip(_u8L("Rebuilds the preview as soon as anything changes. Turn it off on a heavy model if painting "
                        "or dragging a slider starts to stutter - the preview then waits until you let go."));

        if (open_uv_editor && !m_show_uv_editor) {
            m_show_uv_editor = true;
            if (new_mode == cur_mode)
                update_uv_editor();
        }
        if (new_mode != cur_mode)
            apply_view_mode(new_mode);
        if (wf_toggle) {
            m_wireframe_overlay = !m_wireframe_overlay;
            refresh_wireframe();
            m_parent.set_as_dirty();
        }
    }

    ImGui::Separator();
    {
        ImGui::AlignTextToFramePadding();
        heading(_L("Texture layers"));
        if (mv != nullptr) {
            const std::string count = std::to_string(mv->texture_displacement_layers.size()) + " / " +
                                      std::to_string(TEXTURE_DISPLACEMENT_MAX_LAYERS);
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(count.c_str()).x));
            ImGui::TextDisabled("%s", count.c_str());
        }
    }

    std::vector<TextureDisplacementLayer *> ordered;
    if (mv != nullptr) {
        for (TextureDisplacementLayer &l : mv->texture_displacement_layers)
            ordered.push_back(&l);
        std::sort(ordered.begin(), ordered.end(), [](const auto *a, const auto *b) { return a->slot < b->slot; });
    }

    ImDrawList *dl         = ImGui::GetWindowDrawList();
    const float content_rx = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const float rounding   = style.FrameRounding + 1.f;

    const auto open_picker = [&](int slot) {
        m_picker_slot         = slot;
        m_picker_open_request = true;
    };
    const auto draw_drag_icon = [&](ImDrawList *list, float x, float y, float w, float h, ImU32 tint) {
        const auto it = m_panel_icon_map.find("texture_displacement_drag.svg");
        if (it != m_panel_icon_map.end() && !it->second.empty() && it->second[0]->is_valid()) {
            const IconManager::Icon &ic = *it->second[0];
            const float              cx = x + 0.5f * w;
            list->AddImage((ImTextureID) (intptr_t) ic.tex_id, ImVec2(cx - 0.5f * h, y), ImVec2(cx + 0.5f * h, y + h), ic.tl, ic.br, tint);
        } else {
            const float r = std::max(1.f, std::round(m_imgui->scaled(0.08f)));
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 2; ++col)
                    list->AddCircleFilled(ImVec2(x + w * (0.3f + 0.4f * col), y + h * 0.5f + float(row - 1) * 4.f * r), r, tint);
        }
    };
    const auto drag_tint = [dark](int alpha) { return dark ? IM_COL32(255, 255, 255, alpha) : IM_COL32(90, 90, 90, alpha); };

    const float grip_w = std::round(m_imgui->scaled(0.7f));
    const auto grip = [&](const TextureDisplacementLayer &l, float h) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##grip", ImVec2(grip_w, h));
        const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        draw_drag_icon(dl, p.x, p.y, grip_w, h, drag_tint(hot ? 255 : 150));
        hover_tip(_u8L("Drag onto another layer to reorder. The first layer sits on the bare surface; each one after "
                        "it adds its relief on top of the ones before."));
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
            const int slot = l.slot;
            ImGui::SetDragDropPayload("TD_LAYER", &slot, sizeof(slot));
            ImGui::EndDragDropSource();
        }
    };

    int dragged_slot = -1;
    if (const ImGuiPayload *payload = ImGui::GetDragDropPayload(); payload != nullptr && payload->IsDataType("TD_LAYER"))
        dragged_slot = *static_cast<const int *>(payload->Data);
    int dragged_index = -1;
    for (size_t i = 0; i < ordered.size(); ++i)
        if (ordered[i]->slot == dragged_slot)
            dragged_index = int(i);
    const ImU32 ghost_col = ImGui::GetColorU32(ImGuiCol_WindowBg, 0.65f);

    const auto drop_target = [&](size_t index, int slot, const ImVec2 &mn, const ImVec2 &mx) {
        if (!ImGui::BeginDragDropTarget())
            return;
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("TD_LAYER", ImGuiDragDropFlags_AcceptBeforeDelivery |
                                                                                 ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            const int  from  = *static_cast<const int *>(payload->Data);
            const bool after = dragged_index >= 0 && dragged_index < int(index);
            if (from != slot) {
                const float y = after ? mx.y + 2.f : mn.y - 2.f;
                dl->AddLine(ImVec2(mn.x, y), ImVec2(mx.x, y), ImGui::GetColorU32(orca), 2.f);
            }
            if (payload->IsDelivery()) {
                move_slot = from;
                move_to   = int(index) + (after ? 1 : 0);
            }
        }
        ImGui::EndDragDropTarget();
    };

    for (size_t li = 0; li < ordered.size(); ++li) {
        TextureDisplacementLayer &layer     = *ordered[li];
        const bool                is_active = layer.slot == m_active_layer_slot;
        const bool                painted   = slot_painted(layer.slot);
        ImGui::PushID(layer.slot);

        if (is_active) {
            const ImVec2 card_min = ImGui::GetCursorScreenPos();
            dl->ChannelsSplit(2);
            dl->ChannelsSetCurrent(1);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + card_pad);
            ImGui::Indent(card_pad);
            ImGui::BeginGroup();

            {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap_s, style.ItemSpacing.y));
                grip(layer, frame_h);
                ImGui::SameLine();
                if (GLTexture *thumb = get_layer_thumbnail(layer)) {
                    if (ImGui::ImageButton((ImTextureID) (intptr_t) thumb->get_id(), ImVec2(frame_h, frame_h), ImVec2(0.f, 0.f),
                                           ImVec2(1.f, 1.f), 0))
                        open_picker(layer.slot);
                } else if (ImGui::Button("?##thumb", ImVec2(frame_h, frame_h))) {
                    open_picker(layer.slot);
                }
                hover_tip(_u8L("Pick a different image for this layer. The paint, depth and tiling stay as they are."));
                {
                    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                    dl->AddRect(ImVec2(a.x - 2.f, a.y - 2.f), ImVec2(b.x + 2.f, b.y + 2.f), ImGui::GetColorU32(orca), style.FrameRounding);
                }
                ImGui::SameLine();

                const std::string not_painted = _u8L("not painted");
                const float right_w = (painted ? 0.f : ImGui::CalcTextSize(not_painted.c_str()).x + gap_s) + 3.f * frame_h + 2.f * gap_s;
                const float name_w  = std::max(frame_h, ImGui::GetWindowContentRegionMax().x - card_pad - ImGui::GetCursorPosX() - gap_s - right_w);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(orca.x, orca.y, orca.z, 0.2f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(orca.x, orca.y, orca.z, 0.35f));
                ImGui::AlignTextToFramePadding();
                const std::string shown = ellipsize(layer_name(layer), name_w - 2.f * style.FramePadding.x);
                if (ImGui::Selectable((shown + "##name").c_str(), false, 0, ImVec2(name_w, frame_h)))
                    open_picker(layer.slot);
                ImGui::PopStyleColor(2);
                hover_tip(_u8L("Pick a different image for this layer. The paint, depth and tiling stay as they are."));
                if (!painted) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", not_painted.c_str());
                }
                ImGui::SameLine();
                m_imgui->disabled_begin(busy || li == 0);
                if (icon_button(611, "texture_displacement_move_up.svg", frame_h, _L("Move up"), _L("Move up - applied earlier"))) {
                    move_slot = layer.slot;
                    move_to   = int(li) - 1;
                }
                m_imgui->disabled_end();
                ImGui::SameLine();
                m_imgui->disabled_begin(busy || li + 1 >= ordered.size());
                if (icon_button(612, "texture_displacement_move_down.svg", frame_h, _L("Move down"), _L("Move down - applied later"))) {
                    move_slot = layer.slot;
                    move_to   = int(li) + 2;
                }
                m_imgui->disabled_end();
                ImGui::SameLine();
                m_imgui->disabled_begin(busy);
                if (icon_button(600 + layer.slot, "texture_displacement_cross.svg", frame_h, m_desc.at("remove_layer"),
                                _L("Remove this layer")))
                    slot_to_remove = layer.slot;
                m_imgui->disabled_end();
                ImGui::PopStyleVar();
            }

            m_preview_params_dirty |= float_row("##depth", _L("Depth"), &layer.depth_mm, 0.01f, 10.f, "%.3f mm", true, card_pad);
            hover_tip(_u8L("Height of the relief: how far white in the texture lifts the surface, in "
                           "millimetres. Black does not move it at all, unless Midlevel says otherwise."));
            m_preview_params_dirty |= float_row("##tile_size", _L("Tile size"), &layer.tiling_scale, 0.2f, 200.f, "%.2f mm", true, card_pad);
            hover_tip(_u8L("How wide one copy of the texture is on the model. Smaller repeats the pattern "
                           "more often and makes its detail finer; with Tile off, this is the size of the "
                           "single copy."));
            m_preview_params_dirty |= float_row("##rotation", _L("Rotation"), &layer.rotation_deg, 0.f, 360.f, "%.0f°", false, card_pad);
            hover_tip(_u8L("Turns the texture on the surface, in degrees - for lining a pattern up with an "
                           "edge of the model."));

            if (m_layer_expanded[size_t(layer.slot)]) {
                {
                    const ImVec2 p    = ImGui::GetCursorScreenPos();
                    const float  x1   = content_rx - card_pad;
                    const float  dash = std::round(m_imgui->scaled(0.25f));
                    for (float xx = p.x; xx < x1; xx += 2.f * dash)
                        dl->AddLine(ImVec2(xx, p.y), ImVec2(std::min(xx + dash, x1), p.y), col_line);
                    ImGui::Dummy(ImVec2(1.f, 1.f));
                }

                m_preview_params_dirty |= float_row("##midlevel", _L("Midlevel"), &layer.midlevel, 0.f, 10.f, "%.2f", false, card_pad);
                hover_tip(_u8L("Which gray stays where the surface already is. At 0 the texture only pushes "
                               "outwards; at 0.5 mid-gray stays put, so darker grays cut in and lighter ones "
                               "still push out - one image both embosses and engraves.\n\n"
                               "What cuts in has to fit: inside a sharp corner or through a thin wall, a deep "
                               "cut can pass through the other side."));
                if (layer.midlevel > 0.f && layer.depth_mm > 1.f) {
                    ImGui::PushTextWrapPos(content_rx - card_pad);
                    m_imgui->warning_text(_L("Deep inward displacement may self-intersect."));
                    ImGui::PopTextWrapPos();
                }

                m_preview_params_dirty |= float_row("##smoothing", _L("Smoothing"), &layer.smoothing, 0.f, 1.f, "%.2f", false, card_pad);
                hover_tip(_u8L("Blurs the image before it is used, which rounds off hard steps and removes "
                               "speckle from a noisy photo. Raise it if the relief looks harsh or grainy; it "
                               "costs fine detail."));

                m_preview_params_dirty |= m_imgui->bbl_checkbox(_L("Edge fade") + "##edge_smoothing", layer.edge_smoothing);
                hover_tip(_u8L("Flattens the relief as it approaches the edge of the painted area, so it "
                               "blends into the bare surface instead of stopping at a step."));
                ImGui::SameLine();
                m_imgui->disabled_begin(!layer.edge_smoothing);
                ImGui::SetNextItemWidth(-card_pad);
                m_preview_params_dirty |= ImGui::SliderFloat("##edge_amount", &layer.edge_smoothing_amount, 0.02f, 1.f, "%.2f",
                                                             ImGuiSliderFlags_AlwaysClamp);
                m_imgui->disabled_end();
                hover_tip(_u8L("How far in the fade reaches, as a share of the painted area. Small values "
                               "soften a narrow band at the edge; 1 flattens almost all of it."));

                {
                    const float x0 = ImGui::GetCursorPosX();
                    m_preview_params_dirty |= m_imgui->bbl_checkbox(_L("Invert"), layer.invert);
                    hover_tip(_u8L("Turns the relief inside out: what stood out is cut in, and the other way "
                                   "round. The same as using a negative of the image."));
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + 0.5f * (ImGui::GetWindowContentRegionMax().x - card_pad - x0)));

                    const bool has_color     = decode_height_texture(layer).has_color();
                    bool       color_enabled = layer.color_enabled && has_color;
                    m_imgui->disabled_begin(!has_color);
                    if (m_imgui->bbl_checkbox(_L("Colors"), color_enabled)) {
                        layer.color_enabled    = color_enabled;
                        m_preview_params_dirty = true;
                    }
                    m_imgui->disabled_end();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        m_imgui->tooltip(has_color ?
                                             _u8L("Prints the painted area in the texture's colors as well as its "
                                                  "relief. Each color is matched to the nearest of your loaded "
                                                  "filaments; anything you did not paint keeps the object's own.") :
                                             _u8L("This texture is a grayscale height map, so it has no colors to "
                                                  "apply. Import a color image to use this."),
                                         wrap_w);

                    if (color_enabled) {
                        TextureDisplacementOptions &opts = mv->texture_displacement_options;
                        if (m_imgui->bbl_checkbox(_L("Mix filaments"), opts.color_mix_enabled))
                            m_preview_params_dirty = true;
                        hover_tip(_u8L("Interleaves two filaments to fake the colors in between, so a handful "
                                       "of filaments can cover a photo or a gradient. An image of flat colors "
                                       "prints the same either way. Off uses one filament per area."));
                        if (opts.color_mix_enabled) {
                            cached_palette();
                            const int max_mixes = std::max(1, int(EnforcerBlockerType::ExtruderMax) - int(m_palette_filaments.size()));
                            if (int_row("##color_mix_count", _L("Mixed colors"), &opts.color_mix_count, 1, max_mixes, "%d", card_pad))
                                m_preview_params_dirty = true;
                            hover_tip(_u8L("The most mixed filaments a bake may add. They are picked from the "
                                           "texture's colors, and only the ones the bake actually uses are created."));
                            ImGui::TextDisabled("%s", Slic3r::format(_u8L("%1% printable colors from %2% filaments"),
                                                                     int(cached_palette().size()), int(m_palette_filaments.size())).c_str());
                        }
                        if (int_row("##color_despeckle", _L("Denoise"), &opts.color_despeckle, 0, 6, "%d", card_pad))
                            m_preview_params_dirty = true;
                        hover_tip(_u8L("Cleans up single stray triangles of the wrong color, which detail finer "
                                       "than the mesh leaves behind. Raise it if the result looks speckled, "
                                       "lower it if small features are being swallowed."));
                    }
                }

                {
                    const float x0 = ImGui::GetCursorPosX();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", _u8L_CONTEXT("Mapping", "Texture Displacement").c_str());
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + label_w));
                    struct MappingIcon
                    {
                        const char *file;
                        wxString    label;
                        wxString    tip;
                    };
                    const MappingIcon mappings[] = {
                        { "menu_obj_cube.svg", _L("Triplanar (blended)"),
                          _L("Triplanar - projects the texture from all three axes at once and blends between them, so a "
                             "patch wrapping around a sharp edge has no seam.") },
                        { "menu_obj_cylinder.svg", _L("Cylindrical"),
                          _L("Cylindrical - wraps the texture around the painted area's own center, for round shapes.") },
                        { "menu_obj_sphere.svg", _L("Spherical"),
                          _L("Spherical - wraps the texture around the painted area's own center in both directions.") },
                        { "texture_displacement_map_unwrap.svg", _L("Unwrap (LSCM)"),
                          _L("Unwrap - flattens the painted area and maps the texture onto it with as little stretching as "
                             "possible. The area is cut into pieces at its sharp edges first (see Seam angle), so each "
                             "piece can lie flat on its own.") },
                        { "texture_displacement_map_view.svg", _L("From view"),
                          _L("From view - projects straight onto the painted area from where you are looking, like a slide "
                             "projector.") },
                    };
                    for (int mi = 0; mi < int(IM_ARRAYSIZE(mappings)); ++mi) {
                        if (mi > 0)
                            ImGui::SameLine(0.f, gap_s);
                        const auto method = static_cast<TextureProjectionMethod>(mi);
                        if (icon_toggle(830 + mi, mappings[mi].file, layer.projection_method == method, icon_sm, mappings[mi].label,
                                        mappings[mi].tip) &&
                            layer.projection_method != method) {
                            if (method == TextureProjectionMethod::ViewProjected)
                                capture_view_projection(layer);
                            layer.projection_method = method;
                            m_preview_params_dirty  = true;
                            if (method == TextureProjectionMethod::LSCM)
                                m_show_uv_editor = true;
                        }
                    }
                }

                if (layer.projection_method == TextureProjectionMethod::LSCM) {
                    if (m_imgui->button(_u8L("Open UV editor"))) {
                        m_show_uv_editor = true;
                        update_uv_editor();
                    }
                    hover_tip(_u8L("Opens the UV editor, where you can see how the texture is laid out over the "
                                    "painted area, cut seams and move the pieces around."));
                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    if (m_uv_editor_unwrap.empty())
                        ImGui::TextDisabled("%s", _u8L("Not unwrapped yet").c_str());
                    else
                        ImGui::TextDisabled("%s", Slic3r::format(_u8L("%1% islands"), m_uv_editor_unwrap.chart_count).c_str());
                }

                if (layer.projection_method == TextureProjectionMethod::ViewProjected) {
                    if (m_imgui->button(_u8L("Capture current view"))) {
                        capture_view_projection(layer);
                        if (m_project_only_visible && select_visible_faces() == 0)
                            show_error(nullptr, _u8L("Nothing is visible from this angle - turn the model to face the "
                                                     "part you want to project onto."));
                        m_preview_params_dirty = true;
                        update_projector();
                    }
                    hover_tip(_u8L("Projects the texture onto the painted area from where you are looking now, like "
                                    "a slide projector. Faces turned away from you stretch, so line the view up with "
                                    "the surface you care about first."));

                    if (m_imgui->bbl_checkbox(_L("Project only on visible"), m_project_only_visible)) {
                        if (m_project_only_visible && select_visible_faces() == 0)
                            show_error(nullptr, _u8L("Nothing is visible from this angle - turn the model to face the "
                                                     "part you want to project onto."));
                        m_preview_params_dirty = true;
                        update_projector();
                    }
                    hover_tip(_u8L("Paints only the faces you can actually see right now - facing you and not hidden "
                                    "behind anything - and projects onto those. Replaces what the layer had painted."));

                    bool projector_open = m_projector_frame != nullptr && m_projector_frame->IsShown();
                    if (m_imgui->bbl_checkbox(_L("Projection frame"), projector_open))
                        show_projector(projector_open);
                    hover_tip(_u8L("Opens a window you drag over the model. Whatever you can see through it is what "
                                    "gets the texture, and its border becomes the edge of the projection."));

                    if (projector_open) {
                        if (int_row("##projector_opacity", _L("Opacity"), &m_projector_opacity, 20, 255, "%d", card_pad))
                            m_projector_frame->set_opacity(m_projector_opacity);
                        hover_tip(_u8L("How solid the frame window looks while you place it. Lower to see the "
                                       "model through it; it does not affect the result."));

                        if (m_imgui->button(_u8L("Apply projection frame"))) {
                            const int painted_count = apply_projection_frame();
                            if (painted_count == 0)
                                show_error(nullptr, _u8L("Nothing of the model is inside the frame - move it over the "
                                                         "part you want to project onto."));
                            else if (painted_count < 0)
                                show_error(nullptr, _u8L("The frame could not be applied. Make sure it overlaps the 3D view."));
                        }
                        hover_tip(_u8L("Projects the texture through the frame from where you are looking now and "
                                        "paints the faces inside it. Replaces what the layer had painted; the result "
                                        "sticks to the model, so you can orbit afterwards."));
                    }

                    if (layer.view_project_projective) {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("%s", _u8L("Placed by projection frame.").c_str());
                        ImGui::SameLine();
                        if (m_imgui->button(_u8L("Clear"))) {
                            layer.view_project_projective = false;
                            layer.tile_enabled            = true;
                            m_preview_params_dirty        = true;
                        }
                    }
                }

                {
                    const float x0 = ImGui::GetCursorPosX();
                    m_preview_params_dirty |= m_imgui->bbl_checkbox(_L("Tile") + "##tile_enabled", layer.tile_enabled);
                    hover_tip(_u8L("Repeats the texture across the painted area. Off places one copy, like a decal, "
                                    "at the size set by Tile size."));
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + label_w));
                    const wxString tile_na = layer.tile_enabled ? wxString() : _L("Turn Tile on to choose how the texture repeats.");
                    if (icon_toggle(840, "texture_displacement_tile_repeat.svg", layer.tile_method == static_cast<TextureTileMethod>(0),
                                    icon_sm, _L("Repeat"), _L("Repeat"), tile_na)) {
                        layer.tile_method      = static_cast<TextureTileMethod>(0);
                        m_preview_params_dirty = true;
                    }
                    ImGui::SameLine(0.f, gap_s);
                    if (icon_toggle(841, "menu_mirror_x.svg", layer.tile_method == static_cast<TextureTileMethod>(1), icon_sm,
                                    _L("Mirrored repeat"), _L("Mirrored repeat - every other tile is flipped, so edges meet seamlessly"),
                                    tile_na)) {
                        layer.tile_method      = static_cast<TextureTileMethod>(1);
                        m_preview_params_dirty = true;
                    }
                }

                {
                    const float x0 = ImGui::GetCursorPosX();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", _u8L("Blend").c_str());
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + label_w));
                    const std::string blend_add      = _u8L_CONTEXT("Add", "Texture Displacement");
                    const std::string blend_subtract = _u8L_CONTEXT("Subtract", "Texture Displacement");
                    const std::string blend_multiply = _u8L_CONTEXT("Multiply", "Texture Displacement");
                    const std::string blend_divide   = _u8L("Divide");
                    const char *blend_items[] = { blend_add.c_str(), blend_subtract.c_str(), blend_multiply.c_str(), blend_divide.c_str() };
                    const bool base_layer = li == 0;
                    int        blend_mode = base_layer ? 0 : static_cast<int>(layer.blend_mode);
                    ImGui::SetNextItemWidth(-(card_pad + gap_s + icon_sm));
                    m_imgui->disabled_begin(base_layer);
                    if (scoped_combo("##blend_mode", &blend_mode, blend_items, IM_ARRAYSIZE(blend_items)) && !base_layer) {
                        layer.blend_mode       = static_cast<TextureBlendMode>(blend_mode);
                        m_preview_params_dirty = true;
                    }
                    m_imgui->disabled_end();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        m_imgui->tooltip(base_layer ?
                                             _u8L("The base layer has nothing beneath it to combine with, so it always adds.") :
                                             _u8L("What this layer does where it overlaps the ones below. Add and "
                                                  "Subtract pile relief on or carve it away. Multiply and Divide scale "
                                                  "what is underneath, which turns this layer into a mask over it - "
                                                  "there, Depth acts as a strength, and 1 mm leaves white areas "
                                                  "untouched."),
                                         wrap_w);

                    ImGui::SameLine(0.f, gap_s);
                    if (icon_toggle(842, "texture_displacement_adjust.svg", m_adjust_texture_mode, icon_sm, _L("Adjust placement"),
                                    _L("Adjust placement - drag the handle on the model to move the texture"))) {
                        if (!m_adjust_texture_mode) {
                            update_model_object();
                            if (update_adjust_anchor())
                                m_adjust_texture_mode = true;
                            else
                                show_error(nullptr, _u8L("Paint something with this layer first."));
                        } else {
                            m_adjust_texture_mode = false;
                            m_adjust_drag_handle  = AdjustHandle::None;
                        }
                    }
                }
            }

            {
                const bool        expanded = m_layer_expanded[size_t(layer.slot)];
                const std::string text     = expanded ? _u8L("Fewer settings") : _u8L("More settings");
                const float       arrow    = std::round(ImGui::GetFontSize() * 0.55f);
                const ImVec2      ts       = ImGui::CalcTextSize(text.c_str());
                const ImVec2      p        = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##more", ImVec2(arrow + gap_s + ts.x, ts.y))) {
                    m_layer_expanded[size_t(layer.slot)] = !expanded;
                    if (expanded && m_adjust_texture_mode) {
                        m_adjust_texture_mode = false;
                        m_adjust_drag_handle  = AdjustHandle::None;
                    }
                }
                const ImU32 c  = ImGui::GetColorU32(col_link);
                const float cx = p.x + 0.5f * arrow, cy = p.y + 0.5f * ts.y, hs = 0.3f * arrow;
                if (expanded)
                    dl->AddTriangleFilled(ImVec2(cx - hs, cy + 0.5f * hs), ImVec2(cx + hs, cy + 0.5f * hs), ImVec2(cx, cy - 0.5f * hs), c);
                else
                    dl->AddTriangleFilled(ImVec2(cx - hs, cy - 0.5f * hs), ImVec2(cx + hs, cy - 0.5f * hs), ImVec2(cx, cy + 0.5f * hs), c);
                const float tx = p.x + arrow + gap_s;
                dl->AddText(ImVec2(tx, p.y), c, text.c_str());
                if (ImGui::IsItemHovered())
                    dl->AddLine(ImVec2(tx, p.y + ts.y), ImVec2(tx + ts.x, p.y + ts.y), c);
            }

            ImGui::EndGroup();
            ImGui::Unindent(card_pad);
            const float card_max_y = ImGui::GetItemRectMax().y + card_pad;
            drop_target(li, layer.slot, card_min, ImVec2(content_rx, card_max_y));
            if (layer.slot == dragged_slot)
                dl->AddRectFilled(card_min, ImVec2(content_rx, card_max_y), ghost_col, rounding);
            dl->ChannelsSetCurrent(0);
            dl->AddRectFilled(card_min, ImVec2(content_rx, card_max_y), col_card, rounding);
            dl->ChannelsMerge();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + card_pad);
        } else {
            const float  inner = std::round(m_imgui->scaled(0.15f));
            const float  row_h = frame_h + 2.f * inner;
            const ImVec2 p0    = ImGui::GetCursorScreenPos();
            const float  row_w = content_rx - p0.x;
            if (ImGui::InvisibleButton("##row", ImVec2(row_w, row_h)))
                activate_slot = layer.slot;
            const bool row_hovered = ImGui::IsItemHovered();
            ImGui::SetItemAllowOverlap();
            drop_target(li, layer.slot, p0, ImVec2(p0.x + row_w, p0.y + row_h));
            if (row_hovered)
                dl->AddRectFilled(p0, ImVec2(p0.x + row_w, p0.y + row_h), col_card, rounding);
            dl->AddRect(p0, ImVec2(p0.x + row_w, p0.y + row_h), col_line, rounding);

            ImGui::SetCursorScreenPos(ImVec2(p0.x + inner, p0.y + inner));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap_s, style.ItemSpacing.y));
            grip(layer, frame_h);
            ImGui::SameLine();
            {
                const ImVec2 c  = ImGui::GetCursorScreenPos();
                const float  aw = std::round(ImGui::GetFontSize() * 0.5f), hs = 0.3f * aw;
                const float  cx = c.x + 0.5f * aw, cy = c.y + 0.5f * frame_h;
                dl->AddTriangleFilled(ImVec2(cx - 0.5f * hs, cy - hs), ImVec2(cx - 0.5f * hs, cy + hs), ImVec2(cx + 0.5f * hs, cy),
                                      ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::Dummy(ImVec2(aw, frame_h));
            }
            ImGui::SameLine();
            const float th = std::round(frame_h * 0.9f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 0.5f * (frame_h - th));
            if (GLTexture *thumb = get_layer_thumbnail(layer)) {
                if (ImGui::ImageButton((ImTextureID) (intptr_t) thumb->get_id(), ImVec2(th, th), ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), 0))
                    open_picker(layer.slot);
            } else if (ImGui::Button("?##thumb", ImVec2(th, th))) {
                open_picker(layer.slot);
            }
            hover_tip(_u8L("Pick a different image for this layer. The paint, depth and tiling stay as they are."));
            ImGui::SameLine();

            char depth_text[32];
            std::snprintf(depth_text, sizeof(depth_text), "%.3f mm", layer.depth_mm);
            const std::string summary    = painted ? std::string(depth_text) : _u8L("not painted");
            const float       sum_w      = ImGui::CalcTextSize(summary.c_str()).x;
            const float       right_edge = ImGui::GetWindowContentRegionMax().x - inner;
            const float       name_w     = std::max(0.f, right_edge - ImGui::GetCursorPosX() - 2.f * gap_s - sum_w - frame_h);
            ImGui::SetCursorPosY(p0.y - ImGui::GetWindowPos().y + ImGui::GetScrollY() + inner);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(ellipsize(layer_name(layer), name_w).c_str());
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right_edge - frame_h - gap_s - sum_w));
            ImGui::TextDisabled("%s", summary.c_str());
            ImGui::SameLine();
            ImGui::SetCursorPosX(right_edge - frame_h);
            m_imgui->disabled_begin(busy);
            if (icon_button(600 + layer.slot, "texture_displacement_cross.svg", frame_h, m_desc.at("remove_layer"), _L("Remove this layer")))
                slot_to_remove = layer.slot;
            m_imgui->disabled_end();
            ImGui::PopStyleVar();
            if (layer.slot == dragged_slot)
                dl->AddRectFilled(p0, ImVec2(p0.x + row_w, p0.y + row_h), ghost_col, rounding);
            ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + row_h + 0.5f * style.ItemSpacing.y));
        }

        ImGui::PopID();
    }

    if (dragged_slot >= 0) {
        const auto it = std::find_if(ordered.begin(), ordered.end(), [dragged_slot](const auto *l) { return l->slot == dragged_slot; });
        if (it != ordered.end()) {
            const TextureDisplacementLayer &l      = **it;
            ImDrawList                     *fg     = ImGui::GetForegroundDrawList();
            const float                     inner  = std::round(m_imgui->scaled(0.15f));
            const float                     row_h  = frame_h + 2.f * inner;
            const float                     list_x = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x;
            const ImVec2                    mouse  = ImGui::GetIO().MousePos;
            const ImVec2                    mn(mouse.x - inner - 0.5f * grip_w, mouse.y - 0.5f * row_h);
            const ImVec2                    mx(mn.x + (content_rx - list_x), mn.y + row_h);
            fg->AddRectFilled(ImVec2(mn.x + 2.f, mn.y + 4.f), ImVec2(mx.x + 2.f, mx.y + 4.f), IM_COL32(0, 0, 0, 70), rounding);
            fg->AddRectFilled(mn, mx, ImGui::GetColorU32(ImGuiCol_WindowBg, 245.f / 255.f), rounding);
            fg->AddRect(mn, mx, ImGui::GetColorU32(orca), rounding, 0, 1.5f);

            float x = mn.x + inner;
            draw_drag_icon(fg, x, mn.y + inner, grip_w, frame_h, drag_tint(255));
            x += grip_w + gap_s;
            const float th = std::round(frame_h * 0.9f);
            if (GLTexture *thumb = get_layer_thumbnail(l)) {
                const float ty = mn.y + 0.5f * (row_h - th);
                fg->AddImage((ImTextureID) (intptr_t) thumb->get_id(), ImVec2(x, ty), ImVec2(x + th, ty + th));
            }
            x += th + gap_s;

            char depth_text[32];
            std::snprintf(depth_text, sizeof(depth_text), "%.3f mm", l.depth_mm);
            const std::string summary = slot_painted(l.slot) ? std::string(depth_text) : _u8L("not painted");
            const float       sum_w   = ImGui::CalcTextSize(summary.c_str()).x;
            const float       text_y  = mn.y + 0.5f * (row_h - ImGui::GetFontSize());
            fg->AddText(ImVec2(mx.x - inner - sum_w, text_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), summary.c_str());
            const std::string name = ellipsize(layer_name(l), std::max(0.f, mx.x - inner - sum_w - gap_s - x));
            fg->AddText(ImVec2(x, text_y), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
        }
    }

    if (mv != nullptr) {
        const bool        full  = mv->texture_displacement_layers.size() >= TEXTURE_DISPLACEMENT_MAX_LAYERS;
        const std::string label = full ? Slic3r::format(_u8L("All %1% layers used"), TEXTURE_DISPLACEMENT_MAX_LAYERS) :
                                         "+  " + _u8L("Add layer");
        m_imgui->disabled_begin(busy || full);
        if (ImGui::Button((label + "##add_layer").c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0.f)))
            add_texture_layer();
        m_imgui->disabled_end();
    }

    if (pro_mode()) {
        ImGui::Separator();
        heading(_L("Subdivision"));

        if (m_imgui->bbl_checkbox(_L("Only painted area (adaptive)"), m_subdivide_adaptive)) {
            if (m_subdivide_editing)
                rebuild_subdivide_preview();
            m_parent.set_as_dirty();
        }
        hover_tip(_u8L("Adds triangles only where you painted, instead of everywhere. On a large part with a small "
                        "decal that is the difference between thousands of triangles and millions, and your paint "
                        "survives the refinement."));

        if (m_subdivide_adaptive) {
            if (m_subdivide_target_mm <= 0.f && mv != nullptr) {
                const indexed_triangle_set &its = mv->mesh().its;
                double                      sum = 0.0;
                size_t                      cnt = 0;
                for (const stl_triangle_vertex_indices &tri : its.indices)
                    for (int i = 0; i < 3; ++i) {
                        sum += (its.vertices[tri[i]] - its.vertices[tri[(i + 1) % 3]]).norm();
                        ++cnt;
                    }
                m_subdivide_target_mm = cnt > 0 ? std::clamp(float(sum / double(cnt)) * 0.5f, 0.001f, 20.f) : 1.f;
            }
            const auto preview_live = [this]() {
                if (m_subdivide_editing)
                    rebuild_subdivide_preview();
                m_parent.set_as_dirty();
            };

            if (m_imgui->bbl_checkbox(_L("Follow texture detail"), m_subdivide_feature))
                preview_live();
            hover_tip(_u8L("Spends the triangles where the texture actually bends - packed along ridges and edges, "
                            "sparse over flat ground - instead of spreading them evenly. The same detail for fewer "
                            "triangles on most textures."));

            if (float_row("##subdiv_target", m_subdivide_feature ? _L("Max edge") : _L("Target edge"), &m_subdivide_target_mm,
                          0.001f, 20.f, "%.3f mm", true, 0.f))
                preview_live();
            hover_tip(m_subdivide_feature ?
                          _u8L("No triangle in the painted area stays larger than this, even where the texture "
                               "is flat. Keep it near the size of the smallest feature you want to come out.") :
                          _u8L("Triangles in the painted area are split until none is larger than this. Smaller "
                               "means finer detail and, fast, a lot more triangles."));

            if (m_subdivide_feature) {
                if (float_row("##subdiv_detail", _L("Detail"), &m_subdivide_detail_mm, 0.001f, 1.f, "%.3f mm", true, 0.f))
                    preview_live();
                hover_tip(_u8L("How far the mesh may sit from the shape the texture describes, in millimetres. "
                               "Smaller follows fine detail and costs triangles; larger only chases the big "
                               "features."));
                if (float_row("##subdiv_min", _L("Min edge"), &m_subdivide_min_edge_mm, 0.001f, 20.f, "%.3f mm", true, 0.f))
                    preview_live();
                hover_tip(_u8L("A floor on triangle size, so a sharp step in the texture cannot be chased "
                               "forever. Lower it for finer relief, raise it if the count runs away at hard "
                               "edges."));
            }

            if (float_row("##subdiv_border", _L("Edge detail"), &m_subdivide_border_mm, 0.f, 5.f, "%.3f mm", false, 0.f))
                preview_live();
            hover_tip(_u8L("Triangle size along the outline of the painted area, where the relief drops back to the "
                            "bare surface. Lower it if that rim looks jagged; it only costs triangles along the "
                            "outline. 0 turns it off."));

            if (mv != nullptr && any_layer_colors(*mv)) {
                if (float_row("##subdiv_color", _L("Color detail"), &m_subdivide_color_mm, 0.f, 5.f, "%.3f mm", false, 0.f))
                    preview_live();
                hover_tip(_u8L("Triangle size where two colors meet. Each triangle prints in one filament, so a "
                                "color edge can only be as sharp as the triangles along it - and nothing else "
                                "refines there, since the surface is flat across a change of color. 0 turns it off."));
            }

            ImGui::TextDisabled("%s", _u8L("Triangle budget: set with Triangles, below.").c_str());
            if (m_subdivide_editing && m_subdivide_preview_tris > 0)
                m_imgui->text(Slic3r::format(_u8L("Preview: %1% triangles"), m_subdivide_preview_tris));
        } else {
            if (int_row("##subdiv_steps", _L("Steps"), &m_subdivide_count, 0, 5, "%d", 0.f)) {
                if (m_subdivide_editing)
                    rebuild_subdivide_preview();
                m_parent.set_as_dirty();
            }
            hover_tip(_u8L("Splits every triangle of the model into four, this many times over. Each step "
                           "quadruples the count - 3 turns 100 k triangles into 6.4 M - so prefer refining "
                           "only the painted area unless you need the whole model finer."));
        }

        {
            const bool  subdivide_ready = m_subdivide_adaptive ? (m_subdivide_target_mm > 0.f) : (m_subdivide_count >= 1);
            const float half            = std::floor((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f);
            m_imgui->disabled_begin(busy);
            if (ImGui::Button((m_subdivide_editing ? _u8L("Hide preview") : _u8L("Preview subdivision")).c_str(), ImVec2(half, 0.f))) {
                if (m_subdivide_editing) {
                    m_subdivide_editing      = false;
                    m_subdivide_preview_tris = -1;
                    m_subdivide_preview_glmodel.reset();
                } else {
                    m_subdivide_editing = true;
                    rebuild_subdivide_preview();
                }
                m_parent.set_as_dirty();
            }
            m_imgui->disabled_end();
            hover_tip(_u8L("Shows what the refinement would produce as a wireframe, without touching the model."));
            ImGui::SameLine();
            m_imgui->disabled_begin(!subdivide_ready || busy);
            if (ImGui::Button(_u8L("Subdivide").c_str(), ImVec2(half, 0.f))) {
                if (m_subdivide_adaptive) {
                    subdivide_model_adaptive();
                } else {
                    subdivide_model();
                    m_subdivide_count = 0;
                }
                if (m_subdivide_editing)
                    rebuild_subdivide_preview();
                m_parent.set_as_dirty();
            }
            m_imgui->disabled_end();
            hover_tip(m_subdivide_adaptive ?
                          _u8L("Refines the painted area to the target edge length and carries your paint onto "
                               "the finer mesh. The rest of the model is left as it is.") :
                          _u8L("Replaces the model's geometry with the subdivided mesh and clears any not-yet-baked "
                               "paint on it (already-baked relief is unaffected)."));
        }

        ImGui::Separator();
        heading(_L("Remeshing"));
        if (m_remesh_target_edge_mm <= 0.f && mv != nullptr) {
            const indexed_triangle_set &its = mv->mesh().its;
            double                      sum = 0.0;
            size_t                      cnt = 0;
            for (const stl_triangle_vertex_indices &tri : its.indices)
                for (int i = 0; i < 3; ++i) {
                    sum += (its.vertices[tri[i]] - its.vertices[tri[(i + 1) % 3]]).norm();
                    ++cnt;
                }
            m_remesh_target_edge_mm = cnt > 0 ? std::clamp(float(sum / double(cnt)), 0.1f, 20.f) : 1.f;
        }
        float_row("##remesh_edge", _L("Target edge"), &m_remesh_target_edge_mm, 0.1f, 20.f, "%.2f mm", true, 0.f);
        hover_tip(_u8L("Triangle size the whole model is rebuilt with, in millimetres. Displacement works best on an "
                        "even mesh; this is what makes one out of an uneven import."));

        m_imgui->bbl_checkbox(_L("Keep sharp edges") + "##remesh_sharp", m_remesh_keep_sharp_edges);
        hover_tip(_u8L("Holds hard edges and open borders in place while the rest is remeshed. Without it "
                       "the remesher slides vertices along the surface and rounds every crisp edge off - "
                       "a cube comes back with wobbly edges."));
        ImGui::SameLine();
        m_imgui->disabled_begin(!m_remesh_keep_sharp_edges);
        ImGui::SetNextItemWidth(-1.f);
        ImGui::SliderFloat("##remesh_sharp_angle", &m_remesh_sharp_angle_deg, 5.f, 90.f, "%.0f°", ImGuiSliderFlags_AlwaysClamp);
        m_imgui->disabled_end();
        hover_tip(_u8L("How sharp a fold has to be, in degrees, before the remesher treats it as an edge worth "
                        "keeping. Lower protects more of the model's shape; higher rebuilds more of it evenly."));

        m_imgui->disabled_begin(mv == nullptr || busy);
        if (ImGui::Button(_u8L("Remesh").c_str()))
            remesh_model();
        m_imgui->disabled_end();
        hover_tip(_u8L("Rebuilds the whole model with triangles close to this edge length - splitting the big "
                       "ones and merging the small ones - so displacement has an even density to work with. "
                       "Replaces the geometry; your paint is carried onto the new triangles spatially, so it "
                       "survives (already-baked relief is kept too)."));

        if (mv != nullptr) {
            TextureDisplacementOptions &opts = mv->texture_displacement_options;
            ImGui::Separator();
            heading(_L("Result"));

            m_preview_params_dirty |= m_imgui->bbl_checkbox(_L("Displace up to the border"), opts.displace_border);
            hover_tip(_u8L("Lets the relief run right to the edge of the painted area. Turn it off to hold that "
                            "outer ring flat, which keeps the displacement strictly inside your paint but flattens "
                            "the pattern at the border."));

            m_preview_params_dirty |= m_imgui->bbl_checkbox(_L("Smooth result"), opts.smooth_enabled);
            hover_tip(_u8L("Smooths the geometry after the texture has been applied, to take the hard steps out of a "
                            "low-resolution image. Only what the displacement moved is touched. The Smoothing slider "
                            "on a layer is a different thing: it blurs the image before it is used."));
            if (opts.smooth_enabled) {
                float percent = opts.smooth_strength * 100.f;
                if (float_row("##dispsmooth", _L_CONTEXT("Strength", "Texture Displacement"), &percent, 1.f, 100.f, "%.0f %%", false, 0.f)) {
                    opts.smooth_strength   = std::clamp(percent / 100.f, 0.01f, 1.f);
                    m_preview_params_dirty = true;
                }
                hover_tip(_u8L("How far each pass pulls a vertex towards its neighbours. High values round the "
                               "relief off quickly; low values need more passes but keep more of the detail."));
                if (int_row("##dispsmoothit", _L("Passes"), &opts.smooth_iterations, 1, 10, "%d", 0.f))
                    m_preview_params_dirty = true;
                hover_tip(_u8L("How many smoothing passes to run. More passes spread the smoothing further across "
                                "the surface; Strength decides how much each one moves."));

                m_preview_params_dirty |= m_imgui->bbl_checkbox(_L("Ignore outer ring"), opts.smooth_skip_border);
                hover_tip(_u8L("Keeps the outer ring of the painted area out of the smoothing. Its neighbours "
                                "outside the paint never move, so smoothing it drags the relief down and leaves the "
                                "pattern half-melted at the border."));

                if (m_imgui->button(_u8L("Smooth baked mesh now")))
                    smooth_model();
                hover_tip(_u8L("Applies the smoothing above to the model itself, right now, within the painted area. "
                                "For relief that is already baked in - anything not yet baked is smoothed by Bake."));
            }
        }
    }

    static constexpr bool SHOW_PIPELINE_DEV_CONTROLS = false;
    if (mv != nullptr) {
        TextureDisplacementOptions &opts = mv->texture_displacement_options;
        ImGui::Separator();
        if (SHOW_PIPELINE_DEV_CONTROLS) {
            bool classic = !opts.pipeline_v2;
            if (m_imgui->bbl_checkbox(_L("Experimental: classic bake pipeline"), classic)) {
                opts.pipeline_v2       = !classic;
                m_preview_params_dirty = true;
            }
            hover_tip(_u8L("Bake by moving the vertices the mesh already has, after preparing it (remesh, "
                           "adaptive subdivision, and a cut along sharp steps in the texture). Keeps the "
                           "topology, which is what colors need. The default pipeline instead refines, "
                           "cleans up sliver triangles, displaces and simplifies in one run; nothing needs "
                           "preparing first, but it does not produce colors yet."));
        }
        if (opts.pipeline_v2) {
            const bool auto_budget = opts.v2_max_triangles_k < 0;
            int        shown_k     = auto_budget ? v2_recommendation(*mv).budget_k : opts.v2_max_triangles_k;
            m_imgui->disabled_begin(auto_budget);
            // TRN Slider value: %d is the triangle budget in thousands
            if (int_row("##v2budget", _L("Budget"), &shown_k, 0, 4000, auto_budget ? _u8L("%d k (auto)").c_str() : "%d k", 0.f)) {
                opts.v2_max_triangles_k = shown_k;
                m_preview_params_dirty  = true;
            }
            m_imgui->disabled_end();
            hover_tip(_u8L("How many thousand triangles this bake may spend on the area you painted. "
                           "Relief already baked into the rest of the model is kept on top of it, so a "
                           "second bake somewhere else gets the same budget as the first. Raise it if the "
                           "warning below Resolution says the detail will not fit; 0 keeps every triangle "
                           "the refinement produced, however many that is."));
        }
        if (SHOW_PIPELINE_DEV_CONTROLS && opts.pipeline_v2) {
            m_preview_params_dirty |= m_imgui->bbl_checkbox(_L("Align mesh to texture edges"), opts.v2_relocate);
            hover_tip(_u8L("Slide vertices sideways onto the edges in the texture before displacing them. "
                           "Displacement can only move vertices up and down, so without this a sharp step "
                           "in the image lands wherever the triangles happen to be and comes out as a "
                           "staircase. Moving the vertices onto the step first gives a straight wall at the "
                           "same triangle count."));
            m_preview_params_dirty |= m_imgui->bbl_checkbox(_L("Clean up slivers"), opts.v2_regularize);
            hover_tip(_u8L("Collapse the thin triangles refinement inherits from the model's own "
                           "tessellation, before displacement samples them. A sliver's three corners "
                           "land on three unrelated parts of the texture, which is what makes the "
                           "relief look jagged."));
        }
        if (SHOW_PIPELINE_DEV_CONTROLS)
            render_debug_stage_panel(mv);
    }

    m_panel_body_h = ImGui::GetCursorPosY() - style.ItemSpacing.y;
    ImGui::EndChild();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);

    if (slot_to_remove >= 0)
        remove_texture_layer(slot_to_remove);
    else if (move_slot >= 0)
        move_texture_layer(move_slot, move_to);
    else if (activate_slot >= 0)
        set_active_layer(activate_slot);
    if (m_picker_open_request && m_picker_slot >= 0)
        set_active_layer(m_picker_slot);

    {
        const ImVec2 win_min = ImGui::GetWindowPos();
        const ImVec2 win_sz  = ImGui::GetWindowSize();
        render_texture_library_popup(win_min, ImVec2(win_min.x + win_sz.x, win_min.y + win_sz.y));
    }

    if (m_preview_params_dirty && m_subdivide_editing && m_subdivide_adaptive && m_subdivide_feature)
        rebuild_subdivide_preview();
    if (m_preview_params_dirty && (m_auto_update || !ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
        rebuild_preview();
        update_projector();
        m_preview_params_dirty = false;
    }

    const float footer_top = ImGui::GetCursorScreenPos().y;
    ImGui::Separator();
    {
        const float x0     = ImGui::GetCursorPosX();
        const int   base_k = mv != nullptr ? int((mv->mesh().facets_count() + 500) / 1000) : 0;
        const bool  v2     = mv != nullptr && mv->texture_displacement_options.pipeline_v2;

        ImGui::AlignTextToFramePadding();
        if (v2) {
            TextureDisplacementOptions &opts = mv->texture_displacement_options;
            const V2Resolution         &rec  = v2_recommendation(*mv);
            m_imgui->text(_L("Resolution"));
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + label_w));
            bool auto_res = opts.v2_refine_mm <= 0.f;
            m_imgui->disabled_begin(busy);
            if (m_imgui->bbl_checkbox(wxString::FromUTF8("##v2auto"), auto_res)) {
                if (auto_res) {
                    opts.v2_refine_mm       = 0.f;
                    opts.v2_max_triangles_k = -1;
                } else {
                    opts.v2_refine_mm       = rec.edge_mm > 0.f ? rec.edge_mm : 0.3f;
                    opts.v2_max_triangles_k = rec.budget_k > 0 ? rec.budget_k : 750;
                }
                m_preview_params_dirty = true;
                m_parent.set_as_dirty();
            }
            m_imgui->disabled_end();
            hover_tip(_u8L("On: the resolution follows the size of the model and the budget is left at its "
                           "default. Untick to set both yourself - worth doing for a texture much finer or "
                           "much coarser than the part it sits on."));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(x0 + panel_w - ImGui::GetCursorPosX());
            float shown = auto_res ? rec.edge_mm : opts.v2_refine_mm;
            m_imgui->disabled_begin(busy || auto_res);
            if (ImGui::SliderFloat("##v2edge", &shown, 0.02f, 2.f, auto_res ? _u8L("%.2f mm (auto)").c_str() : "%.2f mm",
                                   ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic)) {
                opts.v2_refine_mm      = shown;
                m_preview_params_dirty = true;
                m_parent.set_as_dirty();
            }
            m_imgui->disabled_end();
            if (auto_res && rec.edge_mm > 0.f)
                hover_tip(Slic3r::format(_u8L("Chosen from the size of the model: %1$.2f mm triangles, "
                                              "budget %2% k. Untick Auto to set them yourself."),
                                          rec.edge_mm, rec.budget_k));
            else
                hover_tip(_u8L("How fine the mesh is made under the paint, in millimetres. It has to be "
                               "smaller than the detail you want out of the texture - a 0.5 mm groove needs "
                               "triangles well under 0.5 mm. Smaller costs triangles fast: halving it needs "
                               "four times as many, and once they no longer fit the budget the bake simplifies "
                               "back down and the detail goes with it."));

            const float  edge_now   = auto_res ? rec.edge_mm : opts.v2_refine_mm;
            const int    budget_k   = opts.v2_max_triangles_k < 0 ? rec.budget_k : opts.v2_max_triangles_k;
            const size_t budget     = size_t(std::max(0, budget_k)) * 1000;
            const size_t needed     = estimated_refined_triangles(*mv, edge_now);
            if (budget > 0 && needed > budget * 5 / 4) {
                const auto count = [](size_t n) {
                    return n >= 1000000 ? Slic3r::format("%1$.1f M", double(n) / 1000000.) :
                                          Slic3r::format("%1% k", (n + 500) / 1000);
                };
                ImGui::PushTextWrapPos(x0 + panel_w);
                m_imgui->warning_text(Slic3r::format(_u8L("Needs about %1% triangles, budget %2% - the bake "
                                                          "will simplify back down and lose detail."),
                                                     count(needed), count(budget)));
                ImGui::PopTextWrapPos();
            }
        } else {
            m_imgui->text(_L("Triangles"));
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + label_w));
            ImGui::SetNextItemWidth(x0 + panel_w - ImGui::GetCursorPosX());
            m_imgui->disabled_begin(busy);
            if (ImGui::SliderInt("##subdivbudget", &m_subdivide_budget_k, 10, 2000, "+%d k",
                                 ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic)) {
                if (m_subdivide_editing)
                    rebuild_subdivide_preview();
                m_parent.set_as_dirty();
            }
            m_imgui->disabled_end();
            hover_tip(_u8L("How many thousand triangles the refinement may add to the model. The triangle "
                           "that fits worst is always split first, so even a run that spends the lot has "
                           "spent it where it shows most. Raise it if the relief still looks coarse."));
        }

        const float button_h = std::round(frame_h * 1.25f);
        const float button_w = std::max({
            ImGui::CalcTextSize(_u8L("Close").c_str()).x,
            ImGui::CalcTextSize(_u8L("Stop").c_str()).x,
            ImGui::CalcTextSize(_u8L("Preparing...").c_str()).x,
            ImGui::CalcTextSize(_u8L("Baking...").c_str()).x,
        }) + m_imgui->scaled(0.5f);
        const float row_y  = ImGui::GetCursorPosY();
        const float icon_h = 21.f * scale;
        const float row_h  = std::max(icon_h, button_h);

        const std::vector<std::pair<wxString, wxString>> shortcut = 
            m_tool_type == ToolType::BUCKET_FILL ? m_shortcuts_bucket_fill
            : m_tool_type == ToolType::SMART_FILL  ? m_shortcuts_bucket_fill 
            : m_tool_type == ToolType::BRUSH       ? m_shortcuts_brush 
            : m_tool_type == ToolType::GAP_FILL    ? m_shortcuts_gap_fill 
            : std::vector<std::pair<wxString, wxString>>{};

        ImGui::SetCursorPosY(row_y + (row_h - icon_h) * .5f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(int(m_imgui->scaled(.5f)), style.ItemSpacing.y));
        GLGizmoUtils::render_tooltip_button(m_imgui, m_parent, shortcut, x, y);
        ImGui::SameLine();
        GLGizmoUtils::render_wiki_guide_button(m_parent, scale, "https://www.orcaslicer.com/wiki/print_prepare/prepare_texture_displacement");
        ImGui::SameLine();
        GLGizmoUtils::render_video_guide_button(m_parent, scale, "https://www.youtube.com/watch?v=D7w3tG1kdvE");
        ImGui::PopStyleVar(1);

        ImGui::SameLine(x0 + panel_w - button_w * 2 - style.ItemSpacing.x);

        const bool        can_bake   = !busy && mv != nullptr && mv->is_texture_displacement_painted();
        const std::string bake_label = m_prepare_in_progress ? _u8L("Preparing...") :
                                       m_bake_in_progress    ? _u8L("Baking...") :
                                                               into_u8(m_desc.at("bake"));
        GLGizmoUtils::push_orca_button_style();
        m_imgui->push_bold_font();
        m_imgui->disabled_begin(!can_bake);
        ImGui::SetCursorPosY(row_y + (row_h - button_h) * .5f);
        if (ImGui::Button((bake_label + "##bake").c_str(), ImVec2(button_w, button_h))) {
            if (pro_mode())
                bake();
            else
                bake_standard();
        }
        m_imgui->disabled_end();
        m_imgui->pop_bold_font();
        GLGizmoUtils::pop_orca_button_style();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            m_imgui->tooltip(mv != nullptr && !mv->is_texture_displacement_painted() ?
                                 (m_seam_edit_mode ? _u8L("Nothing is painted yet. The UV editor's seam tool is on, so "
                                                          "strokes on the model mark seams instead of painting - turn "
                                                          "it off in the pane to paint.") :
                                                     _u8L("Nothing is painted yet.")) :
                             pro_mode() ?
                                 _u8L("Turn the painted height maps into real geometry, by moving the vertices that are "
                                      "already there. Use Subdivide first if the mesh is too coarse to show the detail.") :
                                 _u8L("Turn the painted height maps into real geometry. The mesh is remeshed to an even "
                                      "density and refined where the texture bends first, so the detail has vertices to "
                                      "land on - all in one step."),
                             wrap_w);

        ImGui::SameLine();
        if (busy) {
            if (ImGui::Button((_u8L("Stop") + "##stop").c_str(), ImVec2(button_w, button_h)))
                wxGetApp().plater()->get_ui_job_worker().cancel_all();
            hover_tip(_u8L("Stops the bake. Whatever it had already finished stays on the model, and can be undone."));
        } else {
            if (ImGui::Button((_u8L("Close") + "##close").c_str(), ImVec2(button_w, button_h)))
                m_parent.reset_all_gizmos();
            hover_tip(_u8L("Closes the tool without baking. Your paint, layers and settings stay with the model."));
        }

        if (mv != nullptr) {
            const std::string base_count = base_k > 0 ? Slic3r::format(_u8L("%1% k"), base_k) :
                                                        std::to_string(mv->mesh().facets_count());
            std::string note = v2         ? Slic3r::format(_u8L("Model: %1% triangles - the bake refines to the resolution above, up to its budget"), base_count) :
                               pro_mode() ? Slic3r::format(_u8L("Bake moves existing vertices - the model stays at %1% triangles"), base_count) :
                                            Slic3r::format(_u8L("Bake result up to ~%1% k triangles"), base_k + m_subdivide_budget_k);
            if (mv->is_texture_displacement_painted()) {
                std::vector<std::string> skipped;
                for (const TextureDisplacementLayer &l : mv->texture_displacement_layers)
                    if (!slot_painted(l.slot))
                        skipped.push_back(layer_name(l));
                if (skipped.size() > 2)
                    note += ", " + Slic3r::format(_u8L("%1% layers not painted, skipped"), skipped.size());
                else if (!skipped.empty())
                    note += ", " + Slic3r::format(_u8L("%1% not painted, skipped"),
                                                   skipped.size() == 2 ? skipped[0] + ", " + skipped[1] : skipped[0]);
            }
            ImGui::PushTextWrapPos(x0 + panel_w);
            ImGui::TextDisabled("%s", note.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    m_panel_footer_h = ImGui::GetCursorScreenPos().y - footer_top;
    constexpr float resize_epsilon = 0.5f;
    const float content_bottom = ImGui::GetCursorPosY() - style.ItemSpacing.y;
    const float window_bottom  = ImGui::GetWindowContentRegionMax().y;
    const bool  outer_window_needs_fit = content_bottom < window_bottom - resize_epsilon ||
                                         (content_bottom > window_bottom + resize_epsilon &&
                                          ImGui::GetWindowHeight() < ImGui::GetMainViewport()->Size.y -
                                                                             2.f * style.DisplaySafeAreaPadding.y - resize_epsilon);
    if (std::abs(m_panel_body_h - previous_body_h) > resize_epsilon ||
        std::abs(m_panel_footer_h - previous_footer_h) > resize_epsilon || outer_window_needs_fit) {
        m_parent.request_extra_frame();
    }
    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();

    render_paint_cursor_hint();
}

}
