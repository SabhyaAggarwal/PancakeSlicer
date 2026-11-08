///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/ libvgcode is released under the terms of the AGPLv3 or higher
#ifndef VGCODE_VIEWER_HPP
#define VGCODE_VIEWER_HPP

#include "Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace libvgcode {

class ViewerImpl;
struct GCodeInputData;
struct PathVertex;
class ColorRange;
struct ColorPrint;

class Viewer
{
public:
    Viewer();
    ~Viewer();
    Viewer(const Viewer& other) = delete;
    Viewer(Viewer&& other) = delete;
    Viewer& operator = (const Viewer& other) = delete;
    Viewer& operator = (Viewer&& other) = delete;

    void init(const std::string& opengl_context_version);
    void shutdown();
    void reset();
    void load(GCodeInputData&& gcode_data);
    void render(const Mat4x4& view_matrix, const Mat4x4& projection_matrix);
    bool is_rendering_complete() const;
    bool refine(const Mat4x4& view_matrix, const Mat4x4& projection_matrix);
    void render_shadow_casters(const Mat4x4& view_matrix, const Mat4x4& projection_matrix, const Vec3& light_position);
    void set_shadow_map(int texture_unit, const Mat4x4& light_view_projection, float intensity, float texel_size);
    void set_tone(float exposure, float saturation);
    void set_clipping_plane(const std::array<float, 4>& plane);
    void set_light_top_dir(const Vec3& direction);

    EViewType get_view_type() const;
    void set_view_type(EViewType type);
    ENonplanarColor get_nonplanar_color() const;
    void set_nonplanar_color(ENonplanarColor color);
    ETimeMode get_time_mode() const;
    void set_time_mode(ETimeMode mode);
    bool is_top_layer_only_view_range() const;
    void toggle_top_layer_only_view_range();
    bool is_dim_previous_layers() const;
    void set_dim_previous_layers(bool value);
    float get_dim_previous_layers_brightness() const;
    void set_dim_previous_layers_brightness(float value);
    bool is_option_visible(EOptionType type) const;
    void toggle_option_visibility(EOptionType type);
    bool is_extrusion_role_visible(EGCodeExtrusionRole role) const;
    void toggle_extrusion_role_visibility(EGCodeExtrusionRole role);
    const Color& get_extrusion_role_color(EGCodeExtrusionRole role) const;
    void set_extrusion_role_color(EGCodeExtrusionRole role, const Color& color);
    void reset_default_extrusion_roles_colors();
    const Color& get_option_color(EOptionType type) const;
    void set_option_color(EOptionType type, const Color& color);
    void reset_default_options_colors();
    size_t get_tool_colors_count() const;
    const Palette& get_tool_colors() const;
    void set_tool_colors(const Palette& colors);
    size_t get_color_print_colors_count() const;
    const Palette& get_color_print_colors() const;
    void set_color_print_colors(const Palette& colors);
    const ColorRange& get_color_range(EViewType type) const;
    void set_color_range_palette(EViewType type, const Palette& palette);
    float get_travels_radius() const;
    void set_travels_radius(float radius);
    float get_wipes_radius() const;
    void set_wipes_radius(float radius);
    size_t get_layers_count() const;
    const Interval& get_layers_view_range() const;
    void set_layers_view_range(const Interval& range);
    void set_layers_view_range(Interval::value_type min, Interval::value_type max);
    const Interval& get_view_visible_range() const;
    void set_view_visible_range(Interval::value_type min, Interval::value_type max);
    const Interval& get_view_full_range() const;
    const Interval& get_view_enabled_range() const;

    bool is_spiral_vase_mode() const;
    float get_layer_z(size_t layer_id) const;
    std::vector<float> get_layers_zs() const;
    size_t get_layer_id_at(float z) const;
    size_t get_used_extruders_count() const;
    std::vector<uint8_t> get_used_extruders_ids() const;
    std::vector<ETimeMode> get_time_modes() const;
    size_t get_vertices_count() const;
    const PathVertex& get_current_vertex() const;
    size_t get_current_vertex_id() const;
    const PathVertex& get_vertex_at(size_t id) const;
    float get_estimated_time() const;
    float get_estimated_time_at(size_t id) const;
    Color get_vertex_color(const PathVertex& vertex) const;
    size_t get_extrusion_roles_count() const;
    std::vector<EGCodeExtrusionRole> get_extrusion_roles() const;
    size_t get_options_count() const;
    const std::vector<EOptionType>& get_options() const;
    size_t get_color_prints_count(uint8_t extruder_id) const;
    std::vector<ColorPrint> get_color_prints(uint8_t extruder_id) const;
    float get_extrusion_role_estimated_time(EGCodeExtrusionRole role) const;
    float get_travels_estimated_time() const;
    std::vector<float> get_layers_estimated_times() const;
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
    size_t get_used_cpu_memory() const;
    size_t get_used_gpu_memory() const;

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    Vec3 get_cog_position() const;

    float get_cog_marker_scale_factor() const;
    void set_cog_marker_scale_factor(float factor);

    const Vec3& get_tool_marker_position() const;

    float get_tool_marker_offset_z() const;
    void set_tool_marker_offset_z(float offset_z);

    float get_tool_marker_scale_factor() const;
    void set_tool_marker_scale_factor(float factor);

    const Color& get_tool_marker_color() const;
    void set_tool_marker_color(const Color& color);

    float get_tool_marker_alpha() const;
    void set_tool_marker_alpha(float alpha);
#endif

private:
    ViewerImpl* m_impl{ nullptr };
};

}

#endif
