#include "../GCode.hpp"
#include "../LocalesUtils.hpp"
#include "libslic3r/Extruder.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Circle.hpp"
#include "CoolingBuffer.hpp"
#include "ConformalCooling.hpp"
#include <algorithm>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/log/trivial.hpp>
#include <cstddef>
#include <cstdlib>
#include <cmath>
#include <charconv>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <float.h>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>
#include <utility>

#if 0
    #define DEBUG
    #define _DEBUG
    #undef NDEBUG
#endif

#include <assert.h>
#include "libslic3r/Config.hpp"
#include "libslic3r/GCodeWriter.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

CoolingBuffer::CoolingBuffer(GCode &gcodegen) : m_config(gcodegen.config()), m_gcodegen(gcodegen), m_toolchange_prefix(gcodegen.writer().toolchange_prefix()), m_current_extruder(0), m_current_nozzle(0)
{
    this->reset(gcodegen.writer().get_position());

    const std::vector<Extruder> &extruders = gcodegen.writer().extruders();
    m_extruder_ids.reserve(extruders.size());
    for (const Extruder &ex : extruders) {
        m_num_extruders = std::max(ex.id() + 1, m_num_extruders);
        m_extruder_ids.emplace_back(ex.id());
    }

}

void CoolingBuffer::reset(const Vec3d &position)
{
    m_nominal_feed = m_emitted_feed = 0.;
    m_belt_band_active = false;
    m_current_pos.assign(7, 0.f);
    m_current_pos[0] = float(position.x());
    m_current_pos[1] = float(position.y());
    m_current_pos[2] = float(position.z());
    m_current_pos[4] = float(m_config.travel_speed.get_at(m_current_nozzle));
    m_fan_speed = -1;
    m_additional_fan_speed = -1;
    m_current_fan_speed = -1;
}

struct CoolingLine
{
    enum Type {
        TYPE_SET_TOOL           = 1 << 0,
        TYPE_EXTRUDE_END        = 1 << 1,
        TYPE_OVERHANG_FAN_START = 1 << 2,
        TYPE_OVERHANG_FAN_END   = 1 << 3,
        TYPE_G0                 = 1 << 4,
        TYPE_G1                 = 1 << 5,
        TYPE_ADJUSTABLE         = 1 << 6,
        TYPE_EXTERNAL_PERIMETER = 1 << 7,
        TYPE_HAS_F              = 1 << 8,
        TYPE_WIPE               = 1 << 9,
        TYPE_G4                 = 1 << 10,
        TYPE_G92                = 1 << 11,
        TYPE_G2                 = 1 << 12,
        TYPE_G3                 = 1 << 13,
        TYPE_FORCE_RESUME_FAN   = 1 << 14,
        TYPE_SUPPORT_INTERFACE_FAN_START     = 1 << 15,
        TYPE_SUPPORT_INTERFACE_FAN_END       = 1 << 16,
        TYPE_INTERNAL_BRIDGE_FAN_START = 1 << 17,
        TYPE_INTERNAL_BRIDGE_FAN_END   = 1 << 18,
        TYPE_IRONING_FAN_START         = 1 << 19,
        TYPE_IRONING_FAN_END           = 1 << 20,
        TYPE_BELT_BAND_START           = 1 << 21,
        TYPE_BELT_BAND_END             = 1 << 22,
        TYPE_KEEP_VERBATIM             = 1 << 23,
        TYPE_NP_FAN                    = 1 << 24,
        TYPE_NP_FAN_END                = 1 << 25,
        TYPE_NP_PASS_BEGIN             = 1 << 26,
        TYPE_NP_PASS_HOLD              = 1 << 27,
    };

    CoolingLine(unsigned int type, size_t  line_start, size_t  line_end) :
        type(type), line_start(line_start), line_end(line_end),
        length(0.f), feedrate(0.f), time(0.f), time_max(0.f), slowdown(false) {}

    bool adjustable(bool slowdown_external_perimeters) const {
        return (this->type & TYPE_ADJUSTABLE) &&
               (! (this->type & TYPE_EXTERNAL_PERIMETER) || slowdown_external_perimeters) &&
               this->time < this->time_max;
    }

    bool adjustable() const {
        return (this->type & TYPE_ADJUSTABLE) && this->time < this->time_max;
    }

    size_t  type;
    size_t  line_start;
    size_t  line_end;
    float   length;
    float   feedrate;
    float   time;
    float   time_max;
    bool    slowdown;
    float   value  = 0.f;
    float   value2 = 0.f;
};

struct PerExtruderAdjustments
{
    float elapsed_time_total() const {
        float time_total = 0.f;
        for (const CoolingLine &line : lines)
            time_total += line.time;
        return time_total;
    }
    float maximum_time_after_slowdown(bool slowdown_external_perimeters) const {
        float time_total = 0.f;
        for (const CoolingLine &line : lines)
            if (line.adjustable(slowdown_external_perimeters)) {
                if (line.time_max == FLT_MAX)
                    return FLT_MAX;
                else
                    time_total += line.time_max;
            } else
                time_total += line.time;
        return time_total;
    }
    float adjustable_time(bool slowdown_external_perimeters) const {
        float time_total = 0.f;
        for (const CoolingLine &line : lines)
            if (line.adjustable(slowdown_external_perimeters))
                time_total += line.time;
        return time_total;
    }
    float non_adjustable_time(bool slowdown_external_perimeters) const {
        float time_total = 0.f;
        for (const CoolingLine &line : lines)
            if (! line.adjustable(slowdown_external_perimeters))
                time_total += line.time;
        return time_total;
    }
    float slowdown_to_minimum_feedrate(bool slowdown_external_perimeters) {
        float time_total = 0.f;
        for (CoolingLine &line : lines) {
            if (line.adjustable(slowdown_external_perimeters)) {
                assert(line.time_max >= 0.f && line.time_max < FLT_MAX);
                line.slowdown = true;
                line.time     = line.time_max;
                if (line.time > 0.f)
                    line.feedrate = line.length / line.time;
            }
            time_total += line.time;
        }
        this->time_total = time_total;
        return time_total;
    }
    float slow_down_proportional(float factor, bool slowdown_external_perimeters) {
        assert(factor >= 1.f);
        float time_total = 0.f;
        for (CoolingLine &line : lines) {
            if (line.adjustable(slowdown_external_perimeters)) {
                line.slowdown = true;
                line.time     = std::min(line.time_max, line.time * factor);
                if (line.time > 0.f)
                    line.feedrate = line.length / line.time;
            }
            time_total += line.time;
        }
        this->time_total = time_total;
        return time_total;
    }

    void sort_lines_by_decreasing_feedrate() {
        std::sort(lines.begin(), lines.end(), [](const CoolingLine &l1, const CoolingLine &l2) {
            bool adj1 = l1.adjustable();
            bool adj2 = l2.adjustable();
            return (adj1 == adj2) ? l1.feedrate > l2.feedrate : adj1;
        });
        for (n_lines_adjustable = 0;
            n_lines_adjustable < lines.size() && this->lines[n_lines_adjustable].adjustable();
            ++ n_lines_adjustable);
        time_non_adjustable = 0.f;
        for (size_t i = n_lines_adjustable; i < lines.size(); ++ i)
            time_non_adjustable += lines[i].time;
    }

    float time_stretch_when_slowing_down_to_feedrate(float min_feedrate) const {
        float time_stretch = 0.f;
        assert(this->slow_down_min_speed < min_feedrate + EPSILON);
        for (size_t i = 0; i < n_lines_adjustable; ++ i) {
            const CoolingLine &line = lines[i];
            if (line.feedrate > min_feedrate)
                time_stretch += line.time * (line.feedrate / min_feedrate - 1.f);
        }
        return time_stretch;
    }

    void slow_down_to_feedrate(float min_feedrate) {
        assert(this->slow_down_min_speed < min_feedrate + EPSILON);
        float time_total = 0.f;
        for (size_t i = 0; i < n_lines_adjustable; ++ i) {
            CoolingLine &line = lines[i];
            if (line.feedrate > min_feedrate) {
                line.time *= std::max(1.f, line.feedrate / min_feedrate);
                line.feedrate = min_feedrate;
                line.slowdown = true;
            }
            time_total += line.time;
        }
        this->time_total = time_total;
    }

    unsigned int                extruder_id         = 0;
    bool                        cooling_slow_down_enabled = false;
    float                       slow_down_layer_time = 0.f;
    float                       slow_down_min_speed     = 0.f;
    
    bool                        dont_slow_down_outer_wall = false;


    std::vector<CoolingLine>    lines;
    size_t                      n_lines_adjustable  = 0;
    float                       time_non_adjustable = 0;
    float                       time_total          = 0;
    float                       time_maximum        = 0;

    size_t                      idx_line_begin      = 0;
    size_t                      idx_line_end        = 0;
};

float new_feedrate_to_reach_time_stretch(
    std::vector<PerExtruderAdjustments*>::const_iterator it_begin, std::vector<PerExtruderAdjustments*>::const_iterator it_end,
    float min_feedrate, float time_stretch, size_t max_iter = 20)
{
	float new_feedrate = min_feedrate;
    for (size_t iter = 0; iter < max_iter; ++ iter) {
        double nomin = 0;
        double denom = time_stretch;
        for (auto it = it_begin; it != it_end; ++ it) {
			assert((*it)->slow_down_min_speed < min_feedrate + EPSILON);
			for (size_t i = 0; i < (*it)->n_lines_adjustable; ++i) {
				const CoolingLine &line = (*it)->lines[i];
                if (line.feedrate > min_feedrate) {
                    nomin += (double)line.time * (double)line.feedrate;
                    denom += (double)line.time;
                }
            }
        }
        assert(denom > 0);
        if (denom < 0)
            return min_feedrate;
        new_feedrate = (float)(nomin / denom);
        assert(new_feedrate > min_feedrate - EPSILON);
        if (new_feedrate < min_feedrate + EPSILON)
            goto finished;
        for (auto it = it_begin; it != it_end; ++ it)
			for (size_t i = 0; i < (*it)->n_lines_adjustable; ++i) {
				const CoolingLine &line = (*it)->lines[i];
                if (line.feedrate > min_feedrate && line.feedrate < new_feedrate)
                    goto not_finished_yet;
            }
        goto finished;
not_finished_yet:
        min_feedrate = new_feedrate;
    }

finished:
#ifndef NDEBUG
    {
        float time_stretch_final = 0.f;
        for (auto it = it_begin; it != it_end; ++ it)
            time_stretch_final += (*it)->time_stretch_when_slowing_down_to_feedrate(new_feedrate);
        assert(std::abs(time_stretch - time_stretch_final) < EPSILON);
    }
#endif

	return new_feedrate;
}

std::string CoolingBuffer::process_layer(std::string &&gcode, size_t layer_id, bool flush)
{
    if (m_gcode.empty())
        m_gcode = std::move(gcode);
    else
        m_gcode += gcode;

    std::string out;
    if (flush) {
        std::vector<PerExtruderAdjustments> per_extruder_adjustments = this->parse_layer_gcode(m_gcode, m_current_pos, m_inverse_time);
        float layer_time_stretched = this->calculate_layer_slowdown(per_extruder_adjustments);
        layer_time_stretched += this->apply_nonplanar_pass_time(per_extruder_adjustments);
        out = this->apply_layer_cooldown(m_gcode, layer_id, layer_time_stretched, per_extruder_adjustments);
        out = this->apply_zaa_feed_limits(out);
        m_gcode.clear();
    }
    return out;
}

std::string CoolingBuffer::apply_zaa_feed_limits(const std::string &gcode)
{
    std::string out;
    out.reserve(gcode.size());
    double cap = 0.;
    for (size_t start = 0; start < gcode.size();) {
        const size_t end = gcode.find('\n', start);
        std::string line = gcode.substr(start, end == std::string::npos ? end : end - start);
        start = end == std::string::npos ? gcode.size() : end + 1;
        if (boost::starts_with(line, ZaaMaxFeedTag)) {
            cap = atof_decimal_point(line.c_str() + std::strlen(ZaaMaxFeedTag));
            continue;
        }
        if (line.size() > 2 && line[0] == 'G' && line[1] >= '0' && line[1] <= '3' && line[2] == ' ') {
            const size_t comment = line.find(';');
            const size_t f = line.find(" F");
            const bool has_f = f != std::string::npos && (comment == std::string::npos || f < comment);
            if (has_f)
                m_nominal_feed = atof_decimal_point(line.c_str() + f + 2);
            const double feed = cap > 0. ? std::min(m_nominal_feed, cap) : m_nominal_feed;
            if (feed > 0. && (has_f || feed != m_emitted_feed)) {
                if (has_f && feed != m_nominal_feed) {
                    const size_t word_end = line.find_first_of(" \t;", f + 2);
                    line.replace(f + 2, word_end == std::string::npos ? word_end : word_end - f - 2,
                                 std::to_string(static_cast<int>(std::floor(feed))));
                } else if (!has_f) {
                    const size_t insert = comment == std::string::npos ? line.size() : comment;
                    line.insert(insert, " F" + std::to_string(static_cast<int>(std::floor(feed))) + (comment == std::string::npos ? "" : " "));
                }
                m_emitted_feed = feed;
            }
            cap = 0.;
        }
        out += line;
        if (end != std::string::npos)
            out += '\n';
    }
    return out;
}

std::vector<PerExtruderAdjustments> CoolingBuffer::parse_layer_gcode(const std::string &gcode, std::vector<float> &current_pos, bool &inverse_time) const
{
    std::vector<PerExtruderAdjustments> per_extruder_adjustments(m_extruder_ids.size());
    std::vector<size_t>                 map_extruder_to_per_extruder_adjustment(m_num_extruders, 0);
    for (size_t i = 0; i < m_extruder_ids.size(); ++ i) {
        PerExtruderAdjustments &adj         = per_extruder_adjustments[i];
        unsigned int            extruder_id = m_extruder_ids[i];
        adj.extruder_id               = extruder_id;
        adj.cooling_slow_down_enabled = m_config.slow_down_for_layer_cooling.get_at(extruder_id);
        adj.slow_down_layer_time = float(m_config.slow_down_layer_time.get_at(extruder_id));
        adj.slow_down_min_speed           = float(m_config.slow_down_min_speed.get_at(extruder_id));
        adj.dont_slow_down_outer_wall   = m_config.dont_slow_down_outer_wall.get_at(extruder_id);
        map_extruder_to_per_extruder_adjustment[extruder_id] = i;
    }

    unsigned int      current_extruder  = m_current_extruder;
    PerExtruderAdjustments *adjustment  = &per_extruder_adjustments[map_extruder_to_per_extruder_adjustment[current_extruder]];
    const char       *line_start = gcode.c_str();
    const char       *line_end   = line_start;
    size_t            active_speed_modifier = size_t(-1);

    bool layer_had_extrusion = false;
    const bool rotary_printer  = m_config.nonplanar_kinematics.value != RotaryLayout::None;
    bool       nonplanar_block = false;

    for (; *line_start != 0; line_start = line_end)
    {
        while (*line_end != '\n' && *line_end != 0)
            ++ line_end;
        std::string sline(line_start, line_end);
        if (*line_end == '\n')
            ++ line_end;
        CoolingLine line(0, line_start - gcode.c_str(), line_end - gcode.c_str());
        if (boost::starts_with(sline, "G0 "))
            line.type = CoolingLine::TYPE_G0;
        else if (boost::starts_with(sline, "G1 "))
            line.type = CoolingLine::TYPE_G1;
        else if (boost::starts_with(sline, "G92 "))
            line.type = CoolingLine::TYPE_G92;
        else if (boost::starts_with(sline, "G2 "))
            line.type = CoolingLine::TYPE_G2;
        else if (boost::starts_with(sline, "G3 "))
            line.type = CoolingLine::TYPE_G3;
        if (boost::starts_with(sline, "G93"))
            inverse_time = true;
        else if (boost::starts_with(sline, "G94"))
            inverse_time = false;
        else if (boost::starts_with(sline, ";_NONPLANAR_BEGIN"))
            nonplanar_block = true;
        else if (boost::starts_with(sline, ";_NONPLANAR_END"))
            nonplanar_block = false;
        if (line.type) {
            std::vector<float> new_pos(current_pos);
            const char *c = sline.data() + 3;
            bool  rotary = false;
            float inline_f = 0.f;
            for (;;) {
                for (; *c == ' ' || *c == '\t'; ++ c);
                if (*c == 0 || *c == ';')
                    break;

                size_t axis = (*c >= 'X' && *c <= 'Z') ? (*c - 'X') :
                              (*c == 'E') ? 3 : (*c == 'F') ? 4 :
                              (*c == 'I') ? 5 : (*c == 'J') ? 6 : size_t(-1);
                if (*c >= 'A' && *c <= 'C')
                    rotary = true;
                if (axis != size_t(-1)) {
                    ++ c;
                    new_pos[axis] = float(atof_decimal_point(std::string_view(c, sline.data() + sline.size() - c)));
                    if (axis == 4) {
                        inline_f = new_pos[4];
                        new_pos[4] /= 60.f;
                        if ((line.type & CoolingLine::TYPE_G92) == 0)
                            line.type |= CoolingLine::TYPE_HAS_F;
                    } else if (axis == 5 || axis == 6) {
                        new_pos[axis] += current_pos[axis - 5];
                    }
                }
                for (; *c != ' ' && *c != '\t' && *c != 0; ++ c);
            }
            if ((line.type & CoolingLine::TYPE_G92) == 0 && rotary_printer && (inverse_time || rotary || nonplanar_block)) {
                line.type = CoolingLine::TYPE_G1 | CoolingLine::TYPE_KEEP_VERBATIM;
                const float feed = inline_f > 0.f ? inline_f : 60.f * current_pos[4];
                if (inverse_time)
                    line.time = inline_f > 0.f ? 60.f / inline_f : 0.f;
                else if (feed > 0.f)
                    line.time = 60.f * Vec3f(new_pos[0] - current_pos[0], new_pos[1] - current_pos[1], new_pos[2] - current_pos[2]).norm() / feed;
                line.time_max = line.time;
                new_pos[4]    = current_pos[4];
                current_pos   = std::move(new_pos);
                if (!layer_had_extrusion)
                    line.time = line.time_max = 0;
                adjustment->lines.emplace_back(std::move(line));
                continue;
            }
            bool external_perimeter = boost::contains(sline, ";_EXTERNAL_PERIMETER");
            bool wipe               = boost::contains(sline, ";_WIPE");
            if (external_perimeter)
                line.type |= CoolingLine::TYPE_EXTERNAL_PERIMETER;
            if (wipe)
                line.type |= CoolingLine::TYPE_WIPE;

            if (boost::contains(sline, ";_EXTRUDE_SET_SPEED"))
                layer_had_extrusion = true;
            
            bool adjust_external = true;
            if(adjustment->dont_slow_down_outer_wall && external_perimeter) adjust_external = false;
            
            if ((line.type & CoolingLine::TYPE_HAS_F) && ! boost::contains(sline, ";_EXTRUDE_SET_SPEED"))
                active_speed_modifier = size_t(-1);
            if (boost::contains(sline, ";_EXTRUDE_SET_SPEED") && ! wipe && adjust_external) {
                line.type |= CoolingLine::TYPE_ADJUSTABLE;
                active_speed_modifier = adjustment->lines.size();
            }
            if ((line.type & CoolingLine::TYPE_G92) == 0) {
                if (m_config.use_relative_e_distances.value)
                    current_pos[3] = 0.f;
                float dif[4];
                for (size_t i = 0; i < 4; ++ i)
                    dif[i] = new_pos[i] - current_pos[i];
                float dxy2 = 0;
                if (line.type & CoolingLine::TYPE_G2 || line.type & CoolingLine::TYPE_G3) {
                    Vec3f start(current_pos[0], current_pos[1], 0);
                    Vec3f end(new_pos[0], new_pos[1], 0);
                    Vec3f center(new_pos[5], new_pos[6], 0);
                    bool is_ccw = line.type & CoolingLine::TYPE_G3;
                    float dxy = ArcSegment::calc_arc_length(start, end, center, is_ccw);
                    dxy2 = dxy * dxy;
                } else {
                    dxy2 = dif[0] * dif[0] + dif[1] * dif[1];
                }
                float dxyz2 = dxy2 + dif[2] * dif[2];
                if (dxyz2 > 0.f) {
                    line.length = sqrt(dxyz2);
                } else if (std::abs(dif[3]) > 0.f) {
                    line.length = std::abs(dif[3]);
                }
                line.feedrate = new_pos[4];
                assert((line.type & CoolingLine::TYPE_ADJUSTABLE) == 0 || line.feedrate > 0.f);
                if (line.length > 0)
                    line.time = line.length / line.feedrate;
                line.time_max = line.time;
                if ((line.type & CoolingLine::TYPE_ADJUSTABLE) || active_speed_modifier != size_t(-1))
                    line.time_max = (adjustment->slow_down_min_speed == 0.f) ? FLT_MAX : std::max(line.time, line.length / adjustment->slow_down_min_speed);
                if (active_speed_modifier < adjustment->lines.size() && ((line.type & CoolingLine::TYPE_G1) ||
                                                                         (line.type & CoolingLine::TYPE_G2) ||
                                                                         (line.type & CoolingLine::TYPE_G3))) {
                    assert((line.type & CoolingLine::TYPE_HAS_F) == 0);
                    CoolingLine &sm = adjustment->lines[active_speed_modifier];
                    assert(sm.feedrate > 0.f);
                    sm.length   += line.length;
                    sm.time     += line.time;
                    if (sm.time_max != FLT_MAX) {
                        if (line.time_max == FLT_MAX)
                            sm.time_max = FLT_MAX;
                        else
                            sm.time_max += line.time_max;
                    }
                    line.type = 0;
                }
            }
            current_pos = std::move(new_pos);
        } else if (boost::starts_with(sline, ";_EXTRUDE_END")) {
            line.type = CoolingLine::TYPE_EXTRUDE_END;
            active_speed_modifier = size_t(-1);
        } else if (boost::starts_with(sline, m_toolchange_prefix)) {
            unsigned int new_extruder = 0;
            auto ret = std::from_chars(sline.data() + m_toolchange_prefix.size(), sline.data() + sline.size(), new_extruder);
            if (std::errc::invalid_argument != ret.ec) {
                if (new_extruder < map_extruder_to_per_extruder_adjustment.size()) {
                    if (new_extruder != current_extruder) {
                        line.type        = CoolingLine::TYPE_SET_TOOL;
                        current_extruder = new_extruder;
                        adjustment       = &per_extruder_adjustments[map_extruder_to_per_extruder_adjustment[current_extruder]];
                    }
                } else {
                    if (map_extruder_to_per_extruder_adjustment.size() > 1)
                        BOOST_LOG_TRIVIAL(error) << "CoolingBuffer encountered an invalid toolchange, maybe from a custom gcode: " << sline;
                }
            }
        } else if (boost::starts_with(sline, ";_OVERHANG_FAN_START")) {
            line.type = CoolingLine::TYPE_OVERHANG_FAN_START;
        } else if (boost::starts_with(sline, ";_OVERHANG_FAN_END")) {
            line.type = CoolingLine::TYPE_OVERHANG_FAN_END;
        } else if (boost::starts_with(sline, ";_INTERNAL_BRIDGE_FAN_START")) {
            line.type = CoolingLine::TYPE_INTERNAL_BRIDGE_FAN_START;
        } else if (boost::starts_with(sline, ";_INTERNAL_BRIDGE_FAN_END")) {
            line.type = CoolingLine::TYPE_INTERNAL_BRIDGE_FAN_END;
        } else if (boost::starts_with(sline, ";_SUPP_INTERFACE_FAN_START")) {
            line.type = CoolingLine::TYPE_SUPPORT_INTERFACE_FAN_START;
        } else if (boost::starts_with(sline, ";_SUPP_INTERFACE_FAN_END")) {
            line.type = CoolingLine::TYPE_SUPPORT_INTERFACE_FAN_END;
        } else if (boost::starts_with(sline, ";_IRONING_FAN_START")) {
            line.type = CoolingLine::TYPE_IRONING_FAN_START;
        } else if (boost::starts_with(sline, ";_IRONING_FAN_END")) {
            line.type = CoolingLine::TYPE_IRONING_FAN_END;
        } else if (boost::starts_with(sline, ";_BELT_BAND_START")) {
            line.type = CoolingLine::TYPE_BELT_BAND_START;
        } else if (boost::starts_with(sline, ";_BELT_BAND_END")) {
            line.type = CoolingLine::TYPE_BELT_BAND_END;
        } else if (boost::starts_with(sline, "G4 ")) {
            line.type = CoolingLine::TYPE_G4;
            size_t pos_S = sline.find('S', 3);
            size_t pos_P = sline.find('P', 3);
            line.time = line.time_max = float(
                (pos_S > 0) ? atof_decimal_point(sline.c_str() + pos_S + 1) :
                (pos_P > 0) ? atof_decimal_point(sline.c_str() + pos_P + 1) * 0.001 : 0.);
        } else if (boost::starts_with(sline, ";_FORCE_RESUME_FAN_SPEED")) {
            line.type = CoolingLine::TYPE_FORCE_RESUME_FAN;
        } else if (boost::starts_with(sline, Nonplanar::NpFanTag)) {
            line.type  = CoolingLine::TYPE_NP_FAN;
            line.value = float(atof_decimal_point(std::string_view(sline).substr(std::char_traits<char>::length(Nonplanar::NpFanTag))));
        } else if (boost::starts_with(sline, Nonplanar::NpFanEndTag)) {
            line.type = CoolingLine::TYPE_NP_FAN_END;
        } else if (boost::starts_with(sline, ";_NONPLANAR_BEGIN")) {
            line.type = CoolingLine::TYPE_NP_PASS_BEGIN;
        } else if (boost::starts_with(sline, Nonplanar::NpPassHoldTag)) {
            line.type = CoolingLine::TYPE_NP_PASS_HOLD;
            const size_t z = sline.find(" Z"), f = sline.find(" F");
            if (z != std::string::npos && f != std::string::npos) {
                line.value  = float(atof_decimal_point(std::string_view(sline).substr(z + 2)));
                line.value2 = float(atof_decimal_point(std::string_view(sline).substr(f + 2)));
            } else
                line.type = 0;
        }

        if (!layer_had_extrusion) {
            assert((line.type & CoolingLine::TYPE_ADJUSTABLE) == 0);
            line.time = line.time_max = 0;
        }

        if (line.type != 0)
            adjustment->lines.emplace_back(std::move(line));
    }

    return per_extruder_adjustments;
}

static inline void extruder_range_slow_down_non_proportional(
    std::vector<PerExtruderAdjustments*>::iterator it_begin,
    std::vector<PerExtruderAdjustments*>::iterator it_end,
    float time_stretch)
{
    std::vector<PerExtruderAdjustments*> by_min_print_speed(it_begin, it_end);
    float feedrate = 0;
    for (PerExtruderAdjustments *adj : by_min_print_speed) {
        adj->idx_line_begin = 0;
        adj->idx_line_end   = 0;
        if (adj->idx_line_begin < adj->n_lines_adjustable && adj->lines[adj->idx_line_begin].feedrate> feedrate)
            feedrate = adj->lines[adj->idx_line_begin].feedrate;
    }
    assert(feedrate > 0.f);
    std::sort(by_min_print_speed.begin(), by_min_print_speed.end(),
        [](const PerExtruderAdjustments *p1, const PerExtruderAdjustments *p2){ return p1->slow_down_min_speed > p2->slow_down_min_speed; });
    for (;;) {
        for (PerExtruderAdjustments *adj : by_min_print_speed) {
            for (adj->idx_line_end = adj->idx_line_begin;
                adj->idx_line_end < adj->n_lines_adjustable && adj->lines[adj->idx_line_end].feedrate > feedrate - EPSILON;
                 ++ adj->idx_line_end) ;
        }
        float feedrate_next = 0.f;
        for (PerExtruderAdjustments *adj : by_min_print_speed)
            if (adj->idx_line_end < adj->n_lines_adjustable && adj->lines[adj->idx_line_end].feedrate > feedrate_next)
                feedrate_next = adj->lines[adj->idx_line_end].feedrate;
        for (auto adj = by_min_print_speed.begin(); adj != by_min_print_speed.end();) {
            if ((*adj)->slow_down_min_speed == 0.f) {
                float time_adjustable = 0.f;
                for (auto it = adj; it != by_min_print_speed.end(); ++ it)
                    time_adjustable += (*it)->adjustable_time(true);
                float rate = (time_adjustable + time_stretch) / time_adjustable;
                for (auto it = adj; it != by_min_print_speed.end(); ++ it)
                    (*it)->slow_down_proportional(rate, true);
                return;
            } else {
                float feedrate_limit = std::max(feedrate_next, (*adj)->slow_down_min_speed);
                bool  done           = false;
                float time_stretch_max = 0.f;
                for (auto it = adj; it != by_min_print_speed.end(); ++ it)
                    time_stretch_max += (*it)->time_stretch_when_slowing_down_to_feedrate(feedrate_limit);
                if (time_stretch_max >= time_stretch) {
                    feedrate_limit = new_feedrate_to_reach_time_stretch(adj, by_min_print_speed.end(), feedrate_limit, time_stretch, 20);
                    done = true;
                } else
                    time_stretch -= time_stretch_max;
                for (auto it = adj; it != by_min_print_speed.end(); ++ it)
                    (*it)->slow_down_to_feedrate(feedrate_limit);
                if (done)
                    return;
            }
            auto next = adj;
            for (++ next; next != by_min_print_speed.end() && (*next)->slow_down_min_speed > (*adj)->slow_down_min_speed - EPSILON; ++ next);
            adj = next;
        }
        if (feedrate_next == 0.f)
            break;
        for (PerExtruderAdjustments *adj : by_min_print_speed) {
            adj->idx_line_begin = adj->idx_line_end;
            feedrate = feedrate_next;
        }
    }
}

float CoolingBuffer::calculate_layer_slowdown(std::vector<PerExtruderAdjustments> &per_extruder_adjustments)
{
    std::vector<PerExtruderAdjustments*> by_slowdown_time;
    by_slowdown_time.reserve(per_extruder_adjustments.size());
    float elapsed_time_total0 = 0.f;
    for (PerExtruderAdjustments &adj : per_extruder_adjustments) {
        adj.time_total  = adj.elapsed_time_total();
        adj.time_maximum = adj.maximum_time_after_slowdown(true);
        if (adj.cooling_slow_down_enabled && adj.lines.size() > 0) {
            by_slowdown_time.emplace_back(&adj);
            adj.sort_lines_by_decreasing_feedrate();
        } else
            elapsed_time_total0 += adj.elapsed_time_total();
    }

    std::sort(by_slowdown_time.begin(), by_slowdown_time.end(),
        [](const PerExtruderAdjustments *adj1, const PerExtruderAdjustments *adj2)
            { return adj1->slow_down_layer_time < adj2->slow_down_layer_time; });

    for (auto cur_begin = by_slowdown_time.begin(); cur_begin != by_slowdown_time.end(); ++ cur_begin) {
        PerExtruderAdjustments &adj = *(*cur_begin);
        float total = elapsed_time_total0;
        for (auto it = cur_begin; it != by_slowdown_time.end(); ++ it)
            total += (*it)->time_total;
        float slow_down_layer_time = adj.slow_down_layer_time * 1.001f;
        if (total > slow_down_layer_time) {
        } else {
            float max_time = elapsed_time_total0;
            for (auto it = cur_begin; it != by_slowdown_time.end(); ++ it)
                max_time += (*it)->time_maximum;
            if (max_time > slow_down_layer_time) {
                extruder_range_slow_down_non_proportional(cur_begin, by_slowdown_time.end(), slow_down_layer_time - total);
            } else {
                for (auto it = cur_begin; it != by_slowdown_time.end(); ++ it)
                    (*it)->slowdown_to_minimum_feedrate(true);
            }
        }
        elapsed_time_total0 += adj.elapsed_time_total();
    }

    return elapsed_time_total0;
}

float CoolingBuffer::apply_nonplanar_pass_time(std::vector<PerExtruderAdjustments> &per_extruder_adjustments)
{
    if (!m_config.nonplanar_road_cooling.value)
        return 0.f;
    std::vector<std::pair<CoolingLine *, PerExtruderAdjustments *>> lines;
    bool any_hold = false;
    for (PerExtruderAdjustments &adj : per_extruder_adjustments)
        for (CoolingLine &line : adj.lines) {
            lines.emplace_back(&line, &adj);
            any_hold |= (line.type & CoolingLine::TYPE_NP_PASS_HOLD) != 0;
        }
    if (!any_hold)
        return 0.f;
    std::sort(lines.begin(), lines.end(), [](const auto &l1, const auto &l2) { return l1.first->line_start < l2.first->line_start; });

    float  added = 0.f;
    size_t begin = size_t(-1);
    for (size_t i = 0; i < lines.size(); ++i) {
        CoolingLine &line = *lines[i].first;
        if (line.type & CoolingLine::TYPE_NP_PASS_BEGIN) {
            begin = i;
            continue;
        }
        if (!(line.type & CoolingLine::TYPE_NP_PASS_HOLD) || begin == size_t(-1))
            continue;
        const PerExtruderAdjustments &adj = *lines[i].second;
        const size_t                  from = begin;
        begin                              = size_t(-1);
        if (!adj.cooling_slow_down_enabled || adj.slow_down_layer_time <= 0.f)
            continue;
        float elapsed = 0.f, adjustable = 0.f;
        for (size_t j = from; j < i; ++j) {
            elapsed += lines[j].first->time;
            if (lines[j].second == &adj && lines[j].first->adjustable())
                adjustable += lines[j].first->time;
        }
        const float need = adj.slow_down_layer_time - elapsed;
        if (need <= 0.f)
            continue;
        auto stretched = [&](float factor) {
            float t = 0.f;
            for (size_t j = from; j < i; ++j)
                if (lines[j].second == &adj && lines[j].first->adjustable())
                    t += std::min(lines[j].first->time_max, lines[j].first->time * factor) - lines[j].first->time;
            return t;
        };
        float stretch = 0.f;
        if (adjustable > 0.f) {
            float lo = 1.f, hi = 1.f + need / adjustable;
            while (stretched(hi) < need && hi < 1e3f) {
                lo = hi;
                hi *= 2.f;
            }
            for (int iter = 0; iter < 30 && hi - lo > 1e-4f * hi; ++iter) {
                const float mid = 0.5f * (lo + hi);
                (stretched(mid) < need ? lo : hi) = mid;
            }
            for (size_t j = from; j < i; ++j)
                if (CoolingLine &l = *lines[j].first; lines[j].second == &adj && l.adjustable()) {
                    const float t = std::min(l.time_max, l.time * hi);
                    stretch += t - l.time;
                    l.time     = t;
                    l.slowdown = true;
                    if (t > 0.f)
                        l.feedrate = l.length / t;
                }
        }
        added += stretch;
        if (need - stretch > 0.05f) {
            line.time = need - stretch;
            added += line.time;
        }
    }
    return added;
}

std::string CoolingBuffer::apply_layer_cooldown(
    const std::string                      &gcode,
    size_t                                  layer_id,
    float                                   layer_time,
    std::vector<PerExtruderAdjustments>    &per_extruder_adjustments)
{
    std::vector<const CoolingLine*> lines;
    {
        size_t n_lines = 0;
        for (const PerExtruderAdjustments &adj : per_extruder_adjustments)
            n_lines += adj.lines.size();
        lines.reserve(n_lines);
        for (const PerExtruderAdjustments &adj : per_extruder_adjustments)
            for (const CoolingLine &line : adj.lines)
                lines.emplace_back(&line);
        std::sort(lines.begin(), lines.end(), [](const CoolingLine *ln1, const CoolingLine *ln2) { return ln1->line_start < ln2->line_start; } );
    }
    std::string new_gcode;
    new_gcode.reserve(gcode.size() * 2);
    const unsigned int part_cooling_fan_min_pwm = static_cast<unsigned int>(std::max(0, m_config.part_cooling_fan_min_pwm.value));
    bool overhang_fan_control= false;
    int  overhang_fan_speed   = 0;
    bool internal_bridge_fan_control= false;
    int  internal_bridge_fan_speed   = 0;
    bool supp_interface_fan_control= false;
    int  supp_interface_fan_speed = 0;
    bool ironing_fan_control= false;
    int  ironing_fan_speed   = 0;
    int  np_fan_target = 0;
    int  np_hold_fan   = 0;
    auto change_extruder_set_fan = [ this, layer_id, layer_time, &new_gcode, part_cooling_fan_min_pwm,
        &np_fan_target, &np_hold_fan,
        &overhang_fan_control, &overhang_fan_speed,
        &internal_bridge_fan_control, &internal_bridge_fan_speed,
        &supp_interface_fan_control, &supp_interface_fan_speed,
        &ironing_fan_control, &ironing_fan_speed
    ](bool immediately_apply) {
#define EXTRUDER_CONFIG(OPT) m_config.OPT.get_at(m_current_extruder)
        const size_t config_index = m_gcodegen.get_filament_config_index(m_current_extruder, layer_id);
        float fan_min_speed = m_config.fan_min_speed.get_at(config_index);
        float fan_speed_new = EXTRUDER_CONFIG(reduce_fan_stop_start_freq) ? fan_min_speed : 0;
        int additional_fan_speed_new = m_config.additional_cooling_fan_speed.get_at(config_index);
        int close_fan_the_first_x_layers = EXTRUDER_CONFIG(close_fan_the_first_x_layers);
        int full_fan_speed_layer = EXTRUDER_CONFIG(full_fan_speed_layer);
        int initial_layer_fan_speed = EXTRUDER_CONFIG(initial_layer_fan_speed);
        const bool has_initial_layer_override = initial_layer_fan_speed >= 0 && close_fan_the_first_x_layers <= 0;
        supp_interface_fan_speed = EXTRUDER_CONFIG(support_material_interface_fan_speed);

        if (has_initial_layer_override && layer_id == 0) {
            fan_speed_new             = initial_layer_fan_speed;
            overhang_fan_speed        = initial_layer_fan_speed;
            overhang_fan_control      = false;
            internal_bridge_fan_speed   = initial_layer_fan_speed;
            internal_bridge_fan_control = false;
            supp_interface_fan_speed    = initial_layer_fan_speed;
            supp_interface_fan_control  = false;
            ironing_fan_speed           = initial_layer_fan_speed;
            ironing_fan_control         = false;
        } else if (int(layer_id) >= close_fan_the_first_x_layers) {
            float   fan_max_speed             = m_config.fan_max_speed.get_at(config_index);
            float slow_down_layer_time = float(EXTRUDER_CONFIG(slow_down_layer_time));
            float fan_cooling_layer_time      = float(EXTRUDER_CONFIG(fan_cooling_layer_time));
                if (layer_time < slow_down_layer_time) {
                    fan_speed_new = fan_max_speed;
                } else if (layer_time < fan_cooling_layer_time) {
                    assert(layer_time >= slow_down_layer_time);
                    double t = (layer_time - slow_down_layer_time) / (fan_cooling_layer_time - slow_down_layer_time);
                    fan_speed_new = int(floor(t * fan_min_speed + (1. - t) * fan_max_speed) + 0.5);
                }
            overhang_fan_speed   = EXTRUDER_CONFIG(overhang_fan_speed);
            if (int(layer_id) >= close_fan_the_first_x_layers && int(layer_id) + 1 < full_fan_speed_layer) {
                if (has_initial_layer_override && close_fan_the_first_x_layers == 0 && full_fan_speed_layer > 1) {
                    const float anchor = float(initial_layer_fan_speed);
                    const float denom  = float(full_fan_speed_layer - 1);
                    const float t      = float(int(layer_id)) / denom;
                    fan_speed_new      = std::clamp(int(anchor + t * (float(fan_speed_new) - anchor) + 0.5f), 0, 255);
                    overhang_fan_speed = std::clamp(int(anchor + t * (float(overhang_fan_speed) - anchor) + 0.5f), 0, 255);
                } else {
                    float factor = float(int(layer_id + 1) - close_fan_the_first_x_layers) / float(full_fan_speed_layer - close_fan_the_first_x_layers);
                    fan_speed_new    = std::clamp(int(float(fan_speed_new) * factor + 0.5f), 0, 255);
                    overhang_fan_speed = std::clamp(int(float(overhang_fan_speed) * factor + 0.5f), 0, 255);
                }
            }
            supp_interface_fan_speed = EXTRUDER_CONFIG(support_material_interface_fan_speed);
            supp_interface_fan_control = supp_interface_fan_speed >= 0;

            overhang_fan_control = overhang_fan_speed > fan_speed_new;
            
            internal_bridge_fan_speed   = EXTRUDER_CONFIG(internal_bridge_fan_speed);
            internal_bridge_fan_control = internal_bridge_fan_speed >=0;
            
            if( internal_bridge_fan_speed < 0 ) {
                internal_bridge_fan_speed = overhang_fan_speed;
                internal_bridge_fan_control = overhang_fan_control;
            }

            ironing_fan_speed   = EXTRUDER_CONFIG(ironing_fan_speed);
            ironing_fan_control = ironing_fan_speed >= 0;
#undef EXTRUDER_CONFIG
            
        } else {
            overhang_fan_control = false;
            overhang_fan_speed   = 0;
            fan_speed_new      = 0;
            additional_fan_speed_new = 0;
            supp_interface_fan_control = false; 
            supp_interface_fan_speed   = 0;
            internal_bridge_fan_control = false;
            internal_bridge_fan_speed = 0;
            ironing_fan_control = false;
            ironing_fan_speed = 0;
        }
        const bool fans_on = (has_initial_layer_override && layer_id == 0) || int(layer_id) >= close_fan_the_first_x_layers;
        np_fan_target      = fans_on && m_config.enable_overhang_bridge_fan.get_at(m_current_extruder) ? overhang_fan_speed : 0;
        np_hold_fan        = fans_on ? int(m_config.fan_max_speed.get_at(config_index)) : 0;
        if (fan_speed_new != m_fan_speed || (immediately_apply && m_current_fan_speed != fan_speed_new)) {
            m_fan_speed = fan_speed_new;
            m_current_fan_speed = fan_speed_new;
            if (immediately_apply)
                new_gcode  += GCodeWriter::set_fan(m_config.gcode_flavor, m_fan_speed, part_cooling_fan_min_pwm);
        }
        if (additional_fan_speed_new != m_additional_fan_speed) {
            m_additional_fan_speed = additional_fan_speed_new;
            if (immediately_apply && m_config.auxiliary_fan.value)
                new_gcode += GCodeWriter::set_additional_fan(m_additional_fan_speed);
        }
    };

    const char         *pos               = gcode.c_str();
    int                 current_feedrate  = 0;
    change_extruder_set_fan(true);

    std::unordered_map<int, bool> fan_speed_change_requests = {{CoolingLine::TYPE_OVERHANG_FAN_START, false},
                                                               {CoolingLine::TYPE_INTERNAL_BRIDGE_FAN_START, false},
                                                               {CoolingLine::TYPE_SUPPORT_INTERFACE_FAN_START, false},
                                                               {CoolingLine::TYPE_IRONING_FAN_START, false},
                                                               {CoolingLine::TYPE_FORCE_RESUME_FAN, false}};
    bool need_set_fan = m_belt_band_active;
    float np_level       = -1.f;
    int   np_aux_current = -1;
    auto  np_part_fan    = [&](int speed) {
        if (np_level <= 0.f || np_fan_target <= m_fan_speed)
            return speed;
        return std::max(speed, int(std::lround(m_fan_speed + np_level * (np_fan_target - m_fan_speed))));
    };
    auto  np_set_aux     = [&](float level) {
        if (!m_config.auxiliary_fan.value || m_additional_fan_speed <= 0)
            return;
        const int target = level > 0.f && np_fan_target > m_additional_fan_speed ?
                               int(std::lround(m_additional_fan_speed + level * (np_fan_target - m_additional_fan_speed))) : -1;
        if (target != np_aux_current) {
            new_gcode += GCodeWriter::set_additional_fan(target >= 0 ? target : m_additional_fan_speed);
            np_aux_current = target;
        }
    };

    for (const CoolingLine *line : lines) {
        const char *line_start  = gcode.c_str() + line->line_start;
        const char *line_end    = gcode.c_str() + line->line_end;
        if (line_start > pos)
            new_gcode.append(pos, line_start - pos);
        if (line->type & CoolingLine::TYPE_SET_TOOL) {
            unsigned int new_extruder = 0;
            auto ret = std::from_chars(line_start + m_toolchange_prefix.size(), line_end, new_extruder);
            if (std::errc::invalid_argument != ret.ec) {
                if (new_extruder != m_current_extruder) {
                    m_current_extruder = new_extruder;
                    change_extruder_set_fan(true);
                    if (m_belt_band_active)
                        need_set_fan = true;
                }
            }
            new_gcode.append(line_start, line_end - line_start);
        } else if (line->type & CoolingLine::TYPE_OVERHANG_FAN_START) {
            if (overhang_fan_control && !fan_speed_change_requests[CoolingLine::TYPE_OVERHANG_FAN_START]) {
                need_set_fan = true;
                fan_speed_change_requests[CoolingLine::TYPE_OVERHANG_FAN_START] = true;
           }
        } else if (line->type & CoolingLine::TYPE_OVERHANG_FAN_END) {
            if (overhang_fan_control && fan_speed_change_requests[CoolingLine::TYPE_OVERHANG_FAN_START]) {
                fan_speed_change_requests[CoolingLine::TYPE_OVERHANG_FAN_START] = false;
            }
            need_set_fan = true;
        } else if (line->type & CoolingLine::TYPE_INTERNAL_BRIDGE_FAN_START) {
            if (internal_bridge_fan_control && !fan_speed_change_requests[CoolingLine::TYPE_INTERNAL_BRIDGE_FAN_START]) {
                need_set_fan = true;
                fan_speed_change_requests[CoolingLine::TYPE_INTERNAL_BRIDGE_FAN_START] = true;
           }
        } else if (line->type & CoolingLine::TYPE_INTERNAL_BRIDGE_FAN_END) {
            if (internal_bridge_fan_control && fan_speed_change_requests[CoolingLine::TYPE_INTERNAL_BRIDGE_FAN_START]) {
                fan_speed_change_requests[CoolingLine::TYPE_INTERNAL_BRIDGE_FAN_START] = false;
            }
            need_set_fan = true;
        } else if (line->type & CoolingLine::TYPE_SUPPORT_INTERFACE_FAN_START) {
            if (supp_interface_fan_control && !fan_speed_change_requests[CoolingLine::TYPE_SUPPORT_INTERFACE_FAN_START]) {
                fan_speed_change_requests[CoolingLine::TYPE_SUPPORT_INTERFACE_FAN_START] = true;
                need_set_fan = true;
            }
        } else if (line->type & CoolingLine::TYPE_SUPPORT_INTERFACE_FAN_END && fan_speed_change_requests[CoolingLine::TYPE_SUPPORT_INTERFACE_FAN_START]) {
            if (supp_interface_fan_control) {
                fan_speed_change_requests[CoolingLine::TYPE_SUPPORT_INTERFACE_FAN_START] = false;
            }
            need_set_fan = true;
        } else if (line->type & CoolingLine::TYPE_IRONING_FAN_START) {
            if (ironing_fan_control && !fan_speed_change_requests[CoolingLine::TYPE_IRONING_FAN_START]) {
                fan_speed_change_requests[CoolingLine::TYPE_IRONING_FAN_START] = true;
                need_set_fan = true;
            }
        } else if (line->type & CoolingLine::TYPE_IRONING_FAN_END) {
            if (ironing_fan_control && fan_speed_change_requests[CoolingLine::TYPE_IRONING_FAN_START]) {
                fan_speed_change_requests[CoolingLine::TYPE_IRONING_FAN_START] = false;
            }
            need_set_fan = true;
        } else if (line->type & CoolingLine::TYPE_FORCE_RESUME_FAN) {
            if (m_fan_speed != -1 && !std::any_of(fan_speed_change_requests.begin(), fan_speed_change_requests.end(), [](const std::pair<int, bool>& p) { return p.second; })){
                fan_speed_change_requests[CoolingLine::TYPE_FORCE_RESUME_FAN] = true;
                need_set_fan = true;
            }
            if (m_additional_fan_speed != -1 && m_config.auxiliary_fan.value) {
                new_gcode += GCodeWriter::set_additional_fan(m_additional_fan_speed);
                np_aux_current = -1;
                np_set_aux(np_level);
            }
        }
        else if (line->type & CoolingLine::TYPE_BELT_BAND_START) {
            m_belt_band_active = true;
            need_set_fan       = true;
        } else if (line->type & CoolingLine::TYPE_BELT_BAND_END) {
            m_belt_band_active = false;
            need_set_fan       = true;
        }
        else if (line->type & CoolingLine::TYPE_EXTRUDE_END) {
        } else if (line->type & CoolingLine::TYPE_NP_FAN) {
            np_level     = line->value;
            need_set_fan = true;
            np_set_aux(np_level);
        } else if (line->type & CoolingLine::TYPE_NP_FAN_END) {
            np_level     = -1.f;
            need_set_fan = true;
            np_set_aux(0.f);
        } else if (line->type & CoolingLine::TYPE_NP_PASS_HOLD) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "G1 Z%.3f F%d ; hold for the minimum pass time\n", line->value, int(line->value2));
            new_gcode += buf;
            current_feedrate = 0;
            if (line->time > 0.f) {
                const int hold_fan = std::max(np_hold_fan, m_current_fan_speed);
                new_gcode += GCodeWriter::set_fan(m_config.gcode_flavor, hold_fan, part_cooling_fan_min_pwm);
                m_current_fan_speed = hold_fan;
                std::snprintf(buf, sizeof(buf), "G4 P%d\n", int(std::lround(1000. * line->time)));
                new_gcode += buf;
                need_set_fan = true;
            }
        } else if (line->type & CoolingLine::TYPE_KEEP_VERBATIM) {
            std::string kept(line_start, line_end);
            boost::replace_all(kept, ";_EXTRUDE_SET_SPEED", "");
            new_gcode += kept;
            current_feedrate = 0;
        } else if (line->type & (CoolingLine::TYPE_ADJUSTABLE | CoolingLine::TYPE_EXTERNAL_PERIMETER | CoolingLine::TYPE_WIPE | CoolingLine::TYPE_HAS_F)) {
            const char *end = line_start;
            for (; end < line_end && *end != ';'; ++ end);
            const char *fpos            = strstr(line_start + 2, " F") + 2;
            int         new_feedrate    = current_feedrate;
            bool        modify          = false;
            bool        remove          = false;
            assert(fpos != nullptr);
            new_feedrate = line->slowdown ? int(floor(60. * line->feedrate + 0.5)) : atoi(fpos);
            if (new_feedrate == current_feedrate) {
                if ((line->type & (CoolingLine::TYPE_ADJUSTABLE | CoolingLine::TYPE_EXTERNAL_PERIMETER | CoolingLine::TYPE_WIPE)) || line->length == 0.)
                    end = line_end;
                else
                    remove = true;
            } else if (line->slowdown) {
                modify = true;
            } else {
                new_gcode.append(line_start, end - line_start);
                current_feedrate = new_feedrate;
            }
            if (modify || remove) {
                if (modify) {
                    new_gcode.append(line_start, fpos - line_start);
                    current_feedrate = new_feedrate;
                    char buf[64];
                    sprintf(buf, "%d", int(current_feedrate));
                    new_gcode += buf;
                } else {
                    const char *f = fpos;
                    for (f -= 2; f > line_start && (*f == ' ' || *f == '\t'); -- f);

                    if ((f - line_start == 1) && *line_start == 'G' && (*f == '1' || *f == '0')) {
                    } else {
                        new_gcode.append(line_start, f - line_start + 1);
                    }
                }
                for (; fpos != end && *fpos != ' ' && *fpos != ';' && *fpos != '\n'; ++ fpos);
                if (fpos < end)
                    new_gcode.append(fpos, end - fpos);
                else if (remove && new_gcode == "G1") {
                    new_gcode.resize(new_gcode.size() - 2);
                    end = line_end;
                }
            }
            if (end < line_end) {
                if (line->type & (CoolingLine::TYPE_ADJUSTABLE | CoolingLine::TYPE_EXTERNAL_PERIMETER | CoolingLine::TYPE_WIPE)) {
                    std::string comment(end, line_end);
                    boost::replace_all(comment, ";_EXTRUDE_SET_SPEED", "");
                    if (line->type & CoolingLine::TYPE_EXTERNAL_PERIMETER)
                        boost::replace_all(comment, ";_EXTERNAL_PERIMETER", "");
                    if (line->type & CoolingLine::TYPE_WIPE)
                        boost::replace_all(comment, ";_WIPE", "");
                    new_gcode += comment;
                } else {
                    new_gcode.append(end, line_end - end);
                }
            }
        } else {
            new_gcode.append(line_start, line_end - line_start);
        }

        if (need_set_fan) {
            const auto set_fan = [&](int speed) {
                if (!m_belt_band_active)
                    speed = np_part_fan(speed);
                if (m_current_fan_speed != speed) {
                    new_gcode += GCodeWriter::set_fan(m_config.gcode_flavor, speed, part_cooling_fan_min_pwm);
                    m_current_fan_speed = speed;
                }
            };
            if (m_belt_band_active) {
                set_fan(0);
                fan_speed_change_requests[CoolingLine::TYPE_FORCE_RESUME_FAN] = false;
            } else if (fan_speed_change_requests[CoolingLine::TYPE_OVERHANG_FAN_START]){
                set_fan(overhang_fan_speed);
            } else if (fan_speed_change_requests[CoolingLine::TYPE_INTERNAL_BRIDGE_FAN_START]){
                set_fan(internal_bridge_fan_speed);
            }
            else if (fan_speed_change_requests[CoolingLine::TYPE_SUPPORT_INTERFACE_FAN_START]){
                set_fan(supp_interface_fan_speed);
            }
            else if (fan_speed_change_requests[CoolingLine::TYPE_IRONING_FAN_START]){
                set_fan(ironing_fan_speed);
            }
            else if(fan_speed_change_requests[CoolingLine::TYPE_FORCE_RESUME_FAN] && m_current_fan_speed != -1){
                new_gcode += GCodeWriter::set_fan(m_config.gcode_flavor, m_current_fan_speed, part_cooling_fan_min_pwm);
                fan_speed_change_requests[CoolingLine::TYPE_FORCE_RESUME_FAN] = false;
            }
            else {
                set_fan(m_fan_speed);
            }
            need_set_fan = false;
        }
        pos = line_end;
    }
    const char *gcode_end = gcode.c_str() + gcode.size();
    if (pos < gcode_end)
        new_gcode.append(pos, gcode_end - pos);

    return new_gcode;
}

}
