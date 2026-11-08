#ifndef slic3r_GLGizmoTextureDisplacement_hpp_
#define slic3r_GLGizmoTextureDisplacement_hpp_

#include "GLGizmoPainterBase.hpp"
#include "libslic3r/TextureBake/TextureBakeDebug.hpp"
#include "libslic3r/TextureDisplacement.hpp"
#include "slic3r/GUI/GLModel.hpp"
#include "slic3r/GUI/GLTexture.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/IconManager.hpp"
#include "slic3r/GUI/TextureLibrary.hpp"

#include <Eigen/Core>
#include <array>
#include <atomic>
#include "libslic3r/Color.hpp"
#include <cstddef>
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "admesh/stl.h"
#include <cstdint>
#include <functional>
#include "libslic3r/Point.hpp"
#include <imgui.h>
#include <map>
#include <memory>
#include <optional>
#include "slic3r/GUI/Gizmos/GLGizmoBase.hpp"
#include <string>
#include <vector>
#include <utility>
#include <wx/event.h>
#include <wx/string.h>

namespace Slic3r::GUI {

class TextureProjectorFrame;

class GLGizmoTextureDisplacement : public GLGizmoPainterBase
{
public:
    GLGizmoTextureDisplacement(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    static TextureDisplacementPrepareResult prepare_mesh(const indexed_triangle_set                  &base,
                                                         const TextureDisplacementFacetsData         &masks,
                                                         const std::vector<TextureDisplacementLayer> &layers,
                                                         const TextureDisplacementPrepareParams      &params,
                                                         const TextureColorSettings                  &color_settings,
                                                         const DisplacementProgressFn                &progress,
                                                         BakeStageRecorder                           *debug = nullptr);
    static TextureDisplacementFacetsData facets_data_of(const ModelVolume &mv);

    using PaletteEntry = PrintableColor;

    struct MixTarget
    {
        Vec3f lab    = Vec3f::Zero();
        float weight = 0.f;
    };

    static std::vector<MixTarget> mix_targets(const std::vector<TextureDisplacementLayer> &layers);

    static std::vector<PaletteEntry> rank_mixes(const std::vector<ColorRGBA> &filaments,
                                                const std::vector<MixTarget> &targets, int limit);

    static std::vector<PaletteEntry> pick_mixes(const std::vector<PaletteEntry> &ranking, int count, int free_slots,
                                                const std::function<bool(const PaletteEntry &)> &reusable);

    static std::vector<PaletteEntry> make_palette(const std::vector<ColorRGBA> &filaments,
                                                  const std::vector<PaletteEntry> &mixes);

    static ColorQuantizeFn make_palette_quantizer(const std::vector<PaletteEntry> &palette);

    static std::vector<int> palette_filaments(const std::vector<PaletteEntry> &palette,
                                              const std::vector<uint8_t> &triangle_color,
                                              const std::function<int(const PaletteEntry &)> &slot_for_mix);

    TextureColorSettings color_settings_for(const ModelVolume &mv);

    const std::vector<PaletteEntry> &cached_palette();
    std::pair<ColorQuantizeFn, ColorQuantizeFn> palette_quantizers();
    std::vector<PaletteEntry> m_palette_cache;
    ColorQuantizeFn           m_palette_quantizer;
    ColorQuantizeFn           m_palette_pure_quantizer;
    bool                      m_palette_changed = false;
    std::vector<ColorRGBA>                                   m_palette_filaments;
    std::vector<std::shared_ptr<std::vector<unsigned char>>> m_palette_images;
    std::optional<std::vector<PaletteEntry>>                 m_mix_ranking;
    bool                                                     m_palette_mixing     = false;
    int                                                      m_palette_mix_count  = 0;
    int                                                      m_palette_free_slots = 0;
    std::vector<std::string>                                 m_palette_slots;
    static std::vector<ColorRGBA> filament_palette();

    struct PreviewColorRun
    {
        std::pair<size_t, size_t> range;
        ColorRGBA                 color;
    };
    std::vector<PreviewColorRun> m_preview_color_runs;
    static bool any_layer_colors(const ModelVolume &mv);

    struct PaintedColors
    {
        indexed_triangle_set facets;
        std::vector<int>     source;
        std::vector<int>     state;
        std::vector<size_t> outside(const std::vector<bool> &excluded) const;
    };
    static PaintedColors painted_colors(const TriangleMesh &mesh, const TriangleSelector::TriangleSplittingData &paint);

    void render_painter_gizmo() override;

    bool on_mouse(const wxMouseEvent &mouse_event) override;

protected:
    void        on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;
    bool        on_is_activable() const override;

    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    std::string get_gizmo_entering_text() const override { return _u8L("Entering Texture displacement painting"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Texture displacement painting"); }
    std::string get_action_snapshot_name() const override { return _u8L("Texture displacement editing"); }

    EnforcerBlockerType get_left_button_state_type() const override { return m_erase_mode ? EnforcerBlockerType::NONE : EnforcerBlockerType::ENFORCER; }
    EnforcerBlockerType get_right_button_state_type() const override { return m_erase_mode ? EnforcerBlockerType::ENFORCER : EnforcerBlockerType::NONE; }

private:
    bool on_init() override;
    void update_model_object() override;
    void update_from_model_object(bool first_update) override;
    void on_opening() override {}
    void on_shutdown() override;
    PainterGizmoType get_painter_type() const override;

    ModelVolume*       texture_volume();
    const ModelVolume* texture_volume() const;

    void add_texture_layer();
    void remove_texture_layer(int slot);
    void move_texture_layer(int slot, int to_index);
    void swap_layer_slots(int slot_a, int slot_b);
    void set_active_layer(int slot);
    void bake(bool own_snapshot = true);

    int  m_panel_mode = 0;
    bool pro_mode() const { return m_panel_mode != 0; }
    bool apply_standard_mode_presets(ModelVolume *mv);
    void bake_standard();

    void queue_prepare(const TextureDisplacementPrepareParams &params, const std::string &snapshot_name,
                       bool then_bake, const std::string &unchanged_msg);
    bool m_prepare_in_progress = false;

    struct LayerPaintMap
    {
        std::vector<uint8_t>              full;
        std::vector<int>                  part_start;
        std::vector<std::array<Vec3f, 3>> part;
        bool empty() const { return full.empty(); }
    };
    static TextureDisplacementFacetsData masks_after_subdivision(
        const TriangleMesh &new_mesh, const std::vector<int> &source,
        const std::array<LayerPaintMap, TEXTURE_DISPLACEMENT_MAX_LAYERS> &paint);
    static bool plan_remesh(const indexed_triangle_set &src, float target_edge_mm, float sharp_angle_deg,
                            indexed_triangle_set &out);

    void select_whole_model();

    int texture_volume_raycaster_index() const;

    int select_visible_faces(const std::array<float, 12> *uv_clip = nullptr);

    bool m_project_only_visible = false;

    TextureProjectorFrame *m_projector_frame   = nullptr;
    int                    m_projector_opacity = 140;
    const void *m_projector_tex_source    = nullptr;
    float       m_projector_tex_smoothing = -1.f;
    void show_projector(bool show);
    void update_projector();
    int apply_projection_frame();

    void subdivide_model();

    static float layer_texture_aspect(const TextureDisplacementLayer &layer);

    GLTexture *get_layer_thumbnail(const TextureDisplacementLayer &layer);

    GLTexture *get_layer_height_texture(const TextureDisplacementLayer &layer);
    GLTexture *get_layer_color_texture(const TextureDisplacementLayer &layer);

    struct LibraryTexture
    {
        std::shared_ptr<std::vector<unsigned char>> image_data;
        std::unique_ptr<GLTexture>                  thumbnail;
    };
    const LibraryTexture *get_library_texture(const std::string &path);

    void render_texture_library_popup(const ImVec2 &panel_min, const ImVec2 &panel_max);
    int  m_picker_slot         = -1;
    bool m_picker_open_request = false;
    void set_layer_texture(TextureDisplacementLayer &layer, const TextureLibraryEntry &entry);
    void import_custom_texture(TextureDisplacementLayer &layer);

    bool m_erase_mode = false;
    std::array<bool, TEXTURE_DISPLACEMENT_MAX_LAYERS> m_layer_expanded{};

    bool update_adjust_anchor();
    bool on_mouse_adjust_texture(const wxMouseEvent &mouse_event);
    void render_adjust_texture_gizmo();
    void render_paint_cursor_hint();
    void adjust_tangent_basis(Vec3f &u_axis, Vec3f &v_axis) const;

    Vec3f adjust_plane_point() const;

    Vec3f adjust_handle_center(const TextureDisplacementLayer &layer) const;

    TextureDisplacementLayer       *active_layer();
    const TextureDisplacementLayer *active_layer() const;

    void rebuild_preview();
    void render_preview_mesh();

    void rebuild_shaded_preview_mesh();
    void render_shaded_preview_mesh();

    void update_uv_editor();

    void on_island_edited(int island, const Vec2f &offset_delta, float rotation_delta, float scale_factor, bool finished);
    void on_uv_vertex_edited(const std::vector<std::pair<int, Vec2f>> &edits);
    int  m_uv_select_mode = 0;
    std::vector<Eigen::Matrix<float, 2, 3>> uv_editor_island_transforms(const TextureDisplacementLayer &layer);
    void on_uv_command(int cmd, float value);
    std::vector<std::pair<int, float>> m_uv_command_queue;
    void process_uv_commands();
    void run_uv_command(int cmd, float value);
    void push_uv_pane_state();
    void apply_view_mode(int mode);
    static constexpr int       UV_THUMB_PX       = 64;
    const void                *m_uv_thumb_source = nullptr;
    std::vector<unsigned char> m_uv_thumb_rgb;
    void cut_island(TextureDisplacementLayer &layer, int chart);

    void capture_view_projection(TextureDisplacementLayer &layer);

    bool    m_seam_edit_mode = false;
    GLModel m_seam_glmodel;
    bool    on_mouse_seam(const wxMouseEvent &mouse_event);
    void    toggle_seam_at(const Vec2d &mouse_pos);
    void    rebuild_seam_overlay();
    void    render_seam_overlay();
    std::pair<int, int> seam_edge_at(const Vec2d &mouse_pos) const;
    std::pair<int, int> m_seam_hover_edge{ -1, -1 };
    int                 m_seam_hover_vertex = -1;
    GLModel             m_seam_hover_glmodel;
    void                rebuild_seam_hover_overlay();

    bool    m_seam_path_mode   = false;
    int     m_seam_path_anchor = -1;
    GLModel m_seam_anchor_glmodel;
    int     seam_vertex_at(const Vec2d &mouse_pos) const;
    void    mark_seam_path(int v_from, int v_to);
    void    rebuild_seam_anchor_overlay();
    bool m_island_drag_active = false;

    int  m_active_layer_slot = 0;
    bool m_selectors_stale = false;
    bool m_bake_in_progress  = false;

    bool m_auto_update = true;

    int     m_subdivide_count         = 1;
    bool    m_subdivide_editing       = false;
    int     m_subdivide_preview_tris  = -1;
    GLModel m_subdivide_preview_glmodel;
    void    rebuild_subdivide_preview();
    void    render_subdivide_preview();

    bool  m_subdivide_adaptive   = false;
    float m_subdivide_target_mm  = 0.f;
    bool  m_subdivide_feature     = false;
    float m_subdivide_detail_mm   = 0.05f;
    float m_subdivide_min_edge_mm = 0.1f;
    float m_subdivide_border_mm   = 0.4f;
    float m_subdivide_color_mm    = 0.3f;
    int   m_subdivide_budget_k    = 750;
    void  subdivide_model_adaptive();
    static bool collect_paint_region(const TriangleMesh &mesh, const TextureDisplacementFacetsData &facets,
                                     std::vector<uint8_t> &region,
                                     std::array<LayerPaintMap, TEXTURE_DISPLACEMENT_MAX_LAYERS> *paint);

    void  smooth_model();

    float m_remesh_target_edge_mm = 0.f;
    float m_remesh_sharp_angle_deg   = 40.f;
    bool  m_remesh_keep_sharp_edges  = true;
    void  remesh_model();

    std::vector<BakeStageSnapshot> m_debug_stages;
    int  m_debug_stage          = -1;
    bool m_debug_in_progress    = false;
    bool m_debug_check_topology = true;

    double painted_area_mm2(const ModelVolume &mv);
    double      m_painted_area_mm2 = 0.;
    std::string m_painted_area_key;
    size_t estimated_refined_triangles(const ModelVolume &mv, float edge_mm);

    const V2Resolution &v2_recommendation(const ModelVolume &mv);
    V2Resolution        m_v2_rec;
    std::string         m_v2_rec_key;

    void run_stage_debug();
    void show_debug_stage(int index);
    void exit_debug_view();
    void render_debug_stage_panel(ModelVolume *mv);

    GLModel m_preview_glmodel;
    bool m_preview_params_dirty = false;

    bool    m_use_shaded_preview = false;
    bool    m_shaded_preview_dirty = false;
    GLModel m_shaded_preview_glmodel;

    GLModel m_paint_overlay_glmodel;
    GLModel          m_island_overlay_glmodel;
    std::vector<int> m_island_overlay_selection;
    void             rebuild_island_overlay(const std::vector<int> &selection);
    void             render_island_overlay();
    bool    m_paint_overlay_dirty = false;
    void    rebuild_paint_overlay();
    void    render_paint_overlay(GLModel &overlay);
    GLModel     m_other_paint_glmodel;
    std::string m_other_paint_key;
    void        rebuild_other_paint_overlay();
    PaintedColors m_painted_colors;
    std::string   m_painted_colors_key;
    GLModel       m_painted_colors_glmodel;
    std::string   m_painted_colors_drawn_key;
    std::vector<std::pair<int, std::pair<size_t, size_t>>> m_painted_colors_runs;
    void          rebuild_painted_colors(bool whole_stack);
    bool          render_painted_colors(bool whole_stack);
    bool    shaded_preview_ready() const;
    bool    m_shaded_preview_uses_vertex_uv = false;
    int   m_shaded_projection_mode = 0;
    Vec3f m_shaded_patch_center    = Vec3f::Zero();
    Vec3f m_shaded_patch_axis      = Vec3f::UnitZ();
    std::vector<PaletteEntry> m_shaded_preview_palette;

    int                        m_shaded_active_chart = -1;
    std::vector<uint8_t>       m_shaded_active_face;
    Eigen::Matrix<float, 2, 3> m_shaded_baked_active_xf = Eigen::Matrix<float, 2, 3>::Identity();
    Eigen::Matrix<float, 2, 3> m_shaded_island_delta    = Eigen::Matrix<float, 2, 3>::Identity();
    void                       compute_shaded_active_faces(const std::vector<int> &charts, size_t patch_face_count);

    std::vector<int> m_island_move_set;
    std::vector<int> build_island_move_set(const TextureDisplacementLayer &layer, int primary) const;
    static int       island_group_of(const std::vector<int> &groups, int c);
    static void      join_island_groups(std::vector<int> &groups, int a, int b, int chart_count);
    std::vector<Vec2f> compute_layer_vertex_uvs(const indexed_triangle_set &patch,
                                                const TextureDisplacementLayer &layer) const;
    std::vector<Vec2f> compute_layer_corner_uvs(const indexed_triangle_set &patch,
                                                const TextureDisplacementLayer &layer) const;
    int layer_projection_frame(const indexed_triangle_set &local_patch, const TextureDisplacementLayer &layer,
                               Vec3f &center, Vec3f &axis) const;
    indexed_triangle_set patch_in_world(const indexed_triangle_set &patch) const;

    enum class UVCheckMode { None, Checker, Distortion };
    UVCheckMode m_uv_check_mode = UVCheckMode::None;
    GLModel     m_uvcheck_glmodel;
    bool        m_uvcheck_uses_vertex_uv = false;
    int         m_uvcheck_projection_mode = 0;
    Vec3f       m_uvcheck_patch_center    = Vec3f::Zero();
    Vec3f       m_uvcheck_patch_axis      = Vec3f::UnitZ();
    void rebuild_uvcheck_mesh();
    void render_uvcheck_mesh();

    bool m_show_uv_editor = false;
    bool m_uv_unwrap_pending = false;
    bool m_uv_apply_connected_net = false;
    size_t m_uv_overrides_sig = 0;
    enum class UVBackground { None, Height, Checker };
    UVBackground m_uv_editor_bg = UVBackground::None;
    float        m_uv_editor_bg_smoothing = -1.f;
    const void  *m_uv_editor_bg_image     = nullptr;
    std::vector<ColorRGBA> m_uv_editor_distortion_colors;
    void                   compute_uv_editor_distortion_colors(const indexed_triangle_set &patch);

    bool    m_wireframe_overlay = false;
    GLModel m_wireframe_overlay_glmodel;
    size_t  m_wireframe_overlay_vcount = 0;
    void rebuild_wireframe_overlay();
    void build_wireframe_from_its(const indexed_triangle_set &its);
    void refresh_wireframe();
    void render_wireframe_overlay();
    indexed_triangle_set m_preview_its;
    std::shared_ptr<std::atomic<uint64_t>> m_preview_generation = std::make_shared<std::atomic<uint64_t>>(0);
    bool m_preview_job_running = false;
    bool m_preview_job_pending = false;
    void queue_preview_job();

    std::array<std::unique_ptr<GLTexture>, TEXTURE_DISPLACEMENT_MAX_LAYERS> m_thumbnails;
    std::array<const void *, TEXTURE_DISPLACEMENT_MAX_LAYERS>               m_thumbnail_source{};
    std::array<float, TEXTURE_DISPLACEMENT_MAX_LAYERS>                      m_thumbnail_smoothing{};

    std::unique_ptr<GLTexture> m_height_tex;
    const void                *m_height_tex_source    = nullptr;
    float                      m_height_tex_smoothing = -1.f;

    std::unique_ptr<GLTexture> m_color_tex;
    const void                *m_color_tex_source    = nullptr;
    float                      m_color_tex_smoothing = -1.f;

    std::map<std::string, LibraryTexture> m_library_textures;

    struct UVEditorState
    {
        int                                     slot       = -1;
        const void                             *image_data = nullptr;
        float                                   seam_angle = -1.f;
        float                                   padding    = -2.f;
        TriangleSelector::TriangleSplittingData facets;
        std::vector<std::pair<int, int>>        seam_edges;

        bool operator==(const UVEditorState &other) const
        {
            return slot == other.slot && image_data == other.image_data && seam_angle == other.seam_angle &&
                   padding == other.padding && facets == other.facets && seam_edges == other.seam_edges;
        }
    };
    UVEditorState m_uv_editor_state;
    Vec2f m_uv_editor_bbox_min = Vec2f::Zero();
    Vec2f m_uv_editor_bbox_max = Vec2f::Zero();
    PatchUnwrap m_uv_editor_unwrap;

    bool m_undocked = false;

    float m_panel_scroll_target  = 0.f;
    float m_panel_scroll_applied = -1.f;
    float m_panel_body_h   = 0.f;
    float m_panel_footer_h = 0.f;

    bool  m_adjust_texture_mode      = false;
    bool  m_adjust_anchor_valid      = false;
    Vec3f m_adjust_anchor_pos        = Vec3f::Zero();
    Vec3f m_adjust_anchor_normal     = Vec3f::UnitZ();
    enum class AdjustHandle { None, Pan, AxisU, AxisV };
    AdjustHandle m_adjust_drag_handle       = AdjustHandle::None;
    Vec2f        m_adjust_drag_start_offset = Vec2f::Zero();
    Vec2f m_adjust_drag_start_planar = Vec2f::Zero();

    GLModel m_adjust_panel_glmodel;
    GLModel m_adjust_arrow_glmodel;

    std::map<std::string, wxString> m_desc;

    std::vector<std::pair<wxString, wxString>> m_shortcuts_brush;
    std::vector<std::pair<wxString, wxString>> m_shortcuts_bucket_fill;
    std::vector<std::pair<wxString, wxString>> m_shortcuts_gap_fill;

    IconManager                                m_panel_icons;
    std::map<std::string, IconManager::Icons>  m_panel_icon_map;
    bool                                       m_panel_icons_tried = false;
    void                                       ensure_panel_icons();
};

}

#endif
