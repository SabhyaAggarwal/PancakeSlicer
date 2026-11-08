#include <cassert>
#include <cstdlib>
#include <boost/algorithm/string/predicate.hpp>
#include <cctype>
#include <cmath>
#include <array>
#include <iostream>
#include <math.h>
#include <limits>
#include <memory.h>
#include <cstring>
#include <cfloat>
#include <algorithm>
#include <string>

#include "../PrintConfig.hpp"
#include "../GCode.hpp"

#include "PressureEqualizer.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Exception.hpp"
#include "fast_float/fast_float.h"
#include "GCodeWriter.hpp"
#include "libslic3r/Config.hpp"

namespace Slic3r {

static const std::string EXTRUSION_ROLE_TAG = ";_EXTRUSION_ROLE:";
static const std::string EXTRUDE_END_TAG = ";_EXTRUDE_END";
static const std::string EXTRUDE_SET_SPEED_TAG = ";_EXTRUDE_SET_SPEED";
static const std::string EXTERNAL_PERIMETER_TAG = ";_EXTERNAL_PERIMETER";

static constexpr int max_look_back_limit = 128;

static constexpr long max_ignored_gap_between_extruding_segments = 3;

PressureEqualizer::PressureEqualizer(const Slic3r::GCodeConfig &config) : m_use_relative_e_distances(config.use_relative_e_distances.value)
{
    output_buffer.assign(32, 0);
    output_buffer_length      = 0;
    output_buffer_prev_length = 0;

    m_current_extruder = 0;
    memset(m_current_pos, 0, sizeof(float) * 5);
    m_current_extrusion_role = ExtrusionRole::erNone;
    m_retracted = true;
    
    m_max_segment_length = 2.f;

    m_filament_crossections.clear();
    for (double r : config.filament_diameter.values) {
        double a = 0.25f * M_PI * r * r;
        m_filament_crossections.push_back(float(a));
    }

    if(config.max_volumetric_extrusion_rate_slope.value > 0){
		m_max_volumetric_extrusion_rate_slope_positive = float(config.max_volumetric_extrusion_rate_slope.value) * 60.f * 60.f;
    	m_max_volumetric_extrusion_rate_slope_negative = float(config.max_volumetric_extrusion_rate_slope.value) * 60.f * 60.f;
    	m_max_segment_length = float(config.max_volumetric_extrusion_rate_slope_segment_length.value);
        m_extrusion_rate_smoothing_external_perimeter_only = bool(config.extrusion_rate_smoothing_external_perimeter_only.value);
    }

    for (ExtrusionRateSlope &extrusion_rate_slope : m_max_volumetric_extrusion_rate_slopes) {
        extrusion_rate_slope.negative = m_max_volumetric_extrusion_rate_slope_negative;
        extrusion_rate_slope.positive = m_max_volumetric_extrusion_rate_slope_positive;
    }
    
    for (const ExtrusionRole er : {ExtrusionRole::erIroning}) {
        m_max_volumetric_extrusion_rate_slopes[size_t(er)].negative = 0;
        m_max_volumetric_extrusion_rate_slopes[size_t(er)].positive = 0;
    }

    opened_extrude_set_speed_block = false;

#ifdef PRESSURE_EQUALIZER_STATISTIC
    m_stat.reset();
#endif

#ifdef PRESSURE_EQUALIZER_DEBUG
    line_idx = 0;
#endif
}

void PressureEqualizer::process_layer(const std::string &gcode)
{
    if (!gcode.empty()) {
        const char *gcode_begin = gcode.c_str();
        while (*gcode_begin != 0) {
            const char *gcode_end = gcode_begin;
            for (; *gcode_end != 0 && *gcode_end != '\n'; ++gcode_end);

            m_gcode_lines.emplace_back();
            if (!this->process_line(gcode_begin, gcode_end, m_gcode_lines.back())) {
                m_gcode_lines.pop_back();
            }
            gcode_begin = gcode_end;
            if (*gcode_begin == '\n')
                ++gcode_begin;
        }
        assert(!this->opened_extrude_set_speed_block);
    }
    
    long idx_end_current_extrusion = 0;
    while (idx_end_current_extrusion < m_gcode_lines.size()) {
        const long idx_begin_current_extrusion   = find_if(m_gcode_lines.begin() + idx_end_current_extrusion, m_gcode_lines.end(),
                                                          [](GCodeLine line) { return line.extruding(); }) - m_gcode_lines.begin();
        idx_end_current_extrusion = idx_begin_current_extrusion;

        while (idx_end_current_extrusion < m_gcode_lines.size()) {
            const auto just_after_end_extrusion = find_if(m_gcode_lines.begin() + idx_end_current_extrusion, m_gcode_lines.end(),
                                                          [](GCodeLine line) { return !line.extruding(); });
            idx_end_current_extrusion = std::max<long>(0,(just_after_end_extrusion - m_gcode_lines.begin()) - 1);
            const long idx_begin_segment_continuation = advance_segment_beyond_small_gap(idx_end_current_extrusion);
            if (idx_begin_segment_continuation > idx_end_current_extrusion) {
                idx_end_current_extrusion = idx_begin_segment_continuation;
                continue;
            } else {
                break;
            }
        }

        for (int i = idx_begin_current_extrusion; i < idx_end_current_extrusion; ++i) {
            const auto start_idx = std::max<long>(idx_begin_current_extrusion, i - max_look_back_limit);
            adjust_volumetric_rate(start_idx, i);
        }
        idx_end_current_extrusion++;
    }
}

long PressureEqualizer::advance_segment_beyond_small_gap(const long idx_orig)
{
    assert(m_gcode_lines[idx_orig].extruding());
    double distance_traveled = 0.0;
    for (auto idx_cur_pos = idx_orig + 1; idx_cur_pos < m_gcode_lines.size(); idx_cur_pos++) {
        if (m_gcode_lines[idx_cur_pos].extruding()) {
            return idx_cur_pos;
        }

        distance_traveled += m_gcode_lines[idx_cur_pos].dist_xy();
        if (distance_traveled > max_ignored_gap_between_extruding_segments) {
            return idx_orig;
        }
    }
     return idx_orig;
}

LayerResult PressureEqualizer::process_layer(LayerResult &&input)
{
    const bool   is_first_layer       = m_layer_results.empty();
    const size_t next_layer_first_idx = m_gcode_lines.size();

    if (!input.nop_layer_result) {
        this->process_layer(input.gcode);
        input.gcode.clear();
        m_layer_results.emplace(new LayerResult(input));
    }

    if (is_first_layer)
        return LayerResult::make_nop_layer_result();

    LayerResult *prev_layer_result = m_layer_results.front();
    m_layer_results.pop();

    output_buffer_length      = 0;
    output_buffer_prev_length = 0;
    for (size_t line_idx = 0; line_idx < next_layer_first_idx; ++line_idx)
        output_gcode_line(line_idx);
    m_gcode_lines.erase(m_gcode_lines.begin(), m_gcode_lines.begin() + int(next_layer_first_idx));

    if (output_buffer_length > 0)
        prev_layer_result->gcode = std::string(output_buffer.data());

    assert(!input.nop_layer_result || m_layer_results.empty());
    LayerResult out = *prev_layer_result;
    delete prev_layer_result;
    return out;
}

static inline bool is_ws(const char c) { return c == ' ' || c == '\t'; }
static inline bool is_eol(const char c) { return c == 0 || c == '\r' || c == '\n' || c == ';'; }
static inline bool is_ws_or_eol(const char c) { return is_ws(c) || is_eol(c); }

static void eatws(const char *&line)
{
    while (is_ws(*line)) 
        ++ line;
}

static inline int parse_int(const char *&line)
{
    char *endptr = nullptr;
    long result = strtol(line, &endptr, 10);
    if (endptr == nullptr || !is_ws_or_eol(*endptr))
        throw Slic3r::InvalidArgument("PressureEqualizer: Error parsing an int");
    line = endptr;
    return int(result);
}

float string_to_float_decimal_point(const char *line, const size_t str_len, size_t* pos)
{
    float out;
    size_t p = fast_float::from_chars(line, line + str_len, out).ptr - line;
    if (pos)
        *pos = p;
    return out;
}

static inline float parse_float(const char *&line, const size_t line_length)
{
    size_t endptr = 0;
    auto   result = string_to_float_decimal_point(line, line_length, &endptr);
    if (endptr == 0 || !is_ws_or_eol(*(line + endptr)))
        throw Slic3r::RuntimeError("PressureEqualizer: Error parsing a float");
    line = line + endptr;
    return result;
}

bool PressureEqualizer::process_line(const char *line, const char *line_end, GCodeLine &buf)
{
    const size_t len = line_end - line;
    if (strncmp(line, EXTRUSION_ROLE_TAG.data(), EXTRUSION_ROLE_TAG.length()) == 0) {
        line += EXTRUSION_ROLE_TAG.length();
        int role = atoi(line);
        m_current_extrusion_role = ExtrusionRole(role);
#ifdef PRESSURE_EQUALIZER_DEBUG
        ++line_idx;
#endif
        return false;
    }

    buf.type = GCODELINETYPE_OTHER;
    buf.modified = false;
    if (buf.raw.size() < len + 1)
        buf.raw.assign(line, line + len + 1);
    else
        memcpy(buf.raw.data(), line, len);
    buf.raw[len] = 0;
    buf.raw_length = len;

    memcpy(buf.pos_start, m_current_pos, sizeof(float)*5);
    memcpy(buf.pos_end, m_current_pos, sizeof(float)*5);
    memset(buf.pos_provided, 0, 5);

    buf.volumetric_extrusion_rate = 0.f;
    buf.volumetric_extrusion_rate_start = 0.f;
    buf.volumetric_extrusion_rate_end = 0.f;
    buf.max_volumetric_extrusion_rate_slope_positive = 0.f;
    buf.max_volumetric_extrusion_rate_slope_negative = 0.f;
	buf.extrusion_role = m_current_extrusion_role;

    std::string str_line(line, line_end);
    const bool found_extrude_set_speed_tag = boost::contains(str_line, EXTRUDE_SET_SPEED_TAG);
    const bool found_extrude_end_tag = boost::contains(str_line, EXTRUDE_END_TAG);
    assert(!found_extrude_set_speed_tag || !found_extrude_end_tag);

    if (found_extrude_set_speed_tag)
        this->opened_extrude_set_speed_block = true;
    else if (found_extrude_end_tag)
        this->opened_extrude_set_speed_block = false;

    switch (toupper(*line ++)) {
    case 'G': {
        int gcode = -1;
        try {
            gcode = parse_int(line);
        } catch (Slic3r::InvalidArgument &) {
            eatws(line);
            break;
        }

        assert(gcode != -1);
        eatws(line);
        switch (gcode) {
        case 0:
        case 1:
        {
            buf.adjustable_flow = this->opened_extrude_set_speed_block;
            buf.extrude_set_speed_tag = found_extrude_set_speed_tag;
            buf.extrude_end_tag = found_extrude_end_tag;
            float new_pos[5];
            memcpy(new_pos, m_current_pos, sizeof(float)*5);
            bool  changed[5] = { false, false, false, false, false };
            while (!is_eol(*line)) {
                const char axis = toupper(*line++);
                int  i = -1;
                switch (axis) {
                case 'X':
                case 'Y':
                case 'Z':
                    i = axis - 'X';
                    break;
                case 'E':
                    i = 3;
                    break;
                case 'F':
                    i = 4;
                    break;
                default:
                    break;
                }
                if (i != -1) {
                    buf.pos_provided[i] = true;
                    new_pos[i] = parse_float(line, line_end - line);
                    if (i == 3 && m_use_relative_e_distances)
                        new_pos[i] += m_current_pos[i];
                    changed[i] = new_pos[i] != m_current_pos[i];
                    eatws(line);
                }
            }
            if (changed[3]) {
                float diff = new_pos[3] - m_current_pos[3];
                if (diff < 0) {
                    buf.type = GCODELINETYPE_RETRACT;
                    m_retracted = true;
                } else if (! changed[0] && ! changed[1] && ! changed[2]) {
                    buf.type = GCODELINETYPE_UNRETRACT;
                    m_retracted = false;
                } else {
                    assert(changed[0] || changed[1]);
                    buf.type = GCODELINETYPE_EXTRUDE;
                    float diff[4];
                    for (size_t i = 0; i < 4; ++ i)
                        diff[i] = new_pos[i] - m_current_pos[i];
                    float len2 = diff[0]*diff[0]+diff[1]*diff[1]+diff[2]*diff[2];
                    float rate = m_filament_crossections[m_current_extruder] * new_pos[4] * sqrt((diff[3]*diff[3])/len2);
                    buf.volumetric_extrusion_rate       = rate;
                    buf.volumetric_extrusion_rate_start = rate;
                    buf.volumetric_extrusion_rate_end   = rate;

#ifdef PRESSURE_EQUALIZER_STATISTIC
                    m_stat.update(rate, sqrt(len2));
#endif
#ifdef PRESSURE_EQUALIZER_DEBUG
                    if (rate < 40.f) {
                        printf("Extremely low flow rate: %f. Line %d, Length: %f, extrusion: %f Old position: (%f, %f, %f), new position: (%f, %f, %f)\n",
                               rate, int(line_idx), sqrt(len2), sqrt((diff[3] * diff[3]) / len2), m_current_pos[0], m_current_pos[1], m_current_pos[2],
                               new_pos[0], new_pos[1], new_pos[2]);
                    }
#endif
                }
            } else if (changed[0] || changed[1] || changed[2]) {
                buf.type = GCODELINETYPE_MOVE;
            }
            memcpy(m_current_pos, new_pos, sizeof(float) * 5);
            break;
        }
        case 92: 
        {
            while (!is_eol(*line)) {
                const char axis = toupper(*line++);
                switch (axis) {
                case 'X':
                case 'Y':
                case 'Z':
                    m_current_pos[axis - 'X'] = (!is_ws_or_eol(*line)) ? parse_float(line, line_end - line) : 0.f;
                    break;
                case 'E':
                    m_current_pos[3] = (!is_ws_or_eol(*line)) ? parse_float(line, line_end - line) : 0.f;
                    break;
                default:
                    break;
                }
                eatws(line);
            }
            break;
        }
        case 10:
        case 22:
            buf.type = GCODELINETYPE_RETRACT;
            m_retracted = true;
            break;
        case 11:
        case 23:
            buf.type = GCODELINETYPE_UNRETRACT;
            m_retracted = false;
            break;
        default:
        break;
        }
        break;
    }
    case 'M': {
        eatws(line);
        break;
    }
    case 'T':
    {
        int new_extruder = -1;
        try {
            new_extruder = parse_int(line);
        } catch (Slic3r::InvalidArgument &) {
            eatws(line);
            break;
        }
        assert(new_extruder != -1);

        if (new_extruder != int(m_current_extruder)) {
            m_current_extruder = new_extruder;
            m_retracted = true;
            buf.type = GCODELINETYPE_TOOL_CHANGE;
        } else {
            buf.type = GCODELINETYPE_NOOP;
        }
        break;
    }
    }

    buf.extruder_id = m_current_extruder;
    memcpy(buf.pos_end, m_current_pos, sizeof(float)*5);
#ifdef PRESSURE_EQUALIZER_DEBUG
    ++line_idx;
#endif
    return true;
}

void PressureEqualizer::output_gcode_line(const size_t line_idx)
{
    GCodeLine &line = m_gcode_lines[line_idx];
    if (!line.modified) {
        push_to_output(line.raw.data(), line.raw_length, true);
        return;
    }

    const char *comment = line.raw.data();
    while (*comment != ';' && *comment != 0) ++comment;
    if (*comment != ';')
        comment = nullptr;

    float l = line.dist_xyz();
    auto nSegments = size_t(ceil(l / m_max_segment_length));
    
    int delta_volumetric_rate = std::round(std::max({
        fabs(line.volumetric_extrusion_rate_end - line.volumetric_extrusion_rate_start),
        fabs(line.volumetric_extrusion_rate - line.volumetric_extrusion_rate_start),
        fabs(line.volumetric_extrusion_rate - line.volumetric_extrusion_rate_end),
    }));
    
    constexpr int NON_TRIVIAL_RATE_DELTA = 10;
    if (nSegments == 1 || delta_volumetric_rate < NON_TRIVIAL_RATE_DELTA) {
        push_line_to_output(line_idx, line.feedrate() * line.volumetric_correction_avg(), comment);
    } else
    {
        const float original_feedrate = line.feedrate();
        line.pos_start[4] = line.volumetric_extrusion_rate_start * line.pos_end[4] / line.volumetric_extrusion_rate;
        line.pos_end  [4] = line.volumetric_extrusion_rate_end   * line.pos_end[4] / line.volumetric_extrusion_rate;

        if (line.volumetric_extrusion_rate > line.volumetric_extrusion_rate_start &&
            line.volumetric_extrusion_rate > line.volumetric_extrusion_rate_end) {
            const double original_extrusion = (double) l * line.volumetric_extrusion_rate / original_feedrate;

            const double min_steady_extrusion = original_extrusion * m_max_segment_length / l;
            const double max_sloped_extrusion = original_extrusion - min_steady_extrusion;
            assert(max_sloped_extrusion > 0);

            const auto   pow2              = [](const double x) { return x * x; };
            const double e_2               = pow2(line.volumetric_extrusion_rate);
            const double e0_2              = pow2(line.volumetric_extrusion_rate_start);
            const double e1_2              = pow2(line.volumetric_extrusion_rate_end);
            const double sp                = line.max_volumetric_extrusion_rate_slope_positive;
            const double sn                = line.max_volumetric_extrusion_rate_slope_negative;
            const double sloped_extrusion  = (e_2 - e0_2) / 2 / sp + (e_2 - e1_2) / 2 / sn;

            double target_max_extrusion_rate = line.volumetric_extrusion_rate;
            if (sloped_extrusion > max_sloped_extrusion) {
                target_max_extrusion_rate = std::sqrt((2 * max_sloped_extrusion * sp * sn + sn * e0_2 + sp * e1_2) / (sp + sn));

                if (target_max_extrusion_rate <= line.volumetric_extrusion_rate_start ||
                    target_max_extrusion_rate <= line.volumetric_extrusion_rate_end) {
                    goto single_slope_fallback;
                }
            }
            assert(target_max_extrusion_rate > line.volumetric_extrusion_rate_start);
            assert(target_max_extrusion_rate > line.volumetric_extrusion_rate_end);
            assert(target_max_extrusion_rate <= line.volumetric_extrusion_rate);

            delta_volumetric_rate = std::round(std::min({
                fabs(target_max_extrusion_rate - line.volumetric_extrusion_rate_start),
                fabs(target_max_extrusion_rate - line.volumetric_extrusion_rate_end),
            }));

            if (delta_volumetric_rate >= NON_TRIVIAL_RATE_DELTA) {
                const double target_max_feedrate = original_feedrate * target_max_extrusion_rate / line.volumetric_extrusion_rate;
                assert(target_max_feedrate <= original_feedrate);

                const double t_acc = (target_max_extrusion_rate - line.volumetric_extrusion_rate_start) / sp;
                const double l_acc = t_acc * (target_max_feedrate + line.pos_start[4]) / 2;
                const double t_dec = (target_max_extrusion_rate - line.volumetric_extrusion_rate_end) / sn;
                const double l_dec = t_dec * (target_max_feedrate + line.pos_end[4]) / 2;

                float pos_end_bak[5];
                memcpy(pos_end_bak, line.pos_end, sizeof(float) * 5);
                float pos_start[5];
                float pos_end[5];
                memcpy(pos_start, line.pos_start, sizeof(float) * 5);
                memcpy(pos_end, line.pos_end, sizeof(float) * 5);

                float t = l_acc / l;
                for (int i = 0; i < 4; ++i) {
                    pos_end[i]           = pos_start[i] + (pos_end_bak[i] - pos_start[i]) * t;
                    line.pos_provided[i] = true;
                }
                nSegments = size_t(ceil(l_acc / m_max_segment_length));
                assert(nSegments > 0);
                for (size_t i = 1; i <= nSegments; ++i) {
                    t = float(i) / float(nSegments);
                    for (size_t j = 0; j < 4; ++j) {
                        line.pos_end[j]      = pos_start[j] + (pos_end[j] - pos_start[j]) * t;
                        line.pos_provided[j] = true;
                    }
                    push_line_to_output(line_idx, pos_start[4] + (target_max_feedrate - pos_start[4]) * (float(i) - 0.5f) / float(nSegments), comment);
                    comment = nullptr;
                    memcpy(line.pos_start, line.pos_end, sizeof(float)*5);
                }

                t = (l - l_dec) / l;
                for (int i = 0; i < 4; ++i) {
                    line.pos_end[i] = pos_start[i] + (pos_end_bak[i] - pos_start[i]) * t;
                }
                push_line_to_output(line_idx, target_max_feedrate, nullptr);
                memcpy(line.pos_start, line.pos_end, sizeof(float) * 5);

                memcpy(pos_start, line.pos_end, sizeof(float) * 5);
                line.pos_start[4] = target_max_feedrate;
                pos_start[4]      = target_max_feedrate;
                nSegments = size_t(ceil(l_dec / m_max_segment_length));
                assert(nSegments > 0);
                for (size_t i = 1; i <= nSegments; ++ i) {
                    t = float(i) / float(nSegments);
                    for (size_t j = 0; j < 4; ++ j) {
                        line.pos_end[j] = pos_start[j] + (pos_end_bak[j] - pos_start[j]) * t;
                    } 
                    push_line_to_output(line_idx, pos_start[4] + (pos_end_bak[4] - pos_start[4]) * (float(i) - 0.5f) / float(nSegments), nullptr);
                    memcpy(line.pos_start, line.pos_end, sizeof(float)*5);
                }

                for (int i = 0; i < 4; ++i) {
                    line.pos_end[i] = pos_end_bak[i];
                }
                push_line_to_output(line_idx, pos_end_bak[4], nullptr);

                return;
            }
        }
single_slope_fallback:
        bool accelerating = line.volumetric_extrusion_rate_start < line.volumetric_extrusion_rate_end;
        float feed_avg = 0.5f * (line.pos_start[4] + line.pos_end[4]);
        float max_volumetric_extrusion_rate_slope = accelerating ? line.max_volumetric_extrusion_rate_slope_positive :
                                                                   line.max_volumetric_extrusion_rate_slope_negative;
        float t_total = line.dist_xyz() / feed_avg;
        float t_acc    = std::fabs(line.volumetric_extrusion_rate_start - line.volumetric_extrusion_rate_end) / max_volumetric_extrusion_rate_slope;
        float l_acc    = l;
        float l_steady = 0.f;
        if (t_acc < t_total) {
            l_acc    = t_acc * feed_avg;
            l_steady = l - l_acc;
            if (l_steady < 0.5f * m_max_segment_length) {
                l_acc    = l;
                l_steady = 0.f;
            } else
                nSegments = size_t(ceil(l_acc / m_max_segment_length));
        }
        float pos_start[5];
        float pos_end[5];
        float pos_end2[4];
        memcpy(pos_start, line.pos_start, sizeof(float) * 5);
        memcpy(pos_end, line.pos_end, sizeof(float) * 5);
        if (l_steady > 0.f) {
            if (accelerating) {
                memcpy(pos_end2, pos_end, sizeof(float)*4);
                float t = l_acc / l;
                for (int i = 0; i < 4; ++ i) {
                    pos_end[i] = pos_start[i] + (pos_end[i] - pos_start[i]) * t;
                    line.pos_provided[i] = true;
                }
            } else {
                float t = l_steady / l;
                for (int i = 0; i < 4; ++ i) {
                    line.pos_end[i] = pos_start[i] + (pos_end[i] - pos_start[i]) * t;
                    line.pos_provided[i] = true;
                }
                push_line_to_output(line_idx, pos_start[4], comment);
                comment = nullptr;

                float new_pos_start_feedrate = pos_start[4];

                memcpy(line.pos_start, line.pos_end, sizeof(float)*5);
                memcpy(pos_start, line.pos_end, sizeof(float)*5);

                line.pos_start[4] = new_pos_start_feedrate;
                pos_start[4] = new_pos_start_feedrate;
            }
        }
        for (size_t i = 1; i < nSegments; ++ i) {
            float t = float(i) / float(nSegments);
            for (size_t j = 0; j < 4; ++ j) {
                line.pos_end[j] = pos_start[j] + (pos_end[j] - pos_start[j]) * t;
                line.pos_provided[j] = true;
            } 
            push_line_to_output(line_idx, pos_start[4] + (pos_end[4] - pos_start[4]) * (float(i) - 0.5f) / float(nSegments), comment);
            comment = nullptr;
            memcpy(line.pos_start, line.pos_end, sizeof(float)*5);
        }
		if (l_steady > 0.f && accelerating) {
            for (int i = 0; i < 4; ++ i) {
                line.pos_end[i] = pos_end2[i];
                line.pos_provided[i] = true;
            }
            push_line_to_output(line_idx, pos_end[4], comment);
        } else {
            for (int i = 0; i < 4; ++ i) {
                line.pos_end[i] = pos_end[i];
                line.pos_provided[i] = true;
            }
            push_line_to_output(line_idx, pos_end[4], comment);
        }
    }
}

void PressureEqualizer::adjust_volumetric_rate(const size_t first_line_idx, const size_t last_line_idx)
{
    if (last_line_idx - first_line_idx < 2)
        return;

    size_t line_idx = last_line_idx;
    if (line_idx == first_line_idx || !m_gcode_lines[line_idx].extruding())
        return;

    std::array<float, size_t(ExtrusionRole::erCount)> feedrate_per_extrusion_role{};
    feedrate_per_extrusion_role.fill(std::numeric_limits<float>::max());
    feedrate_per_extrusion_role[int(m_gcode_lines[line_idx].extrusion_role)] = m_gcode_lines[line_idx].volumetric_extrusion_rate_start;

    while (line_idx != first_line_idx) {
        size_t idx_prev = line_idx - 1;
        for (; !m_gcode_lines[idx_prev].extruding() && idx_prev != first_line_idx; --idx_prev);
        if (!m_gcode_lines[idx_prev].extruding())
            break;
        if (m_gcode_lines[line_idx].extrusion_role == ExtrusionRole::erIroning) {
            line_idx = idx_prev;
            continue;
        }
        float rate_succ = m_gcode_lines[line_idx].volumetric_extrusion_rate_start;
        line_idx        = idx_prev;
        GCodeLine &line = m_gcode_lines[line_idx];

        for (size_t iRole = 1; iRole < size_t(ExtrusionRole::erCount); ++ iRole) {
            const float &rate_slope = m_max_volumetric_extrusion_rate_slopes[iRole].negative;
            if (rate_slope == 0 || feedrate_per_extrusion_role[iRole] == std::numeric_limits<float>::max())
                continue;

            float rate_end = feedrate_per_extrusion_role[iRole];
            if (iRole == size_t(line.extrusion_role) && rate_succ < rate_end)
                rate_end = rate_succ;

            if (!line.adjustable_flow || line.extrusion_role == ExtrusionRole::erBridgeInfill || line.extrusion_role == ExtrusionRole::erIroning ||
                (m_extrusion_rate_smoothing_external_perimeter_only && line.extrusion_role != ExtrusionRole::erOverhangPerimeter && line.extrusion_role != ExtrusionRole::erExternalPerimeter)) {
                rate_end = line.volumetric_extrusion_rate_end;
            } else if (line.volumetric_extrusion_rate_end > rate_end) {
                line.volumetric_extrusion_rate_end = rate_end;
                line.max_volumetric_extrusion_rate_slope_negative = rate_slope;
                line.modified = true;
            } else if (iRole == size_t(line.extrusion_role)) {
                rate_end = line.volumetric_extrusion_rate_end;
            } else {
            }

            if (line.adjustable_flow) {
                float rate_start = sqrt(rate_end * rate_end + 2 * line.volumetric_extrusion_rate * line.dist_xyz() * rate_slope / line.feedrate());
                if (rate_start < line.volumetric_extrusion_rate_start) {
                    line.volumetric_extrusion_rate_start = rate_start;
                    line.max_volumetric_extrusion_rate_slope_negative = rate_slope;
                    line.modified = true;
                }
            }
            if (line.extrusion_role != ExtrusionRole::erIroning)
                feedrate_per_extrusion_role[iRole] = line.volumetric_extrusion_rate_start;
        }
    }

    feedrate_per_extrusion_role.fill(std::numeric_limits<float>::max());
    feedrate_per_extrusion_role[size_t(m_gcode_lines[line_idx].extrusion_role)] = m_gcode_lines[line_idx].volumetric_extrusion_rate_end;

    assert(m_gcode_lines[line_idx].extruding());
    while (line_idx != last_line_idx) {
        size_t idx_next = line_idx + 1;
        for (; !m_gcode_lines[idx_next].extruding() && idx_next != last_line_idx; ++idx_next);
        if (!m_gcode_lines[idx_next].extruding())
            break;
        if (m_gcode_lines[line_idx].extrusion_role == ExtrusionRole::erIroning) {
            line_idx = idx_next;
            continue;
        }
        float rate_prec = m_gcode_lines[line_idx].volumetric_extrusion_rate_end;
        line_idx = idx_next;
        GCodeLine &line = m_gcode_lines[line_idx];

        for (size_t iRole = 1; iRole < size_t(ExtrusionRole::erCount); ++ iRole) {
            const float &rate_slope = m_max_volumetric_extrusion_rate_slopes[iRole].positive;
            if (rate_slope == 0 || feedrate_per_extrusion_role[iRole] == std::numeric_limits<float>::max())
                continue;

            float rate_start = feedrate_per_extrusion_role[iRole];
            if (!line.adjustable_flow || line.extrusion_role == ExtrusionRole::erBridgeInfill || line.extrusion_role == ExtrusionRole::erIroning ||
                (m_extrusion_rate_smoothing_external_perimeter_only && line.extrusion_role != ExtrusionRole::erOverhangPerimeter && line.extrusion_role != ExtrusionRole::erExternalPerimeter)) {
                rate_start = line.volumetric_extrusion_rate_start;
            } else if (iRole == size_t(line.extrusion_role) && rate_prec < rate_start)
                rate_start = rate_prec;

            if (line.volumetric_extrusion_rate_start > rate_start) {
                line.volumetric_extrusion_rate_start = rate_start;
                line.max_volumetric_extrusion_rate_slope_positive = rate_slope;
                line.modified = true;
            } else if (iRole == size_t(line.extrusion_role)) {
                rate_start = line.volumetric_extrusion_rate_start;
            } else {
            }

            if (line.adjustable_flow) {
                float rate_end = sqrt(rate_start * rate_start + 2 * line.volumetric_extrusion_rate * line.dist_xyz() * rate_slope / line.feedrate());
                if (rate_end < line.volumetric_extrusion_rate_end) {
                    line.volumetric_extrusion_rate_end                = rate_end;
                    line.max_volumetric_extrusion_rate_slope_positive = rate_slope;
                    line.modified                                     = true;
                }
            }
            if (line.extrusion_role != ExtrusionRole::erIroning)
                feedrate_per_extrusion_role[iRole] = line.volumetric_extrusion_rate_end;
        }
    }
}

inline void PressureEqualizer::push_to_output(GCodeG1Formatter &formatter)
{
    return this->push_to_output(formatter.string(), false);
}

inline void PressureEqualizer::push_to_output(const std::string &text, bool add_eol)
{
    return this->push_to_output(text.data(), text.size(), add_eol);
}

inline void PressureEqualizer::push_to_output(const char *text, const size_t len, bool add_eol)
{
    size_t len_new = output_buffer_length + len + 1;
    if (add_eol)
        ++len_new;

    if (output_buffer.size() < len_new) {
        size_t v = len_new;
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        output_buffer.resize(v);
    }

    if (len != 0) {
        memcpy(output_buffer.data() + output_buffer_length, text, len);
        this->output_buffer_prev_length = this->output_buffer_length;
        output_buffer_length += len;
    }
    if (add_eol)
        output_buffer[output_buffer_length++] = '\n';
    output_buffer[output_buffer_length] = 0;
}

inline bool is_just_line_with_extrude_set_speed_tag(const std::string &line)
{
    if (line.empty() && !boost::starts_with(line, "G1 ") && !boost::ends_with(line, EXTRUDE_SET_SPEED_TAG))
        return false;

    const char       *p_line   = line.data() + 3;
    const char *const line_end = line.data() + line.length() - 1;
    while (!is_eol(*p_line)) {
        if (toupper(*p_line++) == 'F')
            break;
        else
            return false;
    }
    parse_float(p_line, line_end - p_line);
    eatws(p_line);
    p_line += EXTRUDE_SET_SPEED_TAG.length();
    return p_line <= line_end && is_eol(*p_line);
}

void PressureEqualizer::push_line_to_output(const size_t line_idx, float new_feedrate, const char *comment)
{
    if (new_feedrate < 60)
        new_feedrate = 60;
    new_feedrate = std::round(new_feedrate / 60.0) * 60.0;
    const GCodeLine &line = m_gcode_lines[line_idx];
    if (line_idx > 0 && output_buffer_length > 0) {
        const std::string prev_line_str = std::string(output_buffer.begin() + int(this->output_buffer_prev_length),
                                                      output_buffer.begin() + int(this->output_buffer_length) + 1);
        if (is_just_line_with_extrude_set_speed_tag(prev_line_str))
            this->output_buffer_length = this->output_buffer_prev_length;
        else
            push_to_output(EXTRUDE_END_TAG.data(), EXTRUDE_END_TAG.length(), true);
    } else
        push_to_output(EXTRUDE_END_TAG.data(), EXTRUDE_END_TAG.length(), true);

    GCodeG1Formatter feedrate_formatter;
    feedrate_formatter.emit_f(new_feedrate);
    feedrate_formatter.emit_string(std::string(EXTRUDE_SET_SPEED_TAG.data(), EXTRUDE_SET_SPEED_TAG.length()));
    if (line.extrusion_role == ExtrusionRole::erExternalPerimeter)
        feedrate_formatter.emit_string(std::string(EXTERNAL_PERIMETER_TAG.data(), EXTERNAL_PERIMETER_TAG.length()));
    push_to_output(feedrate_formatter);

    GCodeG1Formatter extrusion_formatter;
    for (size_t axis_idx = 0; axis_idx < 3; ++axis_idx)
        if (line.pos_provided[axis_idx])
            extrusion_formatter.emit_axis(char('X' + axis_idx), line.pos_end[axis_idx], GCodeFormatter::XYZF_EXPORT_DIGITS);
    extrusion_formatter.emit_axis('E', m_use_relative_e_distances ? (line.pos_end[3] - line.pos_start[3]) : line.pos_end[3], GCodeFormatter::E_EXPORT_DIGITS);

    if (comment != nullptr)
        extrusion_formatter.emit_string(std::string(comment));

    push_to_output(extrusion_formatter);
}

}
