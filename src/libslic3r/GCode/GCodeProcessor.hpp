#ifndef slic3r_GCodeProcessor_hpp_
#define slic3r_GCodeProcessor_hpp_

#include "libslic3r/CommonDefs.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ArcFitter.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/MultiNozzleUtils.hpp"
#include "libslic3r/GCode/MachineFrameTransform.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/GCode/ClearanceVerifier.hpp"

#include <cstddef>
#include <cassert>
#include <cstdint>
#include <array>
#include <deque>
#include <unordered_map>
#include <utility>
#include <map>
#include <memory>
#include <functional>
#include <cstdlib>
#include <set>
#include <vector>
#include <mutex>
#include <string>
#include <string_view>
#include <optional>

namespace Slic3r {

class Print;

inline constexpr float DEFAULT_FILAMENT_DENSITY = 1.245f;

#define NOZZLE_HRC_CHECKER                                          "the_actual_nozzle_hrc_smaller_than_the_required_nozzle_hrc"
#define BED_TEMP_TOO_HIGH_THAN_FILAMENT                             "bed_temperature_too_high_than_filament"
#define NOT_SUPPORT_TRADITIONAL_TIMELAPSE                           "not_support_traditional_timelapse"
#define NOT_GENERATE_TIMELAPSE                                      "not_generate_timelapse"
#define SMOOTH_TIMELAPSE_WITHOUT_PRIME_TOWER                        "smooth_timelapse_without_prime_tower"
#define LONG_RETRACTION_WHEN_CUT                                    "activate_long_retraction_when_cut"

    enum class EMoveType : unsigned char
    {
        Noop,
        Retract,
        Unretract,
        Seam,
        Tool_change,
        Color_change,
        Pause_Print,
        Custom_GCode,
        Travel,
        Wipe,
        Extrude,
        Count
    };

    enum SkipType
    {
        stTimelapse,
        stHeadWrapDetect,
        stOther,
        stNone
    };

    const std::unordered_map<std::string_view, SkipType> skip_type_map{
        {"timelapse", SkipType::stTimelapse},
        {"head_wrap_detect", SkipType::stHeadWrapDetect}
    };

    struct PrintEstimatedStatistics
    {
        enum class ETimeMode : unsigned char
        {
            Normal,
            Stealth,
            Count
        };

        struct Mode
        {
            float time;
            float prepare_time;
            std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> custom_gcode_times;

            void reset() {
                time = 0.0f;
                prepare_time = 0.0f;
                custom_gcode_times.clear();
                custom_gcode_times.shrink_to_fit();
            }
        };

        std::vector<double>                                 volumes_per_color_change;
        std::map<size_t, double>                            model_volumes_per_extruder;
        std::map<size_t, double>                            wipe_tower_volumes_per_extruder;
        std::map<size_t, double>                            support_volumes_per_extruder;
        std::map<size_t, double>                            total_volumes_per_extruder;
        std::map<size_t, double>                            flush_per_filament;
        std::map<ExtrusionRole, std::pair<double, double>>  used_filaments_per_role;

        std::array<Mode, static_cast<size_t>(ETimeMode::Count)> modes;
        unsigned int                                        total_filament_changes;
        unsigned int                                        total_flush_filament_changes;
        unsigned int                                        total_extruder_changes;
        float                                               total_filament_load_time;
        float                                               total_filament_unload_time;
        float                                               total_tool_change_time;
        float                                               total_travel_distance;
        unsigned int                                        total_travel_moves;
        float                                               total_seam_gap_distance;
        float                                               total_seam_scarf_distance;

        PrintEstimatedStatistics() { reset(); }

        void reset() {
            for (auto m : modes) {
                m.reset();
            }
            volumes_per_color_change.clear();
            volumes_per_color_change.shrink_to_fit();
            wipe_tower_volumes_per_extruder.clear();
            model_volumes_per_extruder.clear();
            support_volumes_per_extruder.clear();
            total_volumes_per_extruder.clear();
            flush_per_filament.clear();
            used_filaments_per_role.clear();
            total_filament_changes = 0;
            total_flush_filament_changes = 0;
            total_extruder_changes = 0;
            total_filament_load_time = 0.0f;
            total_filament_unload_time = 0.0f;
            total_tool_change_time = 0.0f;
            total_travel_distance = 0.0f;
            total_travel_moves = 0;
            total_seam_gap_distance = 0.0f;
            total_seam_scarf_distance = 0.0f;
        }
    };

    struct ConflictResult
    {
        std::string        _objName1;
        std::string        _objName2;
        double             _height;
        const void *_obj1;
        const void *_obj2;
        int                layer = -1;
        ConflictResult(const std::string &objName1, const std::string &objName2, double height, const void *obj1, const void *obj2)
            : _objName1(objName1), _objName2(objName2), _height(height), _obj1(obj1), _obj2(obj2)
        {}
        ConflictResult() = default;
    };

    using ConflictResultOpt = std::optional<ConflictResult>;

    struct GCodeCheckResult
    {
        int error_code = 0;
        std::map<int, std::vector<std::pair<int, int>>> print_area_error_infos;
        std::map<int, std::vector<std::pair<int, int>>> print_height_error_infos;
        void reset() {
            error_code = 0;
            print_area_error_infos.clear();
            print_height_error_infos.clear();
        }
    };

    struct FilamentPrintableResult
    {
        std::vector<int> conflict_filament;
        std::string plate_name;
        FilamentPrintableResult(){};
        FilamentPrintableResult(std::vector<int> &conflict_filament, std::string plate_name) : conflict_filament(conflict_filament), plate_name(plate_name) {}
        bool has_value(){
           return !conflict_filament.empty();
        };
    };

    struct GCodeProcessorResult
    {
        struct FilamentSequenceHash
        {
            uint64_t operator()(const std::vector<unsigned int>& layer_filament) const {
                uint64_t key = 0;
                for (auto& f : layer_filament)
                    key |= (uint64_t(1) << f);
                return key;
            }
        };
        ConflictResultOpt conflict_result;
        GCodeCheckResult  gcode_check_result;
        FilamentPrintableResult filament_printable_reuslt;
        std::shared_ptr<MultiNozzleUtils::NozzleGroupResultBase> nozzle_group_result;
        float initial_layer_time;

        struct SettingsIds
        {
            std::string print;
            std::vector<std::string> filament;
            std::string printer;

            void reset() {
                print.clear();
                filament.clear();
                printer.clear();
            }
        };

        struct MoveVertex
        {
            unsigned int gcode_id{ 0 };
            EMoveType type{ EMoveType::Noop };
            ExtrusionRole extrusion_role{ erNone };
            unsigned char extruder_id{ 0 };
            unsigned char cp_color_id{ 0 };
            Vec3f position{ Vec3f::Zero() };
            float delta_extruder{ 0.0f };
            float feedrate{ 0.0f };
            float actual_feedrate{ 0.0f };
            float width{ 0.0f };
            float height{ 0.0f };
            float mm3_per_mm{ 0.0f };
            float travel_dist{ 0.0f };
            float fan_speed{ 0.0f };
            float temperature{ 0.0f };
            float pressure_advance{ 0.0f };
            float acceleration{ 0.0f };
            float jerk{ 0.0f };
            std::array<float, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> time{ 0.0f, 0.0f };
            float layer_duration{ 0.0f };
            unsigned int layer_id{ 0 };
            bool internal_only{ false };

            int  object_label_id{-1};
            float print_z{0.0f};
            unsigned char nonplanar_patch_shell{ 0xFF };
            short nonplanar_patch{ -1 };
            signed char nonplanar_band{ -1 };
            JointPose joints;
            bool has_joints = false;

            float volumetric_rate() const { return feedrate * mm3_per_mm; }
            float actual_volumetric_rate() const { return actual_feedrate * mm3_per_mm; }
        };

        struct SliceWarning {
            int         level;
            std::string msg;
            std::string error_code;
            std::vector<std::string> params;
        };

        struct ObjectMass
        {
            struct Sum
            {
                double mass{ 0. };
                double volume{ 0. };
                Vec3d  moment{ Vec3d::Zero() };
                Vec3d second{ Vec3d::Zero() };

                void add(const Sum &other)
                {
                    mass += other.mass;
                    volume += other.volume;
                    moment += other.moment;
                    second += other.second;
                }
            };
            std::vector<Sum> printed_up_to_layer;
            BoundingBoxf3    box;
            bool assembly{ false };

            Sum  total() const { return printed_up_to_layer.empty() ? Sum{} : printed_up_to_layer.back(); }
            void add(const Sum &sum, const BoundingBoxf3 &extent, size_t layer);
        };

        std::string filename;
        unsigned int id;
        std::vector<MoveVertex> moves;
        std::vector<float> clearance;
        bool nonplanar = false;
        ClearanceReport clearance_report;
        ObjectMass plate_mass;
        std::vector<ObjectMass> object_masses;
        std::vector<ObjectMass> body_masses;
        std::vector<ObjectMass> support_masses;
        std::vector<size_t> lines_ends;
        Pointfs printable_area;
        Pointfs bed_exclude_area;
        Pointfs wrapping_exclude_area;
        std::vector<Pointfs> extruder_areas;
        std::vector<double> extruder_heights;
        bool toolpath_outside;
        bool label_object_enabled;
        bool long_retraction_when_cut {0};
        int timelapse_warning_code {0};
        bool support_traditional_timelapse{true};
        float printable_height;
        float z_offset;
        float belt_tilt_angle{ 0.f };
        float belt_z_origin{ 0.f };
        bool machine_frame_transform_active{ false };
        SettingsIds settings_ids;
        size_t filaments_count;
        bool backtrace_enabled;
        std::vector<std::string> extruder_colors;
        std::vector<float> filament_diameters;
        std::vector<int>   required_nozzle_HRC;
        std::vector<float> filament_densities;
        std::vector<float> filament_costs;
        std::vector<int> filament_vitrification_temperature;
        std::vector<int>   filament_maps;
        std::vector<int>   limit_filament_maps;
        PrintEstimatedStatistics print_statistics;
        std::vector<CustomGCode::Item> custom_gcode_per_print_z;
        bool spiral_vase_mode;
        std::vector<SliceWarning> warnings;
        int nozzle_hrc;
        std::vector<NozzleType> nozzle_type;
        std::vector<ExtruderType> extruder_types;
        std::vector<std::string> printer_extruder_variant;
        std::vector<int>         printer_extruder_id;
        std::unordered_map<std::vector<unsigned int>, std::vector<std::pair<int, int>>,FilamentSequenceHash> layer_filaments;
        std::vector<unsigned int> nozzle_change_sequence;
        std::vector<unsigned int> filament_change_sequence;
        std::vector<unsigned int> used_mixed_filaments;
        std::vector<int> optimal_assignment;
        std::map<std::pair<int,int>, int > filament_change_count_map;

        std::unordered_map<SkipType, float> skippable_part_time;

        BedType bed_type = BedType::btCount;
        void reset();

        mutable std::mutex result_mutex;
        GCodeProcessorResult& operator=(const GCodeProcessorResult &other) { assign(other); return *this; }
        GCodeProcessorResult& operator=(GCodeProcessorResult &&other) { assign(std::move(other)); return *this; }
        template<class Other> void assign(Other &&other)
        {
            filename = std::forward<Other>(other).filename;
            id = std::forward<Other>(other).id;
            moves = std::forward<Other>(other).moves;
            clearance = std::forward<Other>(other).clearance;
            nonplanar = other.nonplanar;
            clearance_report = std::forward<Other>(other).clearance_report;
            plate_mass = std::forward<Other>(other).plate_mass;
            object_masses = std::forward<Other>(other).object_masses;
            body_masses = std::forward<Other>(other).body_masses;
            support_masses = std::forward<Other>(other).support_masses;
            lines_ends = std::forward<Other>(other).lines_ends;
            printable_area = std::forward<Other>(other).printable_area;
            bed_exclude_area = std::forward<Other>(other).bed_exclude_area;
            wrapping_exclude_area = std::forward<Other>(other).wrapping_exclude_area;
            toolpath_outside = std::forward<Other>(other).toolpath_outside;
            label_object_enabled = std::forward<Other>(other).label_object_enabled;
            long_retraction_when_cut = std::forward<Other>(other).long_retraction_when_cut;
            timelapse_warning_code = std::forward<Other>(other).timelapse_warning_code;
            printable_height = std::forward<Other>(other).printable_height;
            settings_ids = std::forward<Other>(other).settings_ids;
            filaments_count = std::forward<Other>(other).filaments_count;
            extruder_colors = std::forward<Other>(other).extruder_colors;
            filament_diameters = std::forward<Other>(other).filament_diameters;
            filament_densities = std::forward<Other>(other).filament_densities;
            filament_costs = std::forward<Other>(other).filament_costs;
            print_statistics = std::forward<Other>(other).print_statistics;
            custom_gcode_per_print_z = std::forward<Other>(other).custom_gcode_per_print_z;
            spiral_vase_mode = std::forward<Other>(other).spiral_vase_mode;
            warnings = std::forward<Other>(other).warnings;
            bed_type = std::forward<Other>(other).bed_type;
            gcode_check_result = std::forward<Other>(other).gcode_check_result;
            limit_filament_maps = std::forward<Other>(other).limit_filament_maps;
            filament_printable_reuslt = std::forward<Other>(other).filament_printable_reuslt;
            nozzle_group_result = std::forward<Other>(other).nozzle_group_result;
            extruder_types = std::forward<Other>(other).extruder_types;
            printer_extruder_variant = std::forward<Other>(other).printer_extruder_variant;
            printer_extruder_id = std::forward<Other>(other).printer_extruder_id;
            layer_filaments = std::forward<Other>(other).layer_filaments;
            filament_change_sequence = std::forward<Other>(other).filament_change_sequence;
            used_mixed_filaments = std::forward<Other>(other).used_mixed_filaments;
            nozzle_change_sequence = std::forward<Other>(other).nozzle_change_sequence;
            optimal_assignment = std::forward<Other>(other).optimal_assignment;
            filament_change_count_map = std::forward<Other>(other).filament_change_count_map;
            skippable_part_time = std::forward<Other>(other).skippable_part_time;
            initial_layer_time = std::forward<Other>(other).initial_layer_time;
            belt_tilt_angle = std::forward<Other>(other).belt_tilt_angle;
            belt_z_origin = std::forward<Other>(other).belt_z_origin;
            machine_frame_transform_active = std::forward<Other>(other).machine_frame_transform_active;
#if ENABLE_GCODE_VIEWER_STATISTICS
            time = std::forward<Other>(other).time;
#endif
        }
        void  lock() const { result_mutex.lock(); }
        void  unlock() const { result_mutex.unlock(); }
    };

    namespace ExtruderPreHeating
    {
        struct FilamentUsageBlock
        {
            int filament_id;
            int extruder_id;
            int nozzle_id;
            unsigned int lower_gcode_id;
            unsigned int upper_gcode_id;
            FilamentUsageBlock(int filament_id_, int extruder_id_, int nozzle_id_, unsigned int lower_gcode_id_, unsigned int upper_gcode_id_) :filament_id(filament_id_), extruder_id(extruder_id_), nozzle_id(nozzle_id_), lower_gcode_id(lower_gcode_id_), upper_gcode_id(upper_gcode_id_) {}
        };

        struct ExtruderUsageBlcok
        {
            int extruder_id = -1;
            unsigned int start_id = -1;
            unsigned int end_id = -1;
            int start_filament = -1;
            int end_filament = -1;
            int start_nozzle_id = -1;
            int end_nozzle_id = -1;
            unsigned int post_extrusion_start_id = -1;
            unsigned int post_extrusion_end_id = -1;
            bool         ignore_cooling_before_tower = false;

            void initialize_step_1(int extruder_id_, int start_id_, int start_filament_, int start_nozzle_id_) {
                extruder_id = extruder_id_;
                start_id = start_id_;
                start_filament = start_filament_;
                start_nozzle_id = start_nozzle_id_;
            };
            void initialize_step_2(int post_extrusion_start_id_) {
                post_extrusion_start_id = post_extrusion_start_id_;
            }
            void initialize_step_3(int end_id_, int end_filament_, int post_extrusion_end_id_, int end_nozzle_id_) {
                end_id = end_id_;
                end_filament = end_filament_;
                post_extrusion_end_id = post_extrusion_end_id_;
                end_nozzle_id = end_nozzle_id_;
            }
            void reset() {
                *this = ExtruderUsageBlcok();
            }
            ExtruderUsageBlcok() = default;
        };
    }


    class CommandProcessor {
    public:
        using command_handler_t = std::function<void(const GCodeReader::GCodeLine& line)>;
    private:
        struct TrieNode {
            command_handler_t handler{ nullptr };
            std::unordered_map<char, std::unique_ptr<TrieNode>> children;
            bool early_quit{ false };
        };
    public:
        CommandProcessor();
        void register_command(const std::string& str, command_handler_t handler,bool early_quit = false);
        bool process_comand(std::string_view cmd, const GCodeReader::GCodeLine& line);
    private:
        std::unique_ptr<TrieNode> root;
    };


    class GCodeProcessor
    {
        static const std::vector<std::string> Reserved_Tags;
        static const std::vector<std::string> Reserved_Tags_compatible;
        static const std::string Flush_Start_Tag;
        static const std::string Flush_End_Tag;
        static const std::string VFlush_Start_Tag;
        static const std::string VFlush_End_Tag;
        static const std::string External_Purge_Tag;
    public:
        static constexpr size_t Output_Block_Size = 65536;

        static const std::string Skippable_Start_Tag;
        static const std::string Skippable_End_Tag;
        static const std::string Skippable_Type_Tag;
        static const std::string Machine_Start_GCode_End_Tag;
        static const std::string Machine_End_GCode_Start_Tag;
        static const std::string Nozzle_Change_Start_Tag;
        static const std::string Nozzle_Change_End_Tag;
        static const std::string Toolchange_Wipe_Tag;
    public:
        enum class ETags : unsigned char
        {
            Role,
            Wipe_Start,
            Wipe_End,
            Height,
            Width,
            Layer_Change,
            Color_Change,
            Pause_Print,
            Custom_Code,
            First_Line_M73_Placeholder,
            Last_Line_M73_Placeholder,
            Estimated_Printing_Time_Placeholder,
            Total_Layer_Number_Placeholder,
            Manual_Tool_Change,
            During_Print_Exhaust_Fan,
            Wipe_Tower_Start,
            Wipe_Tower_End,
            PA_Change,
            Print_Time_Total_Sec_Placeholder,
            Print_Time_Day_Placeholder,
            Print_Time_Hour_Placeholder,
            Print_Time_Minute_Placeholder,
            Print_Time_Sec_Placeholder,
            Used_Filament_Length_Placeholder,
        };

        static const std::string& reserved_tag(ETags tag) { return s_IsBBLPrinter ? Reserved_Tags[static_cast<unsigned char>(tag)] : Reserved_Tags_compatible[static_cast<unsigned char>(tag)]; }
        static bool contains_reserved_tag(const std::string& gcode, std::string& found_tag);
        static bool contains_reserved_tags(const std::string& gcode, unsigned int max_count, std::vector<std::string>& found_tag, bool is_bbl_printer);

        static int get_gcode_last_filament(const std::string &gcode_str);
        static bool get_last_z_from_gcode(const std::string& gcode_str, double& z);
        static bool get_last_position_from_gcode(const std::string &gcode_str, Vec3f &pos);

        static const float Wipe_Width;
        static const float Wipe_Height;

        static bool s_IsBBLPrinter;

    private:
        using AxisCoords = std::array<double, 4>;
        using ExtruderColors = std::vector<unsigned char>;
        using ExtruderTemps = std::vector<float>;

        enum class EUnits : unsigned char
        {
            Millimeters,
            Inches
        };

        enum class EPositioningType : unsigned char
        {
            Absolute,
            Relative
        };

        struct CachedPosition
        {
            AxisCoords position;
            float feedrate;

            void reset();
        };

        struct CpColor
        {
            unsigned char counter;
            unsigned char current;

            void reset();
        };

    public:
        struct FeedrateProfile
        {
            float entry{ 0.0f };
            float cruise{ 0.0f };
            float exit{ 0.0f };
        };

        struct Trapezoid
        {
            float accelerate_until{ 0.0f };
            float decelerate_after{ 0.0f };
            float cruise_feedrate{ 0.0f };

            float acceleration_time(float entry_feedrate, float acceleration) const;
            float cruise_time() const { return (cruise_feedrate != 0.0f) ? cruise_distance() / cruise_feedrate : 0.0f; }
            float deceleration_time(float distance, float acceleration) const;
            float acceleration_distance() const { return accelerate_until; }
            float cruise_distance() const { return decelerate_after - accelerate_until; }
            float deceleration_distance(float distance) const { return distance - decelerate_after; }
            bool is_cruise_only(float distance) const { return std::abs(cruise_distance() - distance) < EPSILON; }
        };

        struct TimeBlock
        {
            struct Flags
            {
                bool recalculate{ false };
                bool nominal_length{ false };
                bool prepare_stage{ false };
                bool nonplanar{ false };
            };

            EMoveType move_type{ EMoveType::Noop };
            ExtrusionRole role{ erNone };
            SkipType skippable_type{ SkipType::stNone };
            unsigned int move_id{ 0 };
            unsigned int g1_line_id{ 0 };
            unsigned int remaining_internal_g1_lines{ 0 };
            unsigned int layer_id{ 0 };
            float distance{ 0.0f };
            float acceleration{ 0.0f };
            float max_entry_speed{ 0.0f };
            float safe_feedrate{ 0.0f };
            float feed_interval{ 0.0f };
            Flags flags;
            FeedrateProfile feedrate_profile;
            Trapezoid trapezoid;

            void calculate_trapezoid();

            float time() const {
                return trapezoid.acceleration_time(feedrate_profile.entry, acceleration) +
                       trapezoid.cruise_time() + trapezoid.deceleration_time(distance, acceleration);
            }
        };


    private:
        friend class ExportLines;
        struct TimeMachine
        {
            struct State
            {
                float feedrate;
                float safe_feedrate;
                AxisCoords axis_feedrate;
                AxisCoords abs_axis_feedrate;
                Vec3f enter_direction;
                Vec3f exit_direction;
                Vec4f jd_unit_vec;

                void reset();
            };

            struct CustomGCodeTime
            {
                bool needed;
                float cache;
                std::vector<std::pair<CustomGCode::Type, float>> times;

                void reset();
            };

            struct G1LinesCacheItem
            {
                unsigned int id;
                unsigned int remaining_internal_g1_lines{ 0 };
                float elapsed_time;
            };

            struct ActualSpeedMove
            {
                unsigned int move_id{ 0 };
                std::optional<Vec3f> position;
                float actual_feedrate{ 0.0f };
                std::optional<float> delta_extruder;
                std::optional<float> feedrate;
                std::optional<float> width;
                std::optional<float> height;
                std::optional<float> mm3_per_mm;
                std::optional<float> fan_speed;
                std::optional<float> temperature;
            };

            bool enabled;
            float acceleration;
            float max_acceleration;
            float retract_acceleration;
            float max_retract_acceleration;
            float travel_acceleration;
            float max_travel_acceleration;
            float extrude_factor_override_percentage;
            double time;
            struct StopTime
            {
                unsigned int g1_line_id;
                float elapsed_time;
            };
            std::vector<StopTime> stop_times;
            std::string line_m73_main_mask;
            std::string line_m73_stop_mask;
            State curr;
            State prev;
            CustomGCodeTime gcode_time;
            std::vector<TimeBlock> blocks;
            std::vector<G1LinesCacheItem> g1_times_cache;
            float first_layer_time;
            std::vector<ActualSpeedMove> actual_speed_moves;
            float prepare_time;

            using AdditionalBufferBlock = std::pair<EMoveType, float>;
            using AdditionalBuffer      = std::vector<AdditionalBufferBlock>;
            AdditionalBuffer m_additional_time_buffer;

            struct HostFeed
            {
                static constexpr size_t PlannerBlocks = 32;
                double             last_arrival{ 0. };
                std::deque<double> starts;
                float stall(const TimeBlock& block, double now);
            };
            HostFeed host_feed;

            void reset();

            static AdditionalBuffer merge_adjacent_additional_time_blocks(const AdditionalBuffer& buffer);

            void calculate_time(GCodeProcessorResult& result, PrintEstimatedStatistics::ETimeMode mode, size_t keep_last_n_blocks = 0, float additional_time = 0.0f, EMoveType target_move_type = EMoveType::Noop, bool is_final = false);
        };

        struct UsedFilaments
        {
            double color_change_cache;
            std::vector<double> volumes_per_color_change;

            double model_extrude_cache;
            std::map<size_t, double> model_volumes_per_filament;

            double wipe_tower_cache;
            std::map<size_t, double>wipe_tower_volumes_per_filament;

            double support_volume_cache;
            std::map<size_t, double>support_volumes_per_filament;

            std::map<size_t, double> flush_per_filament;

            double total_volume_cache;
            std::map<size_t, double>total_volumes_per_filament;

            double role_cache;
            std::map<ExtrusionRole, std::pair<double, double>> filaments_per_role;

            void reset();

            void increase_support_caches(double extruded_volume);
            void increase_model_caches(double extruded_volume);
            void increase_wipe_tower_caches(double extruded_volume);

            void process_color_change_cache();
            void process_model_cache(GCodeProcessor* processor);
            void process_wipe_tower_cache(GCodeProcessor* processor);
            void process_support_cache(GCodeProcessor* processor);
            void process_total_volume_cache(GCodeProcessor* processor);

            void update_flush_per_filament(size_t extrude_id, float flush_length);
            void process_role_cache(GCodeProcessor* processor);
            void process_caches(GCodeProcessor* processor);

            friend class GCodeProcessor;
        };

        struct TimeProcessor
        {
            enum InsertLineType
            {
                PlaceholderReplace,
                TimePredict,
                FilamentChangePredict,
                ExtruderChangePredict,
                PreCooling,
                PreHeating,
            };

            using InsertedLinesMap = std::map<unsigned int, std::vector<std::pair<std::string, InsertLineType>>>;

            struct Planner
            {
                static constexpr size_t queue_size = 64;
                static constexpr size_t refresh_threshold = queue_size * 4;
            };

            bool extruder_unloaded;
            bool machine_envelope_processing_enabled;
            MachineEnvelopeConfig machine_limits;
            float filament_load_times;
            float filament_unload_times;
            float machine_tool_change_time;

            std::array<TimeMachine, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> machines;

            void reset();
        };

        class PreCoolingInjector {
        public:
            struct ExtruderFreeBlock {
                unsigned int free_lower_gcode_id;
                unsigned int free_upper_gcode_id;
                unsigned int partial_free_lower_id;
                unsigned int partial_free_upper_id;
                int last_filament_id;
                int next_filament_id;
                int last_nozzle_id;
                int next_nozzle_id;
                int extruder_id;
                bool ignore_cooling_before_tower = false;
            };

            void process_pre_cooling_and_heating(TimeProcessor::InsertedLinesMap& inserted_operation_lines);
            void build_extruder_free_blocks(const std::vector<ExtruderPreHeating::FilamentUsageBlock>& filament_usage_blocks, const std::vector<ExtruderPreHeating::ExtruderUsageBlcok>& extruder_usage_blocks);

            PreCoolingInjector(
                const std::vector<GCodeProcessorResult::MoveVertex>& moves_,
                const std::vector<std::string>& filament_types_,
                const MultiNozzleUtils::LayeredNozzleGroupResult& nozzle_group_result_,
                const std::vector<int>& filament_nozzle_temps_,
                const std::vector<int>& filament_nozzle_temps_initial_layer_,
                const std::vector<int>& physical_extruder_map_,
                int valid_machine_id_,
                float inject_time_threshold_,
                bool handle_hotend_as_extruder_,
                bool has_filament_switcher_,
                const std::vector<int>& pre_cooling_temp_,
                const std::vector<double>& cooling_rate_,
                const std::vector<double>& heating_rate_,
                const std::vector<std::pair<unsigned int, unsigned int>>& skippable_blocks_,
                const std::vector<int>& extruder_max_nozzle_count_,
                const std::vector<double>& filament_preheat_temperature_delta_,
                const std::vector<double>& filament_max_temperature_drop_when_ec_,
                unsigned int machine_start_gcode_end_id_,
                unsigned int machine_end_gcode_start_id_,
                const std::vector<ExtruderType>& extruder_types_,
                const std::vector<double>& nozzle_diameter_
            ) :
                moves(moves_),
                filament_types(filament_types_),
                nozzle_group_result(nozzle_group_result_),
                filament_nozzle_temps(filament_nozzle_temps_),
                filament_nozzle_temps_initial_layer(filament_nozzle_temps_initial_layer_),
                physical_extruder_map(physical_extruder_map_),
                valid_machine_id(valid_machine_id_),
                inject_time_threshold(inject_time_threshold_),
                handle_hotend_as_extruder(handle_hotend_as_extruder_),
                has_filament_switcher(has_filament_switcher_),
                filament_pre_cooling_temps(pre_cooling_temp_),
                cooling_rate(cooling_rate_),
                heating_rate(heating_rate_),
                skippable_blocks(skippable_blocks_),
                extruder_max_nozzle_count(extruder_max_nozzle_count_),
                filament_preheat_temperature_delta(filament_preheat_temperature_delta_),
                filament_max_temperature_drop_when_ec(filament_max_temperature_drop_when_ec_),
                machine_start_gcode_end_id(machine_start_gcode_end_id_),
                machine_end_gcode_start_id(machine_end_gcode_start_id_),
                extruder_types(extruder_types_),
                nozzle_diameter(nozzle_diameter_)
            {
            }

        private:
            std::vector<ExtruderFreeBlock> m_extruder_free_blocks;
            const std::vector<GCodeProcessorResult::MoveVertex>& moves;
            const std::vector<std::string>& filament_types;
            const MultiNozzleUtils::LayeredNozzleGroupResult& nozzle_group_result;
            const std::vector<int>& filament_nozzle_temps;
            const std::vector<int>& filament_nozzle_temps_initial_layer;
            const std::vector<int>& physical_extruder_map;
            const int valid_machine_id;
            const float inject_time_threshold;
            const bool handle_hotend_as_extruder;
            const bool has_filament_switcher;
            const std::vector<double>& cooling_rate;
            const std::vector<double>& heating_rate;
            const std::vector<int>& filament_pre_cooling_temps;
            const std::vector<std::pair<unsigned int, unsigned int>>& skippable_blocks;
            const std::vector<int>& extruder_max_nozzle_count;
            const std::vector<double>& filament_preheat_temperature_delta;
            const std::vector<double>& filament_max_temperature_drop_when_ec;
            const unsigned int machine_start_gcode_end_id;
            const unsigned int machine_end_gcode_start_id;
            const std::vector<ExtruderType>& extruder_types;
            const std::vector<double>& nozzle_diameter;

            void inject_cooling_heating_command(
                TimeProcessor::InsertedLinesMap& inserted_operation_lines,
                const ExtruderFreeBlock& free_block,
                float curr_temp,
                float target_temp,
                bool pre_cooling,
                bool pre_heating
            );

            void build_by_filament_blocks(const std::vector<ExtruderPreHeating::FilamentUsageBlock>& filament_usage_blocks);
            void build_by_extruder_blocks(const std::vector<ExtruderPreHeating::ExtruderUsageBlcok>& extruder_usage_blocks);
        };
    public:
        class SeamsDetector
        {
            bool m_active{ false };
            std::optional<Vec3f> m_first_vertex;

        public:
            void activate(bool active) {
                if (m_active != active) {
                    m_active = active;
                    if (m_active)
                        m_first_vertex.reset();
                }
            }

            std::optional<Vec3f> get_first_vertex() const { return m_first_vertex; }
            void set_first_vertex(const Vec3f& vertex) { m_first_vertex = vertex; }

            bool is_active() const { return m_active; }
            bool has_first_vertex() const { return m_first_vertex.has_value(); }
        };

        class OptionsZCorrector
        {
            GCodeProcessorResult& m_result;
            std::optional<size_t> m_move_id;
            std::optional<size_t> m_custom_gcode_per_print_z_id;

        public:
            explicit OptionsZCorrector(GCodeProcessorResult& result) : m_result(result) {
            }

            void set() {
                m_move_id = m_result.moves.size() - 1;
                m_custom_gcode_per_print_z_id = m_result.custom_gcode_per_print_z.size() - 1;
            }

            void update(float height) {
                if (!m_move_id.has_value() || !m_custom_gcode_per_print_z_id.has_value())
                    return;

                const Vec3f position = m_result.moves.back().position;

                GCodeProcessorResult::MoveVertex& move = m_result.moves.emplace_back(m_result.moves[*m_move_id]);
                move.position = position;
                move.height = height;
                m_result.moves.erase(m_result.moves.begin() + *m_move_id);
                m_result.custom_gcode_per_print_z[*m_custom_gcode_per_print_z_id].print_z = position.z();
                reset();
            }

            void reset() {
                m_move_id.reset();
                m_custom_gcode_per_print_z_id.reset();
            }
        };

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        struct DataChecker
        {
            struct Error
            {
                float value;
                float tag_value;
                ExtrusionRole role;
            };

            std::string type;
            float threshold{ 0.01f };
            float last_tag_value{ 0.0f };
            unsigned int count{ 0 };
            std::vector<Error> errors;

            DataChecker(const std::string& type, float threshold)
                : type(type), threshold(threshold)
            {}

            void update(float value, ExtrusionRole role) {
                if (role != erCustom) {
                    ++count;
                    if (last_tag_value != 0.0f) {
                        if (std::abs(value - last_tag_value) / last_tag_value > threshold)
                            errors.push_back({ value, last_tag_value, role });
                    }
                }
            }

            void reset() { last_tag_value = 0.0f; errors.clear(); count = 0; }

            std::pair<float, float> get_min() const {
                float delta_min = FLT_MAX;
                float perc_min = 0.0f;
                for (const Error& e : errors) {
                    if (delta_min > e.value - e.tag_value) {
                        delta_min = e.value - e.tag_value;
                        perc_min = 100.0f * delta_min / e.tag_value;
                    }
                }
                return { delta_min, perc_min };
            }

            std::pair<float, float> get_max() const {
                float delta_max = -FLT_MAX;
                float perc_max = 0.0f;
                for (const Error& e : errors) {
                    if (delta_max < e.value - e.tag_value) {
                        delta_max = e.value - e.tag_value;
                        perc_max = 100.0f * delta_max / e.tag_value;
                    }
                }
                return { delta_max, perc_max };
            }

            void output() const {
                if (!errors.empty()) {
                    std::cout << type << ":\n";
                    std::cout << "Errors: " << errors.size() << " (" << 100.0f * float(errors.size()) / float(count) << "%)\n";
                    auto [min, perc_min] = get_min();
                    auto [max, perc_max] = get_max();
                    std::cout << "min: " << min << "(" << perc_min << "%) - max: " << max << "(" << perc_max << "%)\n";
                }
            }
        };
#endif
        struct MassLocation
        {
            int object{ -1 };
            int body{ -1 };
        };
        using MassLocator = std::function<MassLocation(const Vec3d &point, bool support)>;

    private:
        CommandProcessor m_command_processor;
        GCodeReader m_parser;
        DynamicConfig m_belt_render_config;
        EUnits m_units;
        EPositioningType m_global_positioning_type;
        EPositioningType m_e_local_positioning_type;
        std::vector<Vec3f> m_extruder_offsets;
        GCodeFlavor m_flavor;
        std::vector<float> m_nozzle_volume;
        std::unique_ptr<ToolKinematics> m_tool_kinematics;
        JointPose                       m_joints;
        bool                            m_inverse_time{ false };
        unsigned char                   m_nonplanar_patch_shell{ 0xFF };
        short                           m_nonplanar_patch{ -1 };
        signed char                     m_nonplanar_band{ -1 };
        float                           m_nonplanar_feed_interval{ 0.f };
        AxisCoords m_start_position;
        AxisCoords m_end_position;
        AxisCoords m_origin;
        CachedPosition m_cached_position;
        bool m_wiping;
        bool m_flushing;
        bool m_virtual_flushing;
        bool m_wipe_tower;
        bool m_skippable{false};
        SkipType m_skippable_type{SkipType::stNone};
        int m_object_label_id{-1};
        MassLocator m_mass_locator;
        float m_print_z{0.0f};
        std::vector<float> m_remaining_volume;
        ExtruderTemps m_filament_nozzle_temp;
        ExtruderTemps m_filament_nozzle_temp_first_layer;
        std::vector<int> m_physical_extruder_map;
        std::vector<int> m_extruder_max_nozzle_count{1};
        std::vector<std::string> m_filament_types;
        std::vector<double> m_nozzle_diameter;
        std::vector<double> m_hotend_cooling_rate{ 2.f };
        std::vector<double> m_hotend_heating_rate{ 2.f };
        std::vector<int> m_filament_pre_cooling_temp{ 0 };
        std::vector<double> m_filament_preheat_temperature_delta;
        bool m_enable_pre_heating{ false };
        bool m_handle_hotend_as_extruder{ false };
        bool m_has_filament_switcher{ false };
        std::vector<std::pair<unsigned int, unsigned int>> m_skippable_blocks;
        std::vector<ExtruderPreHeating::FilamentUsageBlock> m_filament_blocks;
        std::vector<ExtruderPreHeating::ExtruderUsageBlcok>  m_extruder_blocks;
        unsigned int m_machine_start_gcode_end_line_id{ (unsigned int) (-1) };
        unsigned int m_machine_end_gcode_start_line_id{ (unsigned int) (-1) };
        bool m_skip_end_gcode_delays{ false };
        MultiNozzleUtils::NozzleStatusRecorder m_nozzle_status_recorder;
        std::shared_ptr<MultiNozzleUtils::NozzleGroupResultBase> m_nozzle_group_result;
        bool m_manual_filament_change;

        double          m_x_offset{ 0 };
        double          m_y_offset{ 0 };

        MachineFrameTransform m_machine_frame_transform;
        bool                  m_belt_printer{ false };

        unsigned int m_line_id;
        unsigned int m_last_line_id;
        float m_feedrate;
        float m_width;
        float m_height;
        float m_forced_width;
        float m_forced_height;
        float m_mm3_per_mm;
        float m_travel_dist;
        float m_fan_speed;
        float m_z_offset;
        float m_pressure_advance;
        ExtrusionRole m_extrusion_role;
        std::vector<int> m_filament_maps;
        std::vector<unsigned char> m_last_filament_id;
        std::vector<unsigned char> m_filament_id;
        unsigned char m_extruder_id;
        int m_machine_config_idx{0};
        ExtruderColors m_extruder_colors;
        ExtruderTemps m_extruder_temps;
        bool  m_is_XL_printer = false;
        int m_highest_bed_temp;
        float m_extruded_last_z;
        float m_first_layer_height;
        float m_zero_layer_height;
        bool m_processing_start_custom_gcode;
        bool m_in_config_block;
        unsigned int m_g1_line_id;
        unsigned int m_layer_id;
        CpColor m_cp_color;
        SeamsDetector m_seams_detector;
        OptionsZCorrector m_options_z_corrector;
        size_t m_last_default_color_id;
        bool m_detect_layer_based_on_tag {false};
        int m_seams_count;
        bool m_measure_g29_time {false};
        bool m_single_extruder_multi_material;
        float m_preheat_time;
        int m_preheat_steps;
        bool m_disable_m73;
        std::string m_printer_model;

        enum class EProducer
        {
            Unknown,
            OrcaSlicer,
            Slic3rPE,
            Slic3r,
            SuperSlicer,
            Cura,
            Simplify3D,
            CraftWare,
            ideaMaker,
            KissSlicer
        };

        static const std::vector<std::pair<GCodeProcessor::EProducer, std::string>> Producers;
        EProducer m_producer;

        TimeProcessor m_time_processor;
        std::vector<std::pair<unsigned int, unsigned int>> m_actual_speed_id_map;
        UsedFilaments m_used_filaments;

        Print* m_print{ nullptr };

        GCodeProcessorResult m_result;
        static unsigned int s_result_id;

    public:
        GCodeProcessor();
        void init_filament_maps_and_nozzle_type_when_import_only_gcode();
        void ensure_nozzle_group_result(int min_filament_count);
        bool check_multi_extruder_gcode_valid(const int                         extruder_size,
                                              const Pointfs                     plate_printable_area,
                                              const double                      plate_printable_height,
                                              const Pointfs                     wrapping_exclude_area,
                                              const std::vector<Polygons> &unprintable_areas,
                                              const std::vector<double>   &printable_heights,
                                              const std::vector<int>      &filament_map,
                                              const std::vector<std::set<int>>& unprintable_filament_types );
        void apply_config(const PrintConfig& config);
        void set_print(Print* print) { m_print = print; }
        void set_mass_locator(MassLocator locator, std::vector<GCodeProcessorResult::ObjectMass> objects)
        {
            m_mass_locator = std::move(locator);
            m_result.support_masses.assign(objects.size(), {});
            m_result.object_masses = std::move(objects);
        }
        void initialize_from_context(const std::shared_ptr<MultiNozzleUtils::NozzleGroupResultBase>& nozzle_group_result) {
            m_nozzle_group_result = nozzle_group_result;
        }

        DynamicConfig export_config_for_render() const;

        void enable_stealth_time_estimator(bool enabled);
        bool is_stealth_time_estimator_enabled() const {
            return m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].enabled;
        }
        void enable_machine_envelope_processing(bool enabled) { m_time_processor.machine_envelope_processing_enabled = enabled; }
        void reset();

        const GCodeProcessorResult& get_result() const { return m_result; }
        GCodeProcessorResult& result() { return m_result; }
        GCodeProcessorResult&& extract_result() { return std::move(m_result); }

        void process_file(const std::string& filename, std::function<void()> cancel_callback = nullptr);

        void initialize(const std::string& filename);
        void initialize_result_moves() {
            assert(m_result.moves.empty());
            m_result.moves.emplace_back(GCodeProcessorResult::MoveVertex());
        }
        void process_buffer(const std::string& buffer);
        void finalize(bool post_process);

        float get_time(PrintEstimatedStatistics::ETimeMode mode) const;
        float get_prepare_time(PrintEstimatedStatistics::ETimeMode mode) const;
        std::string get_time_dhm(PrintEstimatedStatistics::ETimeMode mode) const;
        std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> get_custom_gcode_times(PrintEstimatedStatistics::ETimeMode mode, bool include_remaining) const;

        float get_first_layer_time(PrintEstimatedStatistics::ETimeMode mode) const;

        void set_xy_offset(double x, double y) { m_x_offset = x; m_y_offset = y; }

        void detect_layer_based_on_tag(bool enabled) { m_detect_layer_based_on_tag = enabled; }

    private:
        void register_commands();
        void apply_config(const DynamicPrintConfig& config);
        void apply_config_simplify3d(const std::string& filename);
        void apply_config_superslicer(const std::string& filename);
        void process_gcode_line(const GCodeReader::GCodeLine& line, bool producers_enabled);

        void process_tags(const std::string_view comment, bool producers_enabled);
        bool process_producers_tags(const std::string_view comment);
        bool process_bambuslicer_tags(const std::string_view comment);
        bool process_cura_tags(const std::string_view comment);
        bool process_simplify3d_tags(const std::string_view comment);
        bool process_craftware_tags(const std::string_view comment);
        bool process_ideamaker_tags(const std::string_view comment);
        bool process_kissslicer_tags(const std::string_view comment);

        bool detect_producer(const std::string_view comment);

        void process_G0(const GCodeReader::GCodeLine& line);
        void process_G1(const GCodeReader::GCodeLine& line, const std::optional<unsigned int>& remaining_internal_g1_lines = std::nullopt);
        void process_rotary_G1(const GCodeReader::GCodeLine& line);
        void process_G93(const GCodeReader::GCodeLine&) { m_inverse_time = true; }
        void process_G94(const GCodeReader::GCodeLine&) { m_inverse_time = false; }
        enum class G1DiscretizationOrigin {
            G1,
            G2G3,
        };
        void process_G1(const std::array<std::optional<double>, 4>& axes = { std::nullopt, std::nullopt, std::nullopt, std::nullopt },
            const std::optional<double>& feedrate = std::nullopt, G1DiscretizationOrigin origin = G1DiscretizationOrigin::G1,
            const std::optional<unsigned int>& remaining_internal_g1_lines = std::nullopt);

        void process_G2_G3(const GCodeReader::GCodeLine& line, bool clockwise);

        void process_VG1(const GCodeReader::GCodeLine& line);


        void process_G4(const GCodeReader::GCodeLine& line);

        void process_G10(const GCodeReader::GCodeLine& line);

        void process_G11(const GCodeReader::GCodeLine& line);

        void process_G20(const GCodeReader::GCodeLine& line);

        void process_G21(const GCodeReader::GCodeLine& line);

        void process_G22(const GCodeReader::GCodeLine& line);

        void process_G23(const GCodeReader::GCodeLine& line);

        void process_G28(const GCodeReader::GCodeLine& line);

        void process_G29(const GCodeReader::GCodeLine& line);

        void process_G90(const GCodeReader::GCodeLine& line);

        void process_G91(const GCodeReader::GCodeLine& line);

        void process_G92(const GCodeReader::GCodeLine& line);

        void process_M1(const GCodeReader::GCodeLine& line);

        void process_M82(const GCodeReader::GCodeLine& line);

        void process_M83(const GCodeReader::GCodeLine& line);

        void process_M104(const GCodeReader::GCodeLine& line);

        void process_VM104(const GCodeReader::GCodeLine& line);

        void process_VM109(const GCodeReader::GCodeLine& line);

        void process_M106(const GCodeReader::GCodeLine& line);

        void process_M107(const GCodeReader::GCodeLine& line);

        void process_M900(const GCodeReader::GCodeLine& line);
        void process_M572(const GCodeReader::GCodeLine &line);
        void process_SET_PRESSURE_ADVANCE(const GCodeReader::GCodeLine& line);

        void process_M108(const GCodeReader::GCodeLine& line);

        void process_M109(const GCodeReader::GCodeLine& line);

        void process_M132(const GCodeReader::GCodeLine& line);

        void process_M135(const GCodeReader::GCodeLine& line);

        void process_M140(const GCodeReader::GCodeLine& line);

        void process_M190(const GCodeReader::GCodeLine& line);

        void process_M191(const GCodeReader::GCodeLine& line);

        void process_M201(const GCodeReader::GCodeLine& line);

        void process_M203(const GCodeReader::GCodeLine& line);

        void process_M204(const GCodeReader::GCodeLine& line);

        void process_M205(const GCodeReader::GCodeLine& line);

        void process_SET_VELOCITY_LIMIT(const GCodeReader::GCodeLine& line);

        void process_M221(const GCodeReader::GCodeLine& line);

        void process_M400(const GCodeReader::GCodeLine& line);

        void process_M401(const GCodeReader::GCodeLine& line);

        void process_M402(const GCodeReader::GCodeLine& line);

        void process_M566(const GCodeReader::GCodeLine& line);

        void process_M702(const GCodeReader::GCodeLine& line);

        void process_M6211(const GCodeReader::GCodeLine& line);
        void process_elegoo_M6211(const GCodeReader::GCodeLine& line);

        void process_SYNC(const GCodeReader::GCodeLine& line);

        void process_T(const GCodeReader::GCodeLine& line);
        void process_T(const std::string_view command);
        void process_T(const std::string_view command, int nozzle_id);
        void process_M1020(const GCodeReader::GCodeLine &line);

        void process_M622(const GCodeReader::GCodeLine &line);
        void process_M623(const GCodeReader::GCodeLine &line);

        void process_filament_change(int id);
        void process_filament_change(int id, int nozzle_id);
        std::optional<MultiNozzleUtils::NozzleInfo> resolve_target_nozzle(
            const MultiNozzleUtils::NozzleGroupResultBase &group, int id, int nozzle_id) const;
        int  get_machine_config_idx() const;
        bool use_multi_nozzle_change_time_model() const;

        void run_post_process();

        void run_second_pass_injection();
        void handle_offsets_of_second_process(const TimeProcessor::InsertedLinesMap& inserted_operation_lines);

        void store_move_vertex(EMoveType type, EMovePathType path_type = EMovePathType::Noop_move, bool internal_only = false);
        void add_object_mass(int filament_id, float volume);
        void finalize_object_masses();

        void set_extrusion_role(ExtrusionRole role);
        void set_skippable_type(const std::string_view type);

        float minimum_feedrate(PrintEstimatedStatistics::ETimeMode mode, float feedrate) const;
        float minimum_travel_feedrate(PrintEstimatedStatistics::ETimeMode mode, float feedrate) const;
        float get_axis_max_feedrate(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const;
        float get_axis_max_feedrate(PrintEstimatedStatistics::ETimeMode mode, Axis axis, int machine_idx) const;
        float get_axis_max_acceleration(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const;
        float get_axis_max_acceleration(PrintEstimatedStatistics::ETimeMode mode, Axis axis, int machine_idx) const;
        float get_axis_max_jerk_with_jd(PrintEstimatedStatistics::ETimeMode mode, Axis axis, float acceleration) const;
        float get_axis_max_jerk_with_jd(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const;
        float get_junction_deviation(PrintEstimatedStatistics::ETimeMode mode, float acceleration) const;
        float calc_junction_acceleration(const TimeBlock& block, const Vec4f& junction_unit_vec,
                                         PrintEstimatedStatistics::ETimeMode mode) const;
        float calc_vmax_junction_deviation(const TimeBlock& block, const TimeMachine::State& prev,
                                           const TimeMachine::State& curr, bool has_prev_move,
                                           PrintEstimatedStatistics::ETimeMode mode) const;
        float get_axis_max_jerk(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const;
        Vec3f get_xyz_max_jerk(PrintEstimatedStatistics::ETimeMode mode) const;
        float get_retract_acceleration(PrintEstimatedStatistics::ETimeMode mode) const;
        void  set_retract_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value);
    float get_acceleration(PrintEstimatedStatistics::ETimeMode mode) const;
        void  set_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value);
        float get_travel_acceleration(PrintEstimatedStatistics::ETimeMode mode) const;
        void  set_travel_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value);
        float get_filament_load_time(size_t extruder_id);
        float get_filament_unload_time(size_t extruder_id);
        float get_extruder_change_time(size_t extruder_id);
        int   get_filament_vitrification_temperature(size_t extrude_id);
        void process_custom_gcode_time(CustomGCode::Type code);
        void process_filaments(CustomGCode::Type code);

        void calculate_time(GCodeProcessorResult& result, size_t keep_last_n_blocks = 0, float additional_time = 0.0f, EMoveType target_move_type = EMoveType::Noop, bool is_final = false);

        void simulate_st_synchronize(float additional_time = 0.0f, EMoveType target_move_type = EMoveType::Noop);

        void update_estimated_times_stats();

        double extract_absolute_position_on_axis(Axis axis, const GCodeReader::GCodeLine& line, double area_filament_cross_section);

        void update_slice_warnings();

        int get_filament_id(bool force_initialize = true) const;
        int get_last_filament_id(bool force_initialize = true) const;
        int get_extruder_id(bool force_initialize = true)const;
   };

}

#endif
