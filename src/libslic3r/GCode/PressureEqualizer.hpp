#ifndef slic3r_GCode_PressureEqualizer_hpp_
#define slic3r_GCode_PressureEqualizer_hpp_

#include "../ExtrusionEntity.hpp"
#include "../PrintConfig.hpp"
#include <string>
#include <cstddef>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cassert>

#include <queue>

namespace Slic3r {

struct LayerResult;

class GCodeG1Formatter;






class PressureEqualizer
{
public:
    PressureEqualizer() = delete;
    explicit PressureEqualizer(const Slic3r::GCodeConfig &config);
    ~PressureEqualizer() = default;




    LayerResult process_layer(LayerResult &&input);
private:

    void process_layer(const std::string &gcode);

#ifdef PRESSURE_EQUALIZER_STATISTIC
    struct Statistics
    {
        void reset()
        {
            volumetric_extrusion_rate_min = std::numeric_limits<float>::max();
            volumetric_extrusion_rate_max = 0.f;
            volumetric_extrusion_rate_avg = 0.f;
            extrusion_length              = 0.f;
        }
        void update(float volumetric_extrusion_rate, float length)
        {
            volumetric_extrusion_rate_min  = std::min(volumetric_extrusion_rate_min, volumetric_extrusion_rate);
            volumetric_extrusion_rate_max  = std::max(volumetric_extrusion_rate_max, volumetric_extrusion_rate);
            volumetric_extrusion_rate_avg += volumetric_extrusion_rate * length;
            extrusion_length              += length;
        }
        float volumetric_extrusion_rate_min;
        float volumetric_extrusion_rate_max;
        float volumetric_extrusion_rate_avg;
        float extrusion_length;
    };

    struct Statistics m_stat;
#endif



    struct ExtrusionRateSlope {
        float positive;
        float negative;
    };
    ExtrusionRateSlope              m_max_volumetric_extrusion_rate_slopes[size_t(ExtrusionRole::erCount)];
    float                           m_max_volumetric_extrusion_rate_slope_positive;
    float                           m_max_volumetric_extrusion_rate_slope_negative;



    std::vector<float>              m_filament_crossections;



    float                           m_current_pos[5];
    size_t                          m_current_extruder;
    ExtrusionRole     m_current_extrusion_role;
    bool                            m_retracted;
    bool                            m_use_relative_e_distances;



    float                           m_max_segment_length;


    bool                           m_extrusion_rate_smoothing_external_perimeter_only;



    bool                            opened_extrude_set_speed_block = false;

    enum GCodeLineType {
        GCODELINETYPE_INVALID,
        GCODELINETYPE_NOOP,
        GCODELINETYPE_OTHER,
        GCODELINETYPE_RETRACT,
        GCODELINETYPE_UNRETRACT,
        GCODELINETYPE_TOOL_CHANGE,
        GCODELINETYPE_MOVE,
        GCODELINETYPE_EXTRUDE,
    };

    struct GCodeLine
    {
        GCodeLine() :
            type(GCODELINETYPE_INVALID),
            raw_length(0),
            modified(false),
            extruder_id(0),
            volumetric_extrusion_rate(0.f),
            volumetric_extrusion_rate_start(0.f),
            volumetric_extrusion_rate_end(0.f)
            {}

        bool        moving_xy()     const { return fabs(pos_end[0] - pos_start[0]) > 0.f || fabs(pos_end[1] - pos_start[1]) > 0.f; }
        bool        moving_z ()     const { return fabs(pos_end[2] - pos_start[2]) > 0.f; }
        bool        extruding()     const { return moving_xy() && pos_end[3] > pos_start[3]; }
        bool        retracting()    const { return pos_end[3] < pos_start[3]; }
        bool        deretracting()  const { return ! moving_xy() && pos_end[3] > pos_start[3]; }

        float       dist_xy2()      const { return (pos_end[0] - pos_start[0]) * (pos_end[0] - pos_start[0]) + (pos_end[1] - pos_start[1]) * (pos_end[1] - pos_start[1]); }
        float       dist_xyz2()     const { return (pos_end[0] - pos_start[0]) * (pos_end[0] - pos_start[0]) + (pos_end[1] - pos_start[1]) * (pos_end[1] - pos_start[1]) + (pos_end[2] - pos_start[2]) * (pos_end[2] - pos_start[2]); }
        float       dist_xy()       const { return sqrt(dist_xy2()); }
        float       dist_xyz()      const { return sqrt(dist_xyz2()); }
        float       dist_e()        const { return fabs(pos_end[3] - pos_start[3]); }

        float       feedrate()      const { return pos_end[4]; }
        float       time()          const { return dist_xyz() / feedrate(); }
        float       time_inv()      const { return feedrate() / dist_xyz(); }
        float       volumetric_correction_avg() const {

            float avg_correction = std::max(0.05f,0.5f * (volumetric_extrusion_rate_start + volumetric_extrusion_rate_end) / volumetric_extrusion_rate);
            assert(avg_correction > 0.f);
            assert(avg_correction <= 1.00000001f);
            return avg_correction;
        }

        GCodeLineType type;


        std::vector<char>   raw;
        size_t              raw_length;


        bool                modified;


        float       pos_start[5];
        float       pos_end[5];

        bool        pos_provided[5];


        size_t      extruder_id;

        ExtrusionRole extrusion_role;


        float       volumetric_extrusion_rate;

        float       volumetric_extrusion_rate_start;

        float       volumetric_extrusion_rate_end;



        float       max_volumetric_extrusion_rate_slope_positive;
        float       max_volumetric_extrusion_rate_slope_negative;

        bool        adjustable_flow       = false;

        bool        extrude_set_speed_tag = false;
        bool        extrude_end_tag       = false;
    };


    std::vector<char>               output_buffer;
    size_t                          output_buffer_length;
    size_t                          output_buffer_prev_length;

#ifdef PRESSURE_EQUALIZER_DEBUG

    size_t                          line_idx;
#endif

    bool process_line(const char *line, const char *line_end, GCodeLine &buf);
    long advance_segment_beyond_small_gap(long idx_cur_pos);
    void output_gcode_line(size_t line_idx);



    void adjust_volumetric_rate(size_t first_line_idx, size_t last_line_idx);


    inline void push_to_output(GCodeG1Formatter &formatter);
    inline void push_to_output(const std::string &text, bool add_eol);
    inline void push_to_output(const char *text, size_t len, bool add_eol = true);

    void push_line_to_output(size_t line_idx, float new_feedrate, const char *comment);

public:
    std::queue<LayerResult*> m_layer_results;

    std::vector<GCodeLine> m_gcode_lines;
};

}

#endif
