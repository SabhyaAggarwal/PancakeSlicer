///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/ libvgcode is released under the terms of the AGPLv3 or higher
#ifndef VGCODE_VIEWERIMPL_HPP
#define VGCODE_VIEWERIMPL_HPP

#include "Settings.hpp"
#include "SegmentTemplate.hpp"
#include "SegmentsLayer.hpp"
#include "OptionTemplate.hpp"
#include "../include/Types.hpp"
#include <vector>
#include <cstddef>
#include <cstdint>
#include <map>
#include <array>
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
#include "CogMarker.hpp"
#include "ToolMarker.hpp"
#endif
#include "../include/PathVertex.hpp"
#include "../include/ColorRange.hpp"
#include "../include/ColorPrint.hpp"
#include "Bitset.hpp"
#include "ViewRange.hpp"
#include "Layers.hpp"
#include "ExtrusionRoles.hpp"

#include <string>
#include <optional>

namespace libvgcode {

struct GCodeInputData;

class ViewerImpl
{
public:
    ViewerImpl();
    ~ViewerImpl() { shutdown(); }
    ViewerImpl(const ViewerImpl& other) = delete;
    ViewerImpl(ViewerImpl&& other) = delete;
    ViewerImpl& operator = (const ViewerImpl& other) = delete;
    ViewerImpl& operator = (ViewerImpl&& other) = delete;

    void init(const std::string& opengl_context_version);
    void shutdown();
    void reset();
    void load(GCodeInputData&& gcode_data);

    void update_enabled_entities();
    void update_colors();
    void update_colors_texture();

    void render(const Mat4x4& view_matrix, const Mat4x4& projection_matrix);
    bool is_rendering_complete() const { return m_segments_complete; }
    bool refine(const Mat4x4& view_matrix, const Mat4x4& projection_matrix);
    void render_shadow_casters(const Mat4x4& view_matrix, const Mat4x4& projection_matrix, const Vec3& light_position);
    void set_shadow_map(int texture_unit, const Mat4x4& light_view_projection, float intensity, float texel_size);
    void set_tone(float exposure, float saturation);
    void set_clipping_plane(const std::array<float, 4>& plane) { m_clipping_plane = plane; }
    void set_light_top_dir(const Vec3& direction) { m_light_top_dir = direction; }

    EViewType get_view_type() const { return m_settings.view_type; }
    void set_view_type(EViewType type);
    ENonplanarColor get_nonplanar_color() const { return m_settings.nonplanar_color; }
    void set_nonplanar_color(ENonplanarColor color);

    ETimeMode get_time_mode() const { return m_settings.time_mode; }
    void set_time_mode(ETimeMode mode);

    const Interval& get_layers_view_range() const { return m_layers.get_view_range(); }
    void set_layers_view_range(const Interval& range) { set_layers_view_range(range[0], range[1]); }
    void set_layers_view_range(Interval::value_type min, Interval::value_type max);

    bool is_top_layer_only_view_range() const { return m_settings.top_layer_only_view_range; }
    void toggle_top_layer_only_view_range();

    bool is_dim_previous_layers() const { return m_settings.dim_previous_layers; }
    void set_dim_previous_layers(bool value);
    float get_dim_previous_layers_brightness() const { return m_settings.dim_previous_layers_brightness; }
    void set_dim_previous_layers_brightness(float value);

    bool is_spiral_vase_mode() const { return m_settings.spiral_vase_mode; }

    std::vector<ETimeMode> get_time_modes() const;

    size_t get_layers_count() const { return m_layers.count(); }
    float get_layer_z(size_t layer_id) const { return m_layers.get_layer_z(layer_id); }
    std::vector<float> get_layers_zs() const { return m_layers.get_zs(); }

    size_t get_layer_id_at(float z) const { return m_layers.get_layer_id_at(z); }

    size_t get_used_extruders_count() const { return m_used_extruders.size(); }
    std::vector<uint8_t> get_used_extruders_ids() const;

    size_t get_color_prints_count(uint8_t extruder_id) const;
    std::vector<ColorPrint> get_color_prints(uint8_t extruder_id) const;

    AABox get_bounding_box(const std::vector<EMoveType>& types = {
        EMoveType::Retract, EMoveType::Unretract, EMoveType::Seam, EMoveType::ToolChange,
        EMoveType::ColorChange, EMoveType::PausePrint, EMoveType::CustomGCode, EMoveType::Travel,
        EMoveType::Wipe, EMoveType::Extrude }) const;
    AABox get_extrusion_bounding_box(const std::vector<EGCodeExtrusionRole>& roles = {
        EGCodeExtrusionRole::Perimeter, EGCodeExtrusionRole::ExternalPerimeter, EGCodeExtrusionRole::OverhangPerimeter,
        EGCodeExtrusionRole::InternalInfill, EGCodeExtrusionRole::SolidInfill, EGCodeExtrusionRole::TopSolidInfill,
        EGCodeExtrusionRole::Ironing, EGCodeExtrusionRole::BridgeInfill, EGCodeExtrusionRole::GapFill,
        EGCodeExtrusionRole::Skirt, EGCodeExtrusionRole::SupportMaterial, EGCodeExtrusionRole::SupportMaterialInterface,
        EGCodeExtrusionRole::WipeTower, EGCodeExtrusionRole::Custom,
        EGCodeExtrusionRole::BottomSurface, EGCodeExtrusionRole::InternalBridgeInfill, EGCodeExtrusionRole::Brim,
        EGCodeExtrusionRole::SupportTransition, EGCodeExtrusionRole::Mixed
    }) const;

    bool is_option_visible(EOptionType type) const;
    void toggle_option_visibility(EOptionType type);

    bool is_extrusion_role_visible(EGCodeExtrusionRole role) const;
    void toggle_extrusion_role_visibility(EGCodeExtrusionRole role);

    const Interval& get_view_full_range() const { return m_view_range.get_full(); }
    const Interval& get_view_enabled_range() const { return m_view_range.get_enabled(); }
    const Interval& get_view_visible_range() const { return m_view_range.get_visible(); }
    void set_view_visible_range(Interval::value_type min, Interval::value_type max);

    size_t get_vertices_count() const { return m_vertices.size(); }
    const PathVertex& get_current_vertex() const { return get_vertex_at(get_current_vertex_id()); }
    size_t get_current_vertex_id() const { return static_cast<size_t>(m_view_range.get_visible()[1]); }
    const PathVertex& get_vertex_at(size_t id) const {
        return (id < m_vertices.size()) ? m_vertices[id] : PathVertex::DUMMY_PATH_VERTEX;
    }
    float get_estimated_time() const { return m_total_time[static_cast<size_t>(m_settings.time_mode)]; }
    float get_estimated_time_at(size_t id) const;
    Color get_vertex_color(const PathVertex& vertex) const;

    size_t get_extrusion_roles_count() const { return m_extrusion_roles.get_roles_count(); }
    std::vector<EGCodeExtrusionRole> get_extrusion_roles() const { return m_extrusion_roles.get_roles(); }
    float get_extrusion_role_estimated_time(EGCodeExtrusionRole role) const { return m_extrusion_roles.get_time(role, m_settings.time_mode); }

    size_t get_options_count() const { return m_options.size(); }
    const std::vector<EOptionType>& get_options() const { return m_options; }

    float get_travels_estimated_time() const { return m_travels_time[static_cast<size_t>(m_settings.time_mode)]; }
    std::vector<float> get_layers_estimated_times() const { return m_layers.get_times(m_settings.time_mode); }

    size_t get_tool_colors_count() const { return m_tool_colors.size(); }
    const Palette& get_tool_colors() const { return m_tool_colors; }
    void set_tool_colors(const Palette& colors);

    size_t get_color_print_colors_count() const { return m_color_print_colors.size(); }
    const Palette& get_color_print_colors() const { return m_color_print_colors; }
    void set_color_print_colors(const Palette& colors);

    const Color& get_extrusion_role_color(EGCodeExtrusionRole role) const;
    void set_extrusion_role_color(EGCodeExtrusionRole role, const Color& color);
    void reset_default_extrusion_roles_colors();

    const Color& get_option_color(EOptionType type) const;
    void set_option_color(EOptionType type, const Color& color);
    void reset_default_options_colors();

    const ColorRange& get_color_range(EViewType type) const;
    void set_color_range_palette(EViewType type, const Palette& palette);

    float get_travels_radius() const { return m_travels_radius; }
    void set_travels_radius(float radius);
    float get_wipes_radius() const { return m_wipes_radius; }
    void set_wipes_radius(float radius);

    size_t get_used_cpu_memory() const;
    size_t get_used_gpu_memory() const;

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    Vec3 get_cog_marker_position() const { return m_cog_marker.get_position(); }

    float get_cog_marker_scale_factor() const { return m_cog_marker_scale_factor; }
    void set_cog_marker_scale_factor(float factor) { m_cog_marker_scale_factor = std::max(factor, 0.001f); }

    const Vec3& get_tool_marker_position() const { return m_tool_marker.get_position(); }

    float get_tool_marker_offset_z() const { return m_tool_marker.get_offset_z(); }
    void set_tool_marker_offset_z(float offset_z) { m_tool_marker.set_offset_z(offset_z); }

    float get_tool_marker_scale_factor() const { return m_tool_marker_scale_factor; }
    void set_tool_marker_scale_factor(float factor) { m_tool_marker_scale_factor = std::max(factor, 0.001f); }

    const Color& get_tool_marker_color() const { return m_tool_marker.get_color(); }
    void set_tool_marker_color(const Color& color) { m_tool_marker.set_color(color); }

    float get_tool_marker_alpha() const { return m_tool_marker.get_alpha(); }
    void set_tool_marker_alpha(float alpha) { m_tool_marker.set_alpha(alpha); }
#endif

private:
    struct LayerColors
    {
        std::array<int, 2> lit_layers{ -1, -1 };
        int grey_below_layer{ 0 };
        float dim_brightness{ -1.0f };
        int kept_vertex{ -1 };
        bool operator == (const LayerColors& other) const {
            return lit_layers == other.lit_layers && grey_below_layer == other.grey_below_layer &&
                   dim_brightness == other.dim_brightness && kept_vertex == other.kept_vertex;
        }
    };
    Settings m_settings;
    Layers m_layers;
    ExtrusionRoles m_extrusion_roles;
    std::vector<EOptionType> m_options;
    std::map<uint8_t, std::vector<ColorPrint>> m_used_extruders;
    ViewRange m_view_range;
    std::array<float, TIME_MODES_COUNT> m_total_time{ 0.0f, 0.0f };
    std::array<std::vector<float>, TIME_MODES_COUNT> m_layer_start_times;
    std::vector<uint32_t> m_layer_first_vertex;
    bool m_layers_in_vertex_order{ false };
    std::array<float, TIME_MODES_COUNT> m_travels_time{ 0.0f, 0.0f };
    float m_travels_radius{ DEFAULT_TRAVELS_RADIUS_MM };
    float m_wipes_radius{ DEFAULT_WIPES_RADIUS_MM };
    std::array<Color, size_t(EGCodeExtrusionRole::COUNT)> m_extrusion_roles_colors;
    std::array<Color, size_t(EOptionType::COUNT)> m_options_colors;

    bool m_initialized{ false };

    SegmentTemplate m_segment_template;
    OptionTemplate m_option_template;
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    CogMarker m_cog_marker;
    float m_cog_marker_scale_factor{ 1.0f };
    ToolMarker m_tool_marker;
    float m_tool_marker_scale_factor{ 1.0f };
#endif
    std::vector<PathVertex> m_vertices;

    std::vector<float> m_vertices_colors;
    std::vector<uint8_t> m_lod_step;
    float m_segments_margin{ 0.0f };
    bool m_segments_complete{ true };
    bool m_refining{ false };
    bool m_refined{ false };

    BitSet<> m_valid_lines_bitset;
    std::optional<Settings> m_settings_used_for_ranges;
    float m_clearance_margin{ 0.3f };
    float m_clearance_head_floor{ 5.f };
    ColorRange m_height_range;
    ColorRange m_width_range;
    ColorRange m_speed_range;
    ColorRange m_actual_speed_range;
    ColorRange m_fan_speed_range;
    ColorRange m_temperature_range;
    ColorRange m_pressure_advance_range;
    ColorRange m_acceleration_range;
    ColorRange m_jerk_range;
    ColorRange m_volumetric_rate_range;
    ColorRange m_actual_volumetric_rate_range;
    ColorRange m_shell_range;
    ColorRange m_bead_height_range;
    ColorRange m_slope_range;
    std::array<ColorRange, COLOR_RANGE_TYPES_COUNT> m_layer_time_range{
        ColorRange(EColorRangeType::Linear), ColorRange(EColorRangeType::Logarithmic)
    };
    Palette m_tool_colors;
    Palette m_color_print_colors;
    unsigned int m_segments_shader_id{ 0 };
    unsigned int m_segments_caster_shader_id{ 0 };
    unsigned int m_segments_caster_vao_id{ 0 };
    unsigned int m_options_shader_id{ 0 };
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    unsigned int m_cog_marker_shader_id{ 0 };
    unsigned int m_tool_marker_shader_id{ 0 };
#endif
    struct SegmentsUniforms
    {
        int view_matrix{ -1 };
        int projection_matrix{ -1 };
        int camera_position{ -1 };
        int positions_tex{ -1 };
        int height_width_angle_tex{ -1 };
        int colors_tex{ -1 };
        int segment_index_tex{ -1 };
        int reverse_order{ -1 };
        int instances_count{ -1 };
        int first_instance{ -1 };
        int lod_segment_tex{ -1 };
        int lod{ -1 };
        int flip_y{ -1 };
        int clipping_plane{ -1 };

        void init(unsigned int shader_id);
    };
    SegmentsUniforms m_uni_segments;
    SegmentsUniforms m_uni_segments_caster;
    int m_uni_segments_shadow_map_id{ -1 };
    int m_uni_segments_shadow_light_vp_id{ -1 };
    int m_uni_segments_shadow_intensity_id{ -1 };
    int m_uni_segments_shadow_map_texel_id{ -1 };
    int m_uni_segments_exposure_id{ -1 };
    int m_uni_segments_saturation_id{ -1 };
    int m_uni_segments_light_top_dir_id{ -1 };
    struct LayerColorsUniforms
    {
        int lit_layers{ -1 };
        int grey_below_layer{ -1 };
        int dim_brightness{ -1 };
        int kept_vertex{ -1 };

        void init(unsigned int shader_id);
    };
    LayerColorsUniforms m_uni_segments_layer_colors;
    int m_uni_options_view_matrix_id{ -1 };
    int m_uni_options_projection_matrix_id{ -1 };
    int m_uni_options_positions_tex_id{ -1 };
    int m_uni_options_height_width_angle_tex_id{ -1 };
    int m_uni_options_colors_tex_id{ -1 };
    int m_uni_options_segment_index_tex_id{ -1 };
    int m_uni_options_clipping_plane_id{ -1 };
    LayerColorsUniforms m_uni_options_layer_colors;
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    int m_uni_cog_marker_world_center_position{ -1 };
    int m_uni_cog_marker_scale_factor{ -1 };
    int m_uni_cog_marker_view_matrix{ -1 };
    int m_uni_cog_marker_projection_matrix{ -1 };
    int m_uni_tool_marker_world_origin{ -1 };
    int m_uni_tool_marker_scale_factor{ -1 };
    int m_uni_tool_marker_view_matrix{ -1 };
    int m_uni_tool_marker_projection_matrix{ -1 };
    int m_uni_tool_marker_color_base{ -1 };
#endif

#ifdef ENABLE_OPENGL_ES
    class TextureData
    {
    public:
        void init(size_t vertices_count);
        void set_positions(const std::vector<Vec4>& positions);
        void set_heights_widths_angles(const std::vector<Vec4>& heights_widths_angles);
        void set_colors(const std::vector<float>& colors);
        void set_enabled_segments(const std::vector<uint32_t>& enabled_segments);
        void set_enabled_options(const std::vector<uint32_t>& enabled_options);
        void reset();
        size_t get_count() const { return m_count; }
        std::pair<unsigned int, size_t> get_positions_tex_id(size_t id) const;
        std::pair<unsigned int, size_t> get_heights_widths_angles_tex_id(size_t id) const;
        std::pair<unsigned int, size_t> get_colors_tex_id(size_t id) const;
        std::pair<unsigned int, size_t> get_enabled_segments_tex_id(size_t id) const;
        std::pair<unsigned int, size_t> get_enabled_options_tex_id(size_t id) const;

        size_t get_enabled_segments_count() const;
        size_t get_enabled_options_count() const;

        size_t max_texture_capacity() const { return m_width * m_height; }
        size_t get_used_gpu_memory() const;

    private:
        size_t m_width{ 0 };
        size_t m_height{ 0 };
        size_t m_count{ 0 };
        size_t m_positions_size{ 0 };
        size_t m_height_width_angle_size{ 0 };
        size_t m_colors_size{ 0 };
        size_t m_enabled_segments_size{ 0 };
        size_t m_enabled_options_size{ 0 };

        struct TexIds
        {
            std::pair<unsigned int, size_t> positions{ 0, 0 };
            std::pair<unsigned int, size_t> heights_widths_angles{ 0, 0 };
            std::pair<unsigned int, size_t> colors{ 0, 0 };
            std::pair<unsigned int, size_t> enabled_segments{ 0, 0 };
            std::pair<unsigned int, size_t> enabled_options{ 0, 0 };
        };

        std::vector<TexIds> m_tex_ids;
    };

    TextureData m_texture_data;
#else
    unsigned int m_positions_buf_id{ 0 };
    unsigned int m_positions_tex_id{ 0 };
    unsigned int m_heights_widths_angles_buf_id{ 0 };
    unsigned int m_heights_widths_angles_tex_id{ 0 };
    unsigned int m_colors_buf_id{ 0 };
    unsigned int m_colors_tex_id{ 0 };
    unsigned int m_enabled_segments_buf_id{ 0 };
    unsigned int m_enabled_segments_tex_id{ 0 };
    size_t m_enabled_segments_count{ 0 };
    std::vector<uint32_t> m_enabled_segments;
    struct SegmentsBlock
    {
        AABox box;
        uint32_t lod_first{ 0 };
        uint32_t lod_count{ 0 };
    };
    std::vector<SegmentsBlock> m_segments_blocks;
    bool m_segments_blocks_dirty{ true };
    struct SegmentsDrawKey
    {
        Mat4x4 view_matrix;
        Mat4x4 projection_matrix;
        std::array<int, 4> viewport;
        uint64_t version;
        std::array<float, 4> clipping_plane;
        LayerColors layer_colors;
        int shadow_map_texture_unit;
        Mat4x4 shadow_light_vp;
        float shadow_intensity;
        float shadow_map_texel;
        float exposure;
        float saturation;
        Vec3 light_top_dir;
        bool operator == (const SegmentsDrawKey& other) const {
            return view_matrix == other.view_matrix && projection_matrix == other.projection_matrix && viewport == other.viewport &&
                   version == other.version && clipping_plane == other.clipping_plane && layer_colors == other.layer_colors &&
                   shadow_map_texture_unit == other.shadow_map_texture_unit && shadow_light_vp == other.shadow_light_vp &&
                   shadow_intensity == other.shadow_intensity && shadow_map_texel == other.shadow_map_texel &&
                   exposure == other.exposure && saturation == other.saturation && light_top_dir == other.light_top_dir;
        }
    };
    struct SegmentsDraw
    {
        size_t block;
        size_t first;
        size_t count;
        bool lod;
    };
    SegmentsLayer m_segments_layer;
    std::optional<SegmentsDrawKey> m_segments_key;
    std::vector<SegmentsDraw> m_segments_draws;
    size_t m_segments_next_draw{ 0 };
    bool m_segments_layered{ false };
    std::array<int, 4> m_segments_rect{ 0, 0, 0, 0 };
    float m_segment_draw_ms{ 0.0f };
    uint64_t m_segments_version{ 0 };
    unsigned int m_lod_segments_buf_id{ 0 };
    unsigned int m_lod_segments_tex_id{ 0 };
    unsigned int m_enabled_options_buf_id{ 0 };
    unsigned int m_enabled_options_tex_id{ 0 };
    size_t m_enabled_options_count{ 0 };
    size_t m_positions_tex_size{ 0 };
    size_t m_height_width_angle_tex_size{ 0 };
    size_t m_colors_tex_size{ 0 };
    size_t m_enabled_segments_tex_size{ 0 };
    size_t m_lod_segments_tex_size{ 0 };
    size_t m_enabled_options_tex_size{ 0 };
#endif
    int    m_shadow_map_texture_unit{ 4 };
    Mat4x4 m_shadow_light_vp{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    float  m_shadow_intensity{ 0.0f };
    float  m_shadow_map_texel{ 0.0f };
    bool   m_rendering_shadow_casters{ false };

    float m_exposure{ 1.0f };
    float m_saturation{ 1.0f };
    Vec3 m_light_top_dir{ -0.4574957f, 0.4574957f, 0.7624929f };

    std::array<float, 4> m_clipping_plane{ 0.0f, 0.0f, 0.0f, 1.0f };

    void apply_pending_updates();
    void update_view_full_range();
    void update_color_ranges();
    void update_heights_widths();
    void update_lod_steps();
#ifndef ENABLE_OPENGL_ES
    void update_segments_blocks();
#endif
    void render_segments(const Mat4x4& view_matrix, const Mat4x4& projection_matrix, const Vec3& camera_position);
    void render_options(const Mat4x4& view_matrix, const Mat4x4& projection_matrix);
    LayerColors layer_colors() const;
    void set_layer_colors(const LayerColorsUniforms& uni) const;
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    void render_cog_marker(const Mat4x4& view_matrix, const Mat4x4& projection_matrix);
    void render_tool_marker(const Mat4x4& view_matrix, const Mat4x4& projection_matrix);
#endif
};

}

#endif