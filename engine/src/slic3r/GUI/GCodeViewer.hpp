#ifndef slic3r_GCodeViewer_hpp_
#define slic3r_GCodeViewer_hpp_

#include "3DScene.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/ClearanceVerifier.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/Nonplanar/Report.hpp"
#include "IMSlider.hpp"
#include "GLModel.hpp"
#include "I18N.hpp"

#include <algorithm>
#include <boost/iostreams/device/mapped_file.hpp>

#include "LibVGCode/LibVGCodeWrapper.hpp"
#include "libslic3r/Technologies.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/BoundingBox.hpp"
#include <cstddef>
#include "libslic3r/Config.hpp"
#include "libvgcode/include/GCodeInputData.hpp"
#include <iterator>
#include "libvgcode/include/PathVertex.hpp"
#include <libvgcode/include/Types.hpp>

#include <array>
#include <memory>
#include <cstdint>
#include <float.h>
#include <set>
#include "slic3r/GUI/MeshUtils.hpp"
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Slic3r {

class Print;
class TriangleMesh;
class PresetBundle;

namespace GUI {

class PartPlateList;
class OpenGLManager;

static const float GCODE_VIEWER_SLIDER_SCALE = 0.6f;
static const float SLIDER_DEFAULT_RIGHT_MARGIN  = 10.0f;
static const float SLIDER_DEFAULT_BOTTOM_MARGIN = 10.0f;
static inline const float SLIDER_RIGHT_MARGIN = IMSlider::vertical_slider_window_width();
static const float SLIDER_BOTTOM_MARGIN = 64.0f;
class GCodeViewer
{
public:
    enum class EViewType : unsigned char;
    struct SequentialView
    {
#if ENABLE_ACTUAL_SPEED_DEBUG
        struct ActualSpeedImguiWidget
        {
            std::pair<float, float> y_range = { 0.0f, 0.0f };
            std::vector<std::pair<float, ColorRGBA>> levels;
            struct Item
            {
              float pos{ 0.0f };
              float speed{ 0.0f };
              bool internal{ false };
            };
            std::vector<Item> data;
            int plot(const char* label, const std::array<float, 2>& frame_size = { 0.0f, 0.0f });
        };
#endif

        class Marker
        {
            GLModel m_model;
            Vec3f m_world_position;
            Vec3f m_world_offset;
            float m_z_offset{ 0.0f };
            float m_model_z_offset{ 0.5f };
            bool m_visible{ true };
            bool m_is_dark = false;
            float m_scale_factor{ 1.0f };
#if ENABLE_ACTUAL_SPEED_DEBUG
            ActualSpeedImguiWidget m_actual_speed_imgui_widget;
#endif

        public:
            float m_scale = 1.0f;

            void init(std::string filename);

            const BoundingBoxf3& get_bounding_box() const { return m_model.get_bounding_box(); }

            void set_world_position(const Vec3f& position) { m_world_position = position; }
            void set_world_offset(const Vec3f& offset) { m_world_offset = offset; }
            void set_z_offset(float z_offset) { m_z_offset = z_offset; }

#if ENABLE_ACTUAL_SPEED_DEBUG
            void set_actual_speed_y_range(const std::pair<float, float>& y_range) {
                m_actual_speed_imgui_widget.y_range = y_range;
            }
            void set_actual_speed_levels(const std::vector<std::pair<float, ColorRGBA>>& levels) {
                m_actual_speed_imgui_widget.levels = levels;
            }
            void set_actual_speed_data(const std::vector<ActualSpeedImguiWidget::Item>& data) {
                m_actual_speed_imgui_widget.data = data;
            }
#endif

            bool is_visible() const { return m_visible; }
            void set_visible(bool visible) { m_visible = visible; }
            static ColorRGBA default_color() { return { 1.0f, 1.0f, 1.0f, 0.5f }; }
            void set_color(const ColorRGBA& color) { m_model.set_color(color); }

            void render(int canvas_width, int canvas_height, const libvgcode::EViewType& view_type);
            void render_position_window(const libvgcode::Viewer* viewer, int canvas_width, int canvas_height, const libvgcode::EViewType& view_type);
            void on_change_color_mode(bool is_dark) { m_is_dark = is_dark; }
        };

        class GCodeWindow
        {
            struct Line
            {
                std::string command;
                std::string parameters;
                std::string comment;
            };
            bool m_is_dark = false;
            uint64_t m_selected_line_id{ 0 };
            size_t m_last_lines_size{ 0 };
            std::string m_filename;
            boost::iostreams::mapped_file_source m_file;
            std::vector<size_t> m_lines_ends;
            std::vector<Line> m_lines;

        public:
            float m_scale = 1.0f;
            GCodeWindow() = default;
            ~GCodeWindow() { stop_mapping_file(); }
            void load_gcode(const std::string& filename, const std::vector<size_t> &lines_ends);
            void reset() {
                stop_mapping_file();
                m_lines_ends.clear();
                m_lines_ends.shrink_to_fit();
                m_lines.clear();
                m_lines.shrink_to_fit();
                m_filename.clear();
                m_filename.shrink_to_fit();
            }

            void render(float top, float bottom, float right, uint64_t curr_line_id) const;
            void on_change_color_mode(bool is_dark) { m_is_dark = is_dark; }

            void stop_mapping_file();
        };

        Marker marker;
        std::vector<Marker> m_imex_secondary_markers;
        GCodeWindow gcode_window;
        float m_scale = 1.0;
        bool m_show_marker = false;
        void render_marker(const bool has_render_path, int canvas_width, int canvas_height, const libvgcode::EViewType& view_type);
        void render_overlay(const bool has_render_path, float legend_height, const libvgcode::Viewer* viewer, uint32_t gcode_id, int canvas_width, int canvas_height, int right_margin, const libvgcode::EViewType& view_type);
    };
    struct ExtruderFilament
    {
        std::string   type;
        std::string   hex_color;
        unsigned char filament_id;
        bool is_support_filament;
    };
    struct Shells
    {
        GLVolumeCollection volumes;
        bool               visible{false};
        int  print_id{-1};
        int  print_modify_count{-1};
        bool previewing{false};
    };
    ConflictResultOpt m_conflict_result;
    GCodeCheckResult  m_gcode_check_result;
    FilamentPrintableResult filament_printable_reuslt;
    Shells            m_shells;

private:
    std::vector<int> m_plater_extruder;
    bool m_gl_data_initialized{ false };
    unsigned int m_last_result_id{ 0 };
    bool m_last_belt_show_designed{ true };
    std::vector<double> m_belt_layer_zs;
    const GCodeProcessorResult* m_gcode_result;
    std::array<unsigned int, static_cast<size_t>(EMoveType::Count)> m_move_type_counts{};
    std::array<std::array<float, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)>, static_cast<size_t>(EMoveType::Count)> m_move_type_times{};
    std::array<float, static_cast<size_t>(EMoveType::Count)> m_move_type_distances{};
    bool m_only_gcode_in_preview {false};

    std::vector<ExtruderFilament> m_left_extruder_filament;
    std::vector<ExtruderFilament> m_right_extruder_filament;
    size_t m_nozzle_nums;

    BoundingBoxf3 m_paths_bounding_box;
    BoundingBoxf3 m_max_bounding_box;
    BoundingBoxf3 m_shell_bounding_box;
    float m_max_print_height{ 0.0f };
    bool  m_machine_frame_transform_active{ false };
    float m_z_offset{ 0.0f };

    ConfigOptionMode m_user_mode;
    bool m_fold = {false};
    std::string m_marker_filename;
    struct ImexMarkerPlan
    {
        struct Carriage
        {
            int   phys_head    = -1;
            bool  mirror_x     = false;
            bool  mirror_y     = false;
            float x_term       = 0.0f;
            float y_term       = 0.0f;
            float box_offset_x = 0.0f;
            float box_offset_y = 0.0f;
        };
        std::vector<Carriage> carriages;
        int   pri_head = -1;
        float pri_box_offset_x = 0.0f;
        float pri_box_offset_y = 0.0f;
        float box_wx = 0.0f;
        float box_wy = 0.0f;
    };

    struct ImexMarkerKey
    {
        struct Scalars
        {
            int    plate_index      = -1;
            int    gantry_count     = 0;
            int    tools_per_gantry = 0;
            int    tool_layout      = -1;
            double clearance_x      = 0.0;
            double clearance_y      = 0.0;
            bool   firmware_managed = false;
            double bed_min_x = 0.0, bed_min_y = 0.0, bed_max_x = 0.0, bed_max_y = 0.0;

            auto tied() const {
                return std::tie(plate_index, gantry_count, tools_per_gantry, tool_layout,
                                clearance_x, clearance_y, firmware_managed,
                                bed_min_x, bed_min_y, bed_max_x, bed_max_y);
            }
            bool operator==(const Scalars& rhs) const { return tied() == rhs.tied(); }
        };
        Scalars s;
        std::string              mode;
        std::vector<std::string> mode_names;
        std::vector<std::string> mode_active_tools;
    };
    ImexMarkerKey  m_imex_marker_key;
    ImexMarkerPlan m_imex_marker_plan;
    static ImexMarkerPlan resolve_imex_marker_plan(const DynamicPrintConfig& printer_cfg,
                                                   const std::string&        mode,
                                                   const BoundingBoxf&       bed_extents);

    GLModel     m_imex_toolhead_box;
    float       m_imex_box_mesh_wx{ 0.0f };
    float       m_imex_box_mesh_wy{ 0.0f };

    size_t m_extruders_count;
    std::vector<float> m_filament_diameters;
    std::vector<float> m_filament_densities;
    SequentialView m_sequential_view;
    ClearanceReport m_clearance_report;
    float m_clearance_margin{ 0.3f };
    float m_clearance_head_floor{ 5.f };
    std::vector<uint32_t> m_clearance_move_vertices;
    std::string m_clearance_warning;
    void render_clearance_markers(int canvas_width, int canvas_height);
    int m_nonplanar_shells{ 0 };
    std::vector<int> m_nonplanar_bands;
    Nonplanar::NonplanarReport m_nonplanar_report;
    struct NonplanarHoverGrid
    {
        struct Entry
        {
            uint32_t id;
            float    x;
            float    y;
            double   depth;
        };
        struct Layer
        {
            std::vector<uint32_t> vertices;
            std::vector<Entry> projected;
            bool ready{ false };
        };
        std::vector<Layer> by_layer;
        Matrix4d                         projection{ Matrix4d::Zero() };
        std::array<int, 2>               size{ 0, 0 };
        std::array<uint32_t, 2>          range{ 0, 0 };
        std::array<uint32_t, 2>          layers{ 0, 0 };
        std::vector<bool>                roles;
        float                            cell{ 0.f };
        int                              columns{ 0 };
        int                              rows{ 0 };
        std::vector<uint32_t>            cell_start;
        std::vector<Entry>               entries;
    };
    NonplanarHoverGrid m_nonplanar_hover_grid;
    std::vector<float>    m_nonplanar_fallback_z;
    int  m_nonplanar_selected{ -1 };
    bool m_nonplanar_show_fallbacks{ true };
    std::vector<std::unique_ptr<GLModel>> m_nonplanar_hatch;
    bool m_nonplanar_hatch_dirty{ false };
    void render_nonplanar_hatch();
    void load_nonplanar_report(const GCodeProcessorResult &gcode_result, const libvgcode::GCodeInputData &data);
    void render_nonplanar_explain(int canvas_width, int canvas_height);
    void render_nonplanar_legend();

    IMSlider* m_moves_slider;
    IMSlider* m_layers_slider;
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    bool m_cog_marker_fixed_screen_size{ true };
    float m_cog_marker_size{ 1.0f };
    bool m_tool_marker_fixed_screen_size{ false };
    float m_tool_marker_size{ 1.0f };
#endif
    std::vector<libvgcode::EViewType> view_type_items;
    std::vector<std::string> view_type_items_str;
    int       m_view_type_sel = 0;
    std::string m_applied_default_view_type_key;
    std::vector<EMoveType> options_items;

    bool m_legend_visible{ true };
    bool m_legend_enabled{ true };

    float m_legend_height;
    PrintEstimatedStatistics m_print_statistics;
    std::array<float, 2> m_detected_point_sizes = { 0.0f, 0.0f };
    GCodeProcessorResult::SettingsIds m_settings_ids;

    std::vector<CustomGCode::Item> m_custom_gcode_per_print_z;
    GCodeProcessorResult::ObjectMass              m_plate_mass;
    std::vector<GCodeProcessorResult::ObjectMass> m_object_masses;
    std::vector<GCodeProcessorResult::ObjectMass> m_body_masses;
    std::vector<GCodeProcessorResult::ObjectMass> m_support_masses;

    bool m_contained_in_bed{ true };
mutable bool m_no_render_path { false };
    bool m_is_dark = false;

    bool  m_belt_view_enabled = false;
    bool  m_belt_show_designed = true;
    float m_belt_angle_deg = 0.f;

    libvgcode::Viewer m_viewer;
    std::array<float, 4> m_clipping_plane{ 0.0f, 0.0f, 0.0f, 1.0f };
    bool m_loaded_as_preview{ false };

public:
    GCodeViewer();
    ~GCodeViewer();

    void on_change_color_mode(bool is_dark);
    float m_scale = 1.0;
    void set_scale(float scale = 1.0);
    void init(ConfigOptionMode mode, Slic3r::PresetBundle* preset_bundle);
    void update_by_mode(ConfigOptionMode mode);

    void load_as_gcode(const GCodeProcessorResult& gcode_result, const Print& print, const std::vector<std::string>& str_tool_colors,
        const std::vector<std::string>& str_color_print_colors, const BuildVolume& build_volume,
        const std::vector<BoundingBoxf3>& exclude_bounding_box, ConfigOptionMode mode, bool only_gcode = false);
    void load_as_preview(libvgcode::GCodeInputData&& data);
    void update_shells_color_by_extruder(const DynamicPrintConfig* config);
    void set_shell_transparency(float alpha = 0.15f);

    void reset();
    void reset_shell();
    void load_shells(const Print& print, bool initialized, bool force_previewing = false);
    void set_shells_on_preview(bool is_previewing) { m_shells.previewing = is_previewing; }
    void render_all_plates_stats(const std::vector<const GCodeProcessorResult*>& gcode_result_list, bool show = true) const;
    void render_scene(int canvas_width, int canvas_height);
    void render_overlay(int canvas_width, int canvas_height, int right_margin);
    void render_shadow_casters(const Transform3d& light_view_matrix, const Transform3d& light_projection_matrix, const Vec3d& light_position);
    void set_shadow_map(int texture_unit, const Transform3d& light_view_projection, float intensity, float texel_size);
    void set_tone(float exposure, float saturation);
    void set_clipping_plane(const ClippingPlane& plane);
    void set_light_top_dir(const Vec3d& direction);
    bool has_data() const { return !m_viewer.get_extrusion_roles().empty(); }
    bool is_rendering_complete() const { return m_viewer.is_rendering_complete(); }
    bool refine_toolpaths();

    bool can_export_toolpaths() const;
    std::vector<int> get_plater_extruder();

    const float                get_max_print_height() const { return m_max_print_height; }
    bool                       is_machine_frame_transform_active() const { return m_machine_frame_transform_active; }
    const BoundingBoxf3& get_paths_bounding_box() const { return m_paths_bounding_box; }
    const BoundingBoxf3& get_max_bounding_box() const { return m_max_bounding_box; }
    const BoundingBoxf3& get_shell_bounding_box() const { return m_shell_bounding_box; }
    std::vector<double> get_layers_zs() const {
        if (! m_belt_layer_zs.empty())
            return m_belt_layer_zs;
        const std::vector<float> zs = m_viewer.get_layers_zs();
        std::vector<double> ret;
        std::transform(zs.begin(), zs.end(), std::back_inserter(ret), [](float z) { return static_cast<double>(z); });
        return ret;
    }
    std::vector<float> get_layers_times() const { return m_viewer.get_layers_estimated_times(); }

    const std::array<size_t,2> &get_layers_z_range() const { return m_viewer.get_layers_view_range(); }
    const GCodeProcessorResult::ObjectMass&              get_plate_mass() const { return m_plate_mass; }
    const std::vector<GCodeProcessorResult::ObjectMass>& get_object_masses() const { return m_object_masses; }
    const std::vector<GCodeProcessorResult::ObjectMass>& get_body_masses() const { return m_body_masses; }
    const std::vector<GCodeProcessorResult::ObjectMass>& get_support_masses() const { return m_support_masses; }
    size_t get_vertices_count() const { return m_viewer.get_vertices_count(); }
    size_t get_layers_count() const { return m_viewer.get_layers_count(); }
    size_t shadow_casters_signature() const;

    const SequentialView& get_sequential_view() const { return m_sequential_view; }
    void update_sequential_view_current(unsigned int first, unsigned int last);
    bool jump_to_clearance_move(size_t move_id, unsigned int result_id);

    IMSlider *get_moves_slider() { return m_moves_slider; }
    IMSlider *get_layers_slider() { return m_layers_slider; }
    void enable_moves_slider(bool enable) const;
    void update_moves_slider(bool set_to_max = false);
    void update_layers_slider_mode();

    const libvgcode::Interval& get_gcode_view_full_range() const { return m_viewer.get_view_full_range(); }
    const libvgcode::Interval& get_gcode_view_enabled_range() const { return m_viewer.get_view_enabled_range(); }
    const libvgcode::Interval& get_gcode_view_visible_range() const { return m_viewer.get_view_visible_range(); }
    const libvgcode::PathVertex& get_gcode_vertex_at(size_t id) const { return m_viewer.get_vertex_at(id); }

    bool is_contained_in_bed() const { return m_contained_in_bed; }
    bool is_only_gcode_in_preview() const { return m_only_gcode_in_preview; }

    void set_view_type(libvgcode::EViewType type) {
        m_viewer.set_view_type(type);
    }
    void select_view_type(libvgcode::EViewType type);
    void apply_default_view_type();

    static std::string view_type_to_config_name(libvgcode::EViewType type);
    static bool view_type_from_config_name(const std::string& name, libvgcode::EViewType& type);
    static std::vector<std::pair<std::string, std::string>> default_view_type_choices();
    void reset_visible(libvgcode::EViewType type) {
        if (type == libvgcode::EViewType::FeatureType) {
            auto roles = m_viewer.get_extrusion_roles();
            for (size_t i = 0; i < roles.size(); ++i) {
                auto role = roles[i];
                if (!m_viewer.is_extrusion_role_visible(role)) {
                    m_viewer.toggle_extrusion_role_visibility(role);
                }
            }
        }
    }

    libvgcode::EViewType get_view_type() const { return m_viewer.get_view_type(); }

    void set_dim_previous_layers(bool value) { m_viewer.set_dim_previous_layers(value); }
    bool is_dim_previous_layers() const { return m_viewer.is_dim_previous_layers(); }
    void set_dim_previous_layers_brightness(float value) { m_viewer.set_dim_previous_layers_brightness(value); }
    float get_dim_previous_layers_brightness() const { return m_viewer.get_dim_previous_layers_brightness(); }

    void set_layers_z_range(const std::array<unsigned int, 2>& layers_z_range);

    bool is_legend_shown() const { return m_legend_visible && m_legend_enabled; }
    void show_legend(bool show) { m_legend_visible = show; }
    void enable_legend(bool enable) { m_legend_enabled = enable; }
    float get_legend_height() { return m_legend_height; }

    void export_toolpaths_to_obj(const char* filename) const;

    void set_belt_printer(bool enabled, float angle_deg) { m_belt_view_enabled = enabled; m_belt_angle_deg = angle_deg; }
    bool is_belt_view() const { return m_belt_view_enabled && m_belt_angle_deg > 0.f; }
    void toggle_belt_show_designed() { if (m_belt_view_enabled) m_belt_show_designed = !m_belt_show_designed; }
    bool is_belt_show_designed() const { return m_belt_show_designed; }

    size_t get_extruders_count() { return m_extruders_count; }
    void push_combo_style();
    void pop_combo_style();

    void invalidate_legend() {  }

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    float get_cog_marker_scale_factor() const { return m_viewer.get_cog_marker_scale_factor(); }
    void set_cog_marker_scale_factor(float factor) { return m_viewer.set_cog_marker_scale_factor(factor); }
#endif

private:
    int sequential_view_height(int canvas_height) const;
    void render_toolpaths();
    void render_shells(int canvas_width, int canvas_height);

    void render_legend(float &legend_height, int canvas_width, int canvas_height, int right_margin);
    void render_legend_color_arr_recommen(float window_padding);
    void render_slider(int canvas_width, int canvas_height);
};

}
}

#endif
